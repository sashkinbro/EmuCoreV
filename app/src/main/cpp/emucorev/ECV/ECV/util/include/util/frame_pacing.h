// Frame pacing math shared by the Vulkan present path and the guest vblank thread.
// Header-only and free of emulator dependencies so it can be unit tested on the host.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace frame_pacing {

// The guest (PS Vita) display refreshes at 60 Hz; games count vblanks, so the long-run vblank
// rate must stay exactly this no matter what the host panel does.
constexpr int64_t guest_vblank_milli_hz = 60000;

// ---------------------------------------------------------------------------------------------
// Present mode policy
// ---------------------------------------------------------------------------------------------

enum class PresentMode {
    Fifo,
    FifoRelaxed,
    Mailbox,
    Immediate,
};

struct PresentModeSupport {
    bool fifo_relaxed = false;
    bool mailbox = false;
    bool immediate = false;
};

// v-sync on: FIFO (always available, one image per refresh, never tears, no frame is dropped).
// v-sync off: the best mode that does not tear first (mailbox), then the ones that may.
// Immediate is skipped where it is known to exhaust the window system's buffer queue (Android: the
// compositor holds on to the buffers and the driver stalls or drops frames), so the caller passes
// allow_immediate = false there and falls through to FIFO_RELAXED / FIFO.
constexpr PresentMode choose_present_mode(bool vsync, const PresentModeSupport &support, bool allow_immediate = true) {
    if (vsync)
        return PresentMode::Fifo;
    if (support.mailbox)
        return PresentMode::Mailbox;
    if (support.immediate && allow_immediate)
        return PresentMode::Immediate;
    if (support.fifo_relaxed)
        return PresentMode::FifoRelaxed;
    return PresentMode::Fifo;
}

// ---------------------------------------------------------------------------------------------
// Panel refresh measurement
// ---------------------------------------------------------------------------------------------

// Refresh rates are measured from vsync timestamps, so a rate that is really 120.000 Hz shows up as
// 119.97. Snap those to the nominal rate (0.3 %); genuinely different rates (e.g. 100 Hz) are kept.
constexpr int64_t snap_milli_hz(int64_t measured_milli_hz) {
    constexpr std::array<int64_t, 18> nominal = {
        30000, 40000, 45000, 48000, 50000, 60000, 72000, 75000, 80000,
        90000, 100000, 110000, 120000, 125000, 144000, 165000, 180000, 240000
    };
    for (const int64_t rate : nominal) {
        const int64_t diff = measured_milli_hz > rate ? measured_milli_hz - rate : rate - measured_milli_hz;
        if (diff * 333 <= rate) // 0.3 %
            return rate;
    }
    return measured_milli_hz;
}

// Vsync period from the last vsync timestamps. The mean over the whole window (newest - oldest)
// cancels the timestamp jitter of single vsyncs; if the window contains a missed vsync (an interval
// far from the median) the median interval is used instead.
class PeriodEstimator {
public:
    static constexpr int window = 33; // timestamps -> 32 intervals
    static constexpr int min_intervals = 8;

    void push(int64_t timestamp_ns) {
        stamps[next] = timestamp_ns;
        next = (next + 1) % window;
        count = std::min(count + 1, window);
    }

    bool valid() const { return count > min_intervals; }

    int64_t period_ns() const {
        if (!valid())
            return 0;
        std::array<int64_t, window> intervals{};
        const int n = count - 1;
        for (int i = 0; i < n; ++i)
            intervals[i] = at(i + 1) - at(i);
        std::array<int64_t, window> sorted = intervals;
        std::sort(sorted.begin(), sorted.begin() + n);
        const int64_t median = sorted[n / 2];
        for (int i = 0; i < n; ++i) {
            if (intervals[i] * 2 < median || intervals[i] * 2 > median * 3)
                return median;
        }
        return (at(n) - at(0)) / n;
    }

    void reset() {
        count = 0;
        next = 0;
    }

private:
    // i-th oldest stored timestamp
    int64_t at(int i) const { return stamps[(next - count + i + 2 * window) % window]; }

    std::array<int64_t, window> stamps{};
    int count = 0;
    int next = 0;
};

// ---------------------------------------------------------------------------------------------
// Guest vblank locked to the panel
// ---------------------------------------------------------------------------------------------

