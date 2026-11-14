// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <renderer/vulkan/surface_cache_snapshot.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <limits>
#include <set>

namespace renderer::vulkan {
namespace {
constexpr uint64_t max_surface_cache_snapshot_bytes = 128ull << 20;
constexpr uint8_t no_slot = 0xFF;
constexpr uint32_t surface_snapshot_magic = 0x31534356; // "VCS1", little endian
constexpr uint32_t surface_snapshot_version = 1;

class SnapshotWriter {
public:
    void u8(uint8_t value) { data.push_back(value); }
    void u16(uint16_t value) {
        u8(static_cast<uint8_t>(value));
        u8(static_cast<uint8_t>(value >> 8));
    }
    void u32(uint32_t value) {
        for (unsigned shift = 0; shift != 32; shift += 8)
            u8(static_cast<uint8_t>(value >> shift));
    }
    void u64(uint64_t value) {
        for (unsigned shift = 0; shift != 64; shift += 8)
            u8(static_cast<uint8_t>(value >> shift));
    }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void i64(int64_t value) { u64(static_cast<uint64_t>(value)); }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void bytes(const std::vector<uint8_t> &value) {
        u64(value.size());
        data.insert(data.end(), value.begin(), value.end());
    }
    std::vector<uint8_t> data;
};

class SnapshotReader {
public:
    explicit SnapshotReader(const std::vector<uint8_t> &input)
        : input(input) {
    }
    bool u8(uint8_t &value) {
        if (!range(1)) return false;
        value = input[offset++];
        return true;
    }
    bool u16(uint16_t &value) {
        uint8_t a, b;
        if (!u8(a) || !u8(b)) return false;
        value = static_cast<uint16_t>(a | (static_cast<uint16_t>(b) << 8));
        return true;
    }
    bool u32(uint32_t &value) {
        value = 0;
        for (unsigned shift = 0; shift != 32; shift += 8) {
            uint8_t byte;
            if (!u8(byte)) return false;
            value |= static_cast<uint32_t>(byte) << shift;
        }
        return true;
    }
    bool u64(uint64_t &value) {
        value = 0;
        for (unsigned shift = 0; shift != 64; shift += 8) {
            uint8_t byte;
            if (!u8(byte)) return false;
            value |= static_cast<uint64_t>(byte) << shift;
        }
        return true;
    }
    bool i32(int32_t &value) {
        uint32_t raw;
        if (!u32(raw)) return false;
        value = static_cast<int32_t>(raw);
        return true;
    }
    bool i64(int64_t &value) {
        uint64_t raw;
        if (!u64(raw)) return false;
        value = static_cast<int64_t>(raw);
        return true;
    }
    bool boolean(bool &value) {
        uint8_t raw;
        if (!u8(raw) || raw > 1) return false;
        value = raw != 0;
        return true;
    }
    bool bytes(std::vector<uint8_t> &value, uint64_t total_cap, uint64_t &aggregate) {
        uint64_t size;
        if (!u64(size) || size > total_cap - aggregate || size > remaining())
            return false;
        value.assign(input.begin() + offset, input.begin() + offset + static_cast<size_t>(size));
        offset += static_cast<size_t>(size);
        aggregate += size;
        return true;
    }
    size_t remaining() const { return input.size() - offset; }
    bool at_end() const { return offset == input.size(); }

private:
    bool range(size_t size) {
        return size <= input.size() - offset;
    }
    const std::vector<uint8_t> &input;
    size_t offset = 0;
};

bool valid_tiling(SurfaceTiling tiling) {
    return tiling == SurfaceTiling::Linear || tiling == SurfaceTiling::Swizzled || tiling == SurfaceTiling::Tiled;
}

bool valid_component_mapping(const vk::ComponentMapping &mapping) {
    const auto valid = [](vk::ComponentSwizzle component) {
        return static_cast<uint32_t>(component) <= static_cast<uint32_t>(vk::ComponentSwizzle::eA);
    };
    return valid(mapping.r) && valid(mapping.g) && valid(mapping.b) && valid(mapping.a);
}

bool validate_image(const SurfaceCacheImageSnapshot &snapshot, uint64_t &aggregate_bytes, std::string &error) {
    if (!snapshot.present) {
        if (!snapshot.image.bytes.empty() || snapshot.image.width || snapshot.image.height
            || snapshot.image.format != vk::Format::eUndefined || snapshot.image.layout != vkutil::ImageLayout::Undefined
            || snapshot.usage || snapshot.create_flags || snapshot.rgba8_format_list) {
            error = "absent surface-cache image contains data";
            return false;
        }
        return true;
    }
    constexpr VkImageUsageFlags allowed_usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
        | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
        | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
    if (!valid_component_mapping(snapshot.mapping) || !snapshot.usage
        || (static_cast<VkImageUsageFlags>(snapshot.usage) & ~allowed_usage)
        || !(snapshot.usage & vk::ImageUsageFlagBits::eTransferSrc)
        || !(snapshot.usage & vk::ImageUsageFlagBits::eTransferDst)
        || !vkutil::validate_image_snapshot(snapshot.image, error)) {
        if (error.empty())
            error = "invalid surface-cache image metadata";
        return false;
    }
    const auto flags = static_cast<VkImageCreateFlags>(snapshot.create_flags);
    if ((flags & ~static_cast<VkImageCreateFlags>(vk::ImageCreateFlagBits::eMutableFormat))
        || (snapshot.rgba8_format_list
            && (!(snapshot.create_flags & vk::ImageCreateFlagBits::eMutableFormat)
                || (snapshot.image.format != vk::Format::eR8G8B8A8Unorm && snapshot.image.format != vk::Format::eR8G8B8A8Srgb)))) {
        error = "invalid surface-cache image creation flags";
        return false;
    }
    if (snapshot.image.bytes.size() > max_surface_cache_snapshot_bytes - aggregate_bytes) {
        error = "surface-cache snapshot exceeds aggregate image cap";
        return false;
    }
    aggregate_bytes += snapshot.image.bytes.size();
    return true;
}

bool validate_queue_order(const std::vector<uint8_t> &order, size_t count, std::string &error) {
    if (count != surface_cache_snapshot_slots || order.size() != count) {
        error = "surface-cache LRU order has an invalid slot count";
        return false;
    }
    std::array<bool, surface_cache_snapshot_slots> seen{};
    for (const uint8_t slot : order) {
        if (slot >= count || seen[slot]) {
            error = "surface-cache LRU order is not a slot permutation";
            return false;
        }
        seen[slot] = true;
    }
    return true;
}

template <typename T>
bool slot_is_active(const std::vector<T> &entries, uint8_t slot) {
    return slot != no_slot && slot < entries.size() && entries[slot].active;
}

void write_mapping(SnapshotWriter &writer, const vk::ComponentMapping &mapping) {
    writer.u32(static_cast<uint32_t>(mapping.r));
    writer.u32(static_cast<uint32_t>(mapping.g));
    writer.u32(static_cast<uint32_t>(mapping.b));
    writer.u32(static_cast<uint32_t>(mapping.a));
}

bool read_mapping(SnapshotReader &reader, vk::ComponentMapping &mapping) {
    uint32_t r, g, b, a;
    if (!reader.u32(r) || !reader.u32(g) || !reader.u32(b) || !reader.u32(a))
        return false;
    mapping = { static_cast<vk::ComponentSwizzle>(r), static_cast<vk::ComponentSwizzle>(g),
        static_cast<vk::ComponentSwizzle>(b), static_cast<vk::ComponentSwizzle>(a) };
    return true;
}

void write_image(SnapshotWriter &writer, const SurfaceCacheImageSnapshot &image) {
    writer.boolean(image.present);
    if (!image.present)
        return;
    writer.u32(static_cast<uint32_t>(image.usage));
    writer.u32(static_cast<uint32_t>(image.create_flags));
    write_mapping(writer, image.mapping);
    writer.boolean(image.rgba8_format_list);
    writer.u32(image.image.width);
    writer.u32(image.image.height);
    writer.u32(static_cast<uint32_t>(image.image.format));
    writer.u32(static_cast<uint32_t>(image.image.layout));
    writer.bytes(image.image.bytes);
}

bool read_image(SnapshotReader &reader, SurfaceCacheImageSnapshot &image, uint64_t &aggregate) {
    if (!reader.boolean(image.present))
        return false;
    if (!image.present)
        return true;
    uint32_t usage, flags, format, layout;
    if (!reader.u32(usage) || !reader.u32(flags) || !read_mapping(reader, image.mapping)
        || !reader.boolean(image.rgba8_format_list) || !reader.u32(image.image.width)
        || !reader.u32(image.image.height) || !reader.u32(format) || !reader.u32(layout))
        return false;
    image.usage = vk::ImageUsageFlags(static_cast<vk::ImageUsageFlagBits>(usage));
    image.create_flags = vk::ImageCreateFlags(static_cast<vk::ImageCreateFlagBits>(flags));
    image.image.format = static_cast<vk::Format>(format);
    image.image.layout = static_cast<vkutil::ImageLayout>(layout);
    return reader.bytes(image.image.bytes, max_surface_cache_snapshot_bytes, aggregate);
}

void write_color(SnapshotWriter &writer, const ColorSurfaceSnapshot &entry) {
    writer.boolean(entry.active);
    if (!entry.active)
        return;
    writer.u32(entry.guest_address);
    writer.u32(entry.total_bytes);
    writer.u32(static_cast<uint32_t>(entry.tiling));
    writer.u16(entry.width); writer.u16(entry.height); writer.u16(entry.original_width); writer.u16(entry.original_height);
    writer.u32(entry.stride_bytes);
    writer.u64(entry.last_frame_rendered); writer.u64(entry.last_scene_rendered);
    writer.u16(entry.rendered_w); writer.u16(entry.rendered_h);
    writer.i32(entry.written_x0); writer.i32(entry.written_y0); writer.i32(entry.written_x1); writer.i32(entry.written_y1);
    writer.i32(entry.scene_x0); writer.i32(entry.scene_y0); writer.i32(entry.scene_x1); writer.i32(entry.scene_y1);
    writer.i32(entry.post_sync_x0); writer.i32(entry.post_sync_y0);
    writer.u32(entry.post_sync_width); writer.u32(entry.post_sync_height);
    writer.boolean(entry.partial_write_back);
    writer.u32(static_cast<uint32_t>(entry.format));
    write_mapping(writer, entry.swizzle);
    writer.boolean(entry.content_is_blended); writer.boolean(entry.has_phase_view);
    writer.boolean(entry.need_surface_sync_object); writer.boolean(entry.need_surface_sync);
    writer.boolean(entry.need_post_surface_sync); writer.boolean(entry.need_buffer_sync);
    writer.boolean(entry.gpu_read_sync_only); writer.boolean(entry.dirty_object); writer.boolean(entry.dirty);
    writer.i64(entry.repack_sync_age_ns);
    write_image(writer, entry.texture);
    write_image(writer, entry.raw_image);
}

bool read_color(SnapshotReader &reader, ColorSurfaceSnapshot &entry, uint64_t &aggregate) {
    if (!reader.boolean(entry.active))
        return false;
    if (!entry.active)
        return true;
    uint32_t tiling, format;
    if (!reader.u32(entry.guest_address) || !reader.u32(entry.total_bytes) || !reader.u32(tiling)
        || !reader.u16(entry.width) || !reader.u16(entry.height) || !reader.u16(entry.original_width) || !reader.u16(entry.original_height)
        || !reader.u32(entry.stride_bytes) || !reader.u64(entry.last_frame_rendered) || !reader.u64(entry.last_scene_rendered)
        || !reader.u16(entry.rendered_w) || !reader.u16(entry.rendered_h)
        || !reader.i32(entry.written_x0) || !reader.i32(entry.written_y0) || !reader.i32(entry.written_x1) || !reader.i32(entry.written_y1)
        || !reader.i32(entry.scene_x0) || !reader.i32(entry.scene_y0) || !reader.i32(entry.scene_x1) || !reader.i32(entry.scene_y1)
        || !reader.i32(entry.post_sync_x0) || !reader.i32(entry.post_sync_y0)
        || !reader.u32(entry.post_sync_width) || !reader.u32(entry.post_sync_height)
        || !reader.boolean(entry.partial_write_back) || !reader.u32(format) || !read_mapping(reader, entry.swizzle)
        || !reader.boolean(entry.content_is_blended) || !reader.boolean(entry.has_phase_view)
        || !reader.boolean(entry.need_surface_sync_object) || !reader.boolean(entry.need_surface_sync)
        || !reader.boolean(entry.need_post_surface_sync) || !reader.boolean(entry.need_buffer_sync)
        || !reader.boolean(entry.gpu_read_sync_only) || !reader.boolean(entry.dirty_object) || !reader.boolean(entry.dirty)
        || !reader.i64(entry.repack_sync_age_ns))
        return false;
    entry.tiling = static_cast<SurfaceTiling>(tiling);
    entry.format = static_cast<SceGxmColorBaseFormat>(format);
    return read_image(reader, entry.texture, aggregate) && read_image(reader, entry.raw_image, aggregate);
}

void write_depth(SnapshotWriter &writer, const DepthStencilSurfaceSnapshot &entry) {
    writer.boolean(entry.active);
    if (!entry.active)
        return;
    uint32_t control0, control1;
    control0 = (entry.surface.unk1 & 1u)
        | ((entry.surface.force_load & 1u) << 1)
        | ((entry.surface.force_store & 1u) << 2)
        | ((entry.surface._stride & 0xFFu) << 3)
        | ((entry.surface._type_and_format & 0xFFFFFu) << 12);
    control1 = (entry.surface.stencil & 0xFFu)
        | ((entry.surface.mask & 1u) << 8)
        | ((entry.surface.unk2 & 1u) << 9);
    writer.u32(control0);
    writer.u32(entry.surface.depth_data.address());
    writer.u32(entry.surface.stencil_data.address());
    uint32_t depth_bits;
    std::memcpy(&depth_bits, &entry.surface.background_depth, sizeof(depth_bits));
    writer.u32(depth_bits);
    writer.u32(control1);
    writer.i32(entry.memory_width); writer.i32(entry.memory_height);
    writer.u32(entry.stride_samples); writer.u32(static_cast<uint32_t>(entry.multisample_mode));
    writer.boolean(entry.depth_content_stored);
    writer.u32(entry.last_scene_color_addr);
    writer.u32(entry.total_bytes); writer.u32(static_cast<uint32_t>(entry.tiling));
    write_image(writer, entry.texture);
}

bool read_depth(SnapshotReader &reader, DepthStencilSurfaceSnapshot &entry, uint64_t &aggregate) {
    if (!reader.boolean(entry.active))
        return false;
    if (!entry.active)
        return true;
    uint32_t control0, depth_addr, stencil_addr, depth_bits, control1, multisample, tiling;
    if (!reader.u32(control0) || !reader.u32(depth_addr) || !reader.u32(stencil_addr) || !reader.u32(depth_bits)
        || !reader.u32(control1) || !reader.i32(entry.memory_width) || !reader.i32(entry.memory_height)
        || !reader.u32(entry.stride_samples) || !reader.u32(multisample) || !reader.boolean(entry.depth_content_stored)
        || !reader.u32(entry.last_scene_color_addr) || !reader.u32(entry.total_bytes) || !reader.u32(tiling))
        return false;
    entry.surface.unk1 = control0 & 1;
    entry.surface.force_load = (control0 >> 1) & 1;
    entry.surface.force_store = (control0 >> 2) & 1;
    entry.surface._stride = (control0 >> 3) & 0xFF;
    entry.surface._type_and_format = (control0 >> 12) & 0xFFFFF;
    entry.surface.depth_data = Ptr<void>(depth_addr);
    entry.surface.stencil_data = Ptr<void>(stencil_addr);
    std::memcpy(&entry.surface.background_depth, &depth_bits, 4);
    entry.surface.stencil = control1 & 0xFF;
    entry.surface.mask = (control1 >> 8) & 1;
    entry.surface.unk2 = (control1 >> 9) & 1;
    entry.multisample_mode = static_cast<SceGxmMultisampleMode>(multisample);
    entry.tiling = static_cast<SurfaceTiling>(tiling);
    return read_image(reader, entry.texture, aggregate);
}


} // namespace

bool validate_surface_cache_snapshot(const SurfaceCacheSnapshot &snapshot, std::string &error) {
    error.clear();
    if (snapshot.colors.size() > surface_cache_snapshot_slots || snapshot.depth_stencil.size() > surface_cache_snapshot_slots
        || !validate_queue_order(snapshot.color_mru_order, snapshot.colors.size(), error)
        || !validate_queue_order(snapshot.depth_stencil_mru_order, snapshot.depth_stencil.size(), error))
        return false;
    if (snapshot.colors.size() != surface_cache_snapshot_slots) {
        error = "color surface-cache snapshot must contain all LRU slots";
        return false;
    }
    if (snapshot.depth_stencil.size() != surface_cache_snapshot_slots) {
        error = "depth surface-cache snapshot must contain all LRU slots";
        return false;
    }
    uint64_t aggregate_bytes = 0;
    std::set<Address> color_addresses;
    for (const auto &entry : snapshot.colors) {
        if (!entry.active) {
            if (!validate_image(entry.texture, aggregate_bytes, error)
                || !validate_image(entry.raw_image, aggregate_bytes, error))
                return false;
            if (entry.texture.present || entry.raw_image.present) {
                error = "inactive color surface has a GPU image";
                return false;
            }
            continue;
        }
        if (!entry.total_bytes || !entry.width || !entry.height || !entry.original_width || !entry.original_height
            || !entry.stride_bytes || static_cast<uint64_t>(entry.stride_bytes) * entry.original_height != entry.total_bytes
            || !valid_tiling(entry.tiling) || !color_addresses.insert(entry.guest_address).second
            || !entry.texture.present || !valid_component_mapping(entry.swizzle)
            || (entry.guest_address && entry.total_bytes > std::numeric_limits<Address>::max() - entry.guest_address)
            || (!entry.need_surface_sync_object && entry.need_surface_sync)
            || (!entry.dirty_object && entry.dirty)
            || entry.repack_sync_age_ns < 0
            || entry.repack_sync_age_ns > std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::hours(24 * 365)).count()
            || !validate_image(entry.texture, aggregate_bytes, error)
            || (entry.texture.image.width != entry.width || entry.texture.image.height != entry.height)) {
            if (error.empty())
                error = "invalid active color surface metadata";
            return false;
        }
        if (!(entry.texture.usage & vk::ImageUsageFlagBits::eColorAttachment)
            || !(entry.texture.usage & vk::ImageUsageFlagBits::eSampled)) {
            error = "color surface image lacks renderer-required usage";
            return false;
        }
        if (entry.raw_image.present && (!validate_image(entry.raw_image, aggregate_bytes, error)
                || entry.raw_image.image.width != entry.texture.image.width
                || entry.raw_image.image.height != entry.texture.image.height
                || entry.raw_image.image.format != vk::Format::eR16G16B16A16Uint)) {
            if (error.empty())
                error = "raw color image does not match its surface";
            return false;
        }
        if (entry.raw_image.present && !(entry.raw_image.usage & vk::ImageUsageFlagBits::eStorage)) {
            error = "raw color image lacks storage usage";
            return false;
        }
        if (!entry.raw_image.present && !validate_image(entry.raw_image, aggregate_bytes, error)) {
            return false;
        }
    }
    std::set<Address> depth_addresses;
    std::set<Address> stencil_addresses;
    for (const auto &entry : snapshot.depth_stencil) {
        if (!entry.active) {
            if (!validate_image(entry.texture, aggregate_bytes, error))
                return false;
            if (entry.texture.present) {
                error = "inactive depth surface has a GPU image";
                return false;
            }
            continue;
        }
        if (!entry.total_bytes || entry.memory_width <= 0 || entry.memory_height <= 0 || !entry.stride_samples
            || !valid_tiling(entry.tiling) || !entry.texture.present
            || static_cast<uint32_t>(entry.multisample_mode) > static_cast<uint32_t>(SCE_GXM_MULTISAMPLE_4X)
            || (entry.surface.depth_data && !depth_addresses.insert(entry.surface.depth_data.address()).second)
            || (entry.surface.stencil_data && !stencil_addresses.insert(entry.surface.stencil_data.address()).second)
            || !validate_image(entry.texture, aggregate_bytes, error)) {
            if (error.empty())
                error = "invalid active depth-stencil surface metadata";
            return false;
        }
        if (!(entry.texture.usage & vk::ImageUsageFlagBits::eDepthStencilAttachment)) {
            error = "depth-stencil image lacks attachment usage";
            return false;
        }
    }
    if (aggregate_bytes > max_surface_cache_snapshot_bytes) {
        error = "surface-cache snapshot exceeds aggregate image cap";
        return false;
    }
    if (snapshot.last_written_color_slot != no_slot && !slot_is_active(snapshot.colors, snapshot.last_written_color_slot)) {
        error = "last-written color surface points to an inactive slot";
        return false;
    }
    if (snapshot.pending_depth_stencil_slot != no_slot && !slot_is_active(snapshot.depth_stencil, snapshot.pending_depth_stencil_slot)) {
        error = "pending depth scene points to an inactive slot";
        return false;
    }
    if (snapshot.pending_depth_stencil_slot == no_slot && snapshot.pending_depth_stencil_stores) {
        error = "depth-stencil store flag has no pending scene";
        return false;
    }
    if (snapshot.cpu_surfaces_changed.size() > 4096) {
        error = "surface-cache changed-address list exceeds cap";
        return false;
    }
    return true;
}

