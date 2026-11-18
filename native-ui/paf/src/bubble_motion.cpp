#include <emucorev/paf/bubble_motion.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace emucorev::paf {
namespace {
using Vector = std::array<float, 4>;

#if defined(__aarch64__) || defined(__ARM_NEON)
float32x2_t root_step(float32x2_t a, float32x2_t b) {
    // AArch32 VRSQRTS rounds its multiply before subtraction. AArch64's
    // similarly named intrinsic is fused and is not a bit-equivalent port.
    const auto product = vmul_f32(a, b);
    return vmul_f32(vsub_f32(vdup_n_f32(3), product), vdup_n_f32(.5f));
}
float32x2_t reciprocal_step(float32x2_t a, float32x2_t b) {
    const auto product = vmul_f32(a, b);
    return vsub_f32(vdup_n_f32(2), product);
}
#endif

Vector normalize(Vector value) {
    const float length_squared = (value[0] * value[0] + value[1] * value[1]) + value[2] * value[2];
    float reciprocal_length;
#if defined(__aarch64__) || defined(__ARM_NEON)
    // Preserve the reviewed ARM sequence: estimate/refine sqrt, then
    // estimate/refine its reciprocal. Replacing it with 1/sqrt changes bits.
    const auto length = vdup_n_f32(length_squared);
    auto inverse_root = vrsqrte_f32(length);
    inverse_root = vmul_f32(inverse_root, root_step(vmul_f32(inverse_root, inverse_root), length));
    inverse_root = vmul_f32(inverse_root, root_step(vmul_f32(inverse_root, inverse_root), length));
    const auto root = vmul_f32(length, inverse_root);
    auto reciprocal = vrecpe_f32(root);
    reciprocal = vmul_f32(reciprocal, reciprocal_step(reciprocal, root));
    reciprocal = vmul_f32(reciprocal, reciprocal_step(reciprocal, root));
    reciprocal_length = vget_lane_f32(reciprocal, 0);
#else
    // Portable functional fallback; bit equivalence is checked on ARM targets.
    reciprocal_length = 1.f / std::sqrt(length_squared);
#endif
    for (unsigned i = 0; i < 3; ++i) value[i] *= reciprocal_length;
    value[3] = 0;
    return value;
}

Vector cross(const Vector& a, const Vector& b) {
    return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0], 0};
}
}

BubbleBasis bubble_wobble_basis(float cosine, float sine, float strength) {
    if (!std::isfinite(cosine) || !std::isfinite(sine) || !std::isfinite(strength)
        || std::abs(cosine) > 1 || std::abs(sine) > 1 || std::abs(strength) > 100)
        throw std::invalid_argument("Invalid bubble wobble input");
    const float amplitude = strength * 0.15000000596046448f;
    const Vector direction = normalize({amplitude * cosine, amplitude * sine, 1, 0});
    const Vector up = normalize(cross(direction, {1, 0, 0, 0}));
    const Vector right = normalize(cross(up, direction));
    return {right, up, direction};
}

float advance_bubble_phase(float phase, float frame_delta, float delta_scale) {
    const float step = frame_delta * delta_scale;
    if (!std::isfinite(phase) || !std::isfinite(step) || std::abs(step) > 1
        || std::abs(phase) > 6.2831854820251465f)
        throw std::invalid_argument("Invalid bubble motion clock input");
    phase = phase + step * 0.5f;
    while (phase >= 3.1415927410125732f) phase -= 6.2831854820251465f;
    return phase;
}
} // namespace emucorev::paf
