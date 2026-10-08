// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cpu/functions.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <util/log.h>

#include <algorithm>
#include <cassert>
#include <utility>

// Set in the wait type of a wait that runs callbacks.
constexpr SceUInt32 WAITTYPE_CB_BIT = 0x80000000U;

bool ThreadState::wait_for_guest_resume(std::unique_lock<std::mutex> &lock) {
    assert(lock.owns_lock());
    if ((world_stop_requested || vm_suspended || debugger_suspended || session_suspended) && !exiting()) {
        freeze_waiting = true;
        if (world_stop_requested)
            world_stopped = true;
        guest_sched_release_for_block();
        update_status(ThreadStatus::suspended);
        status_cond.wait(lock, [&] {
            return exiting() || (!world_stop_requested && !vm_suspended && !debugger_suspended && !session_suspended);
        });
        freeze_waiting = false;
        if (!exiting() && status == ThreadStatus::suspended)
            update_status(ThreadStatus::running);
    }
    return !exiting();
}

uint32_t ThreadState::run_callback(Address address, const std::vector<uint32_t> &args, CallbackPurpose purpose, std::array<uint32_t, 4> completion, uint32_t external_tag) {
    std::unique_lock<std::mutex> lock(mutex);
    if (!wait_for_guest_resume(lock))
        return 0;
    return run_callback_locked(lock, address, args, purpose, 0, completion, external_tag);
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
    WaitContinuationScope continuation(*this, WaitOperation::delay, {}, nullptr, callbacks, deadline);
    deadline = continuation.deadline();
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
    WaitContinuationScope continuation(*this, WaitOperation::signal, {}, nullptr, callbacks, Deadline::max());
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

WaitResult ThreadState::wait_for_thread_end(const ThreadStatePtr &waiter, SceInt32 *exit_status, bool callbacks, SceUInt32 *timeout) {
    WaitContinuationScope continuation(*waiter, WaitOperation::thread_end,
        { static_cast<uint32_t>(id), waiter->guest_address(exit_status) }, timeout, callbacks, deadline_from(timeout));
    continuation.record->object = waiter->kernel.get_thread(id);
    std::unique_lock<std::mutex> lock(mutex);
    if (!continuation.resuming && status == ThreadStatus::dormant) {
        if (exit_status)
            *exit_status = static_cast<SceInt32>(returned_value);
        return SCE_KERNEL_OK;
    }
    std::unique_lock<std::mutex> end_lock(end_waiters_mutex);
    lock.unlock();
    const Deadline deadline = continuation.deadline();
    const WaitResult result = end_waiters.wait(end_lock, waiter, { SCE_KERNEL_WAITTYPE_WAITTHEND, id }, { exit_status }, deadline, callbacks);
    writeback_timeout(timeout, deadline);
    return result;
}

WaitResult ThreadState::wait(WaitTarget target, Deadline deadline, bool callbacks) {
    std::unique_lock<std::mutex> lock(mutex);
    // Callbacks don't nest, so inside a callback this is a plain wait
    const bool runs_callbacks = callbacks && !is_processing_callbacks;
    const auto woken = [&] { return exiting() || wake_pending || (runs_callbacks && callbacks_pending); };
    if (!wait_continuations.empty()) {
        auto &record = wait_continuations.back();
        const std::lock_guard guard(record->mutex);
        record->state.target = target;
    }
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
    std::shared_ptr<WaitContinuation> dispatch;
    {
        const std::lock_guard lock(mutex);
        if (skip_callback_dispatch_once) {
            skip_callback_dispatch_once = false;
            return 0;
        }
        if (is_processing_callbacks)
            return 0;
        if (wait_continuations.empty()) {
            dispatch = std::make_shared<WaitContinuation>();
            dispatch->state.frame_sequence = next_continuation_sequence++;
            dispatch->state.operation = WaitOperation::callback_dispatch;
            dispatch->state.args[0] = last_import_nid;
            dispatch->state.context = last_import_context;
            wait_continuations.push_back(dispatch);
        }
    }
    const auto processed = continue_callbacks(true);
    if (dispatch)
        end_wait_continuation(dispatch);
    return processed;
}

SceUInt32 ThreadState::continue_callbacks(bool fresh) {
    std::unique_lock lock(mutex);
    if (fresh) {
        callbacks_pending = false;
        callback_cursor = 0;
        for (const auto &weak : callbacks) {
            if (auto cb = weak.lock()) {
                callback_cursor = cb->get_uid();
                break;
            }
        }
    }
    is_processing_callbacks = true;
    SceUInt32 processed = 0;
    while (callback_cursor) {
        auto current = std::find_if(callbacks.begin(), callbacks.end(), [&](const auto &weak) {
            return weak.uid == callback_cursor;
        });
        if (current == callbacks.end()) {
            callback_cursor = 0;
            break;
        }
        auto cb = current->lock();
        if (cb && !wait_for_guest_resume(lock)) {
            callbacks_pending = true;
            break;
        }
        const auto next = std::next(current);
        callback_cursor = next == callbacks.end() ? 0 : next->uid;
        if (!cb) {
            callbacks.erase(current);
            continue;
        }
        const auto notification = cb->take_notification();
        if (!notification)
            continue;
        for (const auto &record : wait_continuations) {
            if (record->state.operation == WaitOperation::callback_dispatch) {
                const std::lock_guard guard(record->mutex);
                ++record->state.args[1];
                break;
            }
        }
        const uint32_t ret = run_callback_locked(lock, cb->get_callback_function().address(),
            { static_cast<uint32_t>(notification->notifier_id), notification->count, static_cast<uint32_t>(notification->arg), cb->get_user_common_ptr().address() },
            CallbackPurpose::notification, cb->get_uid());
        ++processed;
        if (exiting()) {
            callbacks_pending = true;
            break;
        }
        lock.unlock();
        if (ret != 0)
            kernel.delete_callback(cb->get_uid());
        lock.lock();
    }
    is_processing_callbacks = false;
    callbacks.remove_if([](const auto &weak) { return weak.weak.expired(); });
    return processed;
}

void ThreadState::notify_callbacks() {
    const std::lock_guard<std::mutex> lock(mutex);
    callbacks_pending = true;
    wait_cv.notify_all();
}

void ThreadState::add_callback(const CallbackPtr &cb) {
    callbacks.push_back({ cb->get_uid(), cb });
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
    if (status == ThreadStatus::suspended && !world_stop_requested && !debugger_suspended && !session_suspended)
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
    if (parked_for_freeze && status == ThreadStatus::suspended && !vm_suspended && !debugger_suspended && !session_suspended) {
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
        if (!world_stop_requested && !vm_suspended && !session_suspended)
            update_status(ThreadStatus::running);
        status_cond.notify_all();
    }
}

ThreadStatus ThreadState::pause_for_session() {
    std::unique_lock lock(mutex);
    const auto previous_status = status;
    session_suspended = true;
    if (status == ThreadStatus::running && cpu) {
        lock.unlock();
        stop(*cpu);
    }
    return previous_status;
}

void ThreadState::resume_after_session_pause() {
    const std::lock_guard lock(mutex);
    session_suspended = false;
    if (status == ThreadStatus::suspended && !world_stop_requested && !vm_suspended && !debugger_suspended)
        update_status(ThreadStatus::running);
    status_cond.notify_all();
    wait_cv.notify_all();
}

void ThreadState::park_creation_for_capture() {
    const std::lock_guard lock(mutex);
    world_stop_requested = true;
    world_stopped = true;
    freeze_waiting = true;
    if (!creation_wait) {
        creation_wait = std::make_shared<WaitContinuation>();
        creation_wait->state.frame_sequence = next_continuation_sequence++;
        creation_wait->state.operation = WaitOperation::creation_import;
        creation_wait->state.args[0] = last_import_nid;
        creation_wait->state.context = last_import_context;
        wait_continuations.push_back(creation_wait);
    }
    guest_sched_release_for_block();
    update_status(ThreadStatus::suspended);
}

void ThreadState::resume_creation_after_capture() {
    std::unique_lock lock(mutex);
    freeze_waiting = false;
    wait_for_guest_resume(lock);
    std::erase(wait_continuations, creation_wait);
    creation_wait.reset();
}

std::shared_ptr<WaitContinuation> ThreadState::begin_wait_continuation(WaitOperation operation,
    std::array<uint32_t, 8> args, SceUInt32 *timeout, bool callbacks, Deadline deadline) {
    const std::lock_guard lock(mutex);
    if (restoring_wait && restoring_wait->state.operation == operation)
        return std::exchange(restoring_wait, {});
    auto record = std::make_shared<WaitContinuation>();
    record->state.frame_sequence = next_continuation_sequence++;
    record->state.operation = operation;
    record->state.args = args;
    record->state.timeout_address = timeout ? Ptr<SceUInt32>(timeout, mem).address() : 0;
    record->state.callbacks = callbacks;
    if (cpu)
        record->state.context = save_context(*cpu);
    record->deadline = deadline;
    wait_continuations.push_back(record);
    return record;
}

void ThreadState::end_wait_continuation(const std::shared_ptr<WaitContinuation> &record) {
    const std::lock_guard lock(mutex);
    std::erase(wait_continuations, record);
}

std::shared_ptr<WaitContinuation> ThreadState::current_wait_continuation() {
    const std::lock_guard lock(mutex);
    return wait_continuations.empty() ? nullptr : wait_continuations.back();
}

void ThreadState::clear_wait_continuations() {
    std::vector<std::shared_ptr<WaitContinuation>> retired;
    {
        const std::lock_guard lock(mutex);
        retired.swap(wait_continuations);
        restoring_wait.reset();
        creation_wait.reset();
        callback_frames.clear();
    }
    // A restored worker may be deleted before entering its typed helper. Remove
    // its heap nodes under the queue's lock before dropping their ownership;
    // never hold the thread mutex while taking an object lock.
    for (const auto &record : retired) {
        if (record->detach_restored_waiter)
            record->detach_restored_waiter();
        const std::lock_guard guard(record->mutex);
        record->restored_node.reset();
        record->detach_restored_waiter = {};
    }
}

std::shared_ptr<void> take_restored_wait_node(const ThreadStatePtr &thread, WaitTarget target) {
    return thread->take_restored_waiter<void>(target);
}
std::shared_ptr<WaitContinuation> get_wait_continuation(const ThreadStatePtr &thread) {
    return thread->current_wait_continuation();
}
