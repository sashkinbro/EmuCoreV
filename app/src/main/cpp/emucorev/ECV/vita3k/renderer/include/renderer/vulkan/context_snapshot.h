#pragma once

#include <cstdint>
#include <string>
#include <mem/ptr.h>

struct MemState;
struct FeatureState;

namespace renderer::vulkan {

// Bound snapshot-driven query-pool creation. 65,536 entries is deliberately
// generous for Vita visibility buffers while limiting hostile/corrupt snapshots
// to a modest native query pool and bookkeeping vector.
constexpr uint32_t kMaxVisibilityQueriesInSnapshot = 65'536;
constexpr uint32_t kMaxVisibilityStrideInSnapshot = kMaxVisibilityQueriesInSnapshot * sizeof(uint32_t);

struct VKContext;
struct VKRenderTarget;

// Backend-only continuation state for a context parked between renderer batches.
// GXM record state, guest command lists and timestamps are stored by their owners.
struct VKContextSnapshot {
    Address context_address = 0;
    Address render_target_address = 0;
    bool recording_open = false;

    bool scene_wrote_depth = false;
    bool scene_has_drawn = false;
    bool scene_macroblock_flushed = false;
    bool is_first_scene_draw = false;
    float surface_downscale = 1.0f;

    float viewport_x = 0.0f;
    float viewport_y = 0.0f;
    float viewport_width = 0.0f;
    float viewport_height = 0.0f;
    float viewport_min_depth = 0.0f;
    float viewport_max_depth = 1.0f;
    int32_t scissor_x = 0;
    int32_t scissor_y = 0;
    uint32_t scissor_width = 0;
    uint32_t scissor_height = 0;

    uint16_t last_macroblock_x = UINT16_MAX;
    uint16_t last_macroblock_y = UINT16_MAX;
    bool ignore_macroblock = false;
    int32_t rendered_rect_x0 = INT32_MAX;
    int32_t rendered_rect_y0 = INT32_MAX;
    int32_t rendered_rect_x1 = 0;
    int32_t rendered_rect_y1 = 0;
    int32_t draw_rect_x0 = INT32_MAX;
    int32_t draw_rect_y0 = INT32_MAX;
    int32_t draw_rect_x1 = 0;
    int32_t draw_rect_y1 = 0;

    // These are renderer-side sticky GXM settings, not guest pointers or VK handles.
    Address visibility_buffer_address = 0;
    uint32_t visibility_stride = 0;
    int32_t visibility_query_index = -1;
    bool visibility_query_increment = true;
};

bool validate_vk_context_snapshot(const VKContextSnapshot &snapshot, std::string &error);
// Renderer/guest gates must be held. Complete already ended scene segments
// without ending the fresh recording or publishing guest completion events.
bool prepare_vk_context_snapshot(VKContext &context, std::string &error);
bool capture_vk_context_snapshot(const VKContext &context, Address context_address,
    Address render_target_address, VKContextSnapshot &snapshot, std::string &error);

// Called after logical GXM state, render targets, surface-cache images and GPU clocks
// have been restored. Active recordings must be empty between-batch checkpoints.
bool restore_vk_context_snapshot(VKContext &context, MemState &mem, VKRenderTarget *render_target,
    const VKContextSnapshot &snapshot, const FeatureState &features, std::string &error);

// Internal scene setup entry point used by the snapshot restore helper.
bool restore_context_scene_checkpoint(VKContext &context, MemState &mem, VKRenderTarget *render_target,
    const VKContextSnapshot &snapshot, const FeatureState &features, std::string &error);

} // namespace renderer::vulkan
