#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
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

} // namespace renderer::vulkan
