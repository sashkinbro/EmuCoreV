// P7: colour surface format model (probe driven host format choice, guest <-> host repack converters).
#include <renderer/vulkan/colour_format_model.h>
#include <renderer/vulkan/surface_pack.h>

#include <gtest/gtest.h>

#include <set>
#include <vector>

namespace {
using namespace renderer::vulkan;
using namespace renderer::vulkan::colourmodel;
using F = vk::Format;

struct Probe {
    std::set<F> no_attach;
    std::set<F> no_blend;
    FormatSupport operator()(const F f) const {
        FormatSupport s;
        s.attach = !no_attach.count(f);
        s.blend = s.attach && !no_blend.count(f);
        return s;
    }
};
} // namespace

TEST(ColourFormats, EverythingSupportedKeepsThePrimaryFormat) {
    const Probe all;
    for (size_t i = 0; i < fmt_count; i++) {
        const auto e = select_host_format(static_cast<Fmt>(i), all);
        EXPECT_EQ(e.format, chain_of(static_cast<Fmt>(i)).list[0].format) << i;
        EXPECT_TRUE(e.blend_ok);
        EXPECT_FALSE(e.forced);
        EXPECT_FALSE(e.lossy);
    }
}

TEST(ColourFormats, DefaultTableIsTheLegacyMapping) {
    const SurfaceFormatTable table;
    EXPECT_EQ(table[Fmt::U8U8U8U8].format, F::eR8G8B8A8Unorm);
    EXPECT_EQ(table[Fmt::U4U4U4U4].format, F::eR8G8B8A8Unorm);
    EXPECT_EQ(table[Fmt::U2F10F10F10].format, F::eR16G16B16A16Sfloat);
    EXPECT_EQ(table[Fmt::SE5M9M9M9].format, F::eR16G16B16A16Sfloat);
    EXPECT_EQ(table[Fmt::U16].format, F::eR16Unorm);
    EXPECT_EQ(table[Fmt::F11F11F10].format, F::eB10G11R11UfloatPack32);
}

TEST(ColourFormats, UnattachableUnorm16FallsBackToExactFloat32) {
    Probe p;
    p.no_attach = { F::eR16Unorm };
    const auto e = select_host_format(Fmt::U16, p);
    EXPECT_EQ(e.format, F::eR32Sfloat);
    EXPECT_EQ(e.repack, Repack::Unorm16_F32);
    EXPECT_FALSE(e.lossy);
}

TEST(ColourFormats, LosslessWithoutBlendBeatsLossyWithBlend) {
    Probe p;
    p.no_blend = { F::eR32Sfloat, F::eR32G32Sfloat };
    const auto f32 = select_host_format(Fmt::F32, p);
    EXPECT_EQ(f32.format, F::eR32Sfloat);
    EXPECT_FALSE(f32.blend_ok); // the pipeline switches blending off on this surface
    EXPECT_FALSE(f32.lossy);
    p.no_attach = { F::eR32Sfloat };
    const auto reduced = select_host_format(Fmt::F32, p);
    EXPECT_EQ(reduced.format, F::eR16Sfloat);
    EXPECT_EQ(reduced.repack, Repack::F32_F16);
    EXPECT_TRUE(reduced.lossy);
}

TEST(ColourFormats, BlendCapableLosslessAlternativeIsPreferredOverAttachOnly) {
    Probe p;
    p.no_blend = { F::eR16Unorm };
    const auto e = select_host_format(Fmt::U16, p);
    EXPECT_EQ(e.format, F::eR32Sfloat);
    EXPECT_TRUE(e.blend_ok);
}

TEST(ColourFormats, NothingUsableForcesThePrimaryAndSaysSo) {
    Probe p;
    p.no_attach = { F::eR32Sfloat, F::eR16Sfloat };
    const auto e = select_host_format(Fmt::F32, p);
    EXPECT_TRUE(e.forced);
    EXPECT_EQ(e.format, F::eR32Sfloat);
}

TEST(ColourFormats, PackedFormatsFallBackToWiderOnes) {
    Probe p;
    p.no_attach = { F::eR5G6B5UnormPack16, F::eA1R5G5B5UnormPack16, F::eB10G11R11UfloatPack32, F::eA2R10G10B10UnormPack32, F::eR8Snorm, F::eR16Snorm, F::eR16G16Snorm };
    EXPECT_EQ(select_host_format(Fmt::U5U6U5, p).repack, Repack::U565_RGBA8);
    EXPECT_EQ(select_host_format(Fmt::U1U5U5U5, p).repack, Repack::U1555_RGBA8);
    EXPECT_EQ(select_host_format(Fmt::F11F11F10, p).repack, Repack::F11F11F10_F16);
    EXPECT_EQ(select_host_format(Fmt::U2U10U10U10, p).repack, Repack::U2U10U10U10_F16);
    EXPECT_EQ(select_host_format(Fmt::S8, p).format, F::eR16Sfloat);
    EXPECT_EQ(select_host_format(Fmt::S16S16, p).format, F::eR32G32Sfloat);
}

