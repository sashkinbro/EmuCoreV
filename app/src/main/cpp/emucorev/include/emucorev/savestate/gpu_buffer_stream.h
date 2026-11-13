#pragma once

#include <emucorev/savestate/archive.h>
#include <util/fs.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace emucorev::savestate {

inline constexpr uint32_t kGpuSnapshotPageSize = 4096;

struct GpuSnapshotPage {
    uint32_t offset = 0;
    std::vector<uint8_t> bytes; // Used by legacy in-memory snapshots and focused tests.
    uint64_t staged_offset = UINT64_MAX;
};

struct GpuSnapshotBuffer {
    uint32_t address = 0;
    uint32_t size = 0;
    std::vector<GpuSnapshotPage> pages;
    // Schema 3 records the count here until its streamed page section is
    // validated. This avoids allocating from an untrusted count in metadata.
    uint32_t expected_page_count = 0;
};

// A bounded-RAM backing store for large GPU page payloads. The file is owned
// by this object and is removed on every normal or error exit.
class GpuPageStore {
public:
    GpuPageStore() = default;
    ~GpuPageStore();
    GpuPageStore(const GpuPageStore &) = delete;
    GpuPageStore &operator=(const GpuPageStore &) = delete;

    bool open(const fs::path &directory, std::string &error);
    bool append(const uint8_t *bytes, size_t size, uint64_t &offset, std::string &error);
    bool read(uint64_t offset, void *destination, size_t size, std::string &error) const;
    uint64_t size() const { return size_; }

private:
    fs::path path_;
    FILE *file_ = nullptr;
    uint64_t size_ = 0;
};

// VKState stores mappings in descending address order. Snapshot metadata and
// page streams use ascending order for overlap checks and deterministic IO.
void sort_gpu_snapshot_buffers(std::vector<GpuSnapshotBuffer> &buffers);

// Store and restore a versioned, per-page encoded buffer stream in its own
// uncompressed archive section. `max_payload_bytes` must come from the
// already-validated mapped-buffer directory, not from the stream itself.
bool write_gpu_buffer_page_section(Writer &writer, const std::vector<GpuSnapshotBuffer> &buffers,
    const GpuPageStore *store, uint64_t max_payload_bytes, std::string &error);
bool read_gpu_buffer_page_section(SectionReader &reader, std::vector<GpuSnapshotBuffer> &buffers,
    std::shared_ptr<GpuPageStore> &store, const fs::path &staging_directory,
    uint64_t max_payload_bytes, std::string &error);

} // namespace emucorev::savestate
