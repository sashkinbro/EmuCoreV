#pragma once

#include <emucorev/paf/animation_curve.h>
#include <cstdint>

namespace emucorev::paf {

struct TimerTimebase {
    float unit_seconds, inverse_unit;
    static TimerTimebase from_milliseconds(float milliseconds);
};

struct TimerSnapshot {
    float total, inverse_total, current;
    // Retain logical PAF flags for exact reference comparison, including the
    // signed repeat byte and first-tick bit. There are no guest pointers.
    std::uint32_t flags;
    float mapped;
};

enum class TimerRepeat : std::uint32_t { none = 0, loop = 1, round = 4, reverse = 12 };

// Reconstructed scalar Timer operations for the five recovered curves. The
// owner advances it explicitly; there are no threads, callbacks or clocks.
// Widget/Animator scheduling and 2D/3D/custom curves remain separate work.
class AnimationTimer {
public:
    AnimationTimer(TimerTimebase timebase, float duration_milliseconds,
        AnimationCurve curve, TimerRepeat repeat, std::int8_t repeat_count);
    // Return original unclamped normalized time. The interpolated animation
    // value is snapshot().mapped; these can differ at completion/overshoot.
    float tick(float interval = 1.0f);
    void reset();
    TimerSnapshot snapshot() const { return state_; }
    void restore(TimerSnapshot snapshot);

private:
    TimerTimebase timebase_;
    TimerSnapshot state_{};
    AnimationCurve curve_;
};

} // namespace emucorev::paf
