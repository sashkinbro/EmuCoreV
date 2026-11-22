// Host tests for the pure SceAvcdec/SceM4vdec picture helpers (codec/avc_picture.h):
// Annex-B wrapping, pitch/crop/layout arithmetic and the YUV 4:2:0 -> RGB conversion.
#include <codec/avc_picture.h>

#include <gtest/gtest.h>

#include <cstdlib>

using namespace avc_picture;

TEST(AvcPicture, PixelTypeTables) {
    EXPECT_TRUE(is_rgb(kPixelRgba8888));
    EXPECT_TRUE(is_rgb(kPixelRgba565));
    EXPECT_TRUE(is_rgb(kPixelRgba5551));
    EXPECT_TRUE(is_rgb(kPixelBgra8888));
    EXPECT_TRUE(is_yuv(kPixelYuv420Raster));
    EXPECT_TRUE(is_yuv(kPixelYuv420PackedRaster));
    EXPECT_FALSE(is_valid_pixel_type(0x03));
    EXPECT_FALSE(is_valid_pixel_type(0x30));
    EXPECT_EQ(bytes_per_pixel(kPixelRgba8888), 4u);
    EXPECT_EQ(bytes_per_pixel(kPixelBgra8888), 4u);
    EXPECT_EQ(bytes_per_pixel(kPixelRgba565), 2u);
    EXPECT_EQ(bytes_per_pixel(kPixelRgba5551), 2u);
    EXPECT_EQ(bytes_per_pixel(kPixelYuv420Raster), 0u);
}

TEST(AvcPicture, AnnexBAddsStartCodeOnlyWhenMissing) {
    const uint8_t nal[] = { 0x67, 0x42, 0x00, 0x1f };
    std::vector<uint8_t> out;
    append_annexb(out, nal, sizeof(nal));
    EXPECT_EQ(out, (std::vector<uint8_t>{ 0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1f }));

    const uint8_t with4[] = { 0, 0, 0, 1, 0x68, 0xce };
    out.clear();
    append_annexb(out, with4, sizeof(with4));
    EXPECT_EQ(out, std::vector<uint8_t>(with4, with4 + sizeof(with4)));

    const uint8_t with3[] = { 0, 0, 1, 0x65, 0x88 };
    out.clear();
    append_annexb(out, with3, sizeof(with3));
    EXPECT_EQ(out, std::vector<uint8_t>(with3, with3 + sizeof(with3)));

    out.clear();
    append_annexb(out, nal, 0);
    append_annexb(out, nullptr, 4);
    EXPECT_TRUE(out.empty());
}

TEST(AvcPicture, AnnexBMultipleUnits) {
    const uint8_t sps[] = { 0x67, 1, 2 };
    const uint8_t pps[] = { 0x68, 3 };
    const uint8_t idr[] = { 0, 0, 0, 1, 0x65, 9 };
    std::vector<uint8_t> out;
    build_annexb({ { sps, 3 }, { nullptr, 0 }, { pps, 2 }, { idr, 6 } }, out);
    EXPECT_EQ(out, (std::vector<uint8_t>{ 0, 0, 0, 1, 0x67, 1, 2, 0, 0, 0, 1, 0x68, 3, 0, 0, 0, 1, 0x65, 9 }));
    // A leading zero-length-prefixed payload that merely looks similar is still wrapped.
    const uint8_t not_start[] = { 0, 0, 2, 7 };
    out.clear();
    append_annexb(out, not_start, sizeof(not_start));
    EXPECT_EQ(out.size(), 8u);
}

TEST(AvcPicture, PitchArithmetic) {
    EXPECT_EQ(effective_pitch(1024, 960), 1024u);
    EXPECT_EQ(effective_pitch(0, 960), 960u);
    EXPECT_EQ(effective_pitch(512, 960), 960u);
    EXPECT_EQ((copy_extent(960, 544, 1280, 720)), (Extent{ 960, 544 }));
    EXPECT_EQ((copy_extent(1920, 1080, 960, 544)), (Extent{ 960, 544 }));
}

