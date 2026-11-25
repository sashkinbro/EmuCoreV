// EmuCoreV: colour surface format model of the Vulkan renderer (P7).
//
// Pure decisions and pure converters only (no Vulkan calls), so they can be unit tested on the host:
//  - which host format represents each GXM colour base format, chosen from the probed format features
//    (colour attachment, sampling, blending) with a documented fallback chain per format
//  - how guest memory is converted to and from the host format when the two differ (Repack)
//  - which formats the guest "gamma" bit applies to
//
// Fallback order (see select_host_format): a lossless candidate that renders and blends, then a lossless
// candidate that only renders (blending of such a surface is switched off, the pipeline asks blend_ok), then a lossy
// candidate (F32 -> F16 only), and finally the primary candidate whatever the probe said. Never a vendor check.
#pragma once

#include <gxm/types.h>

// vkutil.h sets the vulkan.hpp configuration macros and platform defines before including it: never include vulkan.hpp first
#include <vkutil/vkutil.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace renderer::vulkan::colourmodel {

// Conversion between the guest memory layout of a surface and the host image that stands in for it.
enum class Repack : uint8_t {
    None, // host memory layout == guest memory layout (or handled by the legacy per-format code)
    U4444_RGBA8, // legacy: U4U4U4U4 surface kept as RGBA8
    U2F10F10F10_F16, // guest 32 bit (R10 G10 B10 f10 unsigned, A2 unorm) <-> RGBA16F
    SE5M9M9M9_F16, // guest shared exponent <-> RGBA16F (alpha 1)
    U2U10U10U10_F16, // fallback: guest A2 B10 G10 R10 unorm <-> RGBA16F
    F11F11F10_F16, // fallback: guest R11 G11 B10 float <-> RGBA16F (alpha 1)
    U1555_RGBA8, // fallback: guest A1R5G5B5 <-> RGBA8
    U565_RGBA8, // fallback: guest R5G6B5 <-> RGBA8
    Unorm16_F32, // fallback: U16 / U16U16 <-> R32F / RG32F
    Snorm16_F32, // fallback: S16 / S16S16 <-> R32F / RG32F
    Snorm8_F16, // fallback: S8 / S8S8 / S8S8S8S8 <-> R16F / RG16F / RGBA16F
    F32_F16, // lossy fallback: F32 / F32F32 <-> R16F / RG16F
};

// The base formats a colour surface can have, as a dense index (the GXM values are sparse).
enum class Fmt : uint8_t {
    U8U8U8U8, U8U8U8, U5U6U5, U1U5U5U5, U4U4U4U4, F16, F16F16, F32, S16, S16S16, U16, U16U16, U2U10U10U10,
    U8, S8, U8U8, S8S8, S8S8S8S8, F16F16F16F16, F32F32, F11F11F10, SE5M9M9M9, U2F10F10F10,
    Count, // also "not a colour format this renderer renders to"
};
constexpr size_t fmt_count = static_cast<size_t>(Fmt::Count);

inline Fmt fmt_of(const SceGxmColorBaseFormat base) {
    switch (base) {
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8: return Fmt::U8U8U8U8;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8U8: return Fmt::U8U8U8;
    case SCE_GXM_COLOR_BASE_FORMAT_U5U6U5: return Fmt::U5U6U5;
    case SCE_GXM_COLOR_BASE_FORMAT_U1U5U5U5: return Fmt::U1U5U5U5;
    case SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4: return Fmt::U4U4U4U4;
    case SCE_GXM_COLOR_BASE_FORMAT_F16: return Fmt::F16;
    case SCE_GXM_COLOR_BASE_FORMAT_F16F16: return Fmt::F16F16;
    case SCE_GXM_COLOR_BASE_FORMAT_F32: return Fmt::F32;
    case SCE_GXM_COLOR_BASE_FORMAT_S16: return Fmt::S16;
    case SCE_GXM_COLOR_BASE_FORMAT_S16S16: return Fmt::S16S16;
    case SCE_GXM_COLOR_BASE_FORMAT_U16: return Fmt::U16;
    case SCE_GXM_COLOR_BASE_FORMAT_U16U16: return Fmt::U16U16;
    case SCE_GXM_COLOR_BASE_FORMAT_U2U10U10U10: return Fmt::U2U10U10U10;
    case SCE_GXM_COLOR_BASE_FORMAT_U8: return Fmt::U8;
    case SCE_GXM_COLOR_BASE_FORMAT_S8: return Fmt::S8;
    case SCE_GXM_COLOR_BASE_FORMAT_U8U8: return Fmt::U8U8;
    case SCE_GXM_COLOR_BASE_FORMAT_S8S8: return Fmt::S8S8;
    case SCE_GXM_COLOR_BASE_FORMAT_S8S8S8S8: return Fmt::S8S8S8S8;
    case SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16: return Fmt::F16F16F16F16;
    case SCE_GXM_COLOR_BASE_FORMAT_F32F32: return Fmt::F32F32;
    case SCE_GXM_COLOR_BASE_FORMAT_F11F11F10: return Fmt::F11F11F10;
    case SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9: return Fmt::SE5M9M9M9;
    case SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10: return Fmt::U2F10F10F10;
    default: return Fmt::Count;
    }
}

