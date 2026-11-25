// EmuCoreV: sceGxmTransfer model (P12).
//
// Pure logic (no emulator, no Vulkan), unit tested on the host:
//  - the channel layout of every transfer colour format and exact conversion between any two of them
//  - the exact 2x2 box average of the downscale (replaces the libswscale call of the render thread)
//  - where a transfer image lies inside a cached colour surface (decides whether the transfer can run on the GPU as a blit)
//
// Format names list the channels from the most significant bit: U5U6U5_BGR is B[15:11] G[10:5] R[4:0],
// U8U8U8U8_ABGR is bytes R G B A in memory.
#pragma once

#include <gxm/types.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>

namespace renderer::transfer {

struct Channel {
    uint8_t shift = 0;
    uint8_t bits = 0; // 0: the format has no such channel
};

struct FormatDesc {
    uint8_t bytes = 0; // bytes per pixel, 0: not a colour format (YUV422, RAW)
    std::array<Channel, 4> ch{}; // R G B A
};

inline FormatDesc describe(const SceGxmTransferFormat f) {
    switch (f) {
    case SCE_GXM_TRANSFER_FORMAT_U8_R: return { 1, { { { 0, 8 }, {}, {}, {} } } };
    case SCE_GXM_TRANSFER_FORMAT_U4U4U4U4_ABGR: return { 2, { { { 0, 4 }, { 4, 4 }, { 8, 4 }, { 12, 4 } } } };
    case SCE_GXM_TRANSFER_FORMAT_U1U5U5U5_ABGR: return { 2, { { { 0, 5 }, { 5, 5 }, { 10, 5 }, { 15, 1 } } } };
    case SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR: return { 2, { { { 0, 5 }, { 5, 6 }, { 11, 5 }, {} } } };
    case SCE_GXM_TRANSFER_FORMAT_U8U8_GR: return { 2, { { { 0, 8 }, { 8, 8 }, {}, {} } } };
    case SCE_GXM_TRANSFER_FORMAT_U8U8U8_BGR: return { 3, { { { 0, 8 }, { 8, 8 }, { 16, 8 }, {} } } };
    case SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR: return { 4, { { { 0, 8 }, { 8, 8 }, { 16, 8 }, { 24, 8 } } } };
    case SCE_GXM_TRANSFER_FORMAT_U2U10U10U10_ABGR: return { 4, { { { 0, 10 }, { 10, 10 }, { 20, 10 }, { 30, 2 } } } };
    default: return {};
    }
}

// Bytes per pixel of every format, colour or not (the YUV422 and RAW formats are copied byte for byte).
inline uint32_t pixel_bytes(const SceGxmTransferFormat f) {
    switch (f) {
    case SCE_GXM_TRANSFER_FORMAT_VYUY422:
    case SCE_GXM_TRANSFER_FORMAT_YVYU422:
    case SCE_GXM_TRANSFER_FORMAT_UYVY422:
    case SCE_GXM_TRANSFER_FORMAT_YUYV422:
    case SCE_GXM_TRANSFER_FORMAT_RAW16: return 2;
    case SCE_GXM_TRANSFER_FORMAT_RAW32: return 4;
    case SCE_GXM_TRANSFER_FORMAT_RAW64: return 8;
    case SCE_GXM_TRANSFER_FORMAT_RAW128: return 16;
    default: return describe(f).bytes;
    }
}

inline bool is_colour(const SceGxmTransferFormat f) {
    return describe(f).bytes != 0;
}

// Conversion between two different formats exists when both are colour formats.
inline bool convertible(const SceGxmTransferFormat src, const SceGxmTransferFormat dst) {
    return is_colour(src) && is_colour(dst);
}

inline uint32_t load_pixel(const FormatDesc &d, const uint8_t *p) {
    uint32_t v = 0;
    std::memcpy(&v, p, d.bytes); // little endian host
    return v;
}
inline void store_pixel(const FormatDesc &d, uint8_t *p, const uint32_t v) {
    std::memcpy(p, &v, d.bytes);
}

inline uint32_t field(const uint32_t v, const Channel &c) {
    return (v >> c.shift) & ((1u << c.bits) - 1u);
}

// One normalised channel to another width: exact rounding, 0 and full scale are preserved
inline uint32_t rescale(const uint32_t v, const uint32_t from_bits, const uint32_t to_bits) {
    if (from_bits == to_bits)
        return v;
    const uint32_t from_max = (1u << from_bits) - 1u;
    const uint32_t to_max = (1u << to_bits) - 1u;
    return static_cast<uint32_t>((static_cast<uint64_t>(v) * to_max + from_max / 2) / from_max);
}

// A channel the destination has but the source lacks: colour channels read as 0, alpha as full scale.
// PROVISIONAL: the conversions of the hardware transfer unit are not recovered, this follows how a host texture read defaults them.
inline void convert_pixel(const FormatDesc &src, const FormatDesc &dst, const uint8_t *src_pixel, uint8_t *dst_pixel) {
    const uint32_t in = load_pixel(src, src_pixel);
    uint32_t out = 0;
    for (int c = 0; c < 4; c++) {
        if (dst.ch[c].bits == 0)
            continue;
        const uint32_t v = src.ch[c].bits != 0 ? rescale(field(in, src.ch[c]), src.ch[c].bits, dst.ch[c].bits)
                                               : (c == 3 ? (1u << dst.ch[c].bits) - 1u : 0u);
        out |= v << dst.ch[c].shift;
    }
    store_pixel(dst, dst_pixel, out);
}

// The 2x2 box average of the downscale: per channel (sum + 2) / 4, i.e. round half up. Channel bits the format does not use
// (the unused bit of nothing in these formats) stay zero.
inline void average_2x2(const FormatDesc &d, const uint8_t *p00, const uint8_t *p10, const uint8_t *p01, const uint8_t *p11, uint8_t *out) {
    const uint32_t v[4] = { load_pixel(d, p00), load_pixel(d, p10), load_pixel(d, p01), load_pixel(d, p11) };
    uint32_t result = 0;
    for (const Channel &c : d.ch) {
        if (c.bits == 0)
            continue;
        const uint32_t sum = field(v[0], c) + field(v[1], c) + field(v[2], c) + field(v[3], c);
        result |= ((sum + 2) / 4) << c.shift;
    }
    store_pixel(d, out, result);
}

// A downscale of exactly two to one: the only ratio a box filter reproduces on the GPU with a linear blit.
inline bool is_exact_half(const uint32_t src_w, const uint32_t src_h, const uint32_t dst_w, const uint32_t dst_h) {
    return dst_w != 0 && dst_h != 0 && src_w == dst_w * 2 && src_h == dst_h * 2;
}

// ---- where a transfer image lies in a cached colour surface ----
struct SurfaceGeometry {
    uint32_t base = 0; // guest address of the surface
    uint32_t stride_bytes = 0;
    uint32_t width = 0; // pixels (guest size)
    uint32_t height = 0;
    uint32_t bytes_per_pixel = 0;
};

struct PixelRect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    bool operator==(const PixelRect &) const = default;
};

// The pixel rectangle of an image (address of its first pixel row, x/y offset in pixels, size) inside the surface, or nothing when it
// does not lie entirely inside it with the surface's own row layout (the image stride must be the surface stride).
inline std::optional<PixelRect> locate(const SurfaceGeometry &s, const uint32_t address, const uint32_t stride, const uint32_t x, const uint32_t y, const uint32_t w, const uint32_t h) {
    if (s.stride_bytes == 0 || s.bytes_per_pixel == 0 || w == 0 || h == 0 || stride != s.stride_bytes || address < s.base)
        return std::nullopt;
    const uint64_t offset = static_cast<uint64_t>(address) - s.base;
    const uint64_t row = offset / s.stride_bytes;
    const uint64_t column_bytes = offset % s.stride_bytes;
    if (column_bytes % s.bytes_per_pixel != 0)
        return std::nullopt;
    const uint64_t px = column_bytes / s.bytes_per_pixel + x;
    const uint64_t py = row + y;
    if (px + w > s.width || py + h > s.height)
        return std::nullopt;
    return PixelRect{ static_cast<uint32_t>(px), static_cast<uint32_t>(py), w, h };
}

inline bool rects_overlap(const PixelRect &a, const PixelRect &b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

} // namespace renderer::transfer
