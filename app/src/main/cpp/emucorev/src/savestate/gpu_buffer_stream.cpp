#include <emucorev/savestate/gpu_buffer_stream.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <limits>
#if !defined(_WIN32)
#include <sys/types.h>
#endif

namespace emucorev::savestate {
namespace {
constexpr uint32_t stream_magic = 0x31504742; // "BGP1", little endian
constexpr uint32_t stream_version = 1;
constexpr uint32_t max_buffer_count = 4096;
constexpr uint32_t max_stored_page_overhead = 1u << 16;

bool seek_file(FILE *file, uint64_t offset) {
    if (offset > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()))
        return false;
#if defined(_WIN32)
    return _fseeki64(file, static_cast<__int64>(offset), SEEK_SET) == 0;
#else
    return fseeko(file, static_cast<off_t>(offset), SEEK_SET) == 0;
#endif
}

bool append_u32(Writer &writer, uint32_t value, std::string &error) {
    const uint8_t bytes[] = {
        static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)
    };
    return writer.append_section_data(bytes, sizeof(bytes), error);
}

bool read_u32(SectionReader &reader, uint32_t &value, std::string &error) {
    uint8_t bytes[4];
    if (!reader.read(bytes, sizeof(bytes), error))
        return false;
    value = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8)
        | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    return true;
}

enum class PageEncoding : uint32_t {
    Raw = 0,
    Deflated = 1,
    Zeros = 2,
};

uint64_t declared_page_count(const GpuSnapshotBuffer &buffer) {
    return buffer.expected_page_count ? buffer.expected_page_count : buffer.pages.size();
}

bool valid_buffer_headers(const std::vector<GpuSnapshotBuffer> &buffers,
    uint64_t max_payload_bytes, uint64_t &maximum_page_bytes, std::string &error) {
    maximum_page_bytes = 0;
    if (buffers.size() > max_buffer_count) {
        error = "GPU page stream buffer count exceeds limit";
        return false;
    }
    uint64_t previous_end = 0;
    bool first = true;
    for (const auto &buffer : buffers) {
        if (!buffer.address || !buffer.size || buffer.size % kGpuSnapshotPageSize
            || buffer.size > UINT32_MAX - kGpuSnapshotPageSize
            || (!first && buffer.address < previous_end)) {
            error = "invalid GPU page stream buffer directory";
            return false;
        }
        const uint64_t end = uint64_t(buffer.address) + buffer.size;
        if (end > (uint64_t{1} << 32)) {
            error = "GPU page stream buffer exceeds guest address space";
            return false;
        }
        previous_end = end;
        first = false;

        const uint64_t buffer_page_limit = (uint64_t(buffer.size) + kGpuSnapshotPageSize) / kGpuSnapshotPageSize;
        const uint64_t page_count = declared_page_count(buffer);
        if (page_count > buffer_page_limit) {
            error = "GPU page stream page count exceeds mapped buffer";
            return false;
        }
        const uint64_t page_bytes = page_count * kGpuSnapshotPageSize;
        if (maximum_page_bytes > max_payload_bytes || page_bytes > max_payload_bytes - maximum_page_bytes) {
            error = "GPU page stream payload exceeds validated mapping bound";
            return false;
        }
        maximum_page_bytes += page_bytes;
    }
    if (maximum_page_bytes > max_payload_bytes) {
        error = "GPU page stream payload exceeds validated mapping bound";
        return false;
    }
    return true;
}

bool valid_buffer_directory(const std::vector<GpuSnapshotBuffer> &buffers,
    uint64_t max_payload_bytes, uint64_t &maximum_page_bytes, std::string &error) {
    if (!valid_buffer_headers(buffers, max_payload_bytes, maximum_page_bytes, error))
        return false;
    for (const auto &buffer : buffers) {
        uint32_t previous_page_end = 0;
        for (const auto &page : buffer.pages) {
            if (page.offset % kGpuSnapshotPageSize || page.offset < previous_page_end
                || uint64_t(page.offset) + kGpuSnapshotPageSize > uint64_t(buffer.size) + kGpuSnapshotPageSize) {
                error = "invalid GPU page stream page range";
                return false;
            }
            previous_page_end = page.offset + kGpuSnapshotPageSize;
        }
    }
    return true;
}
} // namespace

GpuPageStore::~GpuPageStore() {
    if (file_)
        std::fclose(file_);
    if (!path_.empty()) {
        boost::system::error_code error;
        fs::remove(path_, error);
    }
}

void sort_gpu_snapshot_buffers(std::vector<GpuSnapshotBuffer> &buffers) {
    std::sort(buffers.begin(), buffers.end(), [](const auto &left, const auto &right) {
        return left.address < right.address;
    });
}

