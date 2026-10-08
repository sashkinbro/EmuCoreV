#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>

namespace renderer {

// A worker acknowledges only at a safe boundary. Renderer batches and blocked
// remainders can remain queued; their typed command graph owns future work.
class SnapshotGate {
    std::mutex mutex;
    std::condition_variable condition;
    bool requested = false;
    bool parked = false;
    bool stopped = false;

public:
    bool acquire(std::chrono::milliseconds budget) {
        std::unique_lock lock(mutex);
        if (requested || stopped)
            return false;
        requested = true;
        if (!condition.wait_for(lock, budget, [&] { return parked || stopped; }) || stopped) {
            requested = false;
            condition.notify_all();
            return false;
        }
        return true;
    }

    void boundary(bool safe_boundary) {
        std::unique_lock lock(mutex);
        if (!requested || !safe_boundary || stopped)
            return;
        parked = true;
        condition.notify_all();
        condition.wait(lock, [&] { return !requested || stopped; });
        parked = false;
        condition.notify_all();
    }

    void release() {
        const std::lock_guard lock(mutex);
        requested = false;
        condition.notify_all();
    }

    void stop() {
        const std::lock_guard lock(mutex);
        stopped = true;
        requested = false;
        condition.notify_all();
    }

    // A restored worker can bootstrap behind a closed gate before any guest
    // callback or host submission is allowed to touch the reconstructed state.
    void reset(bool hold = false) {
        const std::lock_guard lock(mutex);
        stopped = false;
        requested = hold;
        parked = false;
    }
};
}