TEST(AvcPicture, YuvLayout) {
    YuvLayout l;
    ASSERT_TRUE(yuv420_layout(kPixelYuv420Raster, 960, 544, l));
    EXPECT_EQ(l.luma_pitch, 960u);
    EXPECT_EQ(l.chroma_pitch, 480u);
    EXPECT_EQ(l.chroma_offset, 960u * 544u);
    EXPECT_EQ(l.v_offset, 960u * 544u + 480u * 272u);
    EXPECT_EQ(l.total_size, 960u * 544u * 3u / 2u);

    ASSERT_TRUE(yuv420_layout(kPixelYuv420PackedRaster, 1024, 544, l));
    EXPECT_EQ(l.chroma_pitch, 1024u);
    EXPECT_EQ(l.v_offset, 0u);
    EXPECT_EQ(l.total_size, 1024u * 544u * 3u / 2u);

    EXPECT_FALSE(yuv420_layout(kPixelRgba8888, 960, 544, l));
    EXPECT_FALSE(yuv420_layout(kPixelYuv420Raster, 0, 544, l));
    EXPECT_FALSE(yuv420_layout(kPixelYuv420Raster, 960, 0, l));
}

TEST(AvcPicture, BufferSizes) {
    EXPECT_EQ(picture_buffer_size(kPixelRgba8888, 960, 544), 960u * 544u * 4u);
    EXPECT_EQ(picture_buffer_size(kPixelRgba565, 960, 544), 960u * 544u * 2u);
    EXPECT_EQ(picture_buffer_size(kPixelYuv420Raster, 1280, 720), 1280u * 720u * 3u / 2u);
    EXPECT_EQ(picture_buffer_size(0x30, 1280, 720), 0u);
    EXPECT_EQ(picture_buffer_size(kPixelRgba8888, 0, 720), 0u);
    // No 32-bit wrap-around for absurd values.
    EXPECT_EQ(picture_buffer_size(kPixelRgba8888, 0xffffffffu, 0xffffffffu), uint64_t{ 0xffffffffu } * 4 * 0xffffffffu);
}

TEST(AvcPicture, VisibleRectCrop) {
    Rect r;
    ASSERT_TRUE(visible_rect(1920, 1088, 0, 0, 0, 8, r));
    EXPECT_EQ(r, (Rect{ 0, 0, 1920, 1080 }));
    ASSERT_TRUE(visible_rect(960, 544, 4, 6, 2, 2, r));
    EXPECT_EQ(r, (Rect{ 4, 2, 950, 540 }));
    // Odd offsets are rounded down to the chroma grid.
    ASSERT_TRUE(visible_rect(960, 544, 3, 3, 1, 1, r));
    EXPECT_EQ(r, (Rect{ 2, 0, 956, 544 }));
    ASSERT_TRUE(visible_rect(640, 480, 0, 0, 0, 0, r));
    EXPECT_EQ(r, (Rect{ 0, 0, 640, 480 }));
    // Empty or overflowing crops are rejected.
    EXPECT_FALSE(visible_rect(640, 480, 320, 320, 0, 0, r));
    EXPECT_FALSE(visible_rect(640, 480, 0, 0, 400, 100, r));
    EXPECT_FALSE(visible_rect(640, 480, 0xfffffffeu, 0xfffffffeu, 0, 0, r));
}

namespace {
void expect_near(const Rgb &a, const Rgb &b, int tol) {
    EXPECT_LE(std::abs(int(a.r) - int(b.r)), tol) << int(a.r) << "," << int(a.g) << "," << int(a.b);
    EXPECT_LE(std::abs(int(a.g) - int(b.g)), tol) << int(a.r) << "," << int(a.g) << "," << int(a.b);
    EXPECT_LE(std::abs(int(a.b) - int(b.b)), tol) << int(a.r) << "," << int(a.g) << "," << int(a.b);
}
} // namespace

