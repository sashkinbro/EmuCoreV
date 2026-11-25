// P15: region clip INSIDE strips, point size register, point sprite UV flips, wide line brush.
#include <renderer/vulkan/raster_model.h>

#include <gtest/gtest.h>

#include <set>
#include <utility>

using namespace renderer::vulkan::raster;

namespace {
// brute force reference: which pixels of the target do the strips cover, and how often
std::vector<int> coverage(const Strips &s, const uint32_t w, const uint32_t h) {
    std::vector<int> c(static_cast<size_t>(w) * h, 0);
    for (uint32_t i = 0; i < s.count; i++) {
        const Rect &r = s.rect[i];
        for (uint32_t y = 0; y < r.h; y++)
            for (uint32_t x = 0; x < r.w; x++)
                c[static_cast<size_t>(r.y + y) * w + (r.x + x)]++;
    }
    return c;
}
} // namespace

TEST(RegionClipInside, StripsCoverExactlyThePixelsOutsideTheHole) {
    const uint32_t w = 40, h = 30;
    for (const auto &hole : { std::array<int, 4>{ 10, 5, 25, 20 }, { 0, 0, 40, 10 }, { 5, 0, 15, 30 }, { 0, 0, 1, 1 }, { 39, 29, 40, 30 }, { -5, -5, 8, 8 }, { 30, 20, 100, 100 } }) {
        const Strips s = outside_of_hole(w, h, hole[0], hole[1], hole[2], hole[3]);
        const auto c = coverage(s, w, h);
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                const bool inside = static_cast<int>(x) >= std::max(hole[0], 0) && static_cast<int>(x) < std::min<int>(hole[2], w)
                    && static_cast<int>(y) >= std::max(hole[1], 0) && static_cast<int>(y) < std::min<int>(hole[3], h);
                EXPECT_EQ(c[y * w + x], inside ? 0 : 1) << x << "," << y << " hole " << hole[0] << "," << hole[1] << "," << hole[2] << "," << hole[3];
            }
    }
}

TEST(RegionClipInside, EmptyOrOutsideHoleLeavesTheWholeTarget) {
    const Strips a = outside_of_hole(64, 32, 10, 10, 10, 20);
    ASSERT_EQ(a.count, 1u);
    EXPECT_EQ(a.rect[0], (Rect{ 0, 0, 64, 32 }));
    const Strips b = outside_of_hole(64, 32, 100, 100, 120, 120);
    ASSERT_EQ(b.count, 1u);
    EXPECT_EQ(b.rect[0], (Rect{ 0, 0, 64, 32 }));
}

TEST(RegionClipInside, HoleCoveringTheTargetLeavesNothing) {
    EXPECT_EQ(outside_of_hole(64, 32, -10, -10, 100, 100).count, 0u);
}

TEST(RegionClipInside, NeverMoreThanFourRects) {
    EXPECT_EQ(outside_of_hole(64, 32, 10, 10, 20, 20).count, 4u);
}

TEST(PointSize, RegisterIsThePointWidthClampedToTheDevice) {
    EXPECT_FLOAT_EQ(point_size(4, 1.0f, true, 64.0f), 4.0f);
    EXPECT_FLOAT_EQ(point_size(4, 2.0f, true, 64.0f), 8.0f); // scaled with the resolution
    EXPECT_FLOAT_EQ(point_size(0, 1.0f, true, 64.0f), 1.0f); // never smaller than one pixel
    EXPECT_FLOAT_EQ(point_size(200, 1.0f, true, 64.0f), 64.0f);
    EXPECT_FLOAT_EQ(point_size(8, 1.0f, false, 64.0f), 1.0f); // no largePoints: only 1.0 is guaranteed
}

TEST(PointSprite, UvModes) {
    EXPECT_EQ(point_uv_flip(SCE_GXM_POLYGON_MODE_POINT), (PointUvFlip{ false, false }));
    EXPECT_EQ(point_uv_flip(SCE_GXM_POLYGON_MODE_POINT_10UV), (PointUvFlip{ true, false }));
    EXPECT_EQ(point_uv_flip(SCE_GXM_POLYGON_MODE_POINT_01UV), (PointUvFlip{ false, true }));
    EXPECT_EQ(point_uv_flip(SCE_GXM_POLYGON_MODE_TRIANGLE_FILL), (PointUvFlip{ false, false }));
}

TEST(WideLineBrush, OneDrawForThinLines) {
    EXPECT_EQ(line_brush(1.0f).size(), 1u);
    EXPECT_EQ(line_brush(0.0f).size(), 1u);
    EXPECT_EQ(line_brush(1.0f)[0], (Offset{ 0.0f, 0.0f }));
}

TEST(WideLineBrush, OffsetsAreCentredAndDistinct) {
    for (const float width : { 2.0f, 3.0f, 4.0f, 5.0f, 8.0f }) {
        const auto brush = line_brush(width);
        std::set<std::pair<float, float>> unique;
        float sum_x = 0, sum_y = 0;
        for (const Offset &o : brush) {
            unique.insert({ o.x, o.y });
            sum_x += o.x;
            sum_y += o.y;
        }
        EXPECT_EQ(unique.size(), brush.size()) << width;
        EXPECT_NEAR(sum_x, 0.0f, 1e-4f) << width;
        EXPECT_NEAR(sum_y, 0.0f, 1e-4f) << width;
        const int steps = static_cast<int>(width + 0.5f);
        // a horizontal line shifted across covers `steps` rows, a vertical one `steps` columns
        int rows = 0, cols = 0;
        for (const Offset &o : brush) {
            if (o.x == 0.0f) rows++;
            if (o.y == 0.0f) cols++;
        }
        EXPECT_EQ(rows, steps) << width;
        EXPECT_EQ(cols, steps) << width;
    }
}

TEST(WideLineBrush, WidthIsClamped) {
    EXPECT_EQ(line_brush(100.0f).size(), line_brush(max_brush_width).size());
}