bool encode_surface_cache_snapshot(const SurfaceCacheSnapshot &snapshot, std::vector<uint8_t> &data, std::string &error) {
    error.clear();
    if (!validate_surface_cache_snapshot(snapshot, error))
        return false;
    SnapshotWriter writer;
    writer.u32(surface_snapshot_magic);
    writer.u32(surface_snapshot_version);
    writer.u32(static_cast<uint32_t>(snapshot.colors.size()));
    for (const auto &entry : snapshot.colors)
        write_color(writer, entry);
    writer.u32(static_cast<uint32_t>(snapshot.color_mru_order.size()));
    for (uint8_t slot : snapshot.color_mru_order)
        writer.u8(slot);
    writer.u32(static_cast<uint32_t>(snapshot.depth_stencil.size()));
    for (const auto &entry : snapshot.depth_stencil)
        write_depth(writer, entry);
    writer.u32(static_cast<uint32_t>(snapshot.depth_stencil_mru_order.size()));
    for (uint8_t slot : snapshot.depth_stencil_mru_order)
        writer.u8(slot);
    writer.u32(static_cast<uint32_t>(snapshot.cpu_surfaces_changed.size()));
    for (Address address : snapshot.cpu_surfaces_changed)
        writer.u32(address);
    writer.u8(snapshot.last_written_color_slot);
    writer.u8(snapshot.pending_depth_stencil_slot);
    writer.boolean(snapshot.pending_depth_stencil_stores);
    data = std::move(writer.data);
    return true;
}