struct Candidate {
    vk::Format format;
    Repack repack;
    bool lossy;
};

// Candidates per base format, preferred first. The first entry is the legacy mapping of this renderer.
struct Chain {
    std::array<Candidate, 3> list;
    uint8_t count;
};

inline const Chain &chain_of(const Fmt f) {
    using F = vk::Format;
    using R = Repack;
    static const std::array<Chain, fmt_count> chains = { {
        /*U8U8U8U8*/ { { { { F::eR8G8B8A8Unorm, R::None, false } } }, 1 },
        /*U8U8U8*/ { { { { F::eR8G8B8A8Unorm, R::None, false } } }, 1 }, // 24 bit packed is expanded by the legacy sync code
        /*U5U6U5*/ { { { { F::eR5G6B5UnormPack16, R::None, false }, { F::eR8G8B8A8Unorm, R::U565_RGBA8, false } } }, 2 },
        /*U1U5U5U5*/ { { { { F::eA1R5G5B5UnormPack16, R::None, false }, { F::eR8G8B8A8Unorm, R::U1555_RGBA8, false } } }, 2 },
        /*U4U4U4U4*/ { { { { F::eR8G8B8A8Unorm, R::U4444_RGBA8, false } } }, 1 },
        /*F16*/ { { { { F::eR16Sfloat, R::None, false } } }, 1 },
        /*F16F16*/ { { { { F::eR16G16Sfloat, R::None, false } } }, 1 },
        /*F32*/ { { { { F::eR32Sfloat, R::None, false }, { F::eR16Sfloat, R::F32_F16, true } } }, 2 },
        /*S16*/ { { { { F::eR16Snorm, R::None, false }, { F::eR32Sfloat, R::Snorm16_F32, false } } }, 2 },
        /*S16S16*/ { { { { F::eR16G16Snorm, R::None, false }, { F::eR32G32Sfloat, R::Snorm16_F32, false } } }, 2 },
        /*U16*/ { { { { F::eR16Unorm, R::None, false }, { F::eR32Sfloat, R::Unorm16_F32, false } } }, 2 },
        /*U16U16*/ { { { { F::eR16G16Unorm, R::None, false }, { F::eR32G32Sfloat, R::Unorm16_F32, false } } }, 2 },
        /*U2U10U10U10*/ { { { { F::eA2R10G10B10UnormPack32, R::None, false }, { F::eR16G16B16A16Sfloat, R::U2U10U10U10_F16, false } } }, 2 },
        /*U8*/ { { { { F::eR8Unorm, R::None, false } } }, 1 },
        /*S8*/ { { { { F::eR8Snorm, R::None, false }, { F::eR16Sfloat, R::Snorm8_F16, false } } }, 2 },
        /*U8U8*/ { { { { F::eR8G8Unorm, R::None, false } } }, 1 },
        /*S8S8*/ { { { { F::eR8G8Snorm, R::None, false }, { F::eR16G16Sfloat, R::Snorm8_F16, false } } }, 2 },
        /*S8S8S8S8*/ { { { { F::eR8G8B8A8Snorm, R::None, false }, { F::eR16G16B16A16Sfloat, R::Snorm8_F16, false } } }, 2 },
        /*F16F16F16F16*/ { { { { F::eR16G16B16A16Sfloat, R::None, false } } }, 1 },
        /*F32F32*/ { { { { F::eR32G32Sfloat, R::None, false }, { F::eR16G16Sfloat, R::F32_F16, true } } }, 2 },
        /*F11F11F10*/ { { { { F::eB10G11R11UfloatPack32, R::None, false }, { F::eR16G16B16A16Sfloat, R::F11F11F10_F16, false } } }, 2 },
        // RGB9E5 is sample only on desktop GPUs (writes silently dropped): always emulated
        /*SE5M9M9M9*/ { { { { F::eR16G16B16A16Sfloat, R::SE5M9M9M9_F16, false } } }, 1 },
        /*U2F10F10F10*/ { { { { F::eR16G16B16A16Sfloat, R::U2F10F10F10_F16, false } } }, 1 },
    } };
    return chains[static_cast<size_t>(f)];
}

