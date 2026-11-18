#pragma once

namespace emucorev::paf {

// Reconstructed functions, not a complete Timer implementation. Ordering is
// local to this API and must not be used as original PAF timer function IDs.
enum class AnimationCurve { linear, slower, faster, quadratic_in, quadratic_out };

float evaluate_curve(AnimationCurve curve, float rate);

} // namespace emucorev::paf
