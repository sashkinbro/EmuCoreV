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

// CPU-side repack helpers used by surface sync (GPU surface -> guest memory format).
// Kept header-only and free of Vulkan/emulator state so they can be unit tested standalone.

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace renderer::vulkan {

inline uint8_t unorm8_to_unorm4(uint8_t value) {
    return static_cast<uint8_t>((static_cast<uint32_t>(value) * 15 + 127) / 255);
}

inline void pack_rgba8_to_r4g4b4a4(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        uint16_t *dst_row = reinterpret_cast<uint16_t *>(dst + y * pixel_stride * sizeof(uint16_t));
        const uint8_t *src_row = src + y * pixel_stride * 4;

        for (uint32_t x = 0; x < pixel_stride; x++) {
            const uint8_t r = unorm8_to_unorm4(src_row[x * 4 + 0]);
            const uint8_t g = unorm8_to_unorm4(src_row[x * 4 + 1]);
            const uint8_t b = unorm8_to_unorm4(src_row[x * 4 + 2]);
            const uint8_t a = unorm8_to_unorm4(src_row[x * 4 + 3]);

            dst_row[x] = static_cast<uint16_t>(r | (g << 4) | (b << 8) | (a << 12));
        }
    }
}

// F16 (s1e5m10) -> unsigned F10 (e5m5)
inline uint32_t half_to_f10(uint32_t h) {
    // branchless: subtracting the sign bit yields an all-ones mask for positives, 0 for negatives
    const uint32_t f10 = ((h >> 5) & 0x3FF) & (((h >> 15) & 1) - 1);
    // dropping 5 mantissa bits must not turn a positive NaN with only low payload bits (0x7C01..0x7C1F) into Inf
    const uint32_t is_nan = (((h & 0x7FFF) > 0x7C00) ? 1u : 0u) & (((h >> 15) & 1) ^ 1u);
    return f10 | (is_nan & ((f10 & 0x1F) == 0 ? 1u : 0u));
}

// F16 alpha -> 2-bit unorm, rounding to the nearest of {0, 1/3, 2/3, 1}.
// Thresholds are the first halves >= 1/6, 1/2, 5/6 (0x3156, 0x3800, 0x3AAB).
inline uint32_t half_to_unorm2(uint32_t h) {
    if (h & 0x8000)
        return 0;
    return (h >= 0x3AAB) ? 3 : (h >= 0x3800) ? 2
        : (h >= 0x3156)                      ? 1
                                             : 0;
}

inline float half_to_float_unsigned(uint32_t h) {
    if (h & 0x8000)
        return 0.0f;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t man = h & 0x3FF;
    if (exp == 31)
        return man ? 0.0f : 65504.0f;
    if (exp == 0)
        return static_cast<float>(man) * (1.0f / (1 << 24));
    const uint32_t f32 = ((exp + 112) << 23) | (man << 13);
    float result;
    memcpy(&result, &f32, sizeof(result));
    return result;
}

inline uint32_t pack_rgb9e5(float r, float g, float b) {
    constexpr float max_rgb9e5 = 65408.0f;
    r = std::min(r, max_rgb9e5);
    g = std::min(g, max_rgb9e5);
    b = std::min(b, max_rgb9e5);
    const float maxc = std::max(r, std::max(g, b));
    if (maxc <= 0.0f)
        return 0;
    uint32_t max_bits;
    memcpy(&max_bits, &maxc, sizeof(max_bits));
    int exp_shared = std::max(-16, static_cast<int>(max_bits >> 23) - 127) + 1 + 15;
    // scale = 2^(exp_shared - 15 - 9), always a normal float here (exp_shared is in [0, 31])
    uint32_t scale_bits = static_cast<uint32_t>(exp_shared - 24 + 127) << 23;
    float scale;
    memcpy(&scale, &scale_bits, sizeof(scale));
    if (static_cast<uint32_t>(maxc / scale + 0.5f) == 512) {
        exp_shared += 1;
        scale *= 2.0f;
    }
    const uint32_t rs = std::min(511u, static_cast<uint32_t>(r / scale + 0.5f));
    const uint32_t gs = std::min(511u, static_cast<uint32_t>(g / scale + 0.5f));
    const uint32_t bs = std::min(511u, static_cast<uint32_t>(b / scale + 0.5f));
    return rs | (gs << 9) | (bs << 18) | (static_cast<uint32_t>(exp_shared) << 27);
}

inline void pack_rgba16f_to_se5m9m9m9(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    const uint32_t count = pixel_stride * height;
    uint32_t *dst_px = reinterpret_cast<uint32_t *>(dst);
    const uint64_t *src_px = reinterpret_cast<const uint64_t *>(src);

    for (uint32_t i = 0; i < count; i++) {
        const uint64_t v = src_px[i];
        const float r = half_to_float_unsigned(static_cast<uint32_t>(v) & 0xFFFF);
        const float g = half_to_float_unsigned(static_cast<uint32_t>(v >> 16) & 0xFFFF);
        const float b = half_to_float_unsigned(static_cast<uint32_t>(v >> 32) & 0xFFFF);
        dst_px[i] = pack_rgb9e5(r, g, b);
    }
}

inline void pack_rgba16f_to_u2f10f10f10(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    const uint32_t count = pixel_stride * height;
    uint32_t *dst_px = reinterpret_cast<uint32_t *>(dst);
    const uint64_t *src_px = reinterpret_cast<const uint64_t *>(src);

    for (uint32_t i = 0; i < count; i++) {
        const uint64_t v = src_px[i];
        const uint32_t r = half_to_f10(static_cast<uint32_t>(v) & 0xFFFF);
        const uint32_t g = half_to_f10(static_cast<uint32_t>(v >> 16) & 0xFFFF);
        const uint32_t b = half_to_f10(static_cast<uint32_t>(v >> 32) & 0xFFFF);
        const uint32_t a = half_to_unorm2(static_cast<uint32_t>(v >> 48));

        dst_px[i] = r | (g << 10) | (b << 20) | (a << 30);
    }
}

} // namespace renderer::vulkan