// Probed capability of one host format as a colour surface.
struct FormatSupport {
    bool attach = false; // colour attachment + sampled (the guest reads its surfaces as textures) + transfer src
    bool blend = false; // colour attachment blend
};

// The legacy answer for every format (probe not run yet, or a broken driver): the first candidate, blending allowed.
struct SurfaceFormatTable {
    struct Entry {
        vk::Format format = vk::Format::eUndefined;
        Repack repack = Repack::None;
        bool blend_ok = true;
        bool lossy = false;
        bool forced = false; // nothing in the chain was usable, the primary candidate is used anyway
    };
    std::array<Entry, fmt_count> entries{};

    SurfaceFormatTable() {
        for (size_t i = 0; i < fmt_count; i++) {
            const Candidate &c = chain_of(static_cast<Fmt>(i)).list[0];
            entries[i] = { c.format, c.repack, true, c.lossy, false };
        }
    }
    const Entry &operator[](const Fmt f) const { return entries[static_cast<size_t>(f)]; }
};

// select one entry; `supports(format)` returns the probed FormatSupport
template <typename Probe>
inline SurfaceFormatTable::Entry select_host_format(const Fmt f, const Probe &supports) {
    const Chain &chain = chain_of(f);
    // pass 0: lossless + blend, 1: lossless attach only, 2: lossy + blend, 3: lossy attach only
    for (int pass = 0; pass < 4; pass++) {
        const bool lossy = pass >= 2;
        const bool need_blend = (pass & 1) == 0;
        for (uint8_t i = 0; i < chain.count; i++) {
            const Candidate &c = chain.list[i];
            if (c.lossy != lossy)
                continue;
            const FormatSupport s = supports(c.format);
            if (s.attach && (s.blend || !need_blend))
                return { c.format, c.repack, s.blend, c.lossy, false };
        }
    }
    const Candidate &c = chain.list[0];
    return { c.format, c.repack, true, c.lossy, true };
}

// The conversion between a guest base format and the host format a surface of it actually uses.
inline Repack repack_for(const SceGxmColorBaseFormat base, const vk::Format host) {
    const Fmt f = fmt_of(base);
    if (f == Fmt::Count)
        return Repack::None;
    const Chain &chain = chain_of(f);
    for (uint8_t i = 0; i < chain.count; i++)
        if (chain.list[i].format == host)
            return chain.list[i].repack;
    return Repack::None;
}

// Bytes of one pixel of the HOST image for the repack kinds that convert (the guest side is gxm::bits_per_pixel)
inline uint32_t host_bytes_per_pixel(const Repack r, const uint32_t guest_bytes_per_pixel) {
    switch (r) {
    case Repack::None: return 0;
    case Repack::U4444_RGBA8:
    case Repack::U1555_RGBA8:
    case Repack::U565_RGBA8: return 4;
    case Repack::U2F10F10F10_F16:
    case Repack::SE5M9M9M9_F16:
    case Repack::U2U10U10U10_F16:
    case Repack::F11F11F10_F16: return 8;
    case Repack::Unorm16_F32:
    case Repack::Snorm16_F32: return guest_bytes_per_pixel * 2; // u16 -> f32 per component
    case Repack::Snorm8_F16: return guest_bytes_per_pixel * 2; // s8 -> f16 per component
    case Repack::F32_F16: return guest_bytes_per_pixel / 2; // f32 -> f16 per component
    }
    return 0;
}

