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
}
