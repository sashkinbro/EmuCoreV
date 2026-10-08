// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cpu/functions.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <util/log.h>

std::shared_ptr<void> ThreadState::restoring_wait_object(SceUID uid) {
    const std::lock_guard lock(mutex);
    return restoring_wait && static_cast<SceUID>(restoring_wait->state.args[0]) == uid ? restoring_wait->object : nullptr;
}
std::vector<std::shared_ptr<WaitContinuation>> ThreadState::saved_wait_continuations() {
    const std::lock_guard lock(mutex);
    return wait_continuations;
}

namespace {
template <class T, class Objects>
std::shared_ptr<T> snapshot_wait_object(KernelState &kernel, const Objects &objects, SceUID uid) {
    if (const auto it = objects.find(uid); it != objects.end())
        return it->second;
    const auto it = kernel.snapshot_objects.find(uid);
    return it == kernel.snapshot_objects.end() ? nullptr : std::dynamic_pointer_cast<T>(it->second);
}
} // namespace

bool ThreadState::restore_wait_queues() {
    auto self = kernel.get_thread(id);
    for (const auto &record : saved_wait_continuations()) {
        auto &state = record->state;
        const SceUID uid = static_cast<SceUID>(state.args[0]);
        const auto u32 = [&](unsigned index) { return Ptr<SceUInt32>(state.args[index]).get(mem); };
        switch (state.operation) {
        case WaitOperation::delay:
        case WaitOperation::signal:
        case WaitOperation::creation_import:
        case WaitOperation::callback_dispatch:
            break;
        case WaitOperation::thread_end: {
            auto target = kernel.get_thread(uid);
            if (!target) {
                if (state.has_result && !state.queued)
                    break;
                return false;
            }
            record->object = target;
            const std::lock_guard guard(target->end_waiters_mutex);
            target->end_waiters.restore(self, { reinterpret_cast<SceInt32 *>(u32(1)) }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = target, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->end_waiters_mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::event: {
            auto object = snapshot_wait_object<SimpleEvent>(kernel, kernel.simple_events, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            object->waiters.restore(self, { state.args[1], u32(2), Ptr<SceUInt64>(state.args[3]).get(mem) }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::timer: {
            auto object = snapshot_wait_object<Timer>(kernel, kernel.timers, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            object->waiters.restore(self, {}, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::semaphore: {
            auto object = snapshot_wait_object<Semaphore>(kernel, kernel.semaphores, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            object->waiters.restore(self, { static_cast<int32_t>(state.args[1]) }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::mutex:
        case WaitOperation::lw_mutex: {
            auto object = snapshot_wait_object<Mutex>(kernel, state.operation == WaitOperation::lw_mutex ? kernel.lwmutexes : kernel.mutexes, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            object->waiters.restore(self, { static_cast<int32_t>(state.args[1]) }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::cond:
        case WaitOperation::lw_cond: {
            auto object = snapshot_wait_object<Condvar>(kernel, state.operation == WaitOperation::lw_cond ? kernel.lwcondvars : kernel.condvars, uid);
            if (!object || !object->associated_mutex)
                return false;
            record->object = object;
            record->associated_object = object->associated_mutex;
            if (state.phase == WaitPhase::reacquire_mutex) {
                const std::lock_guard guard(object->associated_mutex->mutex);
                object->associated_mutex->waiters.restore(self, { 1 }, record);
                {
                    auto detach = std::move(record->detach_restored_waiter);
                    record->detach_restored_waiter = [owner = object->associated_mutex, detach = std::move(detach)] {
                        const std::lock_guard lock(owner->mutex);
                        detach();
                    };
                }
            } else {
                const std::lock_guard guard(object->mutex);
                object->waiters.restore(self, {}, record);
                {
                    auto detach = std::move(record->detach_restored_waiter);
                    record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                        const std::lock_guard lock(owner->mutex);
                        detach();
                    };
                }
            }
            break;
        }
        case WaitOperation::rw_read:
        case WaitOperation::rw_write: {
            auto object = snapshot_wait_object<RWLock>(kernel, kernel.rwlocks, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            object->waiters.restore(self, { state.operation == WaitOperation::rw_write }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::event_flag: {
            auto object = snapshot_wait_object<EventFlag>(kernel, kernel.eventflags, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            object->waiters.restore(self, { state.args[2], state.args[1], u32(3) }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::pipe_send:
        case WaitOperation::pipe_recv: {
            auto object = snapshot_wait_object<MsgPipe>(kernel, kernel.msgpipes, uid);
            if (!object)
                return false;
            record->object = object;
            const std::lock_guard guard(object->mutex);
            auto &queue = state.operation == WaitOperation::pipe_send ? object->senders : object->receivers;
            queue.restore(self, { state.args[1] & SCE_KERNEL_MSG_PIPE_MODE_FULL ? state.args[3] : 1 }, record);
            {
                auto detach = std::move(record->detach_restored_waiter);
                record->detach_restored_waiter = [owner = object, detach = std::move(detach)] {
                    const std::lock_guard lock(owner->mutex);
                    detach();
                };
            }
            break;
        }
        case WaitOperation::display_vblank:
        case WaitOperation::gxm_display_queue:
        case WaitOperation::audio_output:
        case WaitOperation::renderer_finish: {
            const auto it = kernel.wait_resume_handlers.find(state.operation);
            if (it == kernel.wait_resume_handlers.end() || !it->second.restore_queue || !it->second.restore_queue(*this, record))
                return false;
            break;
        }
        case WaitOperation::none: return false;
        }
    }
    return true;
}

void ThreadState::activate_restored_continuations() {
    const auto now = std::chrono::steady_clock::now();
    for (const auto &record : saved_wait_continuations()) {
        const std::lock_guard guard(record->mutex);
        record->deadline = record->state.infinite ? Deadline::max() : now + std::chrono::microseconds(record->state.remaining_us);
    }
}

SceInt32 ThreadState::resume_wait_continuation(const std::shared_ptr<WaitContinuation> &record) {
    {
        std::unique_lock lock(mutex);
        if (!wait_for_guest_resume(lock))
            return SCE_KERNEL_OK;
        restoring_wait = record;
        if (cpu)
            load_context(*cpu, record->state.context);
    }
    struct TlsGuard {
        ThreadState *previous;
        ~TlsGuard() { g_tls_guest_thread = previous; }
    } tls{ g_tls_guest_thread };
    g_tls_guest_thread = this;
    const auto &state = record->state;
    const SceUID uid = static_cast<SceUID>(state.args[0]);
    auto *timeout = Ptr<SceUInt32>(state.timeout_address).get(mem);
    const auto u32 = [&](unsigned index) { return Ptr<SceUInt32>(state.args[index]).get(mem); };
    SceInt32 result = SCE_KERNEL_OK;
    switch (state.operation) {
    case WaitOperation::delay: result = guest_result(delay_until(record->deadline, state.callbacks)); break;
    case WaitOperation::signal: result = guest_result(wait_for_signal(state.callbacks)); break;
    case WaitOperation::thread_end: {
        if (state.has_result && !state.queued) {
            result = state.result;
            writeback_timeout(timeout, record->deadline);
            end_wait_continuation(record);
            break;
        }
        auto target = std::static_pointer_cast<ThreadState>(record->object);
        result = target ? guest_result(target->wait_for_thread_end(kernel.get_thread(id), reinterpret_cast<SceInt32 *>(u32(1)), state.callbacks, timeout)) : SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
        break;
    }
    case WaitOperation::event:
        result = simple_event_waitorpoll(kernel, "restore", id, uid, state.args[1], u32(2), Ptr<SceUInt64>(state.args[3]).get(mem), timeout, true, state.callbacks);
        break;
    case WaitOperation::timer:
        result = timer_waitorpoll(kernel, "restore", id, uid, state.args[1], u32(2), Ptr<SceUInt64>(state.args[3]).get(mem), timeout, true, state.callbacks);
        break;
    case WaitOperation::semaphore:
        result = semaphore_wait(kernel, "restore", id, uid, static_cast<int32_t>(state.args[1]), timeout, state.callbacks);
        break;
    case WaitOperation::mutex:
    case WaitOperation::lw_mutex:
        result = mutex_lock(kernel, mem, "restore", id, uid, static_cast<int32_t>(state.args[1]), timeout,
            state.operation == WaitOperation::lw_mutex ? SyncWeight::Light : SyncWeight::Heavy, state.callbacks);
        break;
    case WaitOperation::cond:
    case WaitOperation::lw_cond:
        result = condvar_wait(kernel, mem, "restore", id, uid, timeout,
            state.operation == WaitOperation::lw_cond ? SyncWeight::Light : SyncWeight::Heavy, state.callbacks);
        break;
    case WaitOperation::rw_read:
    case WaitOperation::rw_write:
        result = rwlock_lock(kernel, mem, "restore", id, uid, timeout, state.operation == WaitOperation::rw_write, state.callbacks);
        break;
    case WaitOperation::event_flag:
        result = eventflag_wait(kernel, "restore", id, uid, state.args[1], state.args[2], u32(3), timeout, state.callbacks);
        break;
    case WaitOperation::pipe_recv:
    case WaitOperation::pipe_send: {
        void *buffer = Ptr<void>(state.args[2]).get(mem);
        result = static_cast<SceInt32>(state.operation == WaitOperation::pipe_recv
                ? msgpipe_recv(kernel, "restore", id, uid, state.args[1], buffer, state.args[3], timeout, state.callbacks, u32(4), state.args[5])
                : msgpipe_send(kernel, "restore", id, uid, state.args[1], buffer, state.args[3], timeout, state.callbacks, u32(4), state.args[5]));
        if (state.args[5] && result >= 0) {
            if (auto *output = u32(4))
                *output = static_cast<SceSize>(result);
            result = SCE_KERNEL_OK;
        }
        break;
    }
    case WaitOperation::display_vblank:
    case WaitOperation::gxm_display_queue:
    case WaitOperation::audio_output:
    case WaitOperation::renderer_finish: {
        const auto it = kernel.wait_resume_handlers.find(state.operation);
        result = it != kernel.wait_resume_handlers.end() && it->second.resume
            ? it->second.resume(*this, record)
            : SCE_KERNEL_ERROR_INVALID_ARGUMENT;
        break;
    }
    case WaitOperation::callback_dispatch:
        end_wait_continuation(record);
        {
            const std::lock_guard lock(mutex);
            restoring_wait.reset();
        }
        if (state.args[0] == check_callback_import) {
            result = static_cast<SceInt32>(state.args[1]);
            break;
        }
        if (!cpu || !restartable_callback_import(state.args[0]))
            return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
        {
            const std::lock_guard lock(mutex);
            skip_callback_dispatch_once = true;
            last_import_nid = state.args[0];
            last_import_context = state.context;
        }
        kernel.call_import(*cpu, state.args[0], id);
        clear_exclusive(*cpu);
        {
            const std::lock_guard lock(mutex);
            skip_callback_dispatch_once = false;
        }
        result = static_cast<SceInt32>(read_reg(*cpu, 0));
        break;
    case WaitOperation::creation_import:
        end_wait_continuation(record);
        {
            const std::lock_guard lock(mutex);
            restoring_wait.reset();
            last_import_nid = state.args[0];
            last_import_context = state.context;
        }
        if (!cpu || !restartable_creation_import(state.args[0]))
            return SCE_KERNEL_ERROR_INVALID_ARGUMENT;
        kernel.call_import(*cpu, state.args[0], id);
        clear_exclusive(*cpu);
        result = static_cast<SceInt32>(read_reg(*cpu, 0));
        break;
    case WaitOperation::none: result = SCE_KERNEL_ERROR_INVALID_ARGUMENT; break;
    }
    {
        const std::lock_guard lock(mutex);
        restoring_wait.reset();
    }
    end_wait_continuation(record);
    return result;
}

void ThreadState::resume_restored_continuations() {
    while (true) {
        std::shared_ptr<WaitContinuation> wait_record;
        std::shared_ptr<CallbackContinuationSnapshot> callback;
        {
            const std::lock_guard lock(mutex);
            if (!wait_continuations.empty())
                wait_record = wait_continuations.back();
            if (!callback_frames.empty())
                callback = callback_frames.back();
        }
        if (!callback && is_processing_callbacks) {
            continue_callbacks(false);
            continue;
        }
        if (!wait_record && !callback)
            break;
        if (callback && (!wait_record || callback->frame_sequence > wait_record->state.frame_sequence)) {
            if (!callback->guest_returned) {
                run_loop();
                const std::lock_guard lock(mutex);
                callback->guest_returned = true;
                callback->result = returned_value;
            }
            {
                std::unique_lock lock(mutex);
                if (!wait_for_guest_resume(lock))
                    break;
                if (cpu) {
                    load_context(*cpu, callback->previous_context);
                    write_tpidruro(*cpu, callback->previous_tpidruro);
                }
                std::erase(callback_frames, callback);
                if (callback->purpose == CallbackPurpose::thread_end) {
                    status = static_cast<ThreadStatus>(callback->completion[0]);
                    returned_value = callback->completion[1];
                }
            }
            if (callback->purpose == CallbackPurpose::notification) {
                if (callback->result != 0)
                    kernel.delete_callback(callback->callback_uid);
                continue_callbacks(false);
            } else if (callback->purpose == CallbackPurpose::external) {
                if (!kernel.resume_external_callback(*this, *callback)) {
                    LOG_ERROR("Missing callback continuation handler {} for thread {}", callback->external_tag, id);
                    exit_delete(false);
                    break;
                }
            }
        } else {
            const SceInt32 result = resume_wait_continuation(wait_record);
            std::unique_lock lock(mutex);
            if (!wait_for_guest_resume(lock))
                break;
            if (cpu)
                write_reg(*cpu, 0, static_cast<uint32_t>(result));
        }
    }
    const std::lock_guard lock(mutex);
    restored_continuations_pending = false;
}

std::vector<std::shared_ptr<SyncPrimitive>> ThreadState::saved_sync_objects() {
    std::vector<std::shared_ptr<SyncPrimitive>> objects;
    for (const auto &record : saved_wait_continuations()) {
        if (!record->object)
            continue;
        switch (record->state.operation) {
        case WaitOperation::event: objects.push_back(std::static_pointer_cast<SimpleEvent>(record->object)); break;
        case WaitOperation::timer: objects.push_back(std::static_pointer_cast<Timer>(record->object)); break;
        case WaitOperation::semaphore: objects.push_back(std::static_pointer_cast<Semaphore>(record->object)); break;
        case WaitOperation::mutex:
        case WaitOperation::lw_mutex: objects.push_back(std::static_pointer_cast<Mutex>(record->object)); break;
        case WaitOperation::rw_read:
        case WaitOperation::rw_write: objects.push_back(std::static_pointer_cast<RWLock>(record->object)); break;
        case WaitOperation::event_flag: objects.push_back(std::static_pointer_cast<EventFlag>(record->object)); break;
        case WaitOperation::pipe_send:
        case WaitOperation::pipe_recv: objects.push_back(std::static_pointer_cast<MsgPipe>(record->object)); break;
        case WaitOperation::cond:
        case WaitOperation::lw_cond:
            objects.push_back(std::static_pointer_cast<Condvar>(record->object));
            if (record->associated_object)
                objects.push_back(std::static_pointer_cast<Mutex>(record->associated_object));
            break;
        default: break;
        }
    }
    return objects;
}
