#include <emucorev/paf/page_turn.h>
#include <algorithm>
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
}
