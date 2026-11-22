// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

// Pure helpers of sceGxmTransferFill (no emulator dependencies, unit tested on the host).

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace renderer {

// Largest transfer pixel (RAW128).
constexpr uint32_t transfer_fill_max_pixel_bytes = 16;

// Bytes per pixel of a transfer format width in bits (sub-byte widths round up).
constexpr uint32_t transfer_fill_pixel_bytes(uint32_t bits_per_pixel) {
    return (bits_per_pixel + 7) >> 3;
}

// Build the bytes of one filled pixel from the 32 bit fill color.
// - up to 4 bytes per pixel: the low bytes of the color in little endian order (previous behaviour)
// - 8 and 16 bytes per pixel (RAW64 / RAW128): the 32 bit color repeated for every word of the pixel.
//   PROVISIONAL: the hardware semantics of the wide formats are not recovered, replication keeps a
//   uniform fill (clear color) uniform instead of reading past the 4 byte color.
// Returns the pixel size in bytes, 0 if the width is unsupported (nothing is written then).
inline uint32_t build_transfer_fill_pixel(uint32_t fill_color, uint32_t bytes_per_pixel, uint8_t (&pixel)[transfer_fill_max_pixel_bytes]) {
    if (bytes_per_pixel == 0 || bytes_per_pixel > transfer_fill_max_pixel_bytes)
        return 0;
    uint8_t color[4];
    for (uint32_t i = 0; i < 4; ++i)
        color[i] = static_cast<uint8_t>(fill_color >> (8 * i));
    for (uint32_t i = 0; i < bytes_per_pixel; ++i)
        pixel[i] = color[i & 3];
    return bytes_per_pixel;
}

// Guest address span [first, end) touched by a fill. Rows are `stride` bytes apart (may be negative).
// Returns false when the arithmetic leaves the 32 bit guest address space or the region is empty.
inline bool transfer_fill_span(uint32_t base, uint32_t x, uint32_t y, uint32_t width, uint32_t height, int32_t stride,
    uint32_t bytes_per_pixel, uint64_t &first, uint64_t &end) {
    if (width == 0 || height == 0 || bytes_per_pixel == 0)
        return false;
    // Guest controlled geometry: bound it so the 64 bit products below cannot overflow
    // (2^17 * 2^31 stride * 2^4 bytes < 2^63). PROVISIONAL limit, real transfer images are far smaller.
    constexpr uint32_t max_coord = 1u << 16;
    if (x > max_coord || y > max_coord || width > max_coord || height > max_coord || bytes_per_pixel > transfer_fill_max_pixel_bytes)
        return false;
    const int64_t first_row = static_cast<int64_t>(y) * stride;
    const int64_t last_row = (static_cast<int64_t>(y) + height - 1) * stride;
    const int64_t lo = static_cast<int64_t>(base) + std::min(first_row, last_row) + static_cast<int64_t>(x) * bytes_per_pixel;
    const int64_t hi = static_cast<int64_t>(base) + std::max(first_row, last_row) + (static_cast<int64_t>(x) + width) * bytes_per_pixel;
    if (lo < 0 || hi > static_cast<int64_t>(UINT32_MAX) + 1)
        return false;
    first = static_cast<uint64_t>(lo);
    end = static_cast<uint64_t>(hi);
    return true;
}

// Fill one row buffer (width pixels) with the pixel pattern.
inline void fill_transfer_row(uint8_t *row, uint32_t width, const uint8_t *pixel, uint32_t bytes_per_pixel) {
    for (uint32_t x = 0; x < width; ++x)
        std::memcpy(row + static_cast<size_t>(x) * bytes_per_pixel, pixel, bytes_per_pixel);
}

} // namespace renderer
