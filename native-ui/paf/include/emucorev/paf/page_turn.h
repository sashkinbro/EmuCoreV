#pragma once
#include <array>
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
}