// Surfaces whose host copy is not byte identical to guest memory. Used for the 25 ms sync throttle and the sync buffer decision.
inline bool needs_cpu_repack(const Repack r) {
    return r != Repack::None;
}

// ---- gamma ----
// The guest gamma bit makes the hardware sRGB-encode the colour on write. Only the 8 bit unorm formats have an sRGB
// host counterpart (hardware encode) and are known to be gamma encoded; PROVISIONAL: on every other format the bit is
// not applied (the shader sRGB encode of the fragment-fetch path used to run for them and produced washed out floats).
inline bool gamma_applies(const SceGxmColorBaseFormat base) {
    return base == SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8;
}

// ---- converters ----
// Signatures follow surface_pack.h: (dst, src, pixel_stride, height); pixel_stride * height pixels, row stride of both
// images is pixel_stride pixels.

inline float half_to_float(const uint32_t h) {
    const uint32_t sign = (h >> 15) & 1;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t man = h & 0x3FF;
    uint32_t bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign << 31;
        } else {
            // subnormal half -> normal float
            int e = -1;
            uint32_t m = man;
            do {
                e++;
                m <<= 1;
            } while (!(m & 0x400));
            bits = (sign << 31) | (static_cast<uint32_t>(127 - 15 - e) << 23) | ((m & 0x3FF) << 13);
        }
    } else if (exp == 31) {
        bits = (sign << 31) | 0x7F800000u | (man << 13);
    } else {
        bits = (sign << 31) | ((exp + 112) << 23) | (man << 13);
    }
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

// round to nearest even, saturating to the largest finite half (a colour never becomes infinity by the conversion)
inline uint16_t float_to_half(const float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = (bits >> 16) & 0x8000;
    const uint32_t abs_bits = bits & 0x7FFFFFFFu;
    if (abs_bits > 0x7F800000u)
        return static_cast<uint16_t>(sign | 0x7E00); // NaN
    if (abs_bits >= 0x477FF000u) // rounds to >= 65520
        return static_cast<uint16_t>(abs_bits == 0x7F800000u ? (sign | 0x7C00) : (sign | 0x7BFF));
    if (abs_bits < 0x33000001u) // below half of the smallest subnormal
        return static_cast<uint16_t>(sign);
    const int exp = static_cast<int>(abs_bits >> 23) - 127;
    if (exp < -14) {
        // subnormal half
        const uint32_t m = (abs_bits & 0x7FFFFF) | 0x800000;
        const int shift = -exp - 14 + 13; // 14..24
        uint32_t half = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1);
        const uint32_t halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (half & 1)))
            half++;
        return static_cast<uint16_t>(sign | half);
    }
    uint32_t half = ((static_cast<uint32_t>(exp + 15)) << 10) | ((abs_bits & 0x7FFFFF) >> 13);
    const uint32_t rem = abs_bits & 0x1FFF;
    if (rem > 0x1000 || (rem == 0x1000 && (half & 1)))
        half++; // may carry into the exponent: still correct
    return static_cast<uint16_t>(sign | half);
}

// unsigned small floats (no sign bit): e5 + mantissa
inline float small_float_to_float(const uint32_t value, const uint32_t mantissa_bits) {
    // widen into a half: same exponent, mantissa shifted up
    return half_to_float(value << (10 - mantissa_bits));
}
inline uint32_t float_to_small_float(const float value, const uint32_t mantissa_bits) {
    // negatives and NaN clamp to zero / keep NaN; positives round through half then drop the extra mantissa bits
    if (!(value > 0.0f))
        return std::isnan(value) ? (0x1Fu << mantissa_bits) | 1u : 0;
    const uint32_t h = float_to_half(value);
    const uint32_t drop = 10 - mantissa_bits;
    uint32_t v = h >> drop;
    const uint32_t rem = h & ((1u << drop) - 1);
    const uint32_t halfway = 1u << (drop - 1);
    if ((rem > halfway || (rem == halfway && (v & 1))) && (h & 0x7FFF) < 0x7BFF)
        v++;
    return v;
}

