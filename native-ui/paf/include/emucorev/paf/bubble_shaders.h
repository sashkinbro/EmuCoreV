#pragma once

#include <string_view>

namespace emucorev::paf {

// Reconstructed opaque-bubble material for normalized RGBA images at 1x.
// Retains the reviewed half-precision rounding boundaries of the original
// material, without an instruction interpreter or translated register arrays.
std::string_view opaque_bubble_fragment_shader() noexcept;
std::string_view opaque_bubble_vertex_shader() noexcept;

} // namespace emucorev::paf
