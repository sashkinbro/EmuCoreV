#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include <emucorev/paf/wave_grid_normal_renderer.h>
#include <emucorev/paf/wave_lighting_renderer.h>
#include <emucorev/paf/wave_position_renderer.h>
#include <emucorev/paf/wave_preset.h>
#include <emucorev/paf/wave_skydome_renderer.h>
#include <emucorev/paf/wave_surface_renderer.h>

namespace emucorev::paf {

using WaveMat4 = std::array<float, 16>;
using WaveVec4 = std::array<float, 4>;
using WaveVec3 = std::array<float, 3>;
using WaveVec2 = std::array<float, 2>;

inline constexpr float kWaveWorldScaleHack = 500.0f;
inline constexpr float kWaveCameraHalfHorizontalFov = 0.401425729f; // 0x3ecd87ac (23 deg)
inline constexpr float kWaveCameraNear = 100.0f;
inline constexpr float kWaveCameraFar = 10000.0f;
inline constexpr float kWavePerspHackX = 0.19f; // 0x3e428f5c
inline constexpr float kWaveFliptriRadius = 0.5f;
inline constexpr float kWaveDegToRad = 0.017453292f;
inline constexpr std::uint32_t kWaveOffscreenSize = 64u;
inline constexpr float kWaveFlipTexSize = 8.0f;

// Standard 4x4 column-major matrix operations matching paf::math::matrix.
[[nodiscard]] WaveMat4 wave_mat4_identity() noexcept;
[[nodiscard]] WaveMat4 wave_mat4_mul(const WaveMat4& a, const WaveMat4& b) noexcept;
[[nodiscard]] WaveVec4 wave_mat4_mul_vec4(const WaveMat4& m, const WaveVec4& v) noexcept;
[[nodiscard]] WaveMat4 wave_mat4_inverse_rigid(const WaveMat4& m) noexcept;
[[nodiscard]] WaveMat4 wave_mat4_frustum(float left, float right, float bottom, float top,
    float near_z, float far_z) noexcept;
[[nodiscard]] WaveMat4 wave_mat4_ortho(float left, float right, float bottom, float top,
    float near_z, float far_z) noexcept;
[[nodiscard]] WaveMat4 wave_quat_to_matrix(const WaveVec4& q) noexcept;
[[nodiscard]] WaveMat4 wave_mat4_look_at(const WaveVec3& eye, const WaveVec3& target,
    const WaveVec3& up) noexcept;

// Maps delta pitch and clamped roll angles to camera look direction (FUN_81203668).
[[nodiscard]] WaveVec4 wave_pitch_roll_direction(float delta_pitch, float roll) noexcept;

struct WaveCameraMatrices {
    WaveMat4 base_world{};
    WaveMat4 world{};
    WaveMat4 view{};
    WaveMat4 projection{};
    WaveMat4 view_projection{};
    float pitch_up{};
    float pitch_down_cubed{};
};

// Reconstructs the Vita wave camera matrices (0x8109aa32 + 0x810a6bb0).
// Passing NaN for pitch_rad uses cam.pitch_rest with roll_rad=0 (resting pose).
[[nodiscard]] WaveCameraMatrices build_wave_camera(const WaveCameraPreset& cam,
    const WaveInstancePreset& wave, float aspect = 960.0f / 544.0f,
    float pitch_rad = std::numeric_limits<float>::quiet_NaN(), float roll_rad = 0.0f) noexcept;

// Assembles wave uniform blocks matching FUN_81203288, FUN_81202f04, FUN_810a1578,
// FUN_810a18fe, FUN_812035e4, and FUN_81203518.
[[nodiscard]] WaveMat4 wave_model_matrix(const WaveInstancePreset& wave) noexcept;
[[nodiscard]] WaveVec4 wave_cross_section_cubic(const WaveInstancePreset& wave) noexcept;
[[nodiscard]] WaveVec4 wave_decay_uniform(const WaveInstancePreset& wave,
    const WaveMat4& view_model) noexcept;
[[nodiscard]] WaveVec3 wave_spherical_position(float theta, float phi, float distance) noexcept;
[[nodiscard]] WaveFogPreset wave_blend_fog(const WaveFogPreset& fog0, const WaveFogPreset& fog1,
    float t) noexcept;
[[nodiscard]] std::array<float, 16> wave_pack_fog(const WaveFogPreset& fog) noexcept;
[[nodiscard]] std::array<float, 16> wave_pack_material(const WaveMaterialPreset& material) noexcept;
[[nodiscard]] std::array<float, 16> wave_pack_light2(const WaveLightPreset& light) noexcept;
[[nodiscard]] std::array<float, 16> wave_pack_light1(const WaveLightPreset& light) noexcept;

// Complete runtime parameters for all 5 passes of a single Vita Wave + SkyDome frame.
struct WaveSceneFrame {
    std::array<float, 4> clear_color{};
    WavePositionPassParameters position{};
    WaveGridNormalParameters grid_normal{};
    WaveLightingParameters lighting{};
    WaveFliptriParameters fliptri{};
    WaveSurfaceParameters surface{};
    WaveSurfaceRasterState surface_raster{};
    WaveSkydomeParameters skydome{};
};

[[nodiscard]] WaveSceneFrame build_wave_scene_frame(const WaveThemePreset& theme,
    std::uint32_t target_width = 960u, std::uint32_t target_height = 544u,
    float pitch_rad = std::numeric_limits<float>::quiet_NaN(), float roll_rad = 0.0f) noexcept;

} // namespace emucorev::paf
