#pragma once

#include <emucorev/savestate/archive.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace emucorev::savestate {

constexpr uint32_t kMemoryImageFormatVersion = 1;
constexpr uint32_t kMemoryImagePageSize = 4096;
constexpr uint32_t kMemoryImageMaxChunkSize = 4u << 20;
constexpr uint64_t kMemoryImageArenaSize = 1ull << 32;

enum class MemoryChunkEncoding : uint32_t {
    Raw = 0,
    Deflated = 1,
    Zeros = 2,
};

struct MemorySpanInfo {
    uint32_t address = 0;
    uint32_t page_count = 0;
};

// A validated, immutable logical guest-memory image. Nonzero decoded chunks
// are staged in an owned temporary file so validation does not retain the full
// guest arena in host RAM. Zero chunks are represented by metadata and supplied
// to visitors from a bounded scratch buffer.
class MemoryImage {
public:
    using ChunkVisitor = std::function<bool(uint64_t address, const uint8_t *bytes, size_t size, std::string &error)>;

    ~MemoryImage();
    MemoryImage(const MemoryImage &) = delete;
    MemoryImage &operator=(const MemoryImage &) = delete;

    // Call Reader::validate_sections(error) before preflight so the archive-level
    // section checksum is verified before any validated chunks are visited.
    static std::unique_ptr<MemoryImage> preflight(
        Reader &reader, const fs::path &staging_directory, std::string &error);

    const std::vector<MemorySpanInfo> &spans() const { return spans_; }
    // These access only the staged snapshot, never the current guest arena.
    bool contains(uint64_t address, uint64_t size) const;
    bool read_bytes(uint64_t address, void *destination, size_t size, std::string &error) const;
    bool visit_validated_chunks(const ChunkVisitor &visitor, std::string &error) const;

private:
    struct Chunk {
        uint64_t address = 0;
        uint64_t staged_offset = 0;
        uint32_t size = 0;
        bool zeros = false;
    };

    MemoryImage() = default;
    bool open_staging_file(const fs::path &directory, std::string &error);
    bool stage(const uint8_t *bytes, size_t size, uint64_t &offset, std::string &error);

    fs::path staging_path_;
    FILE *staging_file_ = nullptr;
    uint64_t staged_size_ = 0;
    uint32_t chunk_size_ = 0;
    std::vector<MemorySpanInfo> spans_;
    std::vector<Chunk> chunks_;
};

} // namespace emucorev::savestate
