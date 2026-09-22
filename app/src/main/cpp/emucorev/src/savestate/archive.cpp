#include <emucorev/savestate/archive.h>

#include <miniz.h>

#include <cstring>
#include <limits>

namespace emucorev::savestate {

namespace {

bool file_size_of(FILE *file, uint64_t &size) {
    const long current = std::ftell(file);
    if (current < 0)
        return false;
    if (std::fseek(file, 0, SEEK_END) != 0)
        return false;
    const long end = std::ftell(file);
    if (end < 0)
        return false;
    if (std::fseek(file, current, SEEK_SET) != 0)
        return false;
    size = static_cast<uint64_t>(end);
    return true;
}

} // namespace

std::string describe_error(const char *what) {
    return what ? std::string(what) : std::string();
}

uint32_t crc32_buffer(const void *data, size_t size, uint32_t seed) {
    return static_cast<uint32_t>(mz_crc32(seed, static_cast<const unsigned char *>(data), size));
}

bool compress_buffer(const void *data, size_t size, std::vector<uint8_t> &out, std::string &error) {
    const mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(size));
    out.resize(bound);
    mz_ulong out_len = bound;
    const int result = mz_compress2(out.data(), &out_len, static_cast<const unsigned char *>(data),
        static_cast<mz_ulong>(size), MZ_BEST_SPEED);
    if (result != MZ_OK) {
        error = "compression failed";
        out.clear();
        return false;
    }
    out.resize(out_len);
    return true;
}

bool decompress_buffer(const void *data, size_t size, size_t raw_size, std::vector<uint8_t> &out, std::string &error) {
    out.resize(raw_size);
    if (raw_size == 0)
        return true;
    mz_ulong out_len = static_cast<mz_ulong>(raw_size);
    const int result = mz_uncompress(out.data(), &out_len, static_cast<const unsigned char *>(data),
        static_cast<mz_ulong>(size));
    if (result != MZ_OK || out_len != raw_size) {
        error = "decompression failed";
        out.clear();
        return false;
    }
    return true;
}

Writer::~Writer() {
    if (file_)
        abort();
}

bool Writer::open(const fs::path &path, std::string &error) {
    if (file_) {
        error = "writer already open";
        return false;
    }

    path_ = path;
    temp_path_ = path;
    temp_path_ += ".tmp";

    boost::system::error_code ec;
    fs::remove(temp_path_, ec);

    file_ = FOPEN(temp_path_.c_str(), "wb");
    if (!file_) {
        error = "unable to create " + temp_path_.string();
        return false;
    }

    comp_buf_.resize(mz_compressBound(kMaxChunkRawSize) + 16);
    return true;
}

bool Writer::write_bytes(const void *data, size_t size, std::string &error) {
    if (size == 0)
        return true;
    if (std::fwrite(data, 1, size, file_) != size) {
        error = "write failed";
        return false;
    }
    return true;
}

bool Writer::flush_pending(std::string &error) {
    if (in_buf_used_ == 0)
        return true;
    if (!write_bytes(in_buf_, in_buf_used_, error))
        return false;
    in_buf_used_ = 0;
    return true;
}

bool Writer::write_section(SectionId id, const void *data, size_t size, bool compress, std::string &error) {
    if (!begin_section(id, compress, error))
        return false;
    if (!append_section_data(data, size, error)) {
        abort();
        return false;
    }
    return end_section(error);
}

bool Writer::begin_section(SectionId id, bool compress, std::string &error) {
    if (!file_) {
        error = "writer not open";
        return false;
    }
    if (in_section_) {
        error = "section already open";
        return false;
    }

    const long offset = std::ftell(file_);
    if (offset < 0) {
        error = "unable to tell file position";
        return false;
    }

    in_section_ = true;
    section_id_ = id;
    section_compress_ = compress;
    section_offset_ = static_cast<uint64_t>(offset);
    section_raw_size_ = 0;
    section_stored_size_ = 0;
    section_crc_ = 0;
    in_buf_used_ = 0;
    raw_buf_.clear();
    return true;
}

bool Writer::append_section_data(const void *data, size_t size, std::string &error) {
    if (!in_section_) {
        error = "no open section";
        return false;
    }
    if (size == 0)
        return true;

    const auto *bytes = static_cast<const uint8_t *>(data);
    section_crc_ = crc32_buffer(bytes, size, section_crc_);

    if (section_compress_) {
        raw_buf_.insert(raw_buf_.end(), bytes, bytes + size);
        return true;
    }

    if (!flush_pending(error))
        return false;
    if (!write_bytes(bytes, size, error))
        return false;
    section_stored_size_ += size;
    section_raw_size_ += size;
    return true;
}

