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

// Pure (FFmpeg/emulator independent) helpers of the SceAvcdec/SceM4vdec picture handling:
// pixel type tables, picture buffer layout arithmetic, Annex-B wrapping of NAL units and
// the YUV 4:2:0 -> RGB conversion. Header-only so the host tests can exercise them.
//
// Pixel types: vitasdk vita-headers psp2/videodec.h (SceAvcdecPixelFormat) for RGBA8888,
// RGBA565, RGBA5551, YUV420_RASTER (planar, 3 planes) and YUV420_PACKED_RASTER (Y plane +
// interleaved chroma). BGRA8888 = 0x8 is not in the SDK header (reverse engineered, kept from
// the existing implementation).
//
// PROVISIONAL (no SDK/firmware proof): the bit layout of RGBA565/RGBA5551 (assumed to follow the
// RGBA8888 naming, red in the lowest bits), the unit of framePitch for RGB output (pixels), the
// unit of the frame crop offsets (pixels) and the resolution of SCE_AVCDEC_CSC_COEFFICIENT_DEFAULT.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace avc_picture {

constexpr uint32_t kPixelRgba8888 = 0x00;
constexpr uint32_t kPixelRgba565 = 0x01;
constexpr uint32_t kPixelRgba5551 = 0x02;
constexpr uint32_t kPixelBgra8888 = 0x08;
constexpr uint32_t kPixelYuv420Raster = 0x10; // YUV420P3
constexpr uint32_t kPixelYuv420PackedRaster = 0x20; // Y + interleaved UV (NV12 order, as before)

inline bool is_yuv(uint32_t type) {
    return type == kPixelYuv420Raster || type == kPixelYuv420PackedRaster;
}

inline bool is_rgb(uint32_t type) {
    return type == kPixelRgba8888 || type == kPixelRgba565 || type == kPixelRgba5551 || type == kPixelBgra8888;
}

inline bool is_valid_pixel_type(uint32_t type) {
    return is_yuv(type) || is_rgb(type);
}

// Bytes per pixel of an RGB type, 0 for anything else.
inline uint32_t bytes_per_pixel(uint32_t type) {
    switch (type) {
    case kPixelRgba8888:
    case kPixelBgra8888:
        return 4;
    case kPixelRgba565:
    case kPixelRgba5551:
        return 2;
    default:
        return 0;
    }
}

// framePitch smaller than frameWidth cannot hold a row: fall back to the width (what the old
// implementation always assumed).
inline uint32_t effective_pitch(uint32_t pitch, uint32_t width) {
    return pitch >= width ? pitch : width;
}

struct Extent {
    uint32_t width = 0;
    uint32_t height = 0;
    bool operator==(const Extent &) const = default;
};

// Region copied when the decoded picture and the guest buffer differ in size.
inline Extent copy_extent(uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h) {
    return { std::min(src_w, dst_w), std::min(src_h, dst_h) };
}

struct Rect {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool operator==(const Rect &) const = default;
};

// Visible area of a coded picture after removing the frame crop offsets. Offsets are clamped to
// even values (chroma is subsampled) and the result must be non-empty.
inline bool visible_rect(uint32_t coded_w, uint32_t coded_h, uint32_t left, uint32_t right, uint32_t top, uint32_t bottom, Rect &out) {
    out = {};
    left &= ~1u;
    right &= ~1u;
    top &= ~1u;
    bottom &= ~1u;
    const uint64_t cx = static_cast<uint64_t>(left) + right;
    const uint64_t cy = static_cast<uint64_t>(top) + bottom;
    if (cx >= coded_w || cy >= coded_h)
        return false;
    out = { left, top, static_cast<uint32_t>(coded_w - cx), static_cast<uint32_t>(coded_h - cy) };
    return true;
}

struct YuvLayout {
    uint32_t luma_pitch = 0;
    uint32_t chroma_pitch = 0; // per plane (P3) or of the interleaved plane (packed)
    uint64_t chroma_offset = 0; // U plane / interleaved plane, from the start of the picture
    uint64_t v_offset = 0; // V plane (P3 only, 0 for packed)
    uint64_t total_size = 0;
    bool operator==(const YuvLayout &) const = default;
};