TEST(ColourFormats, RepackIsDerivedFromTheFormatPair) {
    EXPECT_EQ(repack_for(SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8, F::eR8G8B8A8Unorm), Repack::None);
    EXPECT_EQ(repack_for(SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4, F::eR8G8B8A8Unorm), Repack::U4444_RGBA8);
    EXPECT_EQ(repack_for(SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10, F::eR16G16B16A16Sfloat), Repack::U2F10F10F10_F16);
    EXPECT_EQ(repack_for(SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9, F::eR16G16B16A16Sfloat), Repack::SE5M9M9M9_F16);
    EXPECT_EQ(repack_for(SCE_GXM_COLOR_BASE_FORMAT_U16, F::eR16Unorm), Repack::None);
    EXPECT_EQ(repack_for(SCE_GXM_COLOR_BASE_FORMAT_U16, F::eR32Sfloat), Repack::Unorm16_F32);
}

TEST(ColourFormats, GammaOnlyAppliesTo8BitUnorm) {
    EXPECT_TRUE(gamma_applies(SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8));
    EXPECT_FALSE(gamma_applies(SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16));
    EXPECT_FALSE(gamma_applies(SCE_GXM_COLOR_BASE_FORMAT_U5U6U5));
}

TEST(ColourConvert, HalfRoundTripsEveryFiniteValue) {
    for (uint32_t h = 0; h < 0x10000; h++) {
        if (((h >> 10) & 0x1F) == 0x1F)
            continue;
        EXPECT_EQ(float_to_half(half_to_float(h)), h) << std::hex << h;
    }
    EXPECT_EQ(float_to_half(1.0f), half_one);
    EXPECT_EQ(float_to_half(1e9f), 0x7BFF); // saturates
    EXPECT_EQ(float_to_half(-1e9f), 0xFBFF);
    EXPECT_EQ(float_to_half(0.0f), 0);
}

TEST(ColourConvert, Unorm16AndSnorm16AreExactThroughFloat32) {
    std::vector<uint8_t> guest(65536 * 2), host(65536 * 4), back(65536 * 2);
    for (uint32_t i = 0; i < 65536; i++)
        put16(&guest[i * 2], static_cast<uint16_t>(i));
    unorm16_to_f32(host.data(), guest.data(), 65536);
    f32_to_unorm16(back.data(), host.data(), 65536);
    EXPECT_EQ(guest, back);
    snorm16_to_f32(host.data(), guest.data(), 65536);
    f32_to_snorm16(back.data(), host.data(), 65536);
    for (uint32_t i = 0; i < 65536; i++) {
        const int16_t v = static_cast<int16_t>(i);
        const int16_t b = static_cast<int16_t>(get16(&back[i * 2]));
        EXPECT_EQ(b, v == -32768 ? -32767 : v) << i; // -32768 and -32767 are both -1.0
    }
}

TEST(ColourConvert, Snorm8IsExactThroughHalf) {
    std::vector<uint8_t> guest(256), host(512), back(256);
    for (int i = 0; i < 256; i++)
        guest[i] = static_cast<uint8_t>(i);
    snorm8_to_f16(host.data(), guest.data(), 256);
    f16_to_snorm8(back.data(), host.data(), 256);
    for (int i = 0; i < 256; i++)
        EXPECT_EQ(static_cast<int8_t>(back[i]), static_cast<int8_t>(guest[i]) == -128 ? -127 : static_cast<int8_t>(guest[i])) << i;
}

TEST(ColourConvert, Float32ToHalfIsTheDocumentedLossyFallback) {
    const float values[] = { 0.0f, 1.0f, -2.5f, 1024.0f, 0.000061035156f };
    std::vector<uint8_t> src(sizeof(values)), half(10), out(sizeof(values));
    std::memcpy(src.data(), values, sizeof(values));
    f32_to_f16(half.data(), src.data(), 5);
    f16_to_f32(out.data(), half.data(), 5);
    EXPECT_EQ(std::memcmp(src.data(), out.data(), sizeof(values)), 0); // exactly representable values survive
}

TEST(ColourConvert, U2F10F10F10MatchesTheSyncWriterExactly) {
    // every 10 bit float and every alpha level: guest -> host -> guest (the writer of surface_pack.h) is the identity
    // for finite values (NaN / infinity payloads are normalised by the writer)
    for (uint32_t alpha = 0; alpha < 4; alpha++) {
        std::vector<uint8_t> guest(1024 * 4), host(1024 * 8), back(1024 * 4);
        for (uint32_t i = 0; i < 1024; i++) {
            const uint32_t f = ((i >> 5) == 31) ? (i & 0x3E0) : i; // skip inf / nan encodings
            put32(&guest[i * 4], f | (f << 10) | ((1023 - f) << 20) | (alpha << 30));
        }
        // the third channel must be finite too
        for (uint32_t i = 0; i < 1024; i++) {
            uint32_t v = get32(&guest[i * 4]);
            if (((v >> 25) & 31) == 31)
                v &= ~(1u << 25);
            put32(&guest[i * 4], v);
        }
        u2f10f10f10_to_rgba16f(host.data(), guest.data(), 1024);
        pack_rgba16f_to_u2f10f10f10(back.data(), host.data(), 1024, 1);
        EXPECT_EQ(guest, back) << alpha;
    }
}

