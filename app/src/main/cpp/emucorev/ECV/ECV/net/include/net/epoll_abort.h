// sceNetEpollAbort support: the abort state shared by an epoll object and the sliced wait loop.
// Header-only and free of any socket/guest type so that it can be unit tested on the host.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

namespace epoll_abort {

// SCE_NET_EPOLL_ABORT_FLAG_PRESERVATION: keep the abort for the next wait when nobody is waiting yet.
constexpr int FLAG_PRESERVATION = 0x1;
// An abort can only be noticed between two polls, so a wait is made of polls of at most this length.
constexpr int SLICE_MICROSECONDS = 20000;

enum class Outcome {
    Ready, // poll reported something (result in *poll_result, may be < 0 for a failure to forward)
    Timeout,
    Aborted
};

struct State {
    std::atomic<uint32_t> generation{ 0 };
    std::atomic<int> waiting{ 0 };
    std::atomic<bool> preserved{ false };

    // false: unknown flags (the caller answers EINVAL).
    bool abort(int flags) {
        if (flags & ~FLAG_PRESERVATION)
            return false;
        if (waiting.load() > 0)
            generation.fetch_add(1);
        else if (flags & FLAG_PRESERVATION)
            preserved.store(true);
        return true;
    }
};

// Runs poll(slice_microseconds) until it returns != 0, the timeout elapses (negative = never) or an abort
// arrives. poll() must block for about the slice (a select with that timeout, or a sleep).
template <class Poll>
Outcome wait(State &state, int timeout_microseconds, Poll &&poll, int *poll_result) {
    struct Waiter {
        std::atomic<int> &count;
        explicit Waiter(std::atomic<int> &c)
            : count(c) {
            ++count;
        }
        ~Waiter() { --count; }
    } waiter(state.waiting);

    if (state.preserved.exchange(false))
        return Outcome::Aborted;
    const uint32_t start_generation = state.generation.load();

    const bool infinite = timeout_microseconds < 0;
    int64_t remaining = infinite ? 0 : timeout_microseconds;
    for (;;) {
        if (state.generation.load() != start_generation)
            return Outcome::Aborted;

        const int slice = infinite ? SLICE_MICROSECONDS : static_cast<int>(std::min<int64_t>(remaining, SLICE_MICROSECONDS));
        const int result = poll(slice);
        if (state.generation.load() != start_generation)
            return Outcome::Aborted;
        if (result != 0) {
            if (poll_result)
                *poll_result = result;
            return Outcome::Ready;
        }
        if (!infinite) {
            remaining -= slice;
            if (remaining <= 0)
                return Outcome::Timeout;
        }
    }
}

} // namespace epoll_abort