TEST(AvcPicture, ReferenceColors601Limited) {
    const auto m = csc_matrix(Csc::Itu601);
    EXPECT_EQ(yuv_to_rgb(16, 128, 128, m), (Rgb{ 0, 0, 0 }));
    EXPECT_EQ(yuv_to_rgb(235, 128, 128, m), (Rgb{ 255, 255, 255 }));
    // BT.601 limited primaries (values from the standard definition).
    expect_near(yuv_to_rgb(81, 90, 240, m), { 255, 0, 0 }, 2);
    expect_near(yuv_to_rgb(145, 54, 34, m), { 0, 255, 0 }, 2);
    expect_near(yuv_to_rgb(41, 240, 110, m), { 0, 0, 255 }, 2);
    expect_near(yuv_to_rgb(126, 128, 128, m), { 126, 126, 126 }, 3); // mid gray: (126-16)*255/219 = 128
}

TEST(AvcPicture, ReferenceColors601Matches) {
    // Independent floating point evaluation of the same BT.601 limited-range formula.
    const auto m = csc_matrix(Csc::Itu601);
    for (int y = 16; y <= 235; y += 31) {
        for (int u = 16; u <= 240; u += 37) {
            for (int v = 16; v <= 240; v += 41) {
                const double yy = (y - 16) * 255.0 / 219.0, cb = (u - 128) * 255.0 / 224.0, cr = (v - 128) * 255.0 / 224.0;
                const auto clampd = [](double d) { return d < 0 ? 0 : (d > 255 ? 255 : int(d + 0.5)); };
                const Rgb ref = { uint8_t(clampd(yy + 1.402 * cr)), uint8_t(clampd(yy - 0.344136 * cb - 0.714136 * cr)), uint8_t(clampd(yy + 1.772 * cb)) };
                expect_near(yuv_to_rgb(uint8_t(y), uint8_t(u), uint8_t(v), m), ref, 1);
            }
        }
    }
}

TEST(AvcPicture, Itu709AndFullRange) {
    const auto m709 = csc_matrix(Csc::Itu709);
    EXPECT_EQ(yuv_to_rgb(16, 128, 128, m709), (Rgb{ 0, 0, 0 }));
    // BT.709 limited red: Y=63, Cb=102, Cr=240.
    expect_near(yuv_to_rgb(63, 102, 240, m709), { 255, 0, 0 }, 2);
    // 601 and 709 differ for a saturated colour.
    EXPECT_NE(yuv_to_rgb(81, 90, 240, csc_matrix(Csc::Itu601)).g, yuv_to_rgb(81, 90, 240, m709).g);

    const auto full = csc_matrix(Csc::Itu601Full);
    EXPECT_EQ(yuv_to_rgb(0, 128, 128, full), (Rgb{ 0, 0, 0 }));
    EXPECT_EQ(yuv_to_rgb(255, 128, 128, full), (Rgb{ 255, 255, 255 }));
    EXPECT_EQ(yuv_to_rgb(100, 128, 128, full), (Rgb{ 100, 100, 100 }));
    // Out of gamut values clamp instead of wrapping.
    EXPECT_EQ(yuv_to_rgb(255, 255, 255, full).r, 255);
    EXPECT_EQ(yuv_to_rgb(0, 0, 0, full).r, 0);
}

TEST(AvcPicture, DefaultCoefficientResolution) {
    EXPECT_EQ(resolve_csc(Csc::Default, false, false), Csc::Itu601);
    EXPECT_EQ(resolve_csc(Csc::Default, true, false), Csc::Itu709);
    EXPECT_EQ(resolve_csc(Csc::Default, false, true), Csc::Itu601Full);
    EXPECT_EQ(resolve_csc(Csc::Default, true, true), Csc::Itu709Full);
    EXPECT_EQ(resolve_csc(Csc::Itu709, false, true), Csc::Itu709); // explicit request wins
    EXPECT_TRUE(is_valid_csc(4));
    EXPECT_FALSE(is_valid_csc(5));
}