inline uint32_t unorm_n(const float v, const uint32_t max) {
    const float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<uint32_t>(c * static_cast<float>(max) + 0.5f);
}
inline int32_t snorm_n(const float v, const int32_t max) {
    const float c = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return static_cast<int32_t>(std::lround(c * static_cast<float>(max)));
}

inline void put16(uint8_t *p, const uint16_t v) { std::memcpy(p, &v, 2); }
inline void put32(uint8_t *p, const uint32_t v) { std::memcpy(p, &v, 4); }
inline void putf(uint8_t *p, const float v) { std::memcpy(p, &v, 4); }
inline uint16_t get16(const uint8_t *p) { uint16_t v; std::memcpy(&v, p, 2); return v; }
inline uint32_t get32(const uint8_t *p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline float getf(const uint8_t *p) { float v; std::memcpy(&v, p, 4); return v; }

constexpr uint16_t half_one = 0x3C00;

// ---- U2F10F10F10 (R10 G10 B10 unsigned e5m5, A2 unorm) ----
inline void u2f10f10f10_to_rgba16f(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get32(src + i * 4);
        put16(dst + i * 8 + 0, static_cast<uint16_t>((v & 0x3FF) << 5));
        put16(dst + i * 8 + 2, static_cast<uint16_t>(((v >> 10) & 0x3FF) << 5));
        put16(dst + i * 8 + 4, static_cast<uint16_t>(((v >> 20) & 0x3FF) << 5));
        put16(dst + i * 8 + 6, float_to_half(static_cast<float>(v >> 30) * (1.0f / 3.0f)));
    }
}

// ---- SE5M9M9M9 (value = m * 2^(e - 24), alpha 1) ----
inline void se5m9m9m9_to_rgba16f(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get32(src + i * 4);
        const float scale = std::ldexp(1.0f, static_cast<int>(v >> 27) - 24);
        put16(dst + i * 8 + 0, float_to_half(static_cast<float>(v & 0x1FF) * scale));
        put16(dst + i * 8 + 2, float_to_half(static_cast<float>((v >> 9) & 0x1FF) * scale));
        put16(dst + i * 8 + 4, float_to_half(static_cast<float>((v >> 18) & 0x1FF) * scale));
        put16(dst + i * 8 + 6, half_one);
    }
}

// ---- U2U10U10U10 (A2 B10 G10 R10 unorm) <-> RGBA16F ----
inline void u2u10_to_rgba16f(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get32(src + i * 4);
        put16(dst + i * 8 + 0, float_to_half(static_cast<float>(v & 0x3FF) * (1.0f / 1023.0f)));
        put16(dst + i * 8 + 2, float_to_half(static_cast<float>((v >> 10) & 0x3FF) * (1.0f / 1023.0f)));
        put16(dst + i * 8 + 4, float_to_half(static_cast<float>((v >> 20) & 0x3FF) * (1.0f / 1023.0f)));
        put16(dst + i * 8 + 6, float_to_half(static_cast<float>(v >> 30) * (1.0f / 3.0f)));
    }
}
inline void rgba16f_to_u2u10(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t r = unorm_n(half_to_float(get16(src + i * 8 + 0)), 1023);
        const uint32_t g = unorm_n(half_to_float(get16(src + i * 8 + 2)), 1023);
        const uint32_t b = unorm_n(half_to_float(get16(src + i * 8 + 4)), 1023);
        const uint32_t a = unorm_n(half_to_float(get16(src + i * 8 + 6)), 3);
        put32(dst + i * 4, r | (g << 10) | (b << 20) | (a << 30));
    }
}

