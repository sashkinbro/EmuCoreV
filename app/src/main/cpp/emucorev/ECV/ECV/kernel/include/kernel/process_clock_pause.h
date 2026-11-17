#pragma once

#include <cstdint>
#include <mutex>
#include <optional>

// The host menu and save-state drain must not advance the guest process clock.
// Wall-clock RTC remains independent. Snapshots encode this as an adjusted start
// tick, so the existing process-clock archive format remains sufficient.
class ProcessClockPause {
    std::mutex mutex;
    std::optional<uint64_t> pause_start;

    uint64_t paused_duration(uint64_t now) const {
        return pause_start && now > *pause_start ? now - *pause_start : 0;
    }

public:
    void set_paused(bool paused, uint64_t now, uint64_t &start_tick) {
        const std::lock_guard lock(mutex);
        if (paused) {
            if (!pause_start) pause_start = now;
        } else if (pause_start) {
            start_tick += paused_duration(now);
            pause_start.reset();
        }
    }

    uint64_t effective_start(const uint64_t &start_tick, uint64_t now) {
        const std::lock_guard lock(mutex);
        return start_tick + paused_duration(now);
    }

    // A loaded clock has already been rebased to 'now'. Only future paused
    // time belongs to the new clock, even if the menu stayed open while loading.
    void rebase(uint64_t now, uint64_t value, uint64_t &start_tick) {
        const std::lock_guard lock(mutex);
        start_tick = value;
        if (pause_start) pause_start = now;
    }
};