bool Writer::end_section(std::string &error) {
    if (!in_section_) {
        error = "no open section";
        return false;
    }

    if (section_compress_) {
        std::vector<uint8_t> compressed;
        if (!raw_buf_.empty()) {
            if (!compress_buffer(raw_buf_.data(), raw_buf_.size(), compressed, error))
                return false;
        }
        const uint64_t raw_size = raw_buf_.size();
        const uint64_t stored_size = compressed.size();
        uint8_t prefix[16];
        std::memcpy(prefix, &raw_size, 8);
        std::memcpy(prefix + 8, &stored_size, 8);
        if (!write_bytes(prefix, sizeof(prefix), error))
            return false;
        if (!compressed.empty() && !write_bytes(compressed.data(), compressed.size(), error))
            return false;
        section_stored_size_ = sizeof(prefix) + compressed.size();
        section_raw_size_ = raw_size;
    } else {
        if (!flush_pending(error))
            return false;
    }

    SectionIndexEntry entry;
    entry.id = static_cast<uint32_t>(section_id_);
    entry.flags = section_compress_ ? kSectionFlagCompressed : kSectionFlagNone;
    entry.offset = section_offset_;
    entry.stored_size = section_stored_size_;
    entry.raw_size = section_raw_size_;
    entry.checksum = section_crc_;
    index_.push_back(entry);

    in_section_ = false;
    raw_buf_.clear();
    return true;
}

bool Writer::finalize(std::string &error) {
    if (!file_) {
        error = "writer not open";
        return false;
    }
    if (in_section_) {
        error = "section still open";
        return false;
    }

    const long index_offset = std::ftell(file_);
    if (index_offset < 0) {
        error = "unable to tell file position";
        return false;
    }

    if (!index_.empty()) {
        if (!write_bytes(index_.data(), index_.size() * sizeof(SectionIndexEntry), error))
            return false;
    }

    const long end_offset = std::ftell(file_);
    if (end_offset < 0) {
        error = "unable to tell file position";
        return false;
    }

    Footer footer;
    footer.index_offset = static_cast<uint64_t>(index_offset);
    footer.section_count = static_cast<uint32_t>(index_.size());
    footer.file_size = static_cast<uint64_t>(end_offset) + sizeof(Footer);
    footer.checksum = crc32_buffer(&footer, sizeof(Footer));

    if (!write_bytes(&footer, sizeof(Footer), error))
        return false;

    if (std::fflush(file_) != 0) {
        error = "flush failed";
        return false;
    }

    std::fclose(file_);
    file_ = nullptr;

    boost::system::error_code ec;
    fs::remove(path_, ec);
    fs::rename(temp_path_, path_, ec);
    if (ec) {
        error = "unable to commit file: " + ec.message();
        fs::remove(temp_path_, ec);
        return false;
    }

    finalized_ = true;
    return true;
}

void Writer::abort() {
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
    if (!temp_path_.empty()) {
        boost::system::error_code ec;
        fs::remove(temp_path_, ec);
    }
    in_section_ = false;
}

uint64_t Writer::bytes_written() const {
    if (!file_)
        return 0;
    const long pos = std::ftell(file_);
    return pos < 0 ? 0 : static_cast<uint64_t>(pos);
}

Reader::~Reader() {
    if (file_)
        std::fclose(file_);
}

