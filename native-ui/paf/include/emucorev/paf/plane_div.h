#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace emucorev::paf {

// Matches RCO Texture::div_mode values parsed in paf::Plugin (0x81057856) and
// indexed via DAT_8129aaac / DAT_8129ab1c in PlaneObj (0x81081ec6 / 0x81082882).
enum class PlaneDivMode : std::uint32_t {
    None = 0,
    Grid1x3 = 1,
    Grid3x1 = 2,
    Grid3x3 = 3,
    Grid3x5 = 4,
    Grid5x3 = 5,
    Grid3x3Border = 6,
    Grid3x3Opaque = 7,
    Grid1x3Opaque = 8,
    Grid3x1Opaque = 9,
};

// Matches Surface::div_ratio_* presets decoded by ScePafGraphics_AFAE7807 (0x8108d0d8).
enum class PlaneDivRatioPreset : std::uint8_t {
    Zero = 0,
    Third = 1,
    FortyNine = 2,
    Half = 3,
    Custom = 4,
};

// Resolves a Surface preset enum to exact IEEE-754 float constants used by paf.
std::array<float, 2> resolve_plane_div_ratio(
    PlaneDivRatioPreset preset,
    std::array<float, 2> custom = {0.f, 0.f}) noexcept;

// ARMv7 NEON vrecpe.f32 + 2x vrecps.f32 reciprocal estimate used in PlaneObj.
float arm_recip_estimate_2step(float x) noexcept;

struct PlaneDivSpec {
    // Base widget center and size in Vita PAF coordinates (+X right, +Y up).
    float pos_x = 0.f;
    float pos_y = 0.f;
    float pos_z = 0.f;
    float size_x = 0.f;
    float size_y = 0.f;

    // Optional Y rotation in radians (matches PlaneObj +0x28 != nullptr check).
    bool has_rot = false;
    float rot_y = 0.f;

    // PlaneObj align modes (+0x54, +0x58): 0 = center, 1 = min, 2 = max.
    std::int32_t align_x = 0;
    std::int32_t align_y = 0;

    // Texture dimensions (Surface +0x1c, +0x1e) and division parameters.
    std::int16_t tex_width = 0;
    std::int16_t tex_height = 0;
    PlaneDivMode div_mode = PlaneDivMode::None;
    std::array<float, 2> div_ratio_h = {0.f, 0.f};
    std::array<float, 2> div_ratio_v = {0.f, 0.f};
    std::array<float, 2> div_ratio_stretch = {0.f, 0.f};

    // Outward border margins in pixels (RCO margin_h=[left,right], margin_v=[top,bottom]).
    std::uint16_t margin_left = 0;
    std::uint16_t margin_right = 0;
    std::uint16_t margin_top = 0;
    std::uint16_t margin_bottom = 0;

    // PlaneObj +0xbc (default 1.0f) and +0xc0 (center motif offset for 3x5/5x3).
    float div_border_scale = 1.f;
    float div_stretch_offset = 0.f;
};

struct PlaneDivLayout {
    float center_x = 0.f;
    float center_y = 0.f;
    float center_z = 0.f;
    float width = 0.f;
    float height = 0.f;
};

struct PlaneDivVertex {
    float x = 0.f;
    float y = 0.f;
    float u = 0.f;
    float v = 0.f;
};

struct PlaneDivMesh {
    PlaneDivLayout layout{};
    std::uint32_t cols = 0;
    std::uint32_t rows = 0;
    std::vector<PlaneDivVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::vector<std::uint16_t> opaque_indices;
};

struct PlaneDivScreenQuad {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float u = 0.f;
    float v = 0.f;
    float uv_width = 0.f;
    float uv_height = 0.f;
};

// Reconstructs PlaneObj::UpdateLayout (0x810848e8) with scale_mode = 0, scale_value = 1.
PlaneDivLayout compute_plane_div_layout(const PlaneDivSpec& spec) noexcept;

// Reconstructs PlaneObj::UpdateLayout + UpdateDivVerticesPosition (0x81084b1a) +
// UpdateDivVerticesTexcoord (0x810852c4) plus firmware index tables.
PlaneDivMesh build_plane_div_mesh(const PlaneDivSpec& spec);

// Converts a Y-down screen-space center & unexpanded box into Y-down textured patches
// covering primary triangles (and optional opaque center pass when include_opaque=true).
std::vector<PlaneDivScreenQuad> build_plane_div_screen_quads(
    PlaneDivSpec spec,
    float screen_center_x,
    float screen_center_y,
    bool include_opaque = false);

} // namespace emucorev::paf
