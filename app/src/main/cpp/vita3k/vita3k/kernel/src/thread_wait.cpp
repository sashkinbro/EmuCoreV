// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cpu/functions.h>
#include <kernel/state.h>
#include <util/log.h>
#include <kernel/thread/thread_state.h>

#include <cassert>
#include <utility>

// Set in the wait type of a wait that runs callbacks.
constexpr SceUInt32 WAITTYPE_CB_BIT = 0x80000000U;

bool ThreadState::wait_for_guest_resume(std::unique_lock<std::mutex> &lock) {
    assert(lock.owns_lock());
    if ((world_stop_requested || vm_suspended || debugger_suspended) && !exiting()) {
        freeze_waiting = true;
        if (world_stop_requested)
            world_stopped = true;
        guest_sched_release_for_block();
        update_status(ThreadStatus::suspended);
        status_cond.wait(lock, [&] {
            return exiting() || (!world_stop_requested && !vm_suspended && !debugger_suspended);
        });
        freeze_waiting = false;
        if (!exiting() && status == ThreadStatus::suspended)
            update_status(ThreadStatus::running);
    }
    return !exiting();
}

uint32_t ThreadState::run_callback(Address address, const std::vector<uint32_t> &args) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!wait_for_guest_resume(lock))
        return 0;
    return run_callback_locked(lock, address, args);
}

void ThreadState::exit(SceInt32 status) {
    std::lock_guard<std::mutex> guard(mutex);
    run_end_callback = true;
    exit_requested = true;
    returned_value = static_cast<uint32_t>(status);
}

void ThreadState::update_status(ThreadStatus status, std::optional<ThreadStatus> expected) {
    if (expected)
        assert(expected.value() == this->status);

    if (status == ThreadStatus::waiting && cpu && cpu.get() == guest_sched_token_cpu())
        guest_sched_release_for_block();

    // Don't apply the requested wait transition if being removed to not block deletion
    if (status == ThreadStatus::waiting && delete_requested)
        return;

    if (status == ThreadStatus::running)
        kernel.thread_wake_counter.fetch_add(1, std::memory_order_relaxed);

    this->status = status;
    status_cond.notify_all();

    if (status == ThreadStatus::dormant) {
        const std::lock_guard<std::mutex> end_lock(end_waiters_mutex);
        end_waiters.wake_if([&](auto &waiter) {
            if (waiter.entry.exit_status)
                *waiter.entry.exit_status = static_cast<SceInt32>(returned_value);
            return true;
        });
    }
}

WaitResult ThreadState::delay_until(Deadline deadline, bool callbacks) {
    while (true) {
        const WaitResult r = wait({ SCE_KERNEL_WAITTYPE_DELAY }, deadline, callbacks);
        if (!r)
            return r;
        // Reaching the deadline is the expected outcome of a delay
        if (*r == SCE_KERNEL_ERROR_WAIT_TIMEOUT)
            return SCE_KERNEL_OK;
    }
}

WaitResult ThreadState::wait_for_signal(bool callbacks) {
    while (true) {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (std::exchange(signal_pending, false))
                return SCE_KERNEL_OK;
        }
        const WaitResult r = wait({ SCE_KERNEL_WAITTYPE_SIGNAL }, Deadline::max(), callbacks);
        if (!r)
            return r;
    }
}

SceInt32 ThreadState::send_signal() {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (signal_pending)
            return SCE_KERNEL_ERROR_ALREADY_SENT;
        signal_pending = true;
    }
    wake();
    return SCE_KERNEL_OK;
}

WaitResult ThreadState::wait_for_thread_end(const ThreadStatePtr &waiter, SceInt32 *exit_status, bool callbacks) {
    std::unique_lock<std::mutex> lock(mutex);
    if (status == ThreadStatus::dormant) {
        if (exit_status)
            *exit_status = static_cast<SceInt32>(returned_value);
        return SCE_KERNEL_OK;
    }
    std::unique_lock<std::mutex> end_lock(end_waiters_mutex);
    lock.unlock();
    return end_waiters.wait(end_lock, waiter, { SCE_KERNEL_WAITTYPE_WAITTHEND, id }, { exit_status }, Deadline::max(), callbacks);
}

WaitResult ThreadState::wait(WaitTarget target, Deadline deadline, bool callbacks) {
    std::unique_lock<std::mutex> lock(mutex);
    // Callbacks don't nest, so inside a callback this is a plain wait
    const bool runs_callbacks = callbacks && !is_processing_callbacks;
    const auto woken = [&] { return exiting() || wake_pending || (runs_callbacks && callbacks_pending); };
    wait_target = { callbacks ? target.type | WAITTYPE_CB_BIT : target.type, target.id };
    update_status(ThreadStatus::waiting);
    bool satisfied = true;
    if (deadline == Deadline::max())
        wait_cv.wait(lock, woken);
    else
        satisfied = wait_cv.wait_until(lock, deadline, woken);
    // A freeze may have accepted WAITING as quiescent. Do not resume or dispatch
    // callbacks until it is lifted; the mutex serializes this with freeze requests.
    const bool resumed = wait_for_guest_resume(lock);
    wait_target = {};
    if (!resumed)
        return std::unexpected{ ThreadExiting{} };
    update_status(ThreadStatus::running);
    wake_pending = false;
    if (runs_callbacks && callbacks_pending) {
        lock.unlock();
        process_callbacks();
        lock.lock();
        // The thread exited or was deleted during the callbacks, so the wait must not complete
        if (exiting())
            return std::unexpected{ ThreadExiting{} };
        // The callbacks may have changed what the caller waits for, so it rechecks before waiting again
        return SCE_KERNEL_OK;
    }
    return satisfied ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_WAIT_TIMEOUT;
}

