#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace renderer::vulkan {

struct VertexAttributeRange {
    size_t offset;
    size_t size;
};

inline bool should_restride_vertex_stream(bool memory_mapping_enabled, uint32_t stride, std::span<const VertexAttributeRange> attributes) {
    if (memory_mapping_enabled || stride == 0 || stride % 4 == 0 || attributes.empty())
        return false;

    return std::all_of(attributes.begin(), attributes.end(), [stride](const VertexAttributeRange &attribute) {
        return attribute.offset <= stride && attribute.size <= stride - attribute.offset;
    });
}

inline size_t restrided_vertex_stream_size(size_t stream_size, uint32_t stride) {
    if (stride == 0 || stride % 4 == 0)
        return stream_size;

    const size_t aligned_stride = static_cast<size_t>(stride) + (4 - (stride % 4));
    const size_t vertex_count = stream_size / stride + (stream_size % stride != 0);
    if (vertex_count > std::numeric_limits<size_t>::max() / aligned_stride)
        return std::numeric_limits<size_t>::max();
    return vertex_count * aligned_stride;
}

inline bool restrided_vertex_stream_fits(size_t stream_size, uint32_t stride, size_t capacity) {
    return restrided_vertex_stream_size(stream_size, stride) <= capacity;
}

inline bool vertex_stream_batch_fits(size_t cursor, size_t capacity, size_t alignment, std::span<const size_t> stream_sizes) {
    if (capacity == 0 || alignment == 0)
        return false;

    std::vector<std::pair<size_t, size_t>> allocated_ranges;
    allocated_ranges.reserve(stream_sizes.size());
    for (const size_t stream_size : stream_sizes) {
        if (stream_size > capacity)
            return false;
        if (cursor > capacity || stream_size > capacity - cursor)
            cursor = 0;

        const size_t end = cursor + stream_size;
        for (const auto &[start, previous_end] : allocated_ranges) {
            if (cursor < previous_end && start < end)
                return false;
        }
        allocated_ranges.emplace_back(cursor, end);

        const size_t remainder = end % alignment;
        const size_t padding = remainder == 0 ? 0 : alignment - remainder;
        if (end > std::numeric_limits<size_t>::max() - padding)
            return false;
        cursor = end + padding;
    }
    return true;
}

// Vulkan vertex fetch addresses each attribute at stride * vertex + offset.
// Repack streams with a non-4-byte stride so every vertex starts aligned.
inline std::vector<uint8_t> restride_vertex_stream(std::span<const uint8_t> stream, uint32_t stride) {
    if (stride == 0 || stride % 4 == 0)
        return { stream.begin(), stream.end() };

    const size_t aligned_stride = static_cast<size_t>(stride) + (4 - (stride % 4));
    const size_t vertex_count = (stream.size() + stride - 1) / stride;
    std::vector<uint8_t> repacked(vertex_count * aligned_stride, 0);

    for (size_t vertex = 0; vertex < vertex_count; vertex++) {
        const size_t source_offset = vertex * stride;
        const size_t bytes_to_copy = std::min<size_t>(stride, stream.size() - source_offset);
        std::copy_n(stream.begin() + source_offset, bytes_to_copy, repacked.begin() + vertex * aligned_stride);
    }

    return repacked;
}

// Vulkan requires every vertex attribute address (binding offset + stride * vertex + attribute offset) to be a multiple of
// the attribute's component size. Vita streams do not follow that (stride 31 / 35, F16 at odd offsets, F32 at a stride that
// is not a multiple of 4). Adreno tolerates it; Mali-G57 reads wrong data (exploded skinned meshes, wrong UV triangles).
// Such streams are repacked: the vertex is padded to 4 bytes (attributes that are already aligned keep their offset) and the
// misaligned attributes are copied behind it to aligned offsets.
struct VertexFetchAttribute {
    uint32_t offset; // byte offset in the guest vertex
    uint32_t size; // bytes of the attribute in the guest vertex
    uint32_t alignment; // component size in bytes (1, 2 or 4)
};

struct RealignedVertexLayout {
    bool needed = false;
    uint32_t stride = 0;
    std::vector<uint32_t> offsets; // new offset of every attribute, same order as the input
};

