// Durable display-queue producers. Prepared guest data and prediction survive
// capacity waits without repeating timestamp reservations after restoration.
#pragma once
#include <display/state.h>
#include <gxm/state.h>
#include <kernel/thread/thread_state.h>

struct PendingDisplaySubmission {
    DisplayCallback callback{};
    bool has_prediction = false;
    DisplayFrameInfo prediction{};
};

namespace gxm {
using DisplaySubmissionCommit = std::function<void(const PendingDisplaySubmission &)>;
// Exposed separately from the guest export so the real wait/resume boundary can
// also be exercised without starting a GPU worker or executing guest callbacks.
SceInt32 wait_display_queue(KernelState &, GxmState &, MemState &, ThreadState &,
    bool finish, const DisplaySubmissionCommit &);
void register_display_queue_wait_handlers(EmuEnvState &, DisplaySubmissionCommit commit = {});
// One indivisible host phase; the worker only acknowledges snapshots between
// these steps, including while a guest callback is blocked or UI-paused.
bool process_display_queue_step(EmuEnvState &);
}
