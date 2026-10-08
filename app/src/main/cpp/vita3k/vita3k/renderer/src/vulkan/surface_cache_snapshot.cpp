// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <renderer/vulkan/surface_cache_snapshot.h>

#include <renderer/vulkan/state.h>
#include <util/align.h>
#include <vulkan/vulkan_format_traits.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <optional>
#include <set>

namespace renderer::vulkan {
namespace {
constexpr uint8_t no_slot = 0xFF;
constexpr uint64_t max_surface_cache_image_bytes = 128ull << 20;

std::optional<uint64_t> image_snapshot_byte_size(const vkutil::Image &image) {
    if (!image.width || !image.height || image.width > 65535 || image.height > 65535
        || vk::planeCount(image.format) != 1)
        return {};
    if (image.layout == vkutil::ImageLayout::Undefined)
        return 0;
    const uint64_t pixels = static_cast<uint64_t>(image.width) * image.height;
    uint32_t depth_bytes = 0;
    bool stencil = false;
    switch (image.format) {
    case vk::Format::eD16Unorm: depth_bytes = 2; break;
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat: depth_bytes = 4; break;
    case vk::Format::eD16UnormS8Uint: depth_bytes = 2; stencil = true; break;
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint: depth_bytes = 4; stencil = true; break;
    case vk::Format::eS8Uint: stencil = true; break;
    default: break;
    }
    uint64_t bytes = 0;
    if (depth_bytes || stencil) {
        if (depth_bytes)
            bytes += pixels * depth_bytes;
        if (stencil) {
            bytes = (bytes + 3) & ~uint64_t{ 3 };
            bytes += pixels;
        }
    } else {
        const uint32_t block_bytes = vk::blockSize(image.format);
        if (!block_bytes || vk::texelsPerBlock(image.format) != 1)
            return {};
        bytes = pixels * block_bytes;
    }
    return bytes <= max_surface_cache_image_bytes ? std::optional<uint64_t>(bytes) : std::nullopt;
}

vk::ImageUsageFlags color_usage(const VKState &state) {
    vk::ImageUsageFlags usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc
        | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment;
    if (state.features.support_shader_interlock)
        usage |= vk::ImageUsageFlagBits::eStorage;
    return usage;
}

vk::ImageCreateFlags color_create_flags(vk::Format format) {
    const bool mutable_rgba8 = format == vk::Format::eR8G8B8A8Unorm || format == vk::Format::eR8G8B8A8Srgb;
    const bool mutable_64bit = vk::blockSize(format) == 8;
    return mutable_rgba8 || mutable_64bit ? vk::ImageCreateFlagBits::eMutableFormat : vk::ImageCreateFlags{};
}

SurfaceCacheImageSnapshot capture_image(vkutil::Image &image, const vk::ImageUsageFlags usage,
    const vk::ImageCreateFlags flags, const bool format_list, const vk::ComponentMapping mapping,
    const vkutil::ImageSnapshotDevice &device, uint64_t &aggregate_bytes, std::string &error) {
    SurfaceCacheImageSnapshot result;
    if (!image.image)
        return result;
    const auto bytes = image_snapshot_byte_size(image);
    if (!bytes || *bytes > max_surface_cache_image_bytes - aggregate_bytes) {
        error = "surface-cache snapshot exceeds aggregate image cap";
        return result;
    }
    result.present = true;
    result.usage = usage;
    result.create_flags = flags;
    result.mapping = mapping;
    result.rgba8_format_list = format_list;
    if (!vkutil::capture_image_snapshot(device, image, result.image, error))
        result = {};
    else
        aggregate_bytes += *bytes;
    return result;
}

bool make_image(VKState &state, const SurfaceCacheImageSnapshot &snapshot,
    const vkutil::ImageSnapshotDevice &device, vkutil::Image &image, std::string &error) {
    if (!snapshot.present)
        return true;
    image = vkutil::Image(snapshot.image.width, snapshot.image.height, snapshot.image.format);
    static const vk::Format rgba8_views[] = { vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8A8Srgb };
    const vk::ImageFormatListCreateInfoKHR format_list{ .viewFormatCount = 2, .pViewFormats = rgba8_views };
    const void *p_next = snapshot.rgba8_format_list ? &format_list : nullptr;
    image.init_image(snapshot.usage, snapshot.mapping, snapshot.create_flags, p_next);
    return vkutil::restore_image_snapshot(device, image, snapshot.image, error);
}

template <typename Queue>
bool capture_lru_order(const Queue &queue, std::vector<uint8_t> &order, std::string &error) {
    order.clear();
    if (queue.items.size() != surface_cache_snapshot_slots || !queue.head) {
        error = "surface-cache queue is not initialized";
        return false;
    }
    const auto *item = queue.head;
    for (size_t i = 0; i < queue.items.size(); ++i) {
        const auto delta = reinterpret_cast<const char *>(item) - reinterpret_cast<const char *>(queue.items.data());
        if (delta < 0 || static_cast<size_t>(delta) >= queue.items.size() * sizeof(queue.items.front())
            || static_cast<size_t>(delta) % sizeof(queue.items.front()) != 0) {
            error = "surface-cache queue contains a foreign LRU link";
            return false;
        }
        order.push_back(static_cast<uint8_t>(delta / sizeof(queue.items.front())));
        item = item->next;
    }
    if (item != queue.head) {
        error = "surface-cache queue LRU links are not a cycle";
        return false;
    }
    if (order.size() != surface_cache_snapshot_slots) {
        error = "surface-cache LRU order has an invalid slot count";
        return false;
    }
    std::array<bool, surface_cache_snapshot_slots> seen{};
    for (const uint8_t slot : order) {
        if (slot >= surface_cache_snapshot_slots || seen[slot]) {
            error = "surface-cache LRU order is not a slot permutation";
            return false;
        }
        seen[slot] = true;
    }
    return true;
}

template <typename Queue>
void restore_lru_order(Queue &queue, const std::vector<uint8_t> &order) {
    for (size_t i = 0; i < order.size(); ++i) {
        auto &item = queue.items[order[i]];
        item.next = &queue.items[order[(i + 1) % order.size()]];
        item.prev = &queue.items[order[(i + order.size() - 1) % order.size()]];
    }
    queue.head = &queue.items[order.front()];
}

template <typename Queue, typename Info>
uint8_t get_slot(const Queue &queue, const Info *info) {
    if (!info)
        return no_slot;
    for (size_t i = 0; i < queue.items.size(); ++i) {
        if (&queue.items[i].content == info)
            return static_cast<uint8_t>(i);
    }
    return no_slot;
}

bool capture_color(ColorSurfaceCacheInfo &info, ColorSurfaceSnapshot &out,
    const VKState &state, const vkutil::ImageSnapshotDevice &device, uint64_t &aggregate_bytes, std::string &error) {
    if (!info.texture.image)
        return true;
    out.active = true;
    out.guest_address = info.data.address();
    out.total_bytes = info.total_bytes;
    out.tiling = info.tiling;
    out.width = info.width;
    out.height = info.height;
    out.original_width = info.original_width;
    out.original_height = info.original_height;
    out.stride_bytes = info.stride_bytes;
    out.last_frame_rendered = info.last_frame_rendered;
    out.last_scene_rendered = info.last_scene_rendered;
    out.rendered_w = info.rendered_w;
    out.rendered_h = info.rendered_h;
    out.written_x0 = info.written_x0;
    out.written_y0 = info.written_y0;
    out.written_x1 = info.written_x1;
    out.written_y1 = info.written_y1;
    out.scene_x0 = info.scene_x0;
    out.scene_y0 = info.scene_y0;
    out.scene_x1 = info.scene_x1;
    out.scene_y1 = info.scene_y1;
    out.post_sync_x0 = info.post_sync_x0;
    out.post_sync_y0 = info.post_sync_y0;
    out.post_sync_width = info.post_sync_width;
    out.post_sync_height = info.post_sync_height;
    out.partial_write_back = info.partial_write_back;
    out.format = info.format;
    out.swizzle = info.swizzle;
    out.content_is_blended = info.content_is_blended;
    out.has_phase_view = info.has_phase_view;
    out.need_surface_sync_object = bool(info.need_surface_sync);
    out.need_surface_sync = info.need_surface_sync && *info.need_surface_sync;
    out.need_post_surface_sync = info.need_post_surface_sync;
    out.need_buffer_sync = info.need_buffer_sync;
    out.gpu_read_sync_only = info.gpu_read_sync_only;
    out.dirty_object = bool(info.dirty);
    out.dirty = info.dirty && *info.dirty;
    out.repack_sync_age_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - info.last_repack_sync_time).count();
    const vk::ImageUsageFlags usage = color_usage(state);
    const vk::ImageCreateFlags flags = color_create_flags(info.texture.format);
    const bool format_list = state.surface_cache.support_image_format_specifier
        && (info.texture.format == vk::Format::eR8G8B8A8Unorm || info.texture.format == vk::Format::eR8G8B8A8Srgb);
    out.texture = capture_image(info.texture, usage, flags, format_list, vkutil::default_comp_mapping, device, aggregate_bytes, error);
    if (!error.empty())
        return false;
    if (info.raw_image) {
        out.raw_image = capture_image(*info.raw_image,
            vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
            vk::ImageCreateFlagBits::eMutableFormat, false, vkutil::default_comp_mapping, device, aggregate_bytes, error);
        if (!error.empty())
            return false;
    }
    return true;
}

bool capture_depth(DepthStencilSurfaceCacheInfo &info, DepthStencilSurfaceSnapshot &out,
    const vkutil::ImageSnapshotDevice &device, uint64_t &aggregate_bytes, std::string &error) {
    if (!info.texture.image)
        return true;
    out.active = true;
    out.surface = info.surface;
    out.memory_width = info.memory_width;
    out.memory_height = info.memory_height;
    out.stride_samples = info.stride_samples;
    out.multisample_mode = info.multisample_mode;
    out.depth_content_stored = info.depth_content_stored;
    out.last_scene_color_addr = info.last_scene_color_addr;
    out.total_bytes = info.total_bytes;
    out.tiling = info.tiling;
    out.texture = capture_image(info.texture,
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst
            | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
        {}, false, vkutil::default_comp_mapping, device, aggregate_bytes, error);
    return error.empty();
}

} // namespace

