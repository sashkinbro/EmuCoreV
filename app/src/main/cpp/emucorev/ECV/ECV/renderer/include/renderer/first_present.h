#pragma once

#include <cstdint>
#include <atomic>
#include <functional>
#include <mutex>
#include <utility>

namespace renderer {
class FirstPresentReadiness {
public:
    // The callback returns false when the frontend could not be reached (JNI not ready, method missing);
    // the notification is then retried on the next presented frame instead of being lost.
    using Callback = std::function<bool(uint64_t, bool)>;
    struct Ticket { uint64_t generation = 0, frame = 0; };
    uint64_t arm(Callback callback) {
        uint64_t generation;
        {
            const std::lock_guard lock(mutex_);
            generation = generation_ = ++next_generation_;
            callback_ = callback;
            pending_delivered_ = false;
        }
        const bool delivered = callback && callback(generation, false);
        if (delivered) {
            const std::lock_guard lock(mutex_);
            if (generation_ == generation) pending_delivered_ = true;
        }
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
        bool pending_delivered;
        {
            const std::lock_guard lock(mutex_);
            if (!success || !ticket.generation || ticket.generation != generation_ || !callback_)
                return;
            callback = callback_;
            pending_delivered = pending_delivered_;
        }
        // Keep the pending -> ready order even when the first pending call could not be delivered.
        if (!pending_delivered && !callback(ticket.generation, false))
            return;
        if (!callback(ticket.generation, true))
            return;
        const std::lock_guard lock(mutex_);
        if (generation_ == ticket.generation) callback_ = {};
    }
    void cancel() { const std::lock_guard lock(mutex_); callback_ = {}; }
private:
    std::mutex mutex_;
    inline static std::atomic<uint64_t> next_generation_{0};
    uint64_t generation_ = 0, frame_ = 0;
    bool pending_delivered_ = false;
    Callback callback_;
};
}