// ---- F11F11F10 (R11 e5m6, G11 e5m6, B10 e5m5, unsigned) <-> RGBA16F (alpha 1) ----
inline void f11f11f10_to_rgba16f(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get32(src + i * 4);
        put16(dst + i * 8 + 0, static_cast<uint16_t>((v & 0x7FF) << 4));
        put16(dst + i * 8 + 2, static_cast<uint16_t>(((v >> 11) & 0x7FF) << 4));
        put16(dst + i * 8 + 4, static_cast<uint16_t>(((v >> 22) & 0x3FF) << 5));
        put16(dst + i * 8 + 6, half_one);
    }
}
inline uint32_t half_to_small(const uint32_t h, const uint32_t mantissa_bits) {
    if (h & 0x8000)
        return 0; // no sign bit
    const uint32_t drop = 10 - mantissa_bits;
    uint32_t v = (h & 0x7FFF) >> drop;
    // a NaN with only dropped payload bits must stay a NaN
    if ((h & 0x7FFF) > 0x7C00 && (v & ((1u << mantissa_bits) - 1)) == 0)
        v |= 1;
    return v;
}
inline void rgba16f_to_f11f11f10(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t r = half_to_small(get16(src + i * 8 + 0), 6);
        const uint32_t g = half_to_small(get16(src + i * 8 + 2), 6);
        const uint32_t b = half_to_small(get16(src + i * 8 + 4), 5);
        put32(dst + i * 4, r | (g << 11) | (b << 22));
    }
}

// ---- 16 bit packed <-> RGBA8 ----
inline uint8_t expand_n(const uint32_t v, const uint32_t bits) {
    return static_cast<uint8_t>((v * 255 + ((1u << bits) - 1) / 2) / ((1u << bits) - 1));
}
inline uint32_t reduce_n(const uint32_t v8, const uint32_t bits) {
    const uint32_t max = (1u << bits) - 1;
    return (v8 * max + 127) / 255;
}
inline void u565_to_rgba8(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get16(src + i * 2);
        dst[i * 4 + 0] = expand_n(v >> 11, 5);
        dst[i * 4 + 1] = expand_n((v >> 5) & 0x3F, 6);
        dst[i * 4 + 2] = expand_n(v & 0x1F, 5);
        dst[i * 4 + 3] = 255;
    }
}
inline void rgba8_to_u565(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        put16(dst + i * 2, static_cast<uint16_t>((reduce_n(src[i * 4 + 0], 5) << 11) | (reduce_n(src[i * 4 + 1], 6) << 5) | reduce_n(src[i * 4 + 2], 5)));
}
// U4U4U4U4 as the legacy sync writes it (surface_pack.h pack_rgba8_to_r4g4b4a4): r | g << 4 | b << 8 | a << 12
inline void u4444_to_rgba8(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get16(src + i * 2);
        dst[i * 4 + 0] = expand_n(v & 0xF, 4);
        dst[i * 4 + 1] = expand_n((v >> 4) & 0xF, 4);
        dst[i * 4 + 2] = expand_n((v >> 8) & 0xF, 4);
        dst[i * 4 + 3] = expand_n((v >> 12) & 0xF, 4);
    }
}
inline void u1555_to_rgba8(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        const uint32_t v = get16(src + i * 2);
        dst[i * 4 + 0] = expand_n((v >> 10) & 0x1F, 5);
        dst[i * 4 + 1] = expand_n((v >> 5) & 0x1F, 5);
        dst[i * 4 + 2] = expand_n(v & 0x1F, 5);
        dst[i * 4 + 3] = (v & 0x8000) ? 255 : 0;
    }
}
inline void rgba8_to_u1555(uint8_t *dst, const uint8_t *src, uint32_t count) {
    for (uint32_t i = 0; i < count; i++)
        put16(dst + i * 2, static_cast<uint16_t>((src[i * 4 + 3] >= 128 ? 0x8000 : 0) | (reduce_n(src[i * 4 + 0], 5) << 10) | (reduce_n(src[i * 4 + 1], 5) << 5) | reduce_n(src[i * 4 + 2], 5)));
}

