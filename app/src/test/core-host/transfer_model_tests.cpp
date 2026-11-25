// P12: transfer format conversion against an independent reference, the 2x2 box downscale and the GPU eligibility geometry.
#include <renderer/transfer_model.h>

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace renderer::transfer;

namespace {
constexpr SceGxmTransferFormat colour_formats[] = {
    SCE_GXM_TRANSFER_FORMAT_U8_R, SCE_GXM_TRANSFER_FORMAT_U4U4U4U4_ABGR, SCE_GXM_TRANSFER_FORMAT_U1U5U5U5_ABGR, SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR,
    SCE_GXM_TRANSFER_FORMAT_U8U8_GR, SCE_GXM_TRANSFER_FORMAT_U8U8U8_BGR, SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TRANSFER_FORMAT_U2U10U10U10_ABGR
};

// reference: channels as doubles in [0, 1], the way the format names read (listed from the MSB)
struct RefPixel {
    double c[4] = { 0, 0, 0, 1 };
    bool present[4] = { false, false, false, false };
};

RefPixel ref_decode(const SceGxmTransferFormat f, const uint64_t v) {
    RefPixel p;
    int shift = 0;
    // channel order in memory from the LSB: R G B A
    for (int c = 0; c < 4; c++) {
        int bits = 0;
        switch (f) {
        case SCE_GXM_TRANSFER_FORMAT_U8_R: bits = c == 0 ? 8 : 0; break;
        case SCE_GXM_TRANSFER_FORMAT_U4U4U4U4_ABGR: bits = 4; break;
        case SCE_GXM_TRANSFER_FORMAT_U1U5U5U5_ABGR: bits = c == 3 ? 1 : 5; break;
        case SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR: bits = c == 3 ? 0 : (c == 1 ? 6 : 5); break;
        case SCE_GXM_TRANSFER_FORMAT_U8U8_GR: bits = c < 2 ? 8 : 0; break;
        case SCE_GXM_TRANSFER_FORMAT_U8U8U8_BGR: bits = c < 3 ? 8 : 0; break;
        case SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR: bits = 8; break;
        case SCE_GXM_TRANSFER_FORMAT_U2U10U10U10_ABGR: bits = c == 3 ? 2 : 10; break;
        default: break;
        }
        if (bits) {
            const uint64_t raw = (v >> shift) & ((1ull << bits) - 1);
            p.c[c] = static_cast<double>(raw) / static_cast<double>((1ull << bits) - 1);
            p.present[c] = true;
            shift += bits;
        }
    }
    return p;
}

uint32_t bits_of(const SceGxmTransferFormat f, const int c) {
    const FormatDesc d = describe(f);
    return d.ch[c].bits;
}
} // namespace

TEST(TransferFormats, DescriptorsMatchTheNamedLayout) {
    for (const SceGxmTransferFormat f : colour_formats) {
        const FormatDesc d = describe(f);
        uint32_t total = 0;
        for (const Channel &c : d.ch)
            total += c.bits;
        EXPECT_EQ(total, d.bytes * 8u) << static_cast<uint32_t>(f);
        EXPECT_EQ(pixel_bytes(f), d.bytes);
        // channels do not overlap
        uint64_t used = 0;
        for (const Channel &c : d.ch) {
            if (!c.bits)
                continue;
            const uint64_t mask = ((1ull << c.bits) - 1) << c.shift;
            EXPECT_EQ(used & mask, 0u);
            used |= mask;
        }
    }
    EXPECT_EQ(pixel_bytes(SCE_GXM_TRANSFER_FORMAT_RAW128), 16u);
    EXPECT_FALSE(is_colour(SCE_GXM_TRANSFER_FORMAT_YUYV422));
    EXPECT_FALSE(convertible(SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, SCE_GXM_TRANSFER_FORMAT_RAW32));
}

TEST(TransferFormats, ConversionMatchesTheReferenceForEveryPair) {
    // 2000 spread values per source format keep the 32 bit formats cheap; the 8/16 bit ones are exhaustive below
    for (const SceGxmTransferFormat sf : colour_formats) {
        for (const SceGxmTransferFormat df : colour_formats) {
            const FormatDesc sd = describe(sf), dd = describe(df);
            const uint64_t count = sd.bytes <= 2 ? (1ull << (8 * sd.bytes)) : 4096;
            for (uint64_t i = 0; i < count; i++) {
                const uint64_t v = sd.bytes <= 2 ? i : (i * 2654435761ull) & ((1ull << (8 * sd.bytes)) - 1);
                uint8_t in[4] = {}, out[4] = {};
                std::memcpy(in, &v, sd.bytes);
                convert_pixel(sd, dd, in, out);
                uint64_t got = 0;
                std::memcpy(&got, out, dd.bytes);

                const RefPixel r = ref_decode(sf, v);
                const RefPixel g = ref_decode(df, got);
                for (int c = 0; c < 4; c++) {
                    if (!bits_of(df, c))
                        continue;
                    const double want = bits_of(sf, c) ? r.c[c] : (c == 3 ? 1.0 : 0.0);
                    // the destination channel is the nearest level of its width
                    const double levels = static_cast<double>((1u << bits_of(df, c)) - 1);
                    const double tolerance = 0.5 / levels + 1e-9;
                    ASSERT_NEAR(g.c[c], want, tolerance) << "src " << static_cast<uint32_t>(sf) << " dst " << static_cast<uint32_t>(df) << " value " << v << " channel " << c;
                }
            }
        }
    }
}

