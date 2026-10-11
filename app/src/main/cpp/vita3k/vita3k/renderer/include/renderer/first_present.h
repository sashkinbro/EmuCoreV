#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

namespace renderer {
class FirstPresentReadiness {
public:
    using Callback = std::function<void(uint64_t, bool)>;
    struct Ticket { uint64_t generation = 0, frame = 0; };
    uint64_t arm(Callback callback) {
        uint64_t generation;
        {
            const std::lock_guard lock(mutex_);
            generation = generation_ = ++next_generation_;
            callback_ = callback;
        }
        if (callback) callback(generation, false);
        return generation;
    }
    void fresh_frame() { const std::lock_guard lock(mutex_); ++frame_; }
    Ticket begin_frame(bool guest_frame) {
        const std::lock_guard lock(mutex_);
        // A static restored framebuffer may be waiting for input. Its first
        // post-arm presentation is sufficient; requiring another flip would
        // make the input mask and game wait on each other.
        return guest_frame && callback_ ? Ticket{generation_, frame_} : Ticket{};
    }
    void presented(Ticket ticket, bool success) {
        Callback callback;
        {
            const std::lock_guard lock(mutex_);
            if (!success || !ticket.generation || ticket.generation != generation_)
                return;
            callback = std::move(callback_);
        }
        if (callback) callback(ticket.generation, true);
    }
    void cancel() { const std::lock_guard lock(mutex_); callback_ = {}; }
private:
    std::mutex mutex_;
    inline static std::atomic<uint64_t> next_generation_{0};
    uint64_t generation_ = 0, frame_ = 0;
    Callback callback_;
};
}