struct SurfaceCacheSnapshotAccess {
    static bool capture(VKSurfaceCache &cache, const vkutil::ImageSnapshotDevice &device,
        SurfaceCacheSnapshot &snapshot, std::string &error) {
        if (!cache.pending_casts.empty()) {
            error = "cannot capture surface cache while deferred casts are pending";
            return false;
        }
        SurfaceCacheSnapshot staged;
        staged.colors.resize(cache.color_surface_queue.items.size());
        staged.depth_stencil.resize(cache.ds_surface_queue.items.size());
        if (staged.colors.size() != surface_cache_snapshot_slots || staged.depth_stencil.size() != surface_cache_snapshot_slots) {
            error = "surface-cache queue is not initialized";
            return false;
        }
        if (!capture_lru_order(cache.color_surface_queue, staged.color_mru_order, error)
            || !capture_lru_order(cache.ds_surface_queue, staged.depth_stencil_mru_order, error))
            return false;
        uint64_t captured_image_bytes = 0;
        for (size_t i = 0; i < staged.colors.size(); ++i) {
            if (!capture_color(cache.color_surface_queue.items[i].content, staged.colors[i], cache.state, device, captured_image_bytes, error))
                return false;
        }
        for (size_t i = 0; i < staged.depth_stencil.size(); ++i) {
            if (!capture_depth(cache.ds_surface_queue.items[i].content, staged.depth_stencil[i], device, captured_image_bytes, error))
                return false;
        }
        staged.cpu_surfaces_changed = cache.cpu_surfaces_changed;
        staged.last_written_color_slot = get_slot(cache.color_surface_queue, cache.last_written_surface);
        staged.pending_depth_stencil_slot = get_slot(cache.ds_surface_queue, cache.pending_ds_scene);
        if ((cache.last_written_surface && staged.last_written_color_slot == no_slot)
            || (cache.pending_ds_scene && staged.pending_depth_stencil_slot == no_slot)) {
            error = "surface-cache association points outside its LRU queue";
            return false;
        }
        staged.pending_depth_stencil_stores = cache.pending_ds_scene_stores;
        if (!validate_surface_cache_snapshot(staged, error))
            return false;
        snapshot = std::move(staged);
        return true;
    }

