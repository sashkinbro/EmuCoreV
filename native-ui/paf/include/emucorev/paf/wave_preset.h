#pragma once

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace emucorev::paf {

struct WaveMaterialPreset {
    std::array<float, 4> color{};
    float diffuse_coeff{};
    float specular_coeff{};
    float specular_power{};
    float fresnel_front{};
    float fresnel_control{};
    float fresnel_coeff{};
    float refraction_coeff{};
    float refraction_amount{};
    float second_reflection{};
};

struct WaveLightPreset {
    std::array<float, 2> theta_phi{};
    float distance{};
    std::array<float, 3> attn{};
    std::array<float, 3> ambient_color{};
    std::array<float, 3> diffuse_color{};
    float fade{};
};

struct WaveFogPreset {
    std::array<float, 3> color{};
    std::array<float, 4> density{};
    std::array<float, 4> light_color{};
    float d_scale{};
    float d_offset{};
};

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

struct WaveSimPreset {
    float gravity{};
    float A{};
    float logA{};
    float patch_size{};
    float wind_speed{};
    float wind_dir{};
    float time_step{};
};

struct WaveInstancePreset {
    float distortion_scale{};
    std::array<float, 3> translation{};
    std::array<float, 4> rotation{};
    std::array<float, 3> scale{};
    float global_scale{};
    float uv_scale{};
    float uv_rotate{};
    std::array<float, 3> center_color{};
    float center_power{};
    std::array<float, 3> edge_color{};
    float edge_power{};
    std::array<float, 4> cross_section{};
    std::array<float, 4> cross_section_flat{};
    float decay{};
    float shadow_nor_blend{};
};

struct WaveCameraPreset {
    std::array<float, 2> edge_fog{};
    float pitch_max{};
    float pitch_rest{};
    float pitch_min{};
    float pitch_debug{};
    float roll_max{};
    float roll_max_zone{};
    float zoom{};
    float camera_chase{};
};

struct WaveMasterPreset {
    float uv_rotate_distort_curve{};
    float uv_rotate_distort_control_chase{};
    float uv_rotate_distort_control{};
};

struct WaveThemePreset {
    std::string_view name;
    WaveMaterialPreset material{};
    WaveLightPreset light{};
    WaveFogPreset fog0{};
    WaveFogPreset fog1{};
    WaveSkyPreset sky{};
    WaveSimPreset sim{};
    WaveInstancePreset wave{};
    WaveCameraPreset camera{};
};

inline constexpr std::size_t kWaveThemeCount = 32;

[[nodiscard]] const WaveMasterPreset& wave_master_preset() noexcept;
[[nodiscard]] std::span<const WaveThemePreset> wave_theme_presets() noexcept;
[[nodiscard]] const WaveThemePreset* find_wave_theme_preset(std::string_view name) noexcept;
[[nodiscard]] const WaveThemePreset& wave_theme_preset_or_default(std::string_view name) noexcept;
[[nodiscard]] const WaveThemePreset& default_wave_theme_preset() noexcept;
[[nodiscard]] const WaveSkyPreset& effects_default_0512_01_sky_preset() noexcept;

} // namespace emucorev::paf