// Layout of a YUV 4:2:0 picture buffer: luma (pitch x height), then the chroma data.
inline bool yuv420_layout(uint32_t type, uint32_t pitch, uint32_t height, YuvLayout &out) {
    out = {};
    if (!is_yuv(type) || pitch == 0 || height == 0)
        return false;
    const uint64_t luma = static_cast<uint64_t>(pitch) * height;
    const uint64_t half_h = height / 2;
    out.luma_pitch = pitch;
    out.chroma_offset = luma;
    if (type == kPixelYuv420Raster) {
        out.chroma_pitch = pitch / 2;
        out.v_offset = luma + static_cast<uint64_t>(out.chroma_pitch) * half_h;
        out.total_size = out.v_offset + static_cast<uint64_t>(out.chroma_pitch) * half_h;
    } else {
        out.chroma_pitch = pitch;
        out.total_size = luma + static_cast<uint64_t>(out.chroma_pitch) * half_h;
    }
    return true;
}

// Bytes a picture buffer must hold, 0 for an invalid description. RGB rows are pitch pixels.
inline uint64_t picture_buffer_size(uint32_t type, uint32_t pitch, uint32_t height) {
    if (is_yuv(type)) {
        YuvLayout layout;
        return yuv420_layout(type, pitch, height, layout) ? layout.total_size : 0;
    }
    const uint32_t bpp = bytes_per_pixel(type);
    if (bpp == 0 || pitch == 0 || height == 0)
        return 0;
    return static_cast<uint64_t>(pitch) * bpp * height;
}

// ---------------------------------------------------------------------------------------------
// Annex-B

inline bool has_start_code(const uint8_t *data, size_t size) {
    if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)
        return true;
    return size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1;
}

struct NalSpan {
    const uint8_t *data = nullptr;
    uint32_t size = 0;
};

// Appends one NAL unit to an Annex-B stream. A unit that already carries a start code is copied
// unchanged, otherwise the 4 byte start code 00 00 00 01 is added. Empty units are skipped.
inline void append_annexb(std::vector<uint8_t> &out, const uint8_t *nal, size_t size) {
    if (!nal || size == 0)
        return;
    if (!has_start_code(nal, size))
        out.insert(out.end(), { 0, 0, 0, 1 });
    out.insert(out.end(), nal, nal + size);
}

inline void build_annexb(const std::vector<NalSpan> &units, std::vector<uint8_t> &out) {
    out.clear();
    for (const auto &unit : units)
        append_annexb(out, unit.data, unit.size);
}

// ---------------------------------------------------------------------------------------------
// YUV 4:2:0 -> RGB

// Values of SceCscCoefficient (SCE_AVCDEC_CSC_COEFFICIENT_*).
enum class Csc : uint8_t {
    Default = 0,
    Itu601 = 1,
    Itu709 = 2,
    Itu601Full = 3,
    Itu709Full = 4,
};

inline bool is_valid_csc(uint32_t value) {
    return value <= static_cast<uint32_t>(Csc::Itu709Full);
}

// Default has no documented meaning: it follows the stream when it is flagged (BT.709 / full
// range), otherwise limited-range ITU-R BT.601. PROVISIONAL.
inline Csc resolve_csc(Csc requested, bool stream_bt709, bool stream_full_range) {
    if (requested != Csc::Default)
        return requested;
    if (stream_bt709)
        return stream_full_range ? Csc::Itu709Full : Csc::Itu709;
    return stream_full_range ? Csc::Itu601Full : Csc::Itu601;
}

// Q16 fixed point matrix: r = y' + rv*cr, g = y' - gu*cb - gv*cr, b = y' + bu*cb.
struct CscMatrix {
    int32_t y_scale;
    int32_t rv;
    int32_t gu;
    int32_t gv;
    int32_t bu;
    int32_t y_offset; // 16 for limited range, 0 for full range
};

namespace detail {
constexpr int32_t q16(double v) {
    return static_cast<int32_t>(v * 65536.0 + (v >= 0 ? 0.5 : -0.5));
}

constexpr CscMatrix make_matrix(double kr, double kb, bool full) {
    const double kg = 1.0 - kr - kb;
    const double ys = full ? 1.0 : 255.0 / 219.0;
    const double cs = full ? 1.0 : 255.0 / 224.0;
    return {
        q16(ys),
        q16(2.0 * (1.0 - kr) * cs),
        q16(2.0 * kb * (1.0 - kb) / kg * cs),
        q16(2.0 * kr * (1.0 - kr) / kg * cs),
        q16(2.0 * (1.0 - kb) * cs),
        full ? 0 : 16,
    };
}
} // namespace detail