bool Reader::open(const fs::path &path, std::string &error) {
    if (file_) {
        error = "reader already open";
        return false;
    }

    path_ = path;
    file_ = FOPEN(path_.c_str(), "rb");
    if (!file_) {
        error = "unable to open " + path_.string();
        return false;
    }

    uint64_t size = 0;
    if (!file_size_of(file_, size) || size < sizeof(Footer)) {
        error = "file is too small to be a save state";
        return false;
    }

    if (std::fseek(file_, static_cast<long>(size - sizeof(Footer)), SEEK_SET) != 0) {
        error = "unable to read footer";
        return false;
    }

    if (std::fread(&footer_, 1, sizeof(Footer), file_) != sizeof(Footer)) {
        error = "unable to read footer";
        return false;
    }

    if (footer_.magic != kFooterMagic) {
        error = "not an EmuCoreV save state";
        return false;
    }
    if (footer_.version != kFormatVersion) {
        error = "unsupported save state version";
        return false;
    }
    if (footer_.file_size != size) {
        error = "save state is truncated";
        return false;
    }

    const uint32_t expected_crc = footer_.checksum;
    footer_.checksum = 0;
    if (crc32_buffer(&footer_, sizeof(Footer)) != expected_crc) {
        error = "save state footer is corrupted";
        return false;
    }
    footer_.checksum = expected_crc;

    if (footer_.section_count == 0 || footer_.section_count > 64) {
        error = "invalid save state index";
        return false;
    }

    index_.resize(footer_.section_count);
    const uint64_t index_bytes = static_cast<uint64_t>(footer_.section_count) * sizeof(SectionIndexEntry);
    if (footer_.index_offset + index_bytes + sizeof(Footer) > size) {
        error = "invalid save state index";
        return false;
    }
    if (std::fseek(file_, static_cast<long>(footer_.index_offset), SEEK_SET) != 0
        || std::fread(index_.data(), 1, index_bytes, file_) != index_bytes) {
        error = "unable to read save state index";
        return false;
    }

    for (const SectionIndexEntry &entry : index_) {
        if (entry.offset + entry.stored_size > size) {
            error = "save state section is out of bounds";
            return false;
        }
    }

    return true;
}

const SectionIndexEntry *Reader::find(SectionId id) const {
    for (const SectionIndexEntry &entry : index_) {
        if (entry.id == static_cast<uint32_t>(id))
            return &entry;
    }
    return nullptr;
}

bool Reader::has_section(SectionId id) const {
    return find(id) != nullptr;
}

bool Reader::read_raw(uint64_t offset, void *dst, size_t size, std::string &error) {
    if (std::fseek(file_, static_cast<long>(offset), SEEK_SET) != 0) {
        error = "seek failed";
        return false;
    }
    if (size && std::fread(dst, 1, size, file_) != size) {
        error = "short read";
        return false;
    }
    return true;
}

bool Reader::read_section(SectionId id, std::vector<uint8_t> &out, std::string &error) {
    const SectionIndexEntry *entry = find(id);
    if (!entry) {
        error = "section not found";
        return false;
    }

    const bool compressed = (entry->flags & kSectionFlagCompressed) != 0;
    if (!compressed) {
        out.resize(entry->raw_size);
        if (entry->raw_size && !read_raw(entry->offset, out.data(), out.size(), error))
            return false;
    } else {
        uint8_t prefix[16];
        if (!read_raw(entry->offset, prefix, sizeof(prefix), error))
            return false;
        uint64_t raw_size = 0;
        uint64_t stored_size = 0;
        std::memcpy(&raw_size, prefix, 8);
        std::memcpy(&stored_size, prefix + 8, 8);
        if (raw_size != entry->raw_size || stored_size > entry->stored_size) {
            error = "corrupted section header";
            return false;
        }
        std::vector<uint8_t> stored(stored_size);
        if (stored_size && !read_raw(entry->offset + sizeof(prefix), stored.data(), stored.size(), error))
            return false;
        if (!decompress_buffer(stored.data(), stored.size(), raw_size, out, error))
            return false;
    }

    if (crc32_buffer(out.data(), out.size()) != entry->checksum) {
        error = "section checksum mismatch";
        out.clear();
        return false;
    }
    return true;
}

std::unique_ptr<SectionReader> Reader::open_section(SectionId id, std::string &error) {
    const SectionIndexEntry *entry = find(id);
    if (!entry) {
        error = "section not found";
        return nullptr;
    }
    if (entry->flags & kSectionFlagCompressed) {
        error = "section is compressed";
        return nullptr;
    }

    auto section = std::make_unique<SectionReader>();
    section->owner_ = this;
    section->start_ = entry->offset;
    section->size_ = entry->stored_size;
    section->offset_ = 0;
    section->compressed_ = false;
    return section;
}

bool SectionReader::read(void *dst, size_t size, std::string &error) {
    if (compressed_) {
        error = "compressed section streaming is not supported";
        return false;
    }
    if (offset_ + size > size_) {
        error = "read past end of section";
        return false;
    }
    if (size && !owner_->read_raw(start_ + offset_, dst, size, error))
        return false;
    offset_ += size;
    return true;
}

bool SectionReader::skip(uint64_t size, std::string &error) {
    if (offset_ + size > size_) {
        error = "skip past end of section";
        return false;
    }
    offset_ += size;
    return true;
}

} // namespace emucorev::savestate