TEST(TransferFormats, SameFormatConversionIsTheIdentity) {
    for (const SceGxmTransferFormat f : colour_formats) {
        const FormatDesc d = describe(f);
        const uint64_t count = d.bytes <= 2 ? (1ull << (8 * d.bytes)) : 4096;
        for (uint64_t i = 0; i < count; i++) {
            const uint64_t v = d.bytes <= 2 ? i : (i * 2654435761ull) & ((1ull << (8 * d.bytes)) - 1);
            uint8_t in[4] = {}, out[4] = {};
            std::memcpy(in, &v, d.bytes);
            convert_pixel(d, d, in, out);
            ASSERT_EQ(std::memcmp(in, out, d.bytes), 0) << static_cast<uint32_t>(f) << " " << v;
        }
    }
}

TEST(TransferFormats, EndsOfRangeAreKept) {
    const FormatDesc rgba8 = describe(SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR);
    const FormatDesc p565 = describe(SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR);
    const uint8_t white[4] = { 255, 255, 255, 255 };
    uint8_t out[4] = {};
    convert_pixel(rgba8, p565, white, out);
    EXPECT_EQ(out[0] | (out[1] << 8), 0xFFFF);
    const uint8_t black[4] = { 0, 0, 0, 0 };
    convert_pixel(rgba8, p565, black, out);
    EXPECT_EQ(out[0] | (out[1] << 8), 0);
    // 565 has no alpha: it reads as opaque
    uint8_t back[4] = {};
    convert_pixel(p565, rgba8, out, back);
    EXPECT_EQ(back[3], 255);
}

TEST(TransferDownscale, BoxAverageRoundsHalfUpPerChannel) {
    const FormatDesc d = describe(SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR);
    const uint8_t a[4] = { 0, 1, 2, 255 }, b[4] = { 1, 1, 2, 255 }, c[4] = { 1, 1, 3, 255 }, e[4] = { 0, 0, 0, 255 };
    uint8_t out[4];
    average_2x2(d, a, b, c, e, out);
    // (0+1+1+0+2)/4 = 1, (1+1+1+0+2)/4 = 1, (2+2+3+0+2)/4 = 2, alpha 255
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[1], 1);
    EXPECT_EQ(out[2], 2);
    EXPECT_EQ(out[3], 255);
}

TEST(TransferDownscale, PackedFormatAverageMatchesPerChannelReference) {
    const FormatDesc d = describe(SCE_GXM_TRANSFER_FORMAT_U5U6U5_BGR);
    for (uint32_t i = 0; i < 4000; i++) {
        uint16_t px[4];
        for (int k = 0; k < 4; k++)
            px[k] = static_cast<uint16_t>((i * 40503u + k * 12345u) * 2654435761u >> 7);
        uint16_t out;
        average_2x2(d, reinterpret_cast<uint8_t *>(&px[0]), reinterpret_cast<uint8_t *>(&px[1]), reinterpret_cast<uint8_t *>(&px[2]), reinterpret_cast<uint8_t *>(&px[3]), reinterpret_cast<uint8_t *>(&out));
        for (const Channel &c : { d.ch[0], d.ch[1], d.ch[2] }) {
            uint32_t sum = 0;
            for (int k = 0; k < 4; k++)
                sum += (px[k] >> c.shift) & ((1u << c.bits) - 1);
            EXPECT_EQ((out >> c.shift) & ((1u << c.bits) - 1), (sum + 2) / 4);
        }
    }
}

TEST(TransferDownscale, OnlyExactHalfRunsOnTheGpu) {
    EXPECT_TRUE(is_exact_half(64, 32, 32, 16));
    EXPECT_FALSE(is_exact_half(64, 32, 32, 17));
    EXPECT_FALSE(is_exact_half(65, 32, 32, 16));
    EXPECT_FALSE(is_exact_half(64, 32, 0, 0));
}

TEST(TransferGpuGeometry, LocatesAnImageInsideASurface) {
    const SurfaceGeometry s{ 0x1000, 1024 * 4, 960, 544, 4 };
    // at the surface base
    EXPECT_EQ(locate(s, 0x1000, 4096, 0, 0, 960, 544), (PixelRect{ 0, 0, 960, 544 }));
    // a sub image: the image address is the surface address plus 3 rows and 16 pixels, with its own x/y on top
    const auto sub = locate(s, 0x1000 + 3 * 4096 + 16 * 4, 4096, 2, 5, 100, 50);
    ASSERT_TRUE(sub);
    EXPECT_EQ(*sub, (PixelRect{ 18, 8, 100, 50 }));
}

TEST(TransferGpuGeometry, RejectsAnythingNotExactlyInsideTheSurface) {
    const SurfaceGeometry s{ 0x1000, 4096, 960, 544, 4 };
    EXPECT_FALSE(locate(s, 0x1000, 2048, 0, 0, 10, 10)); // other stride
    EXPECT_FALSE(locate(s, 0x0FFC, 4096, 0, 0, 10, 10)); // before the surface
    EXPECT_FALSE(locate(s, 0x1002, 4096, 0, 0, 10, 10)); // not on a pixel boundary
    EXPECT_FALSE(locate(s, 0x1000, 4096, 0, 0, 961, 10)); // too wide
    EXPECT_FALSE(locate(s, 0x1000, 4096, 0, 540, 10, 10)); // too tall
    EXPECT_FALSE(locate(s, 0x1000, 4096, 0, 0, 0, 10)); // empty
    EXPECT_FALSE(locate(s, 0x1000 + 543 * 4096, 4096, 0, 1, 10, 1)); // starts on the last row, y pushes it out
}

TEST(TransferGpuGeometry, Overlap) {
    EXPECT_TRUE(rects_overlap({ 0, 0, 10, 10 }, { 9, 9, 5, 5 }));
    EXPECT_FALSE(rects_overlap({ 0, 0, 10, 10 }, { 10, 0, 5, 5 }));
    EXPECT_FALSE(rects_overlap({ 0, 0, 10, 10 }, { 0, 10, 5, 5 }));
}