inline CscMatrix csc_matrix(Csc coefficient) {
    static constexpr CscMatrix m601 = detail::make_matrix(0.299, 0.114, false);
    static constexpr CscMatrix m709 = detail::make_matrix(0.2126, 0.0722, false);
    static constexpr CscMatrix m601f = detail::make_matrix(0.299, 0.114, true);
    static constexpr CscMatrix m709f = detail::make_matrix(0.2126, 0.0722, true);
    switch (coefficient) {
    case Csc::Itu709: return m709;
    case Csc::Itu601Full: return m601f;
    case Csc::Itu709Full: return m709f;
    default: return m601;
    }
}

struct Rgb {
    uint8_t r, g, b;
    bool operator==(const Rgb &) const = default;
};

inline uint8_t clamp8(int32_t v) {
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
}

inline Rgb yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, const CscMatrix &m) {
    const int32_t c = (static_cast<int32_t>(y) - m.y_offset) * m.y_scale + 32768;
    const int32_t cb = static_cast<int32_t>(u) - 128;
    const int32_t cr = static_cast<int32_t>(v) - 128;
    return {
        clamp8((c + cr * m.rv) >> 16),
        clamp8((c - cb * m.gu - cr * m.gv) >> 16),
        clamp8((c + cb * m.bu) >> 16),
    };
}

inline void store_pixel(uint8_t *dst, uint32_t type, const Rgb &p, uint8_t alpha) {
    switch (type) {
    case kPixelBgra8888:
        dst[0] = p.b;
        dst[1] = p.g;
        dst[2] = p.r;
        dst[3] = alpha;
        break;
    case kPixelRgba565: {
        const uint16_t v = static_cast<uint16_t>((p.r >> 3) | ((p.g >> 2) << 5) | ((p.b >> 3) << 11));
        dst[0] = static_cast<uint8_t>(v);
        dst[1] = static_cast<uint8_t>(v >> 8);
        break;
    }
    case kPixelRgba5551: {
        const uint16_t v = static_cast<uint16_t>((p.r >> 3) | ((p.g >> 3) << 5) | ((p.b >> 3) << 10) | (alpha >= 0x80 ? 0x8000 : 0));
        dst[0] = static_cast<uint8_t>(v);
        dst[1] = static_cast<uint8_t>(v >> 8);
        break;
    }
    default: // RGBA8888
        dst[0] = p.r;
        dst[1] = p.g;
        dst[2] = p.b;
        dst[3] = alpha;
        break;
    }
}

struct Yuv420Planes {
    const uint8_t *y = nullptr;
    size_t y_pitch = 0;
    const uint8_t *u = nullptr;
    const uint8_t *v = nullptr; // for interleaved chroma: u + 1
    size_t chroma_pitch = 0;
    size_t chroma_step = 1; // distance between two chroma samples of one row: 1 planar, 2 interleaved
};

// Converts a width x height region. dst_pitch is in bytes. Odd sizes read chroma at x/2, y/2.
inline void convert_yuv420(const Yuv420Planes &src, uint32_t width, uint32_t height, uint32_t out_type, Csc csc, uint8_t alpha, uint8_t *dst, size_t dst_pitch) {
    const uint32_t bpp = bytes_per_pixel(out_type);
    if (bpp == 0 || !src.y || !src.u || !src.v)
        return;
    const CscMatrix m = csc_matrix(csc);
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t *yrow = src.y + row * src.y_pitch;
        const uint8_t *urow = src.u + (row / 2) * src.chroma_pitch;
        const uint8_t *vrow = src.v + (row / 2) * src.chroma_pitch;
        uint8_t *out = dst + row * dst_pitch;
        for (uint32_t col = 0; col < width; ++col) {
            const size_t ci = (col / 2) * src.chroma_step;
            store_pixel(out + col * bpp, out_type, yuv_to_rgb(yrow[col], urow[ci], vrow[ci], m), alpha);
        }
    }
}

} // namespace avc_picture
