#include <emucorev/paf/animation_timer.h>

#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace emucorev::paf {
namespace {
constexpr std::uint32_t state_mask = 0xf000, repeat_mask = 0xf00, first_tick = 0x10000;
constexpr std::uint32_t curve_ids[] = {0,1,2,7,21};
void field(std::uint32_t &flags, std::uint32_t mask, std::uint32_t value) {
    flags = (flags & ~mask) | (value & mask);
}
std::int32_t convert_ticks(float value) {
    // Match VCVT.S32.F32 saturation/truncation without undefined C++ casts.
    if (std::isnan(value)) return 0;
    if (value >= 2147483648.0f) return std::numeric_limits<std::int32_t>::max();
    if (value <= -2147483648.0f) return std::numeric_limits<std::int32_t>::min();
    return static_cast<std::int32_t>(value);
}
}

TimerTimebase TimerTimebase::from_milliseconds(float milliseconds) {
    if (!std::isfinite(milliseconds) || milliseconds <= 0)
        throw std::runtime_error("Invalid timer tick unit");
    const float seconds = milliseconds * std::bit_cast<float>(0x3a83126fu);
    if (!std::isfinite(1.0f / seconds)) throw std::runtime_error("Timer tick unit is too small");
    return {seconds, 1.0f / seconds};
}

AnimationTimer::AnimationTimer(TimerTimebase timebase, float duration_milliseconds,
    AnimationCurve curve, TimerRepeat repeat, std::int8_t repeat_count) : timebase_(timebase), curve_(curve) {
    const auto index = static_cast<unsigned>(curve);
    if (index >= 5) throw std::runtime_error("Unreconstructed timer curve");
    if (repeat != TimerRepeat::none && repeat != TimerRepeat::loop
        && repeat != TimerRepeat::round && repeat != TimerRepeat::reverse)
        throw std::runtime_error("Unreconstructed timer repeat mode");
    if (!std::isfinite(timebase.unit_seconds) || timebase.unit_seconds <= 0
        || !std::isfinite(timebase.inverse_unit) || timebase.inverse_unit <= 0)
        throw std::runtime_error("Invalid timer timebase");
    const float seconds = duration_milliseconds * std::bit_cast<float>(0x3a83126fu);
    const auto ticks = convert_ticks(seconds * timebase.inverse_unit);
    const float quantized = static_cast<float>(ticks > 0 ? ticks : 1) * timebase.unit_seconds;
    state_.total = quantized * std::bit_cast<float>(0x3f7fffefu);
    state_.inverse_total = 1.0f / state_.total;
    state_.flags = std::uint32_t(std::bit_cast<std::uint8_t>(repeat_count))
        | (static_cast<std::uint32_t>(repeat) << 8) | (curve_ids[index] << 20);
    if (duration_milliseconds <= 0.0f) state_.flags |= 0x20000;
}

void AnimationTimer::restore(TimerSnapshot snapshot) {
    const auto repeat = (snapshot.flags >> 8) & 15;
    if (((snapshot.flags >> 20) & 255) != curve_ids[static_cast<unsigned>(curve_)]
        || (snapshot.flags & 0xf00c0000) || ((snapshot.flags >> 12) & 15) > 4
        || (repeat != 0 && repeat != 1 && repeat != 4 && repeat != 12))
        throw std::runtime_error("Unsupported scalar timer snapshot");
    state_ = snapshot;
}

void AnimationTimer::reset() {
    state_.mapped = 0.0f;
    state_.current = 0.0f;
    state_.flags &= ~(state_mask | first_tick);
}

float AnimationTimer::tick(float interval) {
    auto &s = state_;
    const auto initial_state = (s.flags & state_mask) >> 12;
    if (initial_state == 4) return 0.0f;
    if (initial_state == 3) return s.current * s.inverse_total;
    s.flags &= ~first_tick;
    auto repeat = (s.flags & repeat_mask) >> 8;
    auto remaining = std::bit_cast<std::int8_t>(std::uint8_t(s.flags));
    if (initial_state == 2 && repeat && remaining) {
        if (repeat == 1) reset();
        else if (repeat == 4 || repeat == 12) {
            field(s.flags, state_mask, 0x1000);
            s.current = repeat == 12 ? s.total : 0.0f;
        }
    }
    const float delta = timebase_.unit_seconds * interval;
    s.current = repeat == 12 ? s.current - delta : s.current + delta;
    const float normalized = s.current * s.inverse_total;
    auto current_state = (s.flags & state_mask) >> 12;
    if (current_state == 0) {
        s.flags |= first_tick;
        field(s.flags, state_mask, 0x1000);
        current_state = 1;
    }
    if (current_state == 1) {
        s.mapped = evaluate_curve(curve_, normalized);
        // Preserve the recovered comparison even for reverse mode. Do not
        // substitute conventional ping-pong behavior based on the enum name.
        if (s.current >= s.total) {
            field(s.flags, state_mask, 0x2000);
            s.current = repeat == 12 ? 0.0f : s.total;
            s.mapped = repeat == 12 ? 0.0f : 1.0f;
            if (remaining > 0) --remaining;
            field(s.flags, 255, std::bit_cast<std::uint8_t>(remaining));
            if (!remaining) field(s.flags, repeat_mask, 0);
            else if (repeat == 4) field(s.flags, repeat_mask, 0xc00);
            else if (repeat == 12) field(s.flags, repeat_mask, 0x400);
        }
    } else if (current_state == 2) {
        s.current = s.total;
        s.mapped = repeat == 12 ? 0.0f : 1.0f;
    }
    return normalized;
}
} // namespace emucorev::paf
