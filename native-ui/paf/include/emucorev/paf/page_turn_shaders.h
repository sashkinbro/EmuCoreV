#pragma once
#include <string_view>

namespace emucorev::paf {
// Native normalized textures: no guest raw-format casts or resolution hacks.
// Bindings: material UBO 1 (four vec4s), draw-state UBO 3, corner/background 0/1.
std::string_view page_turn_empty_back_fragment_shader() noexcept;
}
