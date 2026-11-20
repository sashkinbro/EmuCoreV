#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace emucorev::paf {

// PS Vita paf/cg/drawobj/focus/focus.fp (0x813008c8) 3-layer pulsing cyan halo renderer.
// Mirrors a single-quadrant RGBA mask (cf115394.png, 104x104) across all 4 quadrants.
class FocusCursorRenderer {
public:
    FocusCursorRenderer();
    ~FocusCursorRenderer();
    FocusCursorRenderer(const FocusCursorRenderer&) = delete;
    FocusCursorRenderer& operator=(const FocusCursorRenderer&) = delete;

    // Draws a pulsing 3-layer cyan focus cursor centered at (center_x, center_y)
    // in logical screen space (0..logical_width, 0..logical_height, top-left origin).
    // If ring_size == 0, only outer pulsing cyan ring halo is skipped.
    void draw(std::uint32_t quadrant_texture, float center_x, float center_y, float size,
        float pulse_phase_rad, std::uint32_t logical_width = 960,
        std::uint32_t logical_height = 544);

    // Draws with explicit base/glow/add RGBA colors (matches focus.fp sa[0..1]).
    void draw_colors(std::uint32_t quadrant_texture, float center_x, float center_y, float size,
        std::array<float, 4> base_color, std::array<float, 4> glow_color,
        std::array<float, 4> add_color, std::uint32_t logical_width = 960,
        std::uint32_t logical_height = 544);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
