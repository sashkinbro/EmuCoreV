#pragma once

#include <array>

namespace emucorev::paf {

using BubbleBasis = std::array<std::array<float, 4>, 3>;

// Reconstructed shell cone/wobble basis. Trigonometric samples are explicit so
// the motion geometry can be verified independently of a platform's libm.
BubbleBasis bubble_wobble_basis(float cosine, float sine, float strength);

// The original shell multiplies two framework delta values before applying
// the 0.5 angular rate. Paused UI must not advance this clock.
float advance_bubble_phase(float phase, float frame_delta, float delta_scale);

} // namespace emucorev::paf
