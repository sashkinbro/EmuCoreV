#pragma once
#include <string_view>

namespace emucorev::paf {
// Original top-surface curl/lighting layout: 28 vertex vec4s at binding 0,
// viewport state at binding 2. Position attribute 0 uses height-normalized units.
std::string_view page_turn_top_vertex_shader() noexcept;
// Top material: original nine-vec4 fragment layout at binding 1; page unit 0.
std::string_view page_turn_top_fragment_shader() noexcept;
// Outer shadow: original two-vec4 layout. p[0].xy contains four packed half
// border values; p[0].z contains the packed half level. p[1] is float RGBA.
std::string_view page_turn_shadow_fragment_shader() noexcept;
// Rectangle passes: five vertex vec4s; attribute 0 contains position.xy, UV.zw.
std::string_view page_turn_quad_vertex_shader() noexcept;
// Bottom surface: 12 vertex vec4s, vertex lookup texture on unit 4. The fragment
// material modulates RGB with the computed shadow and preserves image alpha.
std::string_view page_turn_bottom_vertex_shader() noexcept;
std::string_view page_turn_bottom_fragment_shader() noexcept;
// Native normalized textures: no guest raw-format casts or resolution hacks.
// Bindings: material UBO 1 (four vec4s), draw-state UBO 3, corner/background 0/1.
std::string_view page_turn_empty_back_fragment_shader() noexcept;
}