    static bool restore(VKSurfaceCache &cache, MemState &mem, const vkutil::ImageSnapshotDevice &device,
        const SurfaceCacheSnapshot &snapshot, std::string &error) {
        if (!validate_surface_cache_snapshot(snapshot, error))
            return false;
        VKState &state = cache.state;

        // Build every image before replacing the live cache, so malformed data or a
        // Vulkan allocation/transfer failure leaves the existing cache untouched.
        std::vector<vkutil::Image> color_images(snapshot.colors.size());
        std::vector<std::unique_ptr<vkutil::Image>> raw_images(snapshot.colors.size());
        std::vector<vkutil::Image> depth_images(snapshot.depth_stencil.size());
        for (size_t i = 0; i < snapshot.colors.size(); ++i) {
            const auto &entry = snapshot.colors[i];
            if (!entry.active)
                continue;
            if (!make_image(state, entry.texture, device, color_images[i], error))
                return false;
            if (entry.raw_image.present) {
                raw_images[i] = std::make_unique<vkutil::Image>();
                if (!make_image(state, entry.raw_image, device, *raw_images[i], error))
                    return false;
            }
        }
        for (size_t i = 0; i < snapshot.depth_stencil.size(); ++i) {
            if (snapshot.depth_stencil[i].active
                && !make_image(state, snapshot.depth_stencil[i].texture, device, depth_images[i], error))
                return false;
        }

        cache.reset();
        for (size_t i = 0; i < snapshot.colors.size(); ++i) {
            const auto &source = snapshot.colors[i];
            auto &target = cache.color_surface_queue.items[i].content;
            if (!source.active)
                continue;
            target.texture = std::move(color_images[i]);
            target.raw_image = std::move(raw_images[i]);
            target.data = Ptr<void>(source.guest_address);
            target.total_bytes = source.total_bytes;
            target.tiling = source.tiling;
            target.width = source.width;
            target.height = source.height;
            target.original_width = source.original_width;
            target.original_height = source.original_height;
            target.stride_bytes = source.stride_bytes;
            target.last_frame_rendered = source.last_frame_rendered;
            target.last_scene_rendered = source.last_scene_rendered;
            target.rendered_w = source.rendered_w;
            target.rendered_h = source.rendered_h;
            target.written_x0 = source.written_x0;
            target.written_y0 = source.written_y0;
            target.written_x1 = source.written_x1;
            target.written_y1 = source.written_y1;
            target.scene_x0 = source.scene_x0;
            target.scene_y0 = source.scene_y0;
            target.scene_x1 = source.scene_x1;
            target.scene_y1 = source.scene_y1;
            target.post_sync_x0 = source.post_sync_x0;
            target.post_sync_y0 = source.post_sync_y0;
            target.post_sync_width = source.post_sync_width;
            target.post_sync_height = source.post_sync_height;
            target.partial_write_back = source.partial_write_back;
            target.format = source.format;
            target.swizzle = source.swizzle;
            target.content_is_blended = source.content_is_blended;
            target.has_phase_view = source.has_phase_view;
            target.need_surface_sync = source.need_surface_sync_object
                ? std::make_shared<bool>(source.need_surface_sync) : nullptr;
            target.need_post_surface_sync = source.need_post_surface_sync;
            target.need_buffer_sync = source.need_buffer_sync;
            target.gpu_read_sync_only = source.gpu_read_sync_only;
            target.dirty = source.dirty_object ? std::make_shared<bool>(source.dirty) : nullptr;
            const int64_t age = std::max<int64_t>(0, source.repack_sync_age_ns);
            target.last_repack_sync_time = std::chrono::steady_clock::now() - std::chrono::nanoseconds(age);
            cache.color_address_lookup.emplace(source.guest_address, &target);
        }
        for (size_t i = 0; i < snapshot.depth_stencil.size(); ++i) {
            const auto &source = snapshot.depth_stencil[i];
            auto &target = cache.ds_surface_queue.items[i].content;
            if (!source.active)
                continue;
            target.texture = std::move(depth_images[i]);
            target.surface = source.surface;
            target.memory_width = source.memory_width;
            target.memory_height = source.memory_height;
            target.stride_samples = source.stride_samples;
            target.multisample_mode = source.multisample_mode;
            target.depth_content_stored = source.depth_content_stored;
            target.last_scene_color_addr = source.last_scene_color_addr;
            target.total_bytes = source.total_bytes;
            target.tiling = source.tiling;
            if (source.surface.depth_data)
                cache.depth_address_lookup.emplace(source.surface.depth_data.address(), &target);
            if (source.surface.stencil_data)
                cache.stencil_address_lookup.emplace(source.surface.stencil_data.address(), &target);
        }
        restore_lru_order(cache.color_surface_queue, snapshot.color_mru_order);
        restore_lru_order(cache.ds_surface_queue, snapshot.depth_stencil_mru_order);
        cache.cpu_surfaces_changed = snapshot.cpu_surfaces_changed;
        cache.last_written_surface = snapshot.last_written_color_slot == no_slot
            ? nullptr : &cache.color_surface_queue.items[snapshot.last_written_color_slot].content;
        cache.pending_ds_scene = snapshot.pending_depth_stencil_slot == no_slot
            ? nullptr : &cache.ds_surface_queue.items[snapshot.pending_depth_stencil_slot].content;
        cache.pending_ds_scene_stores = snapshot.pending_depth_stencil_stores;
        cache.target = nullptr;

        for (auto &entry : snapshot.colors) {
            if (!entry.active || !entry.guest_address)
                continue;
            auto &info = *cache.color_address_lookup.at(entry.guest_address);
            if (!is_valid_addr(mem, entry.guest_address) || !cache.protect_cached_surface(mem, info)) {
                error = "failed to restore color surface memory trap";
                return false;
            }
        }
        return true;
    }
};

bool capture_surface_cache_snapshot(VKSurfaceCache &cache, const vkutil::ImageSnapshotDevice &device,
    SurfaceCacheSnapshot &snapshot, std::string &error) {
    error.clear();
    return SurfaceCacheSnapshotAccess::capture(cache, device, snapshot, error);
}

bool restore_surface_cache_snapshot(VKSurfaceCache &cache, MemState &mem,
    const vkutil::ImageSnapshotDevice &device, const SurfaceCacheSnapshot &snapshot, std::string &error) {
    error.clear();
    return SurfaceCacheSnapshotAccess::restore(cache, mem, device, snapshot, error);
}

} // namespace renderer::vulkan
