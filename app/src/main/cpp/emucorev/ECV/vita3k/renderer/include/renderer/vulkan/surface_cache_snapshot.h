// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

#include <renderer/vulkan/surface_cache.h>
#include <vkutil/image_snapshot.h>

#include <cstdint>
#include <string>
#include <vector>

namespace renderer::vulkan {

inline constexpr uint32_t surface_cache_snapshot_slots = 20;

// Image creation details needed to build a fresh, compatible Vulkan image on restore.
// The Vulkan image/view/allocation handles themselves are deliberately absent.
struct SurfaceCacheImageSnapshot {
    bool present = false;
    vk::ImageUsageFlags usage{};
    vk::ImageCreateFlags create_flags{};
    vk::ComponentMapping mapping = vkutil::default_comp_mapping;
    bool rgba8_format_list = false;
    vkutil::ImageSnapshot image;
};

struct ColorSurfaceSnapshot {
    bool active = false;
    Address guest_address = 0;
    uint32_t total_bytes = 0;
    SurfaceTiling tiling = SurfaceTiling::Linear;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t original_width = 0;
    uint16_t original_height = 0;
    uint32_t stride_bytes = 0;
    uint64_t last_frame_rendered = 0;
    uint64_t last_scene_rendered = 0;
    uint16_t rendered_w = 0;
    uint16_t rendered_h = 0;
    int32_t written_x0 = INT32_MAX;
    int32_t written_y0 = INT32_MAX;
    int32_t written_x1 = 0;
    int32_t written_y1 = 0;
    int32_t scene_x0 = 0;
    int32_t scene_y0 = 0;
    int32_t scene_x1 = 0;
    int32_t scene_y1 = 0;
    int32_t post_sync_x0 = 0;
    int32_t post_sync_y0 = 0;
    uint32_t post_sync_width = 0;
    uint32_t post_sync_height = 0;
    bool partial_write_back = false;
    SceGxmColorBaseFormat format = SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8;
    vk::ComponentMapping swizzle = vkutil::default_comp_mapping;
    bool content_is_blended = false;
    bool has_phase_view = false;
    bool need_surface_sync_object = false;
    bool need_surface_sync = false;
    bool need_post_surface_sync = false;
    bool need_buffer_sync = false;
    bool gpu_read_sync_only = false;
    bool dirty_object = false;
    bool dirty = false;
    int64_t repack_sync_age_ns = 0;
    SurfaceCacheImageSnapshot texture;
    SurfaceCacheImageSnapshot raw_image;
};

struct DepthStencilSurfaceSnapshot {
    bool active = false;
    SceGxmDepthStencilSurface surface{};
    int32_t memory_width = 0;
    int32_t memory_height = 0;
    uint32_t stride_samples = 0;
    SceGxmMultisampleMode multisample_mode = SCE_GXM_MULTISAMPLE_NONE;
    bool depth_content_stored = true;
    Address last_scene_color_addr = 0;
    uint32_t total_bytes = 0;
    SurfaceTiling tiling = SurfaceTiling::Linear;
    SurfaceCacheImageSnapshot texture;
};

// The cache has fixed-size queues. Each vector contains one record per queue slot,
// including inactive slots; the order vectors preserve exact MRU-to-LRU ordering.
// Indices and guest addresses are data, not pointers into the current process.
struct SurfaceCacheSnapshot {
    std::vector<ColorSurfaceSnapshot> colors;
    std::vector<uint8_t> color_mru_order;
    std::vector<DepthStencilSurfaceSnapshot> depth_stencil;
    std::vector<uint8_t> depth_stencil_mru_order;
    std::vector<Address> cpu_surfaces_changed;
    uint8_t last_written_color_slot = 0xFF;
    uint8_t pending_depth_stencil_slot = 0xFF;
    bool pending_depth_stencil_stores = false;
};

// Validate count caps, queue permutations, cross-references, metadata ranges and
// aggregate image bytes before any Vulkan object is created.
bool validate_surface_cache_snapshot(const SurfaceCacheSnapshot &snapshot, std::string &error);
bool encode_surface_cache_snapshot(const SurfaceCacheSnapshot &snapshot, std::vector<uint8_t> &data, std::string &error);
bool decode_surface_cache_snapshot(const std::vector<uint8_t> &data, SurfaceCacheSnapshot &snapshot, std::string &error);

// Caller must freeze rendering and submit all recorded work before capture. This
// function copies native GPU images only; it does not flush surfaces to guest RAM
// or alter cache/LRU/dirty state. Capture fails if unserializable pending casts exist.
bool capture_surface_cache_snapshot(VKSurfaceCache &cache, const vkutil::ImageSnapshotDevice &device,
    SurfaceCacheSnapshot &snapshot, std::string &error);

// Caller must have restored guest RAM and stopped rendering. Restore creates fresh
// GPU resources, rebuilds cache/address/LRU associations and reinstalls color-memory
// traps. The surrounding memory restore path must clear stale protect_tree callbacks
// before calling this function. The target association remains owned by GXM.
bool restore_surface_cache_snapshot(VKSurfaceCache &cache, MemState &mem,
    const vkutil::ImageSnapshotDevice &device, const SurfaceCacheSnapshot &snapshot, std::string &error);

} // namespace renderer::vulkan