bool decode_surface_cache_snapshot(const std::vector<uint8_t> &data, SurfaceCacheSnapshot &snapshot, std::string &error) {
    error.clear();
    // Leave headroom under the savestate section's 256 MiB buffering limit.
    if (data.size() > max_surface_cache_snapshot_bytes + (64u << 10)) {
        error = "surface-cache snapshot encoding exceeds size cap";
        return false;
    }
    SnapshotReader reader(data);
    uint32_t magic, version, count;
    if (!reader.u32(magic) || !reader.u32(version) || magic != surface_snapshot_magic || version != surface_snapshot_version
        || !reader.u32(count) || count != surface_cache_snapshot_slots) {
        error = "invalid surface-cache snapshot header";
        return false;
    }
    SurfaceCacheSnapshot staged;
    staged.colors.resize(count);
    uint64_t aggregate_bytes = 0;
    for (auto &entry : staged.colors) {
        if (!read_color(reader, entry, aggregate_bytes)) {
            error = "truncated or malformed color surface-cache entry";
            return false;
        }
    }
    if (!reader.u32(count) || count != surface_cache_snapshot_slots) {
        error = "invalid color surface-cache LRU count";
        return false;
    }
    staged.color_mru_order.resize(count);
    for (auto &slot : staged.color_mru_order) {
        if (!reader.u8(slot)) {
            error = "truncated color surface-cache LRU order";
            return false;
        }
    }
    if (!reader.u32(count) || count != surface_cache_snapshot_slots) {
        error = "invalid depth surface-cache entry count";
        return false;
    }
    staged.depth_stencil.resize(count);
    for (auto &entry : staged.depth_stencil) {
        if (!read_depth(reader, entry, aggregate_bytes)) {
            error = "truncated or malformed depth surface-cache entry";
            return false;
        }
    }
    if (!reader.u32(count) || count != surface_cache_snapshot_slots) {
        error = "invalid depth surface-cache LRU count";
        return false;
    }
    staged.depth_stencil_mru_order.resize(count);
    for (auto &slot : staged.depth_stencil_mru_order) {
        if (!reader.u8(slot)) {
            error = "truncated depth surface-cache LRU order";
            return false;
        }
    }
    if (!reader.u32(count) || count > 4096) {
        error = "invalid changed surface address count";
        return false;
    }
    staged.cpu_surfaces_changed.resize(count);
    for (auto &address : staged.cpu_surfaces_changed) {
        if (!reader.u32(address)) {
            error = "truncated changed surface address list";
            return false;
        }
    }
    if (!reader.u8(staged.last_written_color_slot) || !reader.u8(staged.pending_depth_stencil_slot)
        || !reader.boolean(staged.pending_depth_stencil_stores) || !reader.at_end()) {
        error = "truncated or trailing surface-cache snapshot data";
        return false;
    }
    if (!validate_surface_cache_snapshot(staged, error))
        return false;
    snapshot = std::move(staged);
    return true;
}


} // namespace renderer::vulkan