// Turns panel vsync timestamps into guest vblank ticks. Every tick is emitted on a panel vsync
// (so presents land on a fixed refresh cadence instead of beating against a free-running timer)
// and the long-run tick rate is exactly 60 Hz of panel time: an integer accumulator hands out
// 60000/panel_milli_hz ticks per refresh, e.g. 120 Hz -> every 2nd refresh, 90 Hz -> 2 of 3,
// 144 Hz -> 5 of 12, 60 Hz -> every refresh.
class VblankFollower {
public:
    // Returns how many guest vblanks are due at this panel vsync; 0 while the refresh rate is not
    // known yet. Callers that missed vsyncs (return > 1) should not burst: the old timer never did.
    uint32_t on_vsync(int64_t timestamp_ns) {
        if (last_timestamp_ns == 0 || timestamp_ns <= last_timestamp_ns) {
            last_timestamp_ns = timestamp_ns;
            estimator.push(timestamp_ns);
            return 0;
        }
        const int64_t interval = timestamp_ns - last_timestamp_ns;
        last_timestamp_ns = timestamp_ns;
        estimator.push(timestamp_ns);
        if (!estimator.valid())
            return 0;

        const int64_t period = estimator.period_ns();
        const int64_t candidate = snap_milli_hz(1'000'000'000'000LL / period);
        // 0.5 % hysteresis: a changed estimate must not reset the phase of a locked cadence.
        if (milli_hz_value == 0 || std::abs(candidate - milli_hz_value) * 200 > milli_hz_value) {
            milli_hz_value = candidate;
            accumulator = 0;
        }

        // Refreshes since the previous vsync; more than one means this thread or the compositor missed some.
        const int64_t refreshes = std::clamp<int64_t>((interval * milli_hz_value + 500'000'000'000LL) / 1'000'000'000'000LL, 1, 8);
        accumulator += refreshes * guest_vblank_milli_hz;
        const int64_t ticks = accumulator / milli_hz_value;
        accumulator -= ticks * milli_hz_value;
        return static_cast<uint32_t>(ticks);
    }

    // Forget the previous vsync (source stalled or restarted); the rate estimate is kept and a stale
    // window is harmless: the estimator falls back to the median interval.
    void resync() { last_timestamp_ns = 0; }

    // The refresh rate has been measured; ticks are produced from now on.
    bool locked() const { return milli_hz_value != 0; }

    int64_t milli_hz() const { return milli_hz_value; }

private:
    PeriodEstimator estimator;
    int64_t last_timestamp_ns = 0;
    int64_t milli_hz_value = 0;
    int64_t accumulator = 0;
};

// ---------------------------------------------------------------------------------------------
// Cadence analysis (tests and tooling)
// ---------------------------------------------------------------------------------------------

struct Cadence {
    uint32_t min_refreshes = 0; // shortest time one guest frame stays on screen, in panel refreshes
    uint32_t max_refreshes = 0;
    double mean_refreshes = 0;

    // Every guest frame is shown for the same number of refreshes: no judder.
    bool even() const { return min_refreshes == max_refreshes; }
};

// How long each guest frame stays on screen when the guest flips once every `vblanks_per_frame`
// vblanks (1 = 60 fps, 2 = 30 fps) on a panel running at `panel_milli_hz`.
inline Cadence simulate_cadence(int64_t panel_milli_hz, uint32_t vblanks_per_frame, uint32_t refreshes = 7200) {
    Cadence result;
    int64_t accumulator = 0;
    uint32_t ticks_in_frame = 0;
    uint32_t last_flip_refresh = 0;
    uint64_t total = 0;
    uint32_t flips = 0;
    for (uint32_t refresh = 1; refresh <= refreshes; ++refresh) {
        accumulator += guest_vblank_milli_hz;
        const int64_t ticks = accumulator / panel_milli_hz;
        accumulator -= ticks * panel_milli_hz;
        for (int64_t t = 0; t < ticks; ++t) {
            if (++ticks_in_frame < vblanks_per_frame)
                continue;
            ticks_in_frame = 0;
            if (last_flip_refresh != 0) {
                const uint32_t shown = refresh - last_flip_refresh;
                result.min_refreshes = flips == 0 ? shown : std::min(result.min_refreshes, shown);
                result.max_refreshes = std::max(result.max_refreshes, shown);
                total += shown;
                ++flips;
            }
            last_flip_refresh = refresh;
        }
    }
    result.mean_refreshes = flips ? static_cast<double>(total) / flips : 0.0;
    return result;
}

// ---------------------------------------------------------------------------------------------
// Interval histogram (debug-only lab output)
// ---------------------------------------------------------------------------------------------

// Counts frame-to-frame intervals in 2 ms bins (the last bin is "40 ms and more").
class IntervalHistogram {
public:
    static constexpr int bins = 21;

    void add(double interval_ms) {
        ++counts[std::clamp(static_cast<int>(interval_ms / 2.0), 0, bins - 1)];
        ++total;
        sum_ms += interval_ms;
    }

    uint32_t count() const { return total; }
    double mean_ms() const { return total ? sum_ms / total : 0.0; }
    uint32_t bin(int index) const { return counts[index]; }
    void clear() { *this = IntervalHistogram{}; }

    // "n=300 mean=16.67ms bins(2ms)=0,0,0,0,0,0,0,0,297,3,..." for the lab log
    std::string format() const {
        char head[64];
        std::snprintf(head, sizeof(head), "n=%u mean=%.2fms bins(2ms)=", total, mean_ms());
        std::string out = head;
        for (int i = 0; i < bins; ++i) {
            if (i)
                out += ',';
            out += std::to_string(counts[i]);
        }
        return out;
    }

private:
    std::array<uint32_t, bins> counts{};
    uint32_t total = 0;
    double sum_ms = 0;
};

} // namespace frame_pacing
