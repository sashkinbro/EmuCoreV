#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace renderer {

// The Android HUD reads on a different thread. Publish a coherent pair rather than
// reading the emulation-thread counters directly.
struct PerformanceStatsTelemetry {
    std::atomic<uint64_t> frame_metrics{ 0 };
    std::atomic<int64_t> updated_ms{ 0 };

    static int64_t now_ms() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void publish(uint32_t fps, uint32_t frame_ms) {
        frame_metrics.store(uint64_t(fps) | (uint64_t(frame_ms) << 32), std::memory_order_release);
        updated_ms.store(now_ms(), std::memory_order_release);
    }

    void reset() {
        updated_ms.store(0, std::memory_order_release);
        frame_metrics.store(0, std::memory_order_release);
    }
};

} // namespace renderer
