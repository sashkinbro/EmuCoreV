#pragma once

#include <renderer/vulkan/surface_cache_snapshot.h>
#include <renderer/vulkan/context_snapshot.h>
#include <cstdint>
#include <string>
#include <vector>

struct EmuEnvState;

namespace emucorev::savestate {
class MemoryImage;
struct GxmSnapshot;

struct GpuBufferPage {
    uint32_t offset = 0;
    std::vector<uint8_t> bytes;
};
struct GpuBufferSnapshot {
    Address address = 0;
    uint32_t size = 0;
    std::vector<GpuBufferPage> pages;
};
struct GpuTargetSnapshot {
    Address address = 0;
    vkutil::ImageSnapshot color;
    vkutil::ImageSnapshot depth_stencil;
};
struct GpuContextClock {
    Address address = 0;
    uint64_t frame = 0;
    uint64_t scene = 0;
};
struct GpuSnapshot {
    uint32_t mapping_method = 0;
    float resolution = 1;
    int32_t last_scene = 0;
    int32_t current_frame = 0;
    renderer::vulkan::SurfaceCacheSnapshot surfaces;
    std::vector<GpuBufferSnapshot> buffers;
    std::vector<GpuTargetSnapshot> targets;
    std::vector<GpuContextClock> clocks;
    std::vector<renderer::vulkan::VKContextSnapshot> contexts;
};

// Rendering must be frozen between command batches, with GPU waits drained.
bool capture_gpu_state(EmuEnvState &, GpuSnapshot &, std::string &error);
bool encode_gpu_state(const GpuSnapshot &, std::vector<uint8_t> &, std::string &error);
bool parse_gpu_state(const std::vector<uint8_t> &, GpuSnapshot &, std::string &error);
bool validate_gpu_state(const GpuSnapshot &, const MemoryImage &, std::string &error);
bool validate_gpu_gxm_references(const GpuSnapshot &, const GxmSnapshot &, std::string &error);
bool capture_gpu_buffer_pages(const uint8_t *cpu, const uint8_t *gpu, uint32_t mapping_size,
    uint64_t allocation_size, uint64_t byte_budget, std::vector<GpuBufferPage> &, std::string &error);
// Wait for device work and every live Vulkan request-reader CPU handler before capture.
bool wait_gpu_snapshot_idle(EmuEnvState &, std::string &error);
// Drain and stop old GPU request readers before destroying GXM contexts.
bool prepare_gpu_teardown(EmuEnvState &, std::string &error);
// Old contexts/workers must be joined and caches reset before reconstruction.
bool prepare_gpu_state_restore(EmuEnvState &, std::string &error);
// GXM mappings, targets and logical contexts must already have been recreated.
bool restore_gpu_state(EmuEnvState &, const GpuSnapshot &, std::string &error);
}