void ThreadState::wake() {
    const std::lock_guard<std::mutex> lock(mutex);
    wake_pending = true;
    wait_cv.notify_all();
}

SceUInt32 ThreadState::process_callbacks() {
    if (is_processing_callbacks)
        return 0;

    {
        const std::lock_guard<std::mutex> lock(mutex);
        callbacks_pending = false;
    }
    is_processing_callbacks = true;
    SceUInt32 processed = 0;
    for (auto it = callbacks.begin(); it != callbacks.end();) {
        const CallbackPtr cb = it->lock();
        // Deleted since it was added
        if (!cb) {
            it = callbacks.erase(it);
            continue;
        }
        ++it;
        std::unique_lock<std::mutex> lock(mutex);
        if (!wait_for_guest_resume(lock)) {
            callbacks_pending = true;
            break;
        }
        // Keep the same lock through notification acquisition and context preparation:
        // a freeze or delete cannot interpose and consume a callback that never runs.
        const std::optional<Callback::Notification> notification = cb->take_notification();
        if (!notification)
            continue;
        const uint32_t ret = run_callback_locked(lock, cb->get_callback_function().address(),
            { static_cast<uint32_t>(notification->notifier_id), notification->count, static_cast<uint32_t>(notification->arg), cb->get_user_common_ptr().address() });
        ++processed;
        // A callback that exits the thread also ends the wait it runs in.
        if (exiting()) {
            callbacks_pending = true;
            break;
        }
        lock.unlock();
        // A callback that returns nonzero deletes itself.
        if (ret != 0)
            kernel.delete_callback(cb->get_uid());
    }
    is_processing_callbacks = false;
    return processed;
}

void ThreadState::notify_callbacks() {
    const std::lock_guard<std::mutex> lock(mutex);
    callbacks_pending = true;
    wait_cv.notify_all();
}

void ThreadState::add_callback(const CallbackPtr &cb) {
    callbacks.push_back(cb);
}


void ThreadState::exit_delete(bool exit) {
    std::lock_guard<std::mutex> lock(mutex);

    run_end_callback = exit;
    delete_requested = true;

    if (status == ThreadStatus::running) {
        stop(*cpu);
    } else {
        // dormant or suspend: wake run_loop() so it can observe delete_requested
        status_cond.notify_all();
    }

    // Wake if blocked in a wait
    wait_cv.notify_all();
}

void ThreadState::suspend_and_wait() {
    guest_sched_release_for_block();
    std::unique_lock<std::mutex> lock(mutex);
    vm_suspended = true;

    if (status != ThreadStatus::running)
        return;

    lock.unlock();
    stop(*cpu);
    lock.lock();

    if (!status_cond.wait_for(lock, std::chrono::seconds(5), [&] { return status != ThreadStatus::running || delete_requested; }))
        LOG_WARN("Timed out waiting for thread {} ({}) to suspend, context may be stale", name, id);
}

void ThreadState::resume_if_suspended() {
    const std::lock_guard<std::mutex> lock(mutex);
    vm_suspended = false;
    if (status == ThreadStatus::suspended && !world_stop_requested && !debugger_suspended)
        update_status(ThreadStatus::running);
    status_cond.notify_all();
}

void ThreadState::request_world_stop() {
    std::unique_lock<std::mutex> lock(mutex);
    world_stop_requested = true;

    if (status != ThreadStatus::running)
        return;

    lock.unlock();
    stop(*cpu);
}

bool ThreadState::wait_world_stopped(std::chrono::steady_clock::time_point deadline) {
    guest_sched_release_for_block();
    std::unique_lock<std::mutex> lock(mutex);
    return status_cond.wait_until(lock, deadline, [&] { return status != ThreadStatus::running || delete_requested; });
}

bool ThreadState::resume_from_world() {
    const std::lock_guard<std::mutex> lock(mutex);
    world_stop_requested = false;
    const bool parked_for_freeze = world_stopped || freeze_waiting;
    world_stopped = false;
    // A VM freeze remains in force, including when it arrived after our gate parked.
    if (parked_for_freeze && status == ThreadStatus::suspended && !vm_suspended && !debugger_suspended) {
        update_status(ThreadStatus::running);
        return true;
    }
    // A gate must be notified even if no status transition was needed.
    status_cond.notify_all();
    return false;
}


void ThreadState::suspend() {
    LOG_WARN("[SUSPLOG] suspend thread '{}' ({}) current status {}", name, id, static_cast<int>(status));
    assert(status == ThreadStatus::running);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        suspend_requested = true;
        debugger_suspended = true;
    }
    stop(*cpu);
}

void ThreadState::resume(bool step) {
    LOG_WARN("[SUSPLOG] resume thread '{}' ({}) from status {}", name, id, static_cast<int>(status));
    assert(status == ThreadStatus::suspended || status == ThreadStatus::dormant);
    {
        const std::lock_guard<std::mutex> lock(mutex);
        single_stepping = step;
        suspend_requested = false;
        debugger_suspended = false;
        if (!world_stop_requested && !vm_suspended)
            update_status(ThreadStatus::running);
        status_cond.notify_all();
    }
}
