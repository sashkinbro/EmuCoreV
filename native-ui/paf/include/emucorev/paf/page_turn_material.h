#pragma once
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
// Top surface (flat and rounded) vertex material, 28 vec4s. Returns an empty
// range for a uniform the pass does not declare.
PageTurnUniformRange page_turn_top_uniform(PageTurnUniform uniform) noexcept;
// Bottom surface (flat and rounded) vertex material, 12 vec4s.
PageTurnUniformRange page_turn_bottom_uniform(PageTurnUniform uniform) noexcept;
// Empty folded-back vertex material, 9 vec4s.
PageTurnUniformRange page_turn_empty_back_uniform(PageTurnUniform uniform) noexcept;
}