inline RealignedVertexLayout make_realigned_vertex_layout(uint32_t stride, std::span<const VertexFetchAttribute> attributes) {
    RealignedVertexLayout layout;
    if (stride == 0)
        return layout;
    bool needed = false;
    for (const VertexFetchAttribute &a : attributes)
        needed |= a.alignment > 1 && (stride % a.alignment != 0 || a.offset % a.alignment != 0);
    if (!needed)
        return layout;
    layout.needed = true;
    layout.offsets.resize(attributes.size());
    uint32_t cursor = (stride + 3) & ~3u;
    for (size_t i = 0; i < attributes.size(); i++) {
        const VertexFetchAttribute &a = attributes[i];
        if (a.alignment <= 1 || a.offset % a.alignment == 0) {
            layout.offsets[i] = a.offset;
        } else {
            cursor = (cursor + a.alignment - 1) / a.alignment * a.alignment;
            layout.offsets[i] = cursor;
            cursor += a.size;
        }
    }
    layout.stride = (cursor + 3) & ~3u;
    return layout;
}

inline size_t realigned_vertex_stream_size(size_t stream_size, uint32_t stride, const RealignedVertexLayout &layout) {
    return (stream_size / stride + (stream_size % stride != 0)) * static_cast<size_t>(layout.stride);
}

// Repack `stream` into `dst` (realigned_vertex_stream_size bytes, zeroed here)
inline void realign_vertex_stream_into(std::span<const uint8_t> stream, uint32_t stride, std::span<const VertexFetchAttribute> attributes, const RealignedVertexLayout &layout, uint8_t *dst_base) {
    struct Move {
        uint32_t from, to, size;
    };
    Move moves[16];
    size_t move_count = 0;
    for (size_t i = 0; i < attributes.size() && move_count < 16; i++)
        if (layout.offsets[i] != attributes[i].offset)
            moves[move_count++] = { attributes[i].offset, layout.offsets[i], attributes[i].size };

    const size_t vertex_count = stream.size() / stride + (stream.size() % stride != 0);
    std::memset(dst_base, 0, vertex_count * layout.stride);
    const uint8_t *src_base = stream.data();
    const size_t full_vertices = stream.size() / stride;
    for (size_t v = 0; v < vertex_count; v++) {
        const size_t src = v * stride;
        uint8_t *dst = dst_base + v * layout.stride;
        const size_t available = stream.size() - src;
        std::memcpy(dst, src_base + src, v < full_vertices ? stride : available);
        for (size_t m = 0; m < move_count; m++) {
            const size_t from = src + moves[m].from;
            if (from < stream.size())
                std::memcpy(dst + moves[m].to, src_base + from, std::min<size_t>(moves[m].size, stream.size() - from));
        }
    }
}

inline std::vector<uint8_t> realign_vertex_stream(std::span<const uint8_t> stream, uint32_t stride, std::span<const VertexFetchAttribute> attributes, const RealignedVertexLayout &layout) {
    std::vector<uint8_t> out(realigned_vertex_stream_size(stream.size(), stride, layout));
    realign_vertex_stream_into(stream, stride, attributes, layout, out.data());
    return out;
}

// Characters and other skinned meshes re-submit the same static vertex data every frame: remember where the repacked copy
// of a stream lives in the vertex ring and reuse it while the source bytes (hashed) and the ring contents are unchanged.
struct RealignedStreamCache {
    struct Entry {
        bool valid = false;
        uint32_t guest_address = 0;
        uint32_t size = 0;
        uint64_t layout_id = 0;
        uint64_t hash = 0;
        uint64_t wrap_count = 0;
        uint32_t ring_offset = 0;
    };
    static constexpr size_t kEntries = 256;
    std::array<Entry, kEntries> entries{};

    static size_t slot(uint32_t guest_address, uint32_t size, uint64_t layout_id) {
        uint64_t k = guest_address * 0x9E3779B97F4A7C15ull ^ (size + 0x7F4A7C15ull) * 0xC2B2AE3D27D4EB4Full ^ layout_id;
        return static_cast<size_t>((k ^ (k >> 29)) % kEntries);
    }

    // is the copy written at `entry.ring_offset` (length `copy_size`) still untouched by later ring allocations?
    static bool ring_intact(const Entry &entry, uint32_t copy_size, uint64_t ring_wraps, uint32_t ring_cursor) {
        if (entry.wrap_count == ring_wraps)
            return true;
        return entry.wrap_count + 1 == ring_wraps && ring_cursor <= entry.ring_offset && copy_size > 0;
    }
};

} // namespace renderer::vulkan
