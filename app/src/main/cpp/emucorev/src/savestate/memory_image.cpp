#include <emucorev/savestate/memory_image.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <cerrno>
#if !defined(_WIN32)
#include <sys/types.h>
#endif

namespace emucorev::savestate {

namespace {

constexpr uint32_t kMaxSpanCount = 1u << 20;
constexpr uint32_t kMemoryRecordSize = 24;
constexpr uint32_t kMaxCompressedOverhead = 1u << 16;

bool read_u32(SectionReader &section, uint32_t &value, std::string &error) {
    return section.read(&value, sizeof(value), error);
}

bool read_u64(SectionReader &section, uint64_t &value, std::string &error) {
    return section.read(&value, sizeof(value), error);
}

bool seek_file(FILE *file, uint64_t offset) {
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

void set_error(std::string &error, const char *message) {
    if (error.empty())
        error = message;
}

} // namespace

MemoryImage::~MemoryImage() {
    if (staging_file_)
        std::fclose(staging_file_);
    if (!staging_path_.empty()) {
        boost::system::error_code ec;
        fs::remove(staging_path_, ec);
    }
}

std::unique_ptr<MemoryImage> MemoryImage::preflight(
    Reader &reader, const fs::path &staging_directory, std::string &error) {
    error.clear();
    auto section = reader.open_section(SectionId::Memory, error);
    if (!section)
        return nullptr;

    auto image = std::unique_ptr<MemoryImage>(new MemoryImage());
    if (!image->open_staging_file(staging_directory, error))
        return nullptr;

    uint32_t format_version = 0;
    uint32_t chunk_size = 0;
    uint32_t span_count = 0;
    if (!read_u32(*section, format_version, error)
        || !read_u32(*section, chunk_size, error)
        || !read_u32(*section, span_count, error))
        return nullptr;

    if (format_version != kMemoryImageFormatVersion
        || chunk_size < kMemoryImagePageSize
        || chunk_size > kMemoryImageMaxChunkSize
        || chunk_size % kMemoryImagePageSize != 0) {
        error = "unsupported memory section header";
        return nullptr;
    }
    const uint64_t span_bytes = section->size() - section->offset();
    if (span_count > kMaxSpanCount || static_cast<uint64_t>(span_count) > span_bytes / 8) {
        error = "invalid memory span count";
        return nullptr;
    }

    image->chunk_size_ = chunk_size;
    image->spans_.reserve(span_count);
    uint64_t previous_end = 0;
    uint64_t expected_chunk_count = 0;
    for (uint32_t i = 0; i < span_count; ++i) {
        MemorySpanInfo span;
        if (!read_u32(*section, span.address, error) || !read_u32(*section, span.page_count, error))
            return nullptr;

        const uint64_t start = span.address;
        const uint64_t bytes = static_cast<uint64_t>(span.page_count) * kMemoryImagePageSize;
        const uint64_t end = start + bytes;
        if (span.address == 0 || span.address % kMemoryImagePageSize != 0 || span.page_count == 0
            || start < previous_end || end > kMemoryImageArenaSize) {
            error = "invalid memory span";
            return nullptr;
        }

        const uint64_t chunks = bytes / chunk_size + (bytes % chunk_size != 0);
        expected_chunk_count += chunks;
        if (expected_chunk_count > kMaxSpanCount) {
            error = "too many memory chunks";
            return nullptr;
        }
        previous_end = end;
        image->spans_.push_back(span);
    }

    if (expected_chunk_count > (section->size() - section->offset()) / kMemoryRecordSize) {
        error = "truncated memory chunk records";
        return nullptr;
    }
    image->chunks_.reserve(static_cast<size_t>(expected_chunk_count));

    std::vector<uint8_t> decoded;
    std::vector<uint8_t> compressed;
    for (const MemorySpanInfo &span : image->spans_) {
        const uint64_t span_size = static_cast<uint64_t>(span.page_count) * kMemoryImagePageSize;
        for (uint64_t offset = 0; offset < span_size; offset += chunk_size) {
            const uint32_t expected_size = static_cast<uint32_t>(std::min<uint64_t>(chunk_size, span_size - offset));
            const uint64_t expected_address = static_cast<uint64_t>(span.address) + offset;

            uint64_t address = 0;
            uint32_t raw_size = 0;
            uint32_t encoding = 0;
            uint32_t stored_size = 0;
            uint32_t checksum = 0;
            if (!read_u64(*section, address, error)
                || !read_u32(*section, raw_size, error)
                || !read_u32(*section, encoding, error)
                || !read_u32(*section, stored_size, error)
                || !read_u32(*section, checksum, error))
                return nullptr;

            if (address != expected_address || raw_size != expected_size || raw_size == 0 || raw_size > chunk_size) {
                error = "memory chunk does not exactly cover its span";
                return nullptr;
            }

            Chunk chunk;
            chunk.address = address;
            chunk.size = raw_size;
            if (encoding == static_cast<uint32_t>(MemoryChunkEncoding::Zeros)) {
                if (stored_size != 0 || checksum != 0) {
                    error = "noncanonical zero memory chunk";
                    return nullptr;
                }
                chunk.zeros = true;
            } else if (encoding == static_cast<uint32_t>(MemoryChunkEncoding::Raw)) {
                if (stored_size != raw_size || stored_size > section->size() - section->offset()) {
                    error = "invalid raw memory chunk size";
                    return nullptr;
                }
                decoded.resize(raw_size);
                if (!section->read(decoded.data(), decoded.size(), error))
                    return nullptr;
                if (crc32_buffer(decoded.data(), decoded.size()) != checksum) {
                    error = "memory chunk checksum mismatch";
                    return nullptr;
                }
                if (!image->stage(decoded.data(), decoded.size(), chunk.staged_offset, error))
                    return nullptr;
            } else if (encoding == static_cast<uint32_t>(MemoryChunkEncoding::Deflated)) {
                const uint64_t max_stored = static_cast<uint64_t>(raw_size) + kMaxCompressedOverhead;
                if (stored_size == 0 || stored_size > max_stored || stored_size > section->size() - section->offset()) {
                    error = "invalid deflated memory chunk size";
                    return nullptr;
                }
                compressed.resize(stored_size);
                if (!section->read(compressed.data(), compressed.size(), error))
                    return nullptr;
                if (!decompress_buffer(compressed.data(), compressed.size(), raw_size, decoded, error))
                    return nullptr;
                if (crc32_buffer(decoded.data(), decoded.size()) != checksum) {
                    error = "memory chunk checksum mismatch";
                    return nullptr;
                }
                if (!image->stage(decoded.data(), decoded.size(), chunk.staged_offset, error))
                    return nullptr;
            } else {
                error = "unknown memory chunk encoding";
                return nullptr;
            }
            image->chunks_.push_back(chunk);
        }
    }

    if (section->remaining()) {
        error = "extra data after memory chunks";
        return nullptr;
    }
    if (std::fflush(image->staging_file_) != 0) {
        error = "unable to flush staged memory image";
        return nullptr;
    }
    return image;
}

bool MemoryImage::open_staging_file(const fs::path &directory, std::string &error) {
    boost::system::error_code ec;
    // Boost's exists(path, ec) can set ENOENT for a missing cache directory.
    // create_directories handles missing parents and existing directories;
    // its false return means no creation was needed, not failure.
    fs::create_directories(directory, ec);
    if (ec) {
        error = "unable to create memory staging directory '" + directory.string()
            + "' (error=" + std::to_string(ec.value()) + ": " + ec.message() + ")";
        return false;
    }
    if (!fs::is_directory(directory, ec) || ec) {
        error = "memory staging path is not a directory '" + directory.string()
            + "' (error=" + std::to_string(ec.value()) + ": " + ec.message() + ")";
        return false;
    }

    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        staging_path_ = directory / fs::unique_path("memory-image-%%%%-%%%%-%%%%.tmp");
        staging_file_ = FOPEN(staging_path_.c_str(), "w+bx");
        if (staging_file_)
            return true;
        if (errno != EEXIST)
            break;
    }
    staging_path_.clear();
    error = "unable to create staged memory image";
    return false;
}

bool MemoryImage::stage(const uint8_t *bytes, size_t size, uint64_t &offset, std::string &error) {
    if (size > kMemoryImageArenaSize - staged_size_) {
        error = "staged memory image exceeds guest arena";
        return false;
    }
    offset = staged_size_;
    if (size && std::fwrite(bytes, 1, size, staging_file_) != size) {
        error = "unable to write staged memory image";
        return false;
    }
    staged_size_ += size;
    return true;
}

bool MemoryImage::contains(uint64_t address, uint64_t size) const {
    if (!size)
        return true;
    if (address >= kMemoryImageArenaSize || size > kMemoryImageArenaSize - address)
        return false;
    auto span = std::upper_bound(spans_.begin(), spans_.end(), address,
        [](uint64_t start, const MemorySpanInfo &entry) { return start < entry.address; });
    if (span == spans_.begin())
        return false;
    --span;
    const uint64_t end = address + size;
    while (span != spans_.end() && address >= span->address) {
        const uint64_t span_end = uint64_t{span->address} + uint64_t{span->page_count} * kMemoryImagePageSize;
        if (address >= span_end)
            return false;
        if (end <= span_end)
            return true;
        address = span_end;
        ++span;
    }
    return false;
}

bool MemoryImage::read_bytes(uint64_t address, void *destination, size_t size, std::string &error) const {
    error.clear();
    if (!contains(address, size) || (size && (!destination || !staging_file_))) {
        error = "reference is outside saved guest memory";
        return false;
    }
    if (!size)
        return true;
    auto chunk = std::upper_bound(chunks_.begin(), chunks_.end(), address,
        [](uint64_t start, const Chunk &entry) { return start < entry.address; });
    --chunk; // contains() proved a chunk covers the starting byte.
    auto *output = static_cast<uint8_t *>(destination);
    while (size) {
        const size_t offset = static_cast<size_t>(address - chunk->address);
        const size_t count = std::min(size, static_cast<size_t>(chunk->size) - offset);
        if (chunk->zeros) {
            std::memset(output, 0, count);
        } else if (!seek_file(staging_file_, chunk->staged_offset + offset)
            || std::fread(output, 1, count, staging_file_) != count) {
            error = "unable to read staged guest reference";
            return false;
        }
        output += count;
        address += count;
        size -= count;
        ++chunk;
    }
    return true;
}

bool MemoryImage::visit_validated_chunks(const ChunkVisitor &visitor, std::string &error) const {
    error.clear();
    if (!staging_file_ || !visitor) {
        error = "memory image is not available";
        return false;
    }

    std::vector<uint8_t> scratch(chunk_size_);
    for (const Chunk &chunk : chunks_) {
        if (chunk.zeros) {
            std::fill(scratch.begin(), scratch.begin() + chunk.size, 0);
        } else {
            if (!seek_file(staging_file_, chunk.staged_offset)) {
                error = "unable to seek staged memory image";
                return false;
            }
            if (std::fread(scratch.data(), 1, chunk.size, staging_file_) != chunk.size) {
                error = "unable to read staged memory image";
                return false;
            }
        }
        if (!visitor(chunk.address, scratch.data(), chunk.size, error)) {
            set_error(error, "memory chunk visitor rejected data");
            return false;
        }
    }
    return true;
}

} // namespace emucorev::savestate
