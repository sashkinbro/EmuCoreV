#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace emucorev::paf {
struct PagePoint { float x, y; };
struct PageTurnLayout {
    PagePoint rest;
    PagePoint hit_size;
    float corner_exclusion;
};
struct PageTurnDrawControl {
    PagePoint position, offset;
    std::uint32_t back_color;
};
struct PageFoldProjection {
    PagePoint on_fold, mirrored;
    float distance;
};
struct PageFoldShape {
    PagePoint center, normal;
    std::array<PagePoint, 4> corners;
    std::array<PageFoldProjection, 4> projected;
    PagePoint far_corner;
    PageFoldProjection maximum, minimum;
    PagePoint line_start, line_end;
    bool outside;
    PagePoint aspect;
};
struct PageTurnFan { std::uint32_t first, count; };
struct PageTurnShapeStyle {
    float top_scale = std::bit_cast<float>(0x3e6de871u);
    float top_curl = std::bit_cast<float>(0x3f1b6da8u);
    float bottom_scale = std::bit_cast<float>(0x3c1246bfu);
    float normal_scale_z = std::bit_cast<float>(0x40536db5u);
};
struct PageTurnShapeMaterial {
    std::array<float, 4> deepest;
    PagePoint grep;
    std::array<float, 4> params, fold_plane, aspect;
    float normal_scale_z;
    float fresnel_multiplier;
};
using PageTurnMatrix = std::array<float, 16>;
struct PageTurnPose {
    std::array<float, 3> scale{1, 1, 1};
    std::array<float, 3> position{0, 0, 0};
    std::array<float, 4> rotation{0, 0, 0, 1};
};
struct PageTurnTransforms { PageTurnMatrix model, model_view_projection; };
struct PageTurnPointLight {
    PagePoint angles{std::bit_cast<float>(0x3fa8bf90u), std::bit_cast<float>(0x3f0f9d91u)};
    float distance = std::bit_cast<float>(0x450ba0a1u);
    std::array<float, 3> attenuation{1, 0, 0}, ambient{.2f, .2f, .2f}, diffuse{1, 1, 1};
    float fade = 1;
};
struct PageTurnLightParameters { PageTurnMatrix position, color; };
struct PageTurnMesh {
    // Reused caller-owned storage. Only the counted prefixes are initialized.
    std::array<PagePoint, 1024> vertices;
    std::array<PageTurnFan, 128> front, folded;
    std::uint32_t vertex_count = 0, front_count = 0, folded_count = 0;
};
enum class PageTurnRelease : std::uint32_t {
    beyond_threshold = 0x10000003,
    outward = 0x10000004,
    within_threshold = 0x10000005,
};

// Centered, Y-up widget coordinates. These are the recovered PageTurn layout,
// hit-testing and draw-control operations, not the complete gesture/mesh engine.
PageTurnLayout page_turn_layout(PagePoint size, float fold_fraction, float corner_aspect);
bool page_turn_hit_test(PagePoint size, const PageTurnLayout& layout, PagePoint point);
PagePoint page_turn_drag_target(PagePoint pointer, PagePoint initial, PagePoint offset);
PageTurnDrawControl page_turn_draw_control(PagePoint size, PagePoint position,
    PagePoint offset, std::array<float, 3> opacity);
PageTurnRelease page_turn_release(PagePoint displacement, float threshold,
    bool allow_outward, bool gamepad);
// Geometry consumed by the original fold renderer, in height-normalized units.
PageFoldShape page_turn_fold_shape(PagePoint deepest, PagePoint aspect);
PagePoint page_turn_normalized_aspect(PagePoint size);
void page_turn_mesh(const PageFoldShape& shape, PageTurnMesh& output);
// Uniform values shared by flat/rounded passes. Shape and point must describe
// the same fold, normalized by physical page height with the original
// reciprocal/multiply rounding (aspect.y can differ from 1 by one float step).
PageTurnShapeMaterial page_turn_shape_material(PagePoint point, const PageFoldShape& shape,
    const PageTurnShapeStyle& style = {});
// Column-major matrices; physical-pixel position refers to the page center.
PageTurnTransforms page_turn_transforms(PagePoint size, const PageTurnPose& pose,
    const PageTurnMatrix& projection, const PageTurnMatrix& view);
PageTurnLightParameters page_turn_light_parameters(const PageTurnPointLight& light);
}
