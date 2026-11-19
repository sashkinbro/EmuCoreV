#pragma once
#include <string_view>

namespace emucorev::paf {
// Original top-surface curl/lighting layout: 28 vertex vec4s at binding 0,
// viewport state at binding 2. Position attribute 0 uses height-normalized units.
std::string_view page_turn_top_vertex_shader() noexcept;
// Native normalized textures: no guest raw-format casts or resolution hacks.
// Bindings: material UBO 1 (four vec4s), draw-state UBO 3, corner/background 0/1.
std::string_view page_turn_empty_back_fragment_shader() noexcept;
}
