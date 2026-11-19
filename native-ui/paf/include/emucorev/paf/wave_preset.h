#pragma once

#include <array>

namespace emucorev::paf {

struct WaveSkyPreset {
    std::array<float, 2> theta_phi;
    float distance;
    std::array<float, 3> sun_color;
    float sun_power;
    float sun_control;
    std::array<float, 3> zenith_color;
    std::array<float, 3> horizon_color;
    float wave_fog_alpha;
    float radius;
    float xscale;
    float side_angle;
    float horizon_angle;
    float horizon_blend_range;
    float horizon_curvature;
    float sky_blend_start;
    float sky_blend_range;
    float pos_distortion_scale;
    float grad_distortion_scale;
    float horizon_distortion_scale;
};

[[nodiscard]] const WaveSkyPreset& effects_default_0512_01_sky_preset() noexcept;

} // namespace emucorev::paf