bool GpuPageStore::open(const fs::path &directory, std::string &error) {
    error.clear();
    if (file_) {
        error = "GPU page store is already open";
        return false;
    }
    boost::system::error_code filesystem_error;
    fs::create_directories(directory, filesystem_error);
    if (filesystem_error || !fs::is_directory(directory, filesystem_error) || filesystem_error) {
        error = "unable to create GPU page staging directory";
        return false;
    }
    for (unsigned attempt = 0; attempt < 4; ++attempt) {
        path_ = directory / fs::unique_path("gpu-pages-%%%%-%%%%-%%%%.tmp");
        file_ = FOPEN(path_.c_str(), "w+bx");
        if (file_)
            return true;
        if (errno != EEXIST)
            break;
    }
    path_.clear();
    error = "unable to create GPU page staging file";
    return false;
}

bool GpuPageStore::append(const uint8_t *bytes, size_t size, uint64_t &offset, std::string &error) {
    if (!file_ || (!bytes && size) || size > UINT64_MAX - size_) {
        error = "invalid GPU page staging write";
        return false;
    }
    if (!seek_file(file_, size_) || (size && std::fwrite(bytes, 1, size, file_) != size)) {
        error = "unable to write GPU page staging file";
        return false;
    }
    offset = size_;
    size_ += size;
    return true;
}

bool GpuPageStore::read(uint64_t offset, void *destination, size_t size, std::string &error) const {
    if (!file_ || (!destination && size) || offset > size_ || size > size_ - offset) {
        error = "GPU page staging read is out of range";
        return false;
    }
    if (!seek_file(file_, offset) || (size && std::fread(destination, 1, size, file_) != size)) {
        error = "unable to read GPU page staging file";
        return false;
    }
    return true;
}

bool write_gpu_buffer_page_section(Writer &writer, const std::vector<GpuSnapshotBuffer> &buffers,
    const GpuPageStore *store, uint64_t max_payload_bytes, std::string &error) {
    error.clear();
    uint64_t maximum_page_bytes = 0;
    if (!valid_buffer_directory(buffers, max_payload_bytes, maximum_page_bytes, error))
        return false;
    if (!writer.begin_section(SectionId::GpuBufferPages, false, error))
        return false;
    if (!append_u32(writer, stream_magic, error) || !append_u32(writer, stream_version, error)
        || !append_u32(writer, static_cast<uint32_t>(buffers.size()), error))
        return false;

    std::array<uint8_t, kGpuSnapshotPageSize> raw{};
    std::vector<uint8_t> compressed;
    for (const auto &buffer : buffers) {
        if (!append_u32(writer, buffer.address, error) || !append_u32(writer, buffer.size, error)
            || !append_u32(writer, static_cast<uint32_t>(buffer.pages.size()), error))
            return false;
    }
    for (const auto &buffer : buffers) {
        for (const auto &page : buffer.pages) {
            if (page.bytes.size() == raw.size()) {
                std::memcpy(raw.data(), page.bytes.data(), raw.size());
            } else if (page.bytes.empty() && store && page.staged_offset != UINT64_MAX) {
                if (!store->read(page.staged_offset, raw.data(), raw.size(), error))
                    return false;
            } else {
                error = "GPU page stream page has no complete payload";
                return false;
            }

            PageEncoding encoding;
            uint32_t stored_size = 0;
            uint32_t checksum = crc32_buffer(raw.data(), raw.size());
            const bool all_zero = std::all_of(raw.begin(), raw.end(), [](uint8_t byte) { return byte == 0; });
            if (all_zero) {
                encoding = PageEncoding::Zeros;
                checksum = 0;
            } else {
                if (!compress_buffer(raw.data(), raw.size(), compressed, error))
                    return false;
                if (compressed.size() < raw.size()) {
                    encoding = PageEncoding::Deflated;
                    stored_size = static_cast<uint32_t>(compressed.size());
                } else {
                    encoding = PageEncoding::Raw;
                    stored_size = static_cast<uint32_t>(raw.size());
                }
            }
            if (!append_u32(writer, page.offset, error) || !append_u32(writer, kGpuSnapshotPageSize, error)
                || !append_u32(writer, static_cast<uint32_t>(encoding), error)
                || !append_u32(writer, stored_size, error) || !append_u32(writer, checksum, error))
                return false;
            if (encoding == PageEncoding::Raw) {
                if (!writer.append_section_data(raw.data(), raw.size(), error))
                    return false;
            } else if (encoding == PageEncoding::Deflated) {
                if (!writer.append_section_data(compressed.data(), compressed.size(), error))
                    return false;
            }
        }
    }
    return writer.end_section(error);
}

