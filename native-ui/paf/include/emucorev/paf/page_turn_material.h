#pragma once
#include <emucorev/paf/page_turn.h>
#include <array>
#include <cstdint>

namespace emucorev::paf {
// Recovered PageTurn uniform semantics and their offsets inside each pass's
// material block. Offsets are float indices in the original GXP uniform layout
// (verified by the tracer): the top surface block is 112 floats, the bottom
// surface block 48 floats and the empty-back block 36 floats.
enum class PageTurnUniform : std::uint32_t {
    mvp, model_matrix, material_pack, uv_transform, squeeze_z, deepest, grep,
    normal_scale_z, params, fold_plane, aspect, point_light_pack2,
    point_light_pack1, eye, shadow, exp_minus_range,
};
struct PageTurnUniformRange { std::uint32_t offset; std::uint32_t count; };
struct PageTurnMaterialBlock {
    std::array<float, 112> values{};
    std::uint32_t size = 0; // Counted prefix in floats.
};
// Constructor defaults recovered from PageTurnRenderer. Fresnel control and
// coefficient are adapted by the fold-shape multiplier before packing.
struct PageTurnMaterialStyle {
    std::array<float, 4> color{1.f, 1.f, 1.f, 1.f};
    float diffuse_coefficient = std::bit_cast<float>(0x3f649247u);
    float specular_coefficient = std::bit_cast<float>(0x3f8879eeu);
    float specular_power = std::bit_cast<float>(0x4185f15eu);
    float fresnel_front = std::bit_cast<float>(0x3da491b0u);
    float fresnel_control = std::bit_cast<float>(0x3f4d30adu);
    float fresnel_coefficient = std::bit_cast<float>(0x3e9349e0u);
    float refraction_coefficient = std::bit_cast<float>(0x404321c9u);
    float refraction_amount = std::bit_cast<float>(0x3ec49214u);
    float second_reflection = std::bit_cast<float>(0x3dea454eu);
    float back_side_opacity = std::bit_cast<float>(0x3da491b0u);
};
// Default shadow and empty-back colors registered by PageTurnRenderer.
struct PageTurnEffectStyle {
    float shadow = std::bit_cast<float>(0x437a0000u);
    float shadow_level = std::bit_cast<float>(0x3e9b6da8u);
    float empty_shadow = std::bit_cast<float>(0x4181b13bu);
    float empty_shadow_level = std::bit_cast<float>(0x3ef6db51u);
    float empty_corner_shadow = std::bit_cast<float>(0x3e9a6c1au);
    std::array<float, 3> empty_color{1.f, 1.f, 1.f};
    std::array<float, 3> empty_shadow_color{
        std::bit_cast<float>(0x3ec00000u), std::bit_cast<float>(0x3ecdb6c3u),
        std::bit_cast<float>(0x3ed6db51u)};
};
// Original top-surface MaterialPack layout (four vec4s), with the two
// Fresnel terms multiplied by the shape's recovered progress adaptation.
std::array<float, 16> page_turn_material_pack(float fresnel_multiplier,
    const PageTurnMaterialStyle& style = {});
// Top surface (flat and rounded) vertex material, 28 vec4s. Returns an empty
// range for a uniform the pass does not declare.
PageTurnUniformRange page_turn_top_uniform(PageTurnUniform uniform) noexcept;
// Bottom surface (flat and rounded) vertex material, 12 vec4s.
PageTurnUniformRange page_turn_bottom_uniform(PageTurnUniform uniform) noexcept;
// Empty folded-back vertex material, 9 vec4s.
PageTurnUniformRange page_turn_empty_back_uniform(PageTurnUniform uniform) noexcept;
// Identity camera/projection used by the controlled reconstruction fixture.
inline constexpr PageTurnMatrix page_turn_identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
// Assembled top surface (flat/rounded) vertex block, 28 vec4s. Recovered fields
// are MVP, ModelMatrix, MaterialPack, shape and both point-light packs.
// UVTransform, SqueezeZ and Eye remain zero until their assembly is recovered.
PageTurnMaterialBlock page_turn_top_vertex_material(PagePoint size, PagePoint point,
    const PageTurnShapeStyle& style = {}, const PageTurnPose& pose = {},
    const PageTurnMatrix& projection = page_turn_identity,
    const PageTurnMatrix& view = page_turn_identity,
    const PageTurnPointLight& light = {},
    const PageTurnMaterialStyle& material_style = {});

// Assembled bottom surface (flat/rounded) vertex block, 12 vec4s. Recovered
// fields: MVP and the shape pack; Shadow/UVTransform/SqueezeZ/ExpMinusRange
// stay zero until their assembly is reconstructed.
PageTurnMaterialBlock page_turn_bottom_vertex_material(PagePoint size, PagePoint point,
    const PageTurnShapeStyle& style = {}, const PageTurnPose& pose = {},
    const PageTurnMatrix& projection = page_turn_identity,
    const PageTurnMatrix& view = page_turn_identity);
// Assembled empty folded-back vertex block, 9 vec4s. Recovered fields: MVP,
// FoldPlane and Aspect; Shadow/UVTransform/SqueezeZ stay zero for now.
PageTurnMaterialBlock page_turn_empty_back_vertex_material(PagePoint size, PagePoint point,
    const PageTurnShapeStyle& style = {}, const PageTurnPose& pose = {},
    const PageTurnMatrix& projection = page_turn_identity,
    const PageTurnMatrix& view = page_turn_identity);

// Ordered surface draws for one PageTurn frame. The flat (unfolded) state
// submits the top and bottom surfaces; a folded page also submits the empty
// folded-back surface. Roles follow the reviewed draw protocol; the exact
// order of the two folded-edge surfaces is not yet established.
enum class PageTurnPassRole : std::uint32_t { top, bottom, empty_back };
struct PageTurnPass { PageTurnPassRole role; PageTurnMaterialBlock material; };
// Returns 2 (flat) or 3 (folded) passes; zero when the caller capacity is too
// small. Material blocks carry only the currently recovered uniforms.
std::uint32_t page_turn_draw_passes(PagePoint size, PagePoint point, bool folded,
    const PageTurnShapeStyle& style, PageTurnPass* output, std::uint32_t capacity);
}
