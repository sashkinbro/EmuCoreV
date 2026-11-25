// EmuCoreV: rasteriser odds and ends of the Vulkan renderer (P15).
//
// Pure logic, no Vulkan calls, so that it can be unit tested on the host:
//  - region clip INSIDE: the pixels INSIDE the region are clipped, which a single scissor rectangle cannot express; the
//    complement of the region inside the target is covered by up to four rectangles, one draw per rectangle
//  - the point size register: the size of a point whose vertex shader writes no PSIZE, clamped to the device range
//  - point sprite texture coordinate modes of the polygon mode
//  - wide lines without the wideLines feature: the line is repeated shifted by sub-pixel steps (a brush)
#pragma once

#include <gxm/types.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace renderer::vulkan::raster {

struct Rect {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t w = 0;
    uint32_t h = 0;
    bool operator==(const Rect &) const = default;
};

struct Strips {
    std::array<Rect, 4> rect{};
    uint32_t count = 0;
};

// The target minus the hole [hx0, hx1) x [hy0, hy1) (hole in target pixels, may extend outside the target).
// An empty hole (or one outside the target) leaves the whole target. A hole covering the target leaves nothing (count 0).
inline Strips outside_of_hole(const uint32_t target_w, const uint32_t target_h, int64_t hx0, int64_t hy0, int64_t hx1, int64_t hy1) {
    Strips s;
    hx0 = std::clamp<int64_t>(hx0, 0, target_w);
    hx1 = std::clamp<int64_t>(hx1, 0, target_w);
    hy0 = std::clamp<int64_t>(hy0, 0, target_h);
    hy1 = std::clamp<int64_t>(hy1, 0, target_h);
    if (hx1 <= hx0 || hy1 <= hy0) {
        s.rect[0] = { 0, 0, target_w, target_h };
        s.count = target_w && target_h ? 1 : 0;
        return s;
    }
    const auto add = [&](const int64_t x, const int64_t y, const int64_t w, const int64_t h) {
        if (w > 0 && h > 0)
            s.rect[s.count++] = { static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<uint32_t>(w), static_cast<uint32_t>(h) };
    };
    add(0, 0, target_w, hy0); // above the hole, full width
    add(0, hy1, target_w, static_cast<int64_t>(target_h) - hy1); // below the hole, full width
    add(0, hy0, hx0, hy1 - hy0); // left of the hole, between the two
    add(hx1, hy0, static_cast<int64_t>(target_w) - hx1, hy1 - hy0); // right of the hole
    return s;
}

// The point size of a point whose vertex program writes no PSIZE: the point width register in pixels. Without largePoints
// the device only guarantees 1.0.
inline float point_size(const uint32_t width_register, const float res_multiplier, const bool large_points, const float max_point_size) {
    const float wanted = static_cast<float>(std::max<uint32_t>(width_register, 1)) * res_multiplier;
    const float limit = large_points ? std::max(max_point_size, 1.0f) : 1.0f;
    return std::clamp(wanted, 1.0f, limit);
}

// Point sprite coordinates: PROVISIONAL reading of the mode names (the first digit is where U starts, the second where V
// starts): POINT_10UV runs U from 1 to 0, POINT_01UV runs V from 1 to 0, every other mode keeps the host (0, 0) at the top left.
struct PointUvFlip {
    bool flip_u = false;
    bool flip_v = false;
    bool operator==(const PointUvFlip &) const = default;
};
inline PointUvFlip point_uv_flip(const SceGxmPolygonMode mode) {
    switch (mode) {
    case SCE_GXM_POLYGON_MODE_POINT_10UV: return { true, false };
    case SCE_GXM_POLYGON_MODE_POINT_01UV: return { false, true };
    default: return {};
    }
}

// Wide lines without the wideLines feature: a line of `width` pixels is repeated at these viewport offsets (in pixels).
// The brush is a cross (the offsets along x and along y); wider than max_brush_width is clamped. Width <= 1: one draw.
constexpr float max_brush_width = 8.0f;
struct Offset {
    float x;
    float y;
    bool operator==(const Offset &) const = default;
};
inline std::vector<Offset> line_brush(float width) {
    std::vector<Offset> offsets{ { 0.0f, 0.0f } };
    width = std::min(width, max_brush_width);
    const int steps = static_cast<int>(width + 0.5f);
    if (steps <= 1)
        return offsets;
    offsets.clear();
    const float first = -static_cast<float>(steps - 1) * 0.5f;
    for (int i = 0; i < steps; i++)
        offsets.push_back({ 0.0f, first + static_cast<float>(i) }); // across a horizontal line
    for (int i = 0; i < steps; i++) {
        const float o = first + static_cast<float>(i);
        if (o != 0.0f || steps % 2 == 0)
            offsets.push_back({ o, 0.0f }); // across a vertical line (the centre repeats the first set for odd widths)
    }
    return offsets;
}

} // namespace renderer::vulkan::raster
