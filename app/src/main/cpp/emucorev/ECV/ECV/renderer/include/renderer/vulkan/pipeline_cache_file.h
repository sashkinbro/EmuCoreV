// Identity + validation of the on-disk Vulkan pipeline cache (docs/bugs/B148-stale-pipeline-cache-crash.md).
// A blob is handed to vkCreatePipelineCache ONLY if every field below matches the running build / driver and the payload hash
// verifies. Pure logic, host-tested (app/src/test/core-host/pipeline_cache_file_tests.cpp).
#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace renderer::vulkan {

inline constexpr uint32_t pipeline_cache_file_magic = 0xBEEF4323; // 4323: header carries the build/driver identity (B148)

struct PipelineCacheIdentity {
    uint64_t binary_id = 0; // changes with every relink of the emulator library (ELF build id / file stamp)
    uint32_t translator_version = 0; // shader::CURRENT_VERSION: SPIR-V generator / translator revision
    uint32_t driver_version = 0;
    uint32_t api_version = 0;
    uint32_t vendor_id = 0;
    uint32_t device_id = 0;
    std::array<uint8_t, 16> uuid{}; // VkPhysicalDeviceProperties::pipelineCacheUUID

    bool operator==(const PipelineCacheIdentity &) const = default;
};

struct PipelineCacheFileHeader {
    uint32_t magic = pipeline_cache_file_magic;
    uint32_t header_size = 0; // set by make_header()
    PipelineCacheIdentity identity;
    uint64_t blob_size = 0;
    uint64_t blob_hash = 0; // XXH3_64 of the blob
    uint64_t pipeline_count = 0; // number of 64-bit pipeline keys that follow the header
};

inline PipelineCacheFileHeader make_header(const PipelineCacheIdentity &id, uint64_t blob_size, uint64_t blob_hash, uint64_t pipeline_count) {
    PipelineCacheFileHeader h;
    h.header_size = sizeof(PipelineCacheFileHeader);
    h.identity = id;
    h.blob_size = blob_size;
    h.blob_hash = blob_hash;
    h.pipeline_count = pipeline_count;
    return h;
}

enum class CacheReject : uint8_t {
    None,
    TooSmall,
    Format, // other magic / header size (older build wrote it)
    Build, // another build of the emulator library wrote it
    Translator,
    Driver, // driver / api version
    Device, // vendor / device / pipelineCacheUUID
    Truncated, // the file is shorter than the header says
    Corrupt, // payload hash mismatch
};

inline const char *to_string(CacheReject r) {
    switch (r) {
    case CacheReject::None: return "accepted";
    case CacheReject::TooSmall: return "file too small";
    case CacheReject::Format: return "other file format";
    case CacheReject::Build: return "app build changed";
    case CacheReject::Translator: return "shader translator changed";
    case CacheReject::Driver: return "driver version changed";
    case CacheReject::Device: return "other GPU or pipeline cache uuid";
    case CacheReject::Truncated: return "file truncated";
    case CacheReject::Corrupt: return "payload hash mismatch";
    }
    return "?";
}

// structural + identity checks; the payload hash is verified by the caller once the bytes are read (CacheReject::Corrupt)
inline CacheReject validate_header(const PipelineCacheFileHeader &h, const PipelineCacheIdentity &expected, uint64_t file_size) {
    if (file_size < sizeof(PipelineCacheFileHeader))
        return CacheReject::TooSmall;
    if (h.magic != pipeline_cache_file_magic || h.header_size != sizeof(PipelineCacheFileHeader))
        return CacheReject::Format;
    // order = what a developer wants to read first in the log
    if (h.identity.binary_id != expected.binary_id)
        return CacheReject::Build;
    if (h.identity.translator_version != expected.translator_version)
        return CacheReject::Translator;
    if (h.identity.driver_version != expected.driver_version || h.identity.api_version != expected.api_version)
        return CacheReject::Driver;
    if (h.identity.vendor_id != expected.vendor_id || h.identity.device_id != expected.device_id || h.identity.uuid != expected.uuid)
        return CacheReject::Device;
    // overflow-safe: both counts come from the file
    const uint64_t room = file_size - sizeof(PipelineCacheFileHeader);
    if (h.pipeline_count > room / sizeof(uint64_t) || h.blob_size > room || h.pipeline_count * sizeof(uint64_t) > room - h.blob_size)
        return CacheReject::Truncated;
    return CacheReject::None;
}

} // namespace renderer::vulkan
