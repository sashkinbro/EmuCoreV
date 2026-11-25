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

// GXM YUV texture semantics shared by every conversion route (GPU compute shader, Vulkan sampler YCbCr
// conversion, CPU fallback) and by the host tests. Header only, no dependency on the rest of the renderer.
//
// A GXM YUV format carries a colour space conversion slot (CSC0 or CSC1) in its swizzle bits. The matrix a slot
// uses is chosen by the guest with sceGxmSetYuvProfile. Everything below is the single definition of what a
// profile means; the compute shader (yuv_convert_planar.comp) implements the same formulas.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace renderer::yuv {

// SceGxmYuvProfile. PROVISIONAL: the numeric order of the guest enum is taken from the public SDK header order
// (601 pair, then 709 pair) and has not been confirmed on a firmware/game capture.
enum Profile : uint8_t {
    BT601_STANDARD = 0,
    BT601_FULL_RANGE = 1,
    BT709_STANDARD = 2,
    BT709_FULL_RANGE = 3,
};
constexpr uint32_t profile_count = 4;
// Number of colour space conversion slots (CSC0, CSC1) and of program types that can own a profile table
constexpr uint32_t csc_count = 2;
constexpr uint32_t program_type_count = 2;

// Profile of a slot before the guest calls sceGxmSetYuvProfile. PROVISIONAL: CSC0 = BT.601, CSC1 = BT.709, both
// "standard" (limited) range, which is how SD and HD video is normally authored.
constexpr uint8_t default_profile(uint32_t csc_index) {
    return csc_index == 0 ? BT601_STANDARD : BT709_STANDARD;
}

// Parameters of "RGB = M * (YCbCr - bias) * scale" for one profile.
struct Matrix {
    float y_bias; // luma that maps to black (0..1)
    float y_scale; // luma scale
    float c_bias; // chroma that maps to zero (128/255 for both ranges)
    float c_scale; // chroma scale
    float r_v; // R = Y + r_v * V
    float g_u; // G = Y + g_u * U + g_v * V
    float g_v;
    float b_u; // B = Y + b_u * U
};

inline Matrix make_matrix(uint8_t profile) {
    const bool bt709 = profile == BT709_STANDARD || profile == BT709_FULL_RANGE;
    const bool full = profile == BT601_FULL_RANGE || profile == BT709_FULL_RANGE;
    const double kr = bt709 ? 0.2126 : 0.299;
    const double kb = bt709 ? 0.0722 : 0.114;
    const double kg = 1.0 - kr - kb;

    Matrix m;
    m.y_bias = full ? 0.0f : 16.0f / 255.0f;
    m.y_scale = full ? 1.0f : 255.0f / 219.0f;
    m.c_bias = 128.0f / 255.0f;
    m.c_scale = full ? 1.0f : 255.0f / 224.0f;
    m.r_v = static_cast<float>(2.0 * (1.0 - kr));
    m.b_u = static_cast<float>(2.0 * (1.0 - kb));
    m.g_u = static_cast<float>(-2.0 * kb * (1.0 - kb) / kg);
    m.g_v = static_cast<float>(-2.0 * kr * (1.0 - kr) / kg);
    return m;
}

inline void to_rgb(const Matrix &m, float y, float u, float v, float rgb[3]) {
    const float yy = (y - m.y_bias) * m.y_scale;
    const float uu = (u - m.c_bias) * m.c_scale;
    const float vv = (v - m.c_bias) * m.c_scale;
    rgb[0] = yy + m.r_v * vv;
    rgb[1] = yy + m.g_u * uu + m.g_v * vv;
    rgb[2] = yy + m.b_u * uu;
}