// ---- per component converters (component count = pixels * components) ----
inline void unorm16_to_f32(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        putf(dst + i * 4, static_cast<float>(get16(src + i * 2)) * (1.0f / 65535.0f));
}
inline void f32_to_unorm16(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        put16(dst + i * 2, static_cast<uint16_t>(unorm_n(getf(src + i * 4), 65535)));
}
inline void snorm16_to_f32(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        putf(dst + i * 4, std::max(-1.0f, static_cast<float>(static_cast<int16_t>(get16(src + i * 2))) * (1.0f / 32767.0f)));
}
inline void f32_to_snorm16(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        put16(dst + i * 2, static_cast<uint16_t>(static_cast<int16_t>(snorm_n(getf(src + i * 4), 32767))));
}
inline void snorm8_to_f16(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        put16(dst + i * 2, float_to_half(std::max(-1.0f, static_cast<float>(static_cast<int8_t>(src[i])) * (1.0f / 127.0f))));
}
inline void f16_to_snorm8(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        dst[i] = static_cast<uint8_t>(static_cast<int8_t>(snorm_n(half_to_float(get16(src + i * 2)), 127)));
}
inline void f32_to_f16(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        put16(dst + i * 2, float_to_half(getf(src + i * 4)));
}
inline void f16_to_f32(uint8_t *dst, const uint8_t *src, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        putf(dst + i * 4, half_to_float(get16(src + i * 2)));
}

// Components of one guest pixel for the per component kinds
inline uint32_t guest_components(const Repack r, const uint32_t guest_bytes_per_pixel) {
    switch (r) {
    case Repack::Unorm16_F32:
    case Repack::Snorm16_F32: return guest_bytes_per_pixel / 2;
    case Repack::Snorm8_F16: return guest_bytes_per_pixel;
    case Repack::F32_F16: return guest_bytes_per_pixel / 4;
    default: return 1;
    }
}

// Guest memory -> host image data (upload of CPU writes). `pixels` = pixel_stride * height. Returns false when the kind has no converter.
inline bool guest_to_host(const Repack r, uint8_t *host, const uint8_t *guest, const uint32_t pixels, const uint32_t guest_bytes_per_pixel) {
    const uint32_t n = pixels * guest_components(r, guest_bytes_per_pixel);
    switch (r) {
    case Repack::U2F10F10F10_F16: u2f10f10f10_to_rgba16f(host, guest, pixels); return true;
    case Repack::SE5M9M9M9_F16: se5m9m9m9_to_rgba16f(host, guest, pixels); return true;
    case Repack::U2U10U10U10_F16: u2u10_to_rgba16f(host, guest, pixels); return true;
    case Repack::F11F11F10_F16: f11f11f10_to_rgba16f(host, guest, pixels); return true;
    case Repack::U4444_RGBA8: u4444_to_rgba8(host, guest, pixels); return true;
    case Repack::U565_RGBA8: u565_to_rgba8(host, guest, pixels); return true;
    case Repack::U1555_RGBA8: u1555_to_rgba8(host, guest, pixels); return true;
    case Repack::Unorm16_F32: unorm16_to_f32(host, guest, n); return true;
    case Repack::Snorm16_F32: snorm16_to_f32(host, guest, n); return true;
    case Repack::Snorm8_F16: snorm8_to_f16(host, guest, n); return true;
    case Repack::F32_F16: f32_to_f16(host, guest, n); return true;
    default: return false; // None needs none
    }
}

// Host image data -> guest memory (surface sync). The U2F10F10F10 and SE5M9M9M9 writers live in surface_pack.h.
inline bool host_to_guest_extra(const Repack r, uint8_t *guest, const uint8_t *host, const uint32_t pixels, const uint32_t guest_bytes_per_pixel) {
    const uint32_t n = pixels * guest_components(r, guest_bytes_per_pixel);
    switch (r) {
    case Repack::U2U10U10U10_F16: rgba16f_to_u2u10(guest, host, pixels); return true;
    case Repack::F11F11F10_F16: rgba16f_to_f11f11f10(guest, host, pixels); return true;
    case Repack::U565_RGBA8: rgba8_to_u565(guest, host, pixels); return true;
    case Repack::U1555_RGBA8: rgba8_to_u1555(guest, host, pixels); return true;
    case Repack::Unorm16_F32: f32_to_unorm16(guest, host, n); return true;
    case Repack::Snorm16_F32: f32_to_snorm16(guest, host, n); return true;
    case Repack::Snorm8_F16: f16_to_snorm8(guest, host, n); return true;
    case Repack::F32_F16: f16_to_f32(guest, host, n); return true;
    default: return false;
    }
}

} // namespace renderer::vulkan::colourmodel