bool read_gpu_buffer_page_section(SectionReader &reader, std::vector<GpuSnapshotBuffer> &buffers,
    std::shared_ptr<GpuPageStore> &store, const fs::path &staging_directory,
    uint64_t max_payload_bytes, std::string &error) {
    error.clear();
    uint64_t maximum_page_bytes = 0;
    if (buffers.size() > max_buffer_count) {
        error = "GPU page stream buffer count exceeds limit";
        return false;
    }
    std::vector<GpuSnapshotBuffer> staged_directory = buffers;
    for (auto &buffer : staged_directory) {
        for (auto &page : buffer.pages) {
            page.bytes.clear();
            page.staged_offset = UINT64_MAX;
        }
    }
    if (!valid_buffer_headers(staged_directory, max_payload_bytes, maximum_page_bytes, error))
        return false;

    uint32_t magic, version, buffer_count;
    if (!read_u32(reader, magic, error) || !read_u32(reader, version, error)
        || !read_u32(reader, buffer_count, error) || magic != stream_magic || version != stream_version
        || buffer_count != buffers.size()) {
        if (error.empty())
            error = "invalid GPU page stream header";
        return false;
    }

    for (size_t i = 0; i < buffers.size(); ++i) {
        uint32_t address, size, page_count;
        if (!read_u32(reader, address, error) || !read_u32(reader, size, error)
            || !read_u32(reader, page_count, error))
            return false;
        if (address != buffers[i].address || size != buffers[i].size
            || page_count != declared_page_count(buffers[i])) {
            error = "GPU page stream directory does not match GPU snapshot metadata at buffer "
                + std::to_string(i) + " (stream address=0x" + std::to_string(address)
                + ", metadata address=0x" + std::to_string(buffers[i].address)
                + ", stream size=" + std::to_string(size)
                + ", metadata size=" + std::to_string(buffers[i].size)
                + ", stream pages=" + std::to_string(page_count)
                + ", metadata pages=" + std::to_string(declared_page_count(buffers[i])) + ")";
            return false;
        }
        staged_directory[i].pages.resize(page_count);
        staged_directory[i].expected_page_count = 0;
    }

    auto staged_store = std::make_shared<GpuPageStore>();
    if (!staged_store->open(staging_directory, error))
        return false;
    std::array<uint8_t, kGpuSnapshotPageSize> decoded{};
    std::vector<uint8_t> encoded;
    std::vector<uint8_t> decompressed;
    for (size_t i = 0; i < buffers.size(); ++i) {
        uint32_t previous_page_end = 0;
        for (size_t p = 0; p < staged_directory[i].pages.size(); ++p) {
            uint32_t offset, raw_size, encoding_value, stored_size, checksum;
            if (!read_u32(reader, offset, error) || !read_u32(reader, raw_size, error)
                || !read_u32(reader, encoding_value, error) || !read_u32(reader, stored_size, error)
                || !read_u32(reader, checksum, error))
                return false;
            if (raw_size != kGpuSnapshotPageSize || offset % kGpuSnapshotPageSize || offset < previous_page_end
                || uint64_t(offset) + raw_size > uint64_t(buffers[i].size) + kGpuSnapshotPageSize) {
                error = "invalid GPU page stream page range";
                return false;
            }
            previous_page_end = offset + raw_size;

            switch (static_cast<PageEncoding>(encoding_value)) {
            case PageEncoding::Zeros:
                if (stored_size != 0 || checksum != 0) {
                    error = "invalid zero GPU page record";
                    return false;
                }
                decoded.fill(0);
                break;
            case PageEncoding::Raw:
                if (stored_size != raw_size || stored_size > reader.size() - reader.offset()) {
                    error = "invalid raw GPU page size";
                    return false;
                }
                if (!reader.read(decoded.data(), decoded.size(), error))
                    return false;
                if (crc32_buffer(decoded.data(), decoded.size()) != checksum) {
                    error = "GPU page checksum mismatch";
                    return false;
                }
                break;
            case PageEncoding::Deflated:
                if (stored_size == 0 || stored_size > uint64_t(raw_size) + max_stored_page_overhead
                    || stored_size > reader.size() - reader.offset()) {
                    error = "invalid deflated GPU page size";
                    return false;
                }
                encoded.resize(stored_size);
                if (!reader.read(encoded.data(), encoded.size(), error)
                    || !decompress_buffer(encoded.data(), encoded.size(), decoded.size(), decompressed, error))
                    return false;
                if (crc32_buffer(decompressed.data(), decompressed.size()) != checksum) {
                    error = "GPU page checksum mismatch";
                    return false;
                }
                std::memcpy(decoded.data(), decompressed.data(), decoded.size());
                break;
            default:
                error = "unknown GPU page encoding";
                return false;
            }

            uint64_t staged_offset = UINT64_MAX;
            if (!staged_store->append(decoded.data(), decoded.size(), staged_offset, error))
                return false;
            staged_directory[i].pages[p].offset = offset;
            staged_directory[i].pages[p].staged_offset = staged_offset;
        }
    }
    if (reader.remaining()) {
        error = "extra data after GPU buffer page stream";
        return false;
    }
    buffers = std::move(staged_directory);
    store = std::move(staged_store);
    return true;
}

} // namespace emucorev::savestate
