// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>

namespace renderer::vulkan {

struct SurfaceRect {
    int32_t x0;
    int32_t y0;
    int32_t x1;
    int32_t y1;

    bool operator==(const SurfaceRect &) const = default;
};

inline bool surface_partial_writeback_eligible(
    const bool linear,
    const bool needs_copy_buffer,
    const bool covers_surface,
    const bool identity_swizzle) {
    return linear && !needs_copy_buffer && !covers_surface && identity_swizzle;
}

inline std::optional<SurfaceRect> intersect_surface_rects(
    SurfaceRect sync_rect,
    SurfaceRect scene_rect,
    const int32_t surface_width,
    const int32_t surface_height) {
    if (surface_width <= 0 || surface_height <= 0 ||
        sync_rect.x1 <= sync_rect.x0 || sync_rect.y1 <= sync_rect.y0 ||
        scene_rect.x1 <= scene_rect.x0 || scene_rect.y1 <= scene_rect.y0)
        return std::nullopt;

    const SurfaceRect bounds{ 0, 0, surface_width, surface_height };
    const int32_t x0 = std::max({ sync_rect.x0, scene_rect.x0, bounds.x0 });
    const int32_t y0 = std::max({ sync_rect.y0, scene_rect.y0, bounds.y0 });
    const int32_t x1 = std::min({ sync_rect.x1, scene_rect.x1, bounds.x1 });
    const int32_t y1 = std::min({ sync_rect.y1, scene_rect.y1, bounds.y1 });
    if (x1 <= x0 || y1 <= y0)
        return std::nullopt;
    return SurfaceRect{ x0, y0, x1, y1 };
}

inline std::optional<size_t> surface_sync_rows_span(
    const size_t row_stride,
    const size_t row_bytes,
    const size_t row_count) {
    if (row_count == 0 || row_bytes == 0)
        return size_t{ 0 };
    if (row_stride < row_bytes)
        return std::nullopt;
    const size_t rows_before_last = row_count - 1;
    if (rows_before_last > (std::numeric_limits<size_t>::max() - row_bytes) / row_stride)
        return std::nullopt;
    return rows_before_last * row_stride + row_bytes;
}

inline bool copy_surface_sync_rows(
    uint8_t *destination,
    const size_t destination_size,
    const uint8_t *source,
    const size_t source_size,
    const size_t row_stride,
    const size_t row_bytes,
    const size_t row_count) {
    const auto span = surface_sync_rows_span(row_stride, row_bytes, row_count);
    if (!span || *span > destination_size || *span > source_size)
        return false;
    if (*span == 0)
        return true;
    if (!destination || !source)
        return false;

    for (size_t row = 0; row < row_count; row++) {
        std::memcpy(destination + row * row_stride, source + row * row_stride, row_bytes);
    }
    return true;
}

} // namespace renderer::vulkan
