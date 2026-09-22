// EmuCoreV save-state container.
//
// Streaming, append-only file format:
//   [section payloads][section index][footer]
//
// Sections are written sequentially so a 512 MiB state never has to be held
// in host memory. Large sections store their own compressed chunks; small
// sections can be deflated as a whole by the container.

#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include <util/fs.h>

namespace emucorev::savestate {

constexpr uint32_t kFormatVersion = 1;
constexpr uint32_t kFooterMagic = 0x53564345; // "ECVS"
constexpr uint32_t kMaxChunkRawSize = 1u << 20;
constexpr uint32_t kMissingU32 = 0xFFFFFFFFu;

enum class SectionId : uint32_t {
    Meta = 1,
    Memory = 2,
    Threads = 3,
    Kernel = 4,
    Modules = 5,
    Display = 6,
    Gxm = 7,
    Io = 8,
    ObjStore = 9,
    KernelSync = 10,
    Ngs = 11,
    Audio = 12,
    Input = 13,
};

enum SectionFlags : uint32_t {
    kSectionFlagNone = 0,
    kSectionFlagCompressed = 1u << 0,
};

struct SectionIndexEntry {
    uint32_t id = 0;
    uint32_t flags = 0;
    uint64_t offset = 0;
    uint64_t stored_size = 0;
    uint64_t raw_size = 0;
    uint32_t checksum = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(SectionIndexEntry) == 40);

struct Footer {
    uint32_t magic = kFooterMagic;
    uint32_t version = kFormatVersion;
    uint64_t index_offset = 0;
    uint32_t section_count = 0;
    uint32_t reserved = 0;
    uint64_t file_size = 0;
    uint32_t checksum = 0;
    uint32_t reserved2 = 0;
};
static_assert(sizeof(Footer) == 40);

class Writer {
public:
    ~Writer();

    bool open(const fs::path &path, std::string &error);
    bool write_section(SectionId id, const void *data, size_t size, bool compress, std::string &error);

    bool begin_section(SectionId id, bool compress, std::string &error);
    bool append_section_data(const void *data, size_t size, std::string &error);
    bool end_section(std::string &error);

    bool finalize(std::string &error);
    void abort();

    uint64_t bytes_written() const;
    const fs::path &path() const { return path_; }

private:
    bool flush_pending(std::string &error);
    bool write_bytes(const void *data, size_t size, std::string &error);

    fs::path path_;
    fs::path temp_path_;
    FILE *file_ = nullptr;
    bool finalized_ = false;

    std::vector<SectionIndexEntry> index_;

    bool in_section_ = false;
    SectionId section_id_ = SectionId::Meta;
    bool section_compress_ = false;
    uint64_t section_offset_ = 0;
    uint64_t section_raw_size_ = 0;
    uint64_t section_stored_size_ = 0;
    uint32_t section_crc_ = 0;
    uint8_t in_buf_[1u << 16];
    size_t in_buf_used_ = 0;
    std::vector<uint8_t> comp_buf_;
    std::vector<uint8_t> raw_buf_;
};

class Reader;

class SectionReader {
public:
    bool read(void *dst, size_t size, std::string &error);
    bool skip(uint64_t size, std::string &error);
    bool remaining() const { return offset_ < size_; }
    uint64_t offset() const { return offset_; }
    uint64_t size() const { return size_; }

private:
    friend class Reader;
    Reader *owner_ = nullptr;
    uint64_t start_ = 0;
    uint64_t size_ = 0;
    uint64_t offset_ = 0;
    bool compressed_ = false;
    uint64_t raw_offset_ = 0;
    std::vector<uint8_t> inflate_buf_;
};

class Reader {
public:
    ~Reader();

    bool open(const fs::path &path, std::string &error);
    bool has_section(SectionId id) const;
    bool read_section(SectionId id, std::vector<uint8_t> &out, std::string &error);
    std::unique_ptr<SectionReader> open_section(SectionId id, std::string &error);

    uint32_t format_version() const { return footer_.version; }
    uint32_t section_count() const { return footer_.section_count; }

private:
    friend class SectionReader;
    bool read_raw(uint64_t offset, void *dst, size_t size, std::string &error);
    const SectionIndexEntry *find(SectionId id) const;

    fs::path path_;
    FILE *file_ = nullptr;
    Footer footer_;
    std::vector<SectionIndexEntry> index_;
};

std::string describe_error(const char *what);

uint32_t crc32_buffer(const void *data, size_t size, uint32_t seed = 0);
bool compress_buffer(const void *data, size_t size, std::vector<uint8_t> &out, std::string &error);
bool decompress_buffer(const void *data, size_t size, size_t raw_size, std::vector<uint8_t> &out, std::string &error);

} // namespace emucorev::savestate
