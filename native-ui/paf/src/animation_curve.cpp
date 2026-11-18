#include <emucorev/paf/animation_curve.h>

#include <cmath>
#include <stdexcept>

namespace emucorev::paf {
namespace {
float quadratic_in(float rate) {
    if (rate >= 1.0f) return 1.0f;
    if (rate <= 0.0f) return 0.0f;
    return rate * rate;
}
}

float evaluate_curve(AnimationCurve curve, float rate) {
    if (curve < AnimationCurve::linear || curve > AnimationCurve::quadratic_out)
        throw std::runtime_error("Unreconstructed animation curve");
    // The original uses explicit floating-point comparisons. In particular,
    // negative zero clamps to positive zero, and unordered (NaN) comparisons
    // continue into the function rather than being treated as <= zero.
    if (rate >= 1.0f) return 1.0f;
    if (rate <= 0.0f) return 0.0f;
    switch (curve) {
    case AnimationCurve::linear: return rate;
    case AnimationCurve::slower: return std::sqrt(rate);
    case AnimationCurve::faster:
    case AnimationCurve::quadratic_in: return rate * rate;
    case AnimationCurve::quadratic_out: return 1.0f - quadratic_in(1.0f - rate);
    }
    throw std::runtime_error("Unreconstructed animation curve");
}
} // namespace emucorev::paf