TEST(ColourConvert, SE5M9M9M9RoundTripsRepresentableColours) {
    std::vector<uint8_t> guest(4), host(8), back(4);
    const auto decode = [](uint32_t x, uint32_t shift) { return static_cast<double>((x >> shift) & 0x1FF) * std::ldexp(1.0, static_cast<int>(x >> 27) - 24); };
    for (uint32_t e = 6; e < 28; e += 3) {
        for (const uint32_t m : { 0u, 1u, 100u, 511u }) {
            const uint32_t v = 300u | (m << 9) | (256u << 18) | (e << 27);
            put32(guest.data(), v);
            se5m9m9m9_to_rgba16f(host.data(), guest.data(), 1);
            pack_rgba16f_to_se5m9m9m9(back.data(), host.data(), 1, 1);
            const uint32_t w = get32(back.data());
            for (const uint32_t shift : { 0u, 9u, 18u }) {
                const double a = decode(v, shift), b = decode(w, shift);
                EXPECT_NEAR(a, b, std::max(a, 1e-30) * 0.01) << e << " " << m;
            }
        }
    }
}

TEST(ColourConvert, U2U10IsExactThroughHalf) {
    std::vector<uint8_t> guest(1024 * 4), host(1024 * 8), back(1024 * 4);
    for (uint32_t i = 0; i < 1024; i++)
        put32(&guest[i * 4], i | (((i * 7) & 0x3FF) << 10) | (((1023 - i) & 0x3FF) << 20) | ((i & 3) << 30));
    u2u10_to_rgba16f(host.data(), guest.data(), 1024);
    rgba16f_to_u2u10(back.data(), host.data(), 1024);
    EXPECT_EQ(guest, back);
}

TEST(ColourConvert, F11F11F10RoundTripsAndDropsNegatives) {
    std::vector<uint8_t> guest(2048 * 4), host(2048 * 8), back(2048 * 4);
    for (uint32_t i = 0; i < 2048; i++) {
        const uint32_t r = ((i >> 6) == 31) ? (i & 0x7C0) : i; // skip inf / nan
        put32(&guest[i * 4], r | (r << 11) | ((((r >> 1) >> 5) == 31 ? 0 : (r >> 1)) << 22));
    }
    f11f11f10_to_rgba16f(host.data(), guest.data(), 2048);
    rgba16f_to_f11f11f10(back.data(), host.data(), 2048);
    EXPECT_EQ(guest, back);
    // negative host values clamp to zero (no sign bit in the guest format)
    put16(&host[0], float_to_half(-3.0f));
    rgba16f_to_f11f11f10(back.data(), host.data(), 1);
    EXPECT_EQ(get32(back.data()) & 0x7FF, 0u);
}

TEST(ColourConvert, SixteenBitPackedFormatsRoundTripThroughRgba8) {
    std::vector<uint8_t> guest(65536 * 2), host(65536 * 4), back(65536 * 2);
    for (uint32_t i = 0; i < 65536; i++)
        put16(&guest[i * 2], static_cast<uint16_t>(i));
    u565_to_rgba8(host.data(), guest.data(), 65536);
    rgba8_to_u565(back.data(), host.data(), 65536);
    EXPECT_EQ(guest, back);
    u1555_to_rgba8(host.data(), guest.data(), 65536);
    rgba8_to_u1555(back.data(), host.data(), 65536);
    for (uint32_t i = 0; i < 65536; i++)
        EXPECT_EQ(get16(&back[i * 2]), i & 0xFFFF) << i;
    u4444_to_rgba8(host.data(), guest.data(), 65536);
    pack_rgba8_to_r4g4b4a4(back.data(), host.data(), 65536, 1);
    EXPECT_EQ(guest, back);
}

TEST(ColourConvert, DispatcherCoversEveryFallbackKind) {
    std::vector<uint8_t> a(64, 0), b(64, 0);
    for (const Repack r : { Repack::U4444_RGBA8, Repack::U2F10F10F10_F16, Repack::SE5M9M9M9_F16, Repack::U2U10U10U10_F16, Repack::F11F11F10_F16,
             Repack::U1555_RGBA8, Repack::U565_RGBA8, Repack::Unorm16_F32, Repack::Snorm16_F32, Repack::Snorm8_F16, Repack::F32_F16 })
        EXPECT_TRUE(guest_to_host(r, a.data(), b.data(), 1, 2)) << static_cast<int>(r);
    EXPECT_FALSE(guest_to_host(Repack::None, a.data(), b.data(), 1, 4));
    EXPECT_EQ(host_bytes_per_pixel(Repack::Unorm16_F32, 4), 8u); // U16U16 -> RG32F
    EXPECT_EQ(host_bytes_per_pixel(Repack::F32_F16, 8), 4u); // F32F32 -> RG16F
}