TEST(AvcPicture, ConvertPlanarToRgbaWithPitchAndAlpha) {
    // 4x2 picture, source rows padded to a pitch of 6, chroma planes padded to 4.
    const uint8_t y[2 * 6] = { 81, 81, 145, 145, 0, 0, 81, 81, 145, 145, 0, 0 };
    const uint8_t u[1 * 4] = { 90, 54, 0, 0 };
    const uint8_t v[1 * 4] = { 240, 34, 0, 0 };
    Yuv420Planes planes;
    planes.y = y;
    planes.y_pitch = 6;
    planes.u = u;
    planes.v = v;
    planes.chroma_pitch = 4;
    planes.chroma_step = 1;
    uint8_t out[2 * 20];
    std::fill(out, out + sizeof(out), 0xee);
    convert_yuv420(planes, 4, 2, kPixelRgba8888, Csc::Itu601, 0x80, out, 20);
    for (int row = 0; row < 2; ++row) {
        const uint8_t *p = out + row * 20;
        EXPECT_NEAR(p[0], 255, 2);
        EXPECT_NEAR(p[1], 0, 2);
        EXPECT_NEAR(p[2], 0, 2);
        EXPECT_EQ(p[3], 0x80);
        EXPECT_NEAR(p[8], 0, 2);
        EXPECT_NEAR(p[9], 255, 2);
        EXPECT_NEAR(p[10], 0, 2);
        EXPECT_EQ(p[11], 0x80);
        // Bytes past the 4 converted pixels of the row stay untouched.
        EXPECT_EQ(p[16], 0xee);
    }
}

TEST(AvcPicture, ConvertPackedChromaMatchesPlanar) {
    // 2x2 pixels, interleaved U,V chroma (NV12 order, YUV420_PACKED_RASTER).
    const uint8_t y[4] = { 81, 81, 81, 81 };
    const uint8_t uv[2] = { 90, 240 };
    Yuv420Planes planes;
    planes.y = y;
    planes.y_pitch = 2;
    planes.u = uv;
    planes.v = uv + 1;
    planes.chroma_pitch = 2;
    planes.chroma_step = 2;
    uint8_t out[16] = {};
    convert_yuv420(planes, 2, 2, kPixelBgra8888, Csc::Itu601, 255, out, 8);
    EXPECT_NEAR(out[0], 0, 2); // B
    EXPECT_NEAR(out[2], 255, 2); // R
    EXPECT_EQ(out[3], 255);
    EXPECT_EQ(out[8 + 2], out[2]);
}

TEST(AvcPicture, Rgb565And5551Packing) {
    uint8_t px[2] = {};
    store_pixel(px, kPixelRgba565, { 255, 0, 0 }, 255);
    EXPECT_EQ(px[0] | (px[1] << 8), 0x001f); // red in the low bits (provisional layout)
    store_pixel(px, kPixelRgba565, { 0, 0, 255 }, 255);
    EXPECT_EQ(px[0] | (px[1] << 8), 0xf800);
    store_pixel(px, kPixelRgba5551, { 0, 255, 0 }, 255);
    EXPECT_EQ(px[0] | (px[1] << 8), 0x83e0);
    store_pixel(px, kPixelRgba5551, { 0, 255, 0 }, 0x10);
    EXPECT_EQ(px[0] | (px[1] << 8), 0x03e0);
}

TEST(AvcPicture, ConvertIgnoresInvalidTarget) {
    const uint8_t y[4] = {};
    const uint8_t c[1] = { 128 };
    Yuv420Planes planes{ y, 2, c, c, 1, 1 };
    uint8_t out[8];
    std::fill(out, out + sizeof(out), 0x55);
    convert_yuv420(planes, 2, 2, kPixelYuv420Raster, Csc::Default, 255, out, 8); // YUV is not an RGB target
    EXPECT_EQ(out[0], 0x55);
}