inline uint8_t to_unorm8(float v) {
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

// One pixel from 8-bit samples, the result is the 8-bit RGB a unorm8 render target would hold.
inline void convert_pixel(uint8_t profile, uint8_t y, uint8_t u, uint8_t v, uint8_t rgb8[3]) {
    float rgb[3];
    to_rgb(make_matrix(profile), y / 255.0f, u / 255.0f, v / 255.0f, rgb);
    for (int i = 0; i < 3; i++)
        rgb8[i] = to_unorm8(rgb[i]);
}

// ---- texture format decoding -------------------------------------------------------------------------------------
// swizzle = (SceGxmTextureFormat >> 12) & 7, i.e. SceGxmTexture::swizzle_format.

// YUV420P2 / YUV420P3: bit 0 = chroma order (0 = U then V, 1 = V then U), bit 1 = CSC slot.
constexpr uint32_t csc_of_420(uint32_t swizzle) {
    return (swizzle >> 1) & 1;
}
constexpr bool chroma_swapped_420(uint32_t swizzle) {
    return (swizzle & 1) != 0;
}

// YUV422: bits 0-1 = byte order (YUYV, YVYU, UYVY, VYUY), bit 2 = CSC slot.
constexpr uint32_t csc_of_422(uint32_t swizzle) {
    return (swizzle >> 2) & 1;
}

// Positions of the four components inside the 4-byte macro pixel that holds two horizontal pixels.
struct Order422 {
    uint8_t y0, u, y1, v;
};
constexpr Order422 order_422(uint32_t swizzle) {
    switch (swizzle & 3) {
    case 0: // YUYV
        return { 0, 1, 2, 3 };
    case 1: // YVYU
        return { 0, 3, 2, 1 };
    case 2: // UYVY
        return { 1, 0, 3, 2 };
    default: // VYUY
        return { 1, 2, 3, 0 };
    }
}

// ---- plane geometry ----------------------------------------------------------------------------------------------
// Same plane placement the renderer has always used for linear YUV420: the chroma plane follows the luma plane
// at layout_width * layout_height bytes (layout size is the padded one for textures with mip chains).
struct PlaneLayout420 {
    uint32_t c0_offset; // P2: interleaved CbCr plane, P3: first chroma plane
    uint32_t c1_offset; // P3: second chroma plane (unused for P2)
    uint32_t y_stride; // bytes per row of the luma plane
    uint32_t c_stride; // bytes per row of the chroma plane(s)
    uint32_t chroma_width; // chroma samples per row
    uint32_t chroma_height;
    uint32_t total_bytes; // end of the last plane
};

inline PlaneLayout420 plane_layout_420(bool is_p3, uint32_t width, uint32_t height, uint32_t stride_pixels, uint32_t layout_width, uint32_t layout_height) {
    PlaneLayout420 l;
    l.chroma_width = (width + 1) / 2;
    l.chroma_height = (height + 1) / 2;
    l.y_stride = stride_pixels;
    l.c0_offset = layout_width * layout_height;
    if (is_p3) {
        l.c_stride = std::max(stride_pixels / 2, l.chroma_width);
        l.c1_offset = l.c0_offset + layout_width * layout_height / 4;
        l.total_bytes = l.c1_offset + l.c_stride * l.chroma_height;
    } else {
        l.c_stride = std::max(stride_pixels, l.chroma_width * 2);
        l.c1_offset = l.c0_offset;
        l.total_bytes = l.c0_offset + l.c_stride * l.chroma_height;
    }
    return l;
}

// ---- CPU conversion (last-resort fallback, and the reference for the shader) -------------------------------------
// Chroma reconstruction (PROVISIONAL, the GXM texture unit does not document it): bilinear, horizontally co-sited
// with the even luma sample and vertically centred between the two luma rows (MPEG-2 / H.264 "left" siting). This
// is also what the compute shader and the Vulkan YCbCr route (COSITED_EVEN / MIDPOINT) do.
struct ChromaTap {
    uint32_t i0, i1; // sample indices
    float f; // weight of i1
};

inline ChromaTap chroma_tap_x(uint32_t x, uint32_t count) {
    ChromaTap t;
    t.i0 = std::min(x >> 1, count - 1);
    t.i1 = std::min((x >> 1) + 1, count - 1);
    t.f = (x & 1) ? 0.5f : 0.0f;
    return t;
}

inline ChromaTap chroma_tap_y(uint32_t y, uint32_t count) {
    const float pos = static_cast<float>(y) * 0.5f - 0.25f;
    const float fl = std::floor(pos);
    const int j0 = static_cast<int>(fl);
    ChromaTap t;
    t.f = pos - fl;
    t.i0 = static_cast<uint32_t>(std::clamp(j0, 0, static_cast<int>(count) - 1));
    t.i1 = static_cast<uint32_t>(std::clamp(j0 + 1, 0, static_cast<int>(count) - 1));
    return t;
}

// dst: RGBA8, dst_stride_pixels pixels per row. src points to the luma plane.
inline void convert_420_to_rgba8(uint8_t profile, bool is_p3, bool chroma_swapped, uint8_t *dst, uint32_t dst_stride_pixels,
    const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride_pixels, uint32_t layout_width, uint32_t layout_height) {
    const PlaneLayout420 l = plane_layout_420(is_p3, width, height, stride_pixels, layout_width, layout_height);
    const Matrix m = make_matrix(profile);
    const uint8_t *c0 = src + l.c0_offset;
    const uint8_t *c1 = src + l.c1_offset;

    for (uint32_t y = 0; y < height; y++) {
        const ChromaTap ty = chroma_tap_y(y, l.chroma_height);
        for (uint32_t x = 0; x < width; x++) {
            const ChromaTap tx = chroma_tap_x(x, l.chroma_width);
            float first = 0.0f, second = 0.0f;
            const uint32_t xs[2] = { tx.i0, tx.i1 };
            const uint32_t ys[2] = { ty.i0, ty.i1 };
            const float wx[2] = { 1.0f - tx.f, tx.f };
            const float wy[2] = { 1.0f - ty.f, ty.f };
            for (int j = 0; j < 2; j++) {
                for (int i = 0; i < 2; i++) {
                    const float w = wx[i] * wy[j];
                    if (is_p3) {
                        first += w * c0[ys[j] * l.c_stride + xs[i]];
                        second += w * c1[ys[j] * l.c_stride + xs[i]];
                    } else {
                        first += w * c0[ys[j] * l.c_stride + xs[i] * 2];
                        second += w * c0[ys[j] * l.c_stride + xs[i] * 2 + 1];
                    }
                }
            }
            const float u = (chroma_swapped ? second : first) / 255.0f;
            const float v = (chroma_swapped ? first : second) / 255.0f;
            float rgb[3];
            to_rgb(m, src[y * l.y_stride + x] / 255.0f, u, v, rgb);
            uint8_t *out = dst + (static_cast<size_t>(y) * dst_stride_pixels + x) * 4;
            out[0] = to_unorm8(rgb[0]);
            out[1] = to_unorm8(rgb[1]);
            out[2] = to_unorm8(rgb[2]);
            out[3] = 255;
        }
    }
}

// src: linear packed 4:2:2, stride_pixels pixels (2 bytes each) per row.
inline void convert_422_to_rgba8(uint8_t profile, uint32_t swizzle, uint8_t *dst, uint32_t dst_stride_pixels,
    const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride_pixels) {
    const Matrix m = make_matrix(profile);
    const Order422 o = order_422(swizzle);
    const uint32_t macro_count = (width + 1) / 2;

    for (uint32_t y = 0; y < height; y++) {
        const uint8_t *row = src + static_cast<size_t>(y) * stride_pixels * 2;
        for (uint32_t x = 0; x < width; x++) {
            const ChromaTap tx = chroma_tap_x(x, macro_count);
            const uint8_t *p0 = row + tx.i0 * 4;
            const uint8_t *p1 = row + tx.i1 * 4;
            const float u = (p0[o.u] * (1.0f - tx.f) + p1[o.u] * tx.f) / 255.0f;
            const float v = (p0[o.v] * (1.0f - tx.f) + p1[o.v] * tx.f) / 255.0f;
            const uint8_t luma = row[(x >> 1) * 4 + ((x & 1) ? o.y1 : o.y0)];
            float rgb[3];
            to_rgb(m, luma / 255.0f, u, v, rgb);
            uint8_t *out = dst + (static_cast<size_t>(y) * dst_stride_pixels + x) * 4;
            out[0] = to_unorm8(rgb[0]);
            out[1] = to_unorm8(rgb[1]);
            out[2] = to_unorm8(rgb[2]);
            out[3] = 255;
        }
    }
}

// ---- conversion route selection ----------------------------------------------------------------------------------
// Where a YUV texture is converted to RGBA8. Chosen per texture from the probed device capabilities:
//   Ycbcr  - Vulkan sampler YCbCr conversion (VK_KHR_sampler_ycbcr_conversion / core 1.1) inside a compute pass
//   Shader - compute shader that does the colour space maths itself (yuv_convert_planar.comp)
//   Cpu    - last fallback: conversion on the CPU while the texture is uploaded (also the OpenGL backend)
enum class Route : uint8_t {
    Cpu,
    Shader,
    Ycbcr,
};

// Memory layout of the guest texture
enum class Kind : uint8_t {
    P2, // YUV420P2: luma plane + interleaved chroma plane
    P3, // YUV420P3: luma plane + two chroma planes
    Packed422, // YUV422: packed macro pixels
};
constexpr uint32_t kind_count = 3;

struct RouteCaps {
    // the compute shader route can run: compute capable queue, rgba8 storage image, R8 / RG8 / RGBA8 sampled
    // images, the pipeline was created
    bool shader = false;
    // per Kind: the YCbCr conversion route can run (feature enabled, format features, pipeline created)
    bool ycbcr[kind_count] = { false, false, false };
};

// force: -1 = automatic, otherwise the Route to try first (debug switch). A forced route that the device cannot
// run falls back the same way the automatic selection does.
inline Route select_route(const RouteCaps &caps, Kind kind, int force = -1) {
    const bool want_ycbcr = force < 0 || force == static_cast<int>(Route::Ycbcr);
    const bool want_shader = force < 0 || force == static_cast<int>(Route::Shader) || force == static_cast<int>(Route::Ycbcr);
    if (force == static_cast<int>(Route::Cpu))
        return Route::Cpu;
    if (want_ycbcr && caps.ycbcr[static_cast<uint32_t>(kind)])
        return Route::Ycbcr;
    if (want_shader && caps.shader)
        return Route::Shader;
    return Route::Cpu;
}

} // namespace renderer::yuv
