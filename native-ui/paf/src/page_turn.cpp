#include <emucorev/paf/page_turn.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace emucorev::paf {
namespace {
void check_size(PagePoint size) {
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0)
        throw std::invalid_argument("Invalid page-turn size");
}
PagePoint inverse_size(PagePoint size) {
    check_size(size);
    // Original vector reciprocal estimate with two AArch32, non-fused steps.
    size.x = std::clamp(size.x, 1.f, 3.4028234663852886e18f);
    size.y = std::clamp(size.y, 1.f, 3.4028234663852886e18f);
#if defined(__aarch64__) || defined(__ARM_NEON)
    const float values[]{size.x, size.y};
    const auto value = vld1_f32(values);
    auto inverse = vrecpe_f32(value);
    inverse = vmul_f32(inverse, vsub_f32(vdup_n_f32(2), vmul_f32(inverse, value)));
    inverse = vmul_f32(inverse, vsub_f32(vdup_n_f32(2), vmul_f32(inverse, value)));
    return {vget_lane_f32(inverse, 0), vget_lane_f32(inverse, 1)};
#else
    return {1.f / size.x, 1.f / size.y};
#endif
}
float reciprocal(float value) {
#if defined(__aarch64__) || defined(__ARM_NEON)
    const auto source = vdup_n_f32(value);
    auto result = vrecpe_f32(source);
    result = vmul_f32(result, vsub_f32(vdup_n_f32(2), vmul_f32(result, source)));
    result = vmul_f32(result, vsub_f32(vdup_n_f32(2), vmul_f32(result, source)));
    return vget_lane_f32(result, 0);
#else
    return 1.f / value;
#endif
}
float root(float value) {
#if defined(__aarch64__) || defined(__ARM_NEON)
    const auto source = vdup_n_f32(value);
    auto inverse = vrsqrte_f32(source);
    for (unsigned i = 0; i < 2; ++i) {
        const auto square = vmul_f32(inverse, inverse);
        const auto step = vmul_f32(vsub_f32(vdup_n_f32(3), vmul_f32(square, source)), vdup_n_f32(.5f));
        inverse = vmul_f32(inverse, step);
    }
    return vget_lane_f32(vmul_f32(source, inverse), 0);
#else
    return std::sqrt(value);
#endif
}
PagePoint add(PagePoint a, PagePoint b) { return {a.x + b.x, a.y + b.y}; }
PagePoint subtract(PagePoint a, PagePoint b) { return {a.x - b.x, a.y - b.y}; }
PagePoint multiply(PagePoint a, float b) { return {a.x * b, a.y * b}; }
float dot(PagePoint a, PagePoint b) { return a.x * b.x + a.y * b.y; }
PageFoldProjection project(PagePoint center, PagePoint normal, PagePoint point) {
    const PagePoint tangent{-normal.y, normal.x};
    const auto delta = subtract(point, center);
    auto along = multiply(tangent, dot(tangent, delta));
    along = multiply(along, reciprocal(dot(tangent, tangent)));
    const auto projected = add(center, along);
    const float distance = dot(normal, delta);
    return {projected, distance < 0 ? add(projected, subtract(projected, point)) : point, distance};
}
bool clip(PagePoint& start, PagePoint& end, PagePoint origin, PagePoint normal) {
    const float a = dot(normal, subtract(start, origin));
    const float b = dot(normal, subtract(end, origin));
    if (a >= 0 && b >= 0) return false;
    if (a < 0 && b < 0) return true;
    const auto projected = subtract(start, multiply(normal, a));
    const float numerator = dot(normal, subtract(projected, start));
    const float ratio = numerator * reciprocal(b - a);
    const auto intersection = add(start, multiply(subtract(end, start), ratio));
    if (a < 0) start = intersection;
    else end = intersection;
    return false;
}
}
PageTurnLayout page_turn_layout(PagePoint size, float fold_fraction, float corner_aspect) {
    check_size(size);
    if (!std::isfinite(fold_fraction) || !std::isfinite(corner_aspect))
        throw std::invalid_argument("Invalid page-turn fold settings");
    const float fold = size.y * fold_fraction;
    return {{size.x * .5f - fold * corner_aspect, size.y * .5f - fold},
        {fold + 50.f, fold + 50.f}, fold * .5f - 10.f};
}
bool page_turn_hit_test(PagePoint size, const PageTurnLayout& layout, PagePoint point) {
    const auto inverse = inverse_size(size);
    const float dx = point.x - size.x * .5f, dy = point.y - size.y * .5f;
    if (dx + dy > -layout.corner_exclusion) return false;
    const float x = (point.x + size.x * .5f) * inverse.x;
    const float y = (point.y + size.y * .5f) * inverse.y;
    return x <= 1 && x >= 1.f - layout.hit_size.x * inverse.x
        && y <= 1 && y >= 1.f - layout.hit_size.y * inverse.y;
}
PagePoint page_turn_drag_target(PagePoint pointer, PagePoint initial, PagePoint offset) {
    return {(pointer.x + offset.x) + (pointer.x - initial.x) * .7f,
        (pointer.y + offset.y) + (pointer.y - initial.y) * .7f};
}
PageTurnDrawControl page_turn_draw_control(PagePoint size, PagePoint position,
    PagePoint offset, std::array<float, 3> opacity) {
    const auto inverse = inverse_size(size);
    const float alpha = (opacity[0] * opacity[1]) * opacity[2];
    return {{(position.x + size.x * .5f) * inverse.x,
             (position.y + size.y * .5f) * inverse.y},
        {offset.x * inverse.x, offset.y * inverse.y},
        alpha < .999f ? 0x00ffffffu : 0xffffffffu};
}
PageTurnRelease page_turn_release(PagePoint displacement, float threshold, bool allow_outward, bool gamepad) {
    if (allow_outward && displacement.x + displacement.y > (gamepad ? 80.f : 50.f))
        return PageTurnRelease::outward;
    const float squared = displacement.x * displacement.x + displacement.y * displacement.y;
    return squared > threshold * threshold ? PageTurnRelease::beyond_threshold : PageTurnRelease::within_threshold;
}
PageFoldShape page_turn_fold_shape(PagePoint deepest, PagePoint aspect) {
    check_size(aspect);
    if (!std::isfinite(deepest.x) || !std::isfinite(deepest.y))
        throw std::invalid_argument("Invalid page-turn fold point");
    constexpr float upper = std::bit_cast<float>(0x3f7fff58u);
    constexpr float lower = std::bit_cast<float>(0x3fffffacu);
    const PagePoint bounded{std::max(-aspect.x * lower, std::min(aspect.x * upper, deepest.x)),
        std::max(-aspect.y * lower, std::min(aspect.y * upper, deepest.y))};
    const auto delta = subtract(bounded, aspect);
    const float squared = dot(delta, delta);
    if (!std::isfinite(squared) || squared < 1.1754943508222875e-38f)
        throw std::invalid_argument("Degenerate page-turn fold extent");
    PageFoldShape result{};
    result.center = multiply(add(bounded, aspect), .5f);
    result.normal = multiply(delta, reciprocal(root(squared)));
    result.corners = {{{0, 0}, {aspect.x, 0}, aspect, {0, aspect.y}}};
    for (unsigned i = 0; i < 4; ++i)
        result.projected[i] = project(result.center, result.normal, result.corners[i]);
    result.far_corner = aspect;
    result.maximum = result.minimum = result.projected[0];
    for (const auto& corner : result.projected) {
        if (corner.distance < 0 && corner.distance < result.minimum.distance) result.minimum = corner;
        if (corner.distance > 0 && corner.distance > result.maximum.distance) result.maximum = corner;
    }
    const PagePoint tangent{-result.normal.y, result.normal.x};
    result.line_start = add(result.center, multiply(tangent, 1000));
    result.line_end = subtract(result.center, multiply(tangent, 1000));
    // Apply all four half-planes even after a rejection, as the original does.
    result.outside = clip(result.line_start, result.line_end, {0, 0}, {1, 0});
    result.outside = clip(result.line_start, result.line_end, {0, 0}, {0, 1}) || result.outside;
    result.outside = clip(result.line_start, result.line_end, aspect, {-1, -0.f}) || result.outside;
    result.outside = clip(result.line_start, result.line_end, aspect, {-0.f, -1}) || result.outside;
    result.aspect = aspect;
    return result;
}
}
