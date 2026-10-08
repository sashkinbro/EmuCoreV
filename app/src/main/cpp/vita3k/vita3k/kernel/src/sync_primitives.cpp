// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <chrono>
#include <cpu/functions.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>

#include <algorithm>
#include <array>
#include <kernel/types.h>
#include <set>
#include <util/lock_and_find.h>
#include <util/log.h>

static constexpr bool LOG_SYNC_PRIMITIVES = false;

// ***********
// * Helpers *
// ***********

inline static int unknown_mutex_id(const char *export_name, SyncWeight weight) {
    if (weight == SyncWeight::Light)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_LW_MUTEX_ID);
    return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_MUTEX_ID);
}

inline static int unknown_cond_id(const char *export_name, SyncWeight weight) {
    if (weight == SyncWeight::Light)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_LW_COND_ID);
    return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_COND_ID);
}

inline static MutexPtrs &get_mutexes(KernelState &kernel, SyncWeight weight) {
    return weight == SyncWeight::Light ? kernel.lwmutexes : kernel.mutexes;
}

inline static CondvarPtrs &get_condvars(KernelState &kernel, SyncWeight weight) {
    return weight == SyncWeight::Light ? kernel.lwcondvars : kernel.condvars;
}

namespace {

struct EvfOp {
    uint64_t ms;
    SceUID evf;
    SceUID thread;
    uint8_t op; // 0=SET 1=CLEAR 2=WAIT_OK 3=WAIT_BLOCK 4=CANCEL
    uint32_t bits;
    uint32_t flags_after;
    uint32_t woken;
};
constexpr size_t EVF_RING_SIZE = 512;
std::array<EvfOp, EVF_RING_SIZE> evf_ring{};
std::atomic<uint64_t> evf_ring_next{ 0 };

// every thread that EVER set each flag: the provable-cycle breaker needs "who could wake this"
std::mutex evf_setters_mutex;
std::unordered_map<SceUID, std::set<SceUID>> evf_setters;

void evf_record(SceUID evf, SceUID thread, uint8_t op, uint32_t bits, uint32_t flags_after, uint32_t woken) {
    if (op == 0 && thread > 0) {
        const std::lock_guard<std::mutex> lock(evf_setters_mutex);
        evf_setters[evf].insert(thread);
    }
    const uint64_t idx = evf_ring_next.fetch_add(1, std::memory_order_relaxed);
    const uint64_t ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    evf_ring[idx % EVF_RING_SIZE] = EvfOp{ ms, evf, thread, op, bits, flags_after, woken };
}

struct MutexCacheEntry {
    SceUID uid = 0;
    SyncWeight weight = SyncWeight::Light;
    MutexPtr ptr;
};
thread_local std::array<MutexCacheEntry, 8> g_mutex_cache;
thread_local uint32_t g_mutex_cache_next = 0;
} // namespace

// Return a cached mutex while it is still registered.
inline static MutexPtr find_mutex(KernelState &kernel, SceUID mutexid, SyncWeight weight) {
    for (const auto &entry : g_mutex_cache) {
        if (entry.uid == mutexid && entry.weight == weight && entry.ptr
            && !entry.ptr->deleted.load(std::memory_order_relaxed))
            return entry.ptr;
    }
    auto mutex = lock_and_find(mutexid, get_mutexes(kernel, weight), kernel.mutex);
    if (mutex)
        g_mutex_cache[g_mutex_cache_next++ % g_mutex_cache.size()] = { mutexid, weight, mutex };
    return mutex;
}

// Returns the condition variable with this id, or null.
inline static CondvarPtr find_condvar(KernelState &kernel, SceUID condid, SyncWeight weight) {
    return lock_and_find(condid, get_condvars(kernel, weight), kernel.mutex);
}

// Never wait for an object lock while holding the registry lock. A lookup
// already in flight is serialized with deletion by the object's liveness check.
template <typename T>
static bool remove_sync_object(std::map<SceUID, std::shared_ptr<T>> &objects, std::mutex &registry_mutex, SceUID uid) {
    std::shared_ptr<T> object;
    {
        const std::lock_guard<std::mutex> guard(registry_mutex);
        const auto it = objects.find(uid);
        if (it == objects.end())
            return false;
        object = std::move(objects.extract(it).mapped());
    }
    object->mark_deleted();
    return true;
}

void SimpleEvent::on_delete() { waiters.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE); }
void Timer::on_delete() { waiters.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE); }
void Semaphore::on_delete() { waiters.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE); }
void Mutex::on_delete() {
    waiters.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE);
    owner = nullptr;
}
void RWLock::on_delete() { waiters.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE); }
void EventFlag::on_delete() { waiters.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE); }
void Condvar::on_delete() {
    waiters.wake_all(lightweight ? SCE_KERNEL_ERROR_WAIT_DELETE_LW_COND : SCE_KERNEL_ERROR_WAIT_DELETE_COND);
}
void MsgPipe::on_delete() {
    senders.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE);
    receivers.wake_all(SCE_KERNEL_ERROR_WAIT_DELETE);
}

// *****************
// * Simple events *
// *****************

SceUID simple_event_create(KernelState &kernel, MemState &mem, const char *export_name, const char *name, SceUID thread_id, SceUInt32 attr, SceUInt32 init_pattern) {
    if (!name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }

    const SceUID uid = kernel.get_next_uid();

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} pattern: {:#b}",
            export_name, uid, thread_id, name, attr, init_pattern);
    }

    const SimpleEventPtr event = std::make_shared<SimpleEvent>(attr);
    event->uid = uid;
    event->pattern = init_pattern;
    strncpy(event->name, name, KERNELOBJECT_MAX_NAME_LENGTH);
    event->attr = attr;

    event->last_user_data = 0;
    event->auto_reset = (event->attr & SCE_KERNEL_EVENT_ATTR_AUTO_RESET);
    event->cb_wakeup_only = (event->attr & SCE_KERNEL_ATTR_NOTIFY_CB_WAKEUP_ONLY);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    kernel.simple_events.emplace(uid, event);

    return uid;
}

SceUID simple_event_find(KernelState &kernel, const char *export_name, const char *pName) {
    if (!pName)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);

    if (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);

    if (LOG_SYNC_PRIMITIVES)
        LOG_DEBUG("{}: name: \"{}\"", export_name, pName);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);

    const auto it = std::find_if(kernel.simple_events.begin(), kernel.simple_events.end(), [=](const auto &event) {
        return strncmp(event.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != kernel.simple_events.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

SceInt32 simple_event_waitorpoll(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, SceUInt32 wait_pattern, SceUInt32 *result_pattern, SceUInt64 *user_data, SceUInt32 *timeout, bool is_wait, bool callbacks) {
    const SimpleEventPtr event = lock_and_find(event_id, kernel.simple_events, kernel.mutex);
    if (!event) {
        // this may also be a timer event
        return timer_waitorpoll(kernel, export_name, thread_id, event_id, wait_pattern, result_pattern, user_data, timeout, is_wait, callbacks);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} existing_pattern: {:#b} wait_pattern: {:#b} timeout: {}"
                  " waiting_threads: {}",
            export_name, event->uid, thread_id, event->name, event->attr, event->pattern, wait_pattern, timeout ? *timeout : 0,
            event->waiters.size());
    }

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;

    if (result_pattern)
        *result_pattern = event->pattern;

    if (event->pattern & wait_pattern) {
        if (event->auto_reset)
            // all common bits are zeroed
            event->pattern &= ~wait_pattern;

        if (user_data)
            *user_data = event->last_user_data;

        return SCE_KERNEL_OK;
    } else if (is_wait) {
        thread->set_wait_reason("event", event_id, wait_pattern);
        const Deadline deadline = deadline_from(timeout);
        const WaitResult r = event->waiters.wait(event_lock, thread, { SCE_KERNEL_WAITTYPE_EVENT, event_id }, { wait_pattern, result_pattern, user_data }, deadline, callbacks);
        writeback_timeout(timeout, deadline);
        const SceInt32 err = guest_result(r);
        if (err < 0) {
            // set it only if a timeout occurs
            // otherwise set in simple_event_setorpulse
            if (user_data)
                *user_data = event->last_user_data;
            if (result_pattern)
                *result_pattern = event->pattern;
        }
        return err;
    } else {
        return SCE_KERNEL_ERROR_EVENT_COND;
    }
}

SceInt32 simple_event_setorpulse(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, SceUInt32 pattern, SceUInt64 user_data, bool is_set) {
    const SimpleEventPtr event = lock_and_find(event_id, kernel.simple_events, kernel.mutex);
    if (!event) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} existing_pattern: {:#b} set_pattern: {:#b}"
                  " waiting_threads: {}",
            export_name, event->uid, thread_id, event->name, event->attr, event->pattern, pattern,
            event->waiters.size());
    }

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;

    const SceUInt32 old_pattern = event->pattern;
    const SceUInt64 old_user_data = event->last_user_data;
    const SceUInt32 new_pattern = event->pattern | pattern;

    event->pattern = new_pattern;
    event->last_user_data = user_data;

    event->waiters.wake_if([&](auto &waiter) {
        const SimpleEvent::WaitEntry &wait = waiter.entry;
        if (!(event->pattern & wait.pattern))
            return false;

        if (wait.result_pattern)
            *wait.result_pattern = new_pattern;

        if (wait.user_data)
            *wait.user_data = event->last_user_data;

        if (event->auto_reset)
            // all common bit are zeroed
            event->pattern &= ~wait.pattern;

        return true;
    });

    if (!is_set) {
        event->pattern = old_pattern;
        event->last_user_data = old_user_data;
    }

    return SCE_KERNEL_OK;
}

SceInt32 simple_event_clear(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, SceUInt32 clear_pattern) {
    const SimpleEventPtr event = lock_and_find(event_id, kernel.simple_events, kernel.mutex);
    if (!event) {
        // this may also be a timer event
        return timer_clear(kernel, export_name, thread_id, event_id, clear_pattern);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} clear_pattern: {:#b}",
            export_name, event_id, thread_id, clear_pattern);
    }

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;

    event->pattern &= clear_pattern;

    return SCE_KERNEL_OK;
}

SceInt32 simple_event_delete(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id) {
    return remove_sync_object(kernel.simple_events, kernel.mutex, event_id) ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;
}

// *********
// * Timer *
// *********

inline uint64_t get_current_time() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
        .count();
}

SceUID timer_create(KernelState &kernel, MemState &mem, const char *export_name, const char *name, SceUID thread_id, SceUInt32 attr) {
    if (!name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }

    const SceUID uid = kernel.get_next_uid();

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {}",
            export_name, uid, thread_id, name, attr);
    }

    const TimerPtr timer = std::make_shared<Timer>(attr);
    timer->uid = uid;
    timer->next_event = std::numeric_limits<uint64_t>::max();
    strncpy(timer->name, name, KERNELOBJECT_MAX_NAME_LENGTH);
    timer->attr = attr;

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    kernel.timers.emplace(uid, timer);

    return uid;
}

SceUID timer_find(KernelState &kernel, const char *export_name, const char *pName) {
    if (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);

    if (LOG_SYNC_PRIMITIVES)
        LOG_DEBUG("{}: name: \"{}\"", export_name, pName);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);

    const auto it = std::find_if(kernel.timers.begin(), kernel.timers.end(), [=](const auto &timer) {
        return strncmp(timer.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != kernel.timers.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

static void timer_schedule_event(TimerPtr &timer) {
    uint64_t curr_time = get_current_time();
    timer->next_event = curr_time + timer->event_interval;

    // the first waiter has to wait for the new event time
    timer->waiters.notify_all();
}

SceInt32 timer_set(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID timer_handle, SceUID type, SceKernelSysClock *interval, SceInt32 repeats) {
    TimerPtr timer = lock_and_find(timer_handle, kernel.timers, kernel.mutex);
    if (!timer)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID);

    if (!interval)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} type: {} interval: {} repeats: {}"
                  " waiting_threads: {}",
            export_name, timer->uid, thread_id, timer->name, timer->attr, type, *interval,
            repeats, timer->waiters.size());
    }

    auto timer_lock = timer->lock();
    if (!timer_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;
    timer->is_pulse = type != 0;
    timer->is_repeat = repeats != 0;
    timer->event_interval = *interval;

    if (timer->is_started)
        timer_schedule_event(timer);

    return SCE_KERNEL_OK;
}

// this function is actually only called by simple_event_waitorpoll
// as the only way to wait for a timer is using the event function (a timer is an event)
SceInt32 timer_waitorpoll(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, SceUInt32 bit_pattern, SceUInt32 *result_pattern, SceUInt64 *user_data, SceUInt32 *timeout, bool is_wait, bool callbacks) {
    TimerPtr timer = lock_and_find(event_id, kernel.timers, kernel.mutex);
    if (!timer) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} timeout: {}"
                  " waiting_threads: {}",
            export_name, timer->uid, thread_id, timer->name, timer->attr, timeout ? *timeout : 0,
            timer->waiters.size());
    }

    if (timeout)
        LOG_WARN_ONCE("Ignoring timeout");

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    auto lock = timer->lock();
    if (!lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;

    if (result_pattern)
        *result_pattern = SCE_KERNEL_EVENT_TIMER;
    if (user_data)
        *user_data = 0;

    uint64_t current_time = get_current_time();
    auto set_next_event = [&]() {
        if (timer->is_repeat) {
            // the event repeats every timer->event_interval, go to the next one after current_time
            timer->next_event += ((current_time - timer->next_event - 1) / timer->event_interval + 1) * timer->event_interval;
        } else {
            timer->next_event = std::numeric_limits<uint64_t>::max();
        }
    };

    if (timer->next_event < current_time) {
        if (!timer->is_pulse) {
            // we can reach pulse event only by waiting
            timer->event_set = true;
        }

        set_next_event();
    }

    if (timer->event_set) {
        if (timer->attr & SCE_KERNEL_EVENT_ATTR_AUTO_RESET) {
            timer->event_set = false;
        }

        return SCE_KERNEL_OK;
    } else if (is_wait) {
        WaitQueue<std::monostate>::Waiter waiter{ .thread = thread, .priority = thread->priority };
        timer->waiters.push(waiter);

        while (true) {
            if (waiter.result)
                return *waiter.result;
            // only the first waiter waits for the event, the others wait until they are first
            const bool is_first = timer->waiters.front() == &waiter;
            Deadline deadline = Deadline::max();
            if (is_first && timer->next_event != std::numeric_limits<uint64_t>::max()) {
                const uint64_t wait_time = timer->next_event > current_time ? timer->next_event - current_time : 0;
                deadline = std::chrono::steady_clock::now() + std::chrono::microseconds(wait_time);
            }

            lock.unlock();
            const WaitResult r = thread->wait({ SCE_KERNEL_WAITTYPE_EVENT, event_id }, deadline, callbacks);
            lock.lock();
            if (!r) {
                timer->waiters.remove(waiter);
                timer->waiters.notify_all();
                return guest_result(r);
            }

            current_time = get_current_time();
            if (timer->waiters.front() == &waiter && (timer->event_set || current_time > timer->next_event))
                break;
        }

        timer->waiters.remove(waiter);

        timer->event_set = !timer->is_pulse && !(timer->attr & SCE_KERNEL_EVENT_ATTR_AUTO_RESET);
        set_next_event();
        // notify the other waiting threads
        timer->waiters.notify_all();

        return SCE_KERNEL_OK;
    } else {
        return SCE_KERNEL_ERROR_EVENT_COND;
    }
}

// this function is actually only called by simple_event_clear
SceInt32 timer_clear(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, SceUInt32 clear_pattern) {
    TimerPtr timer = lock_and_find(event_id, kernel.timers, kernel.mutex);
    if (!timer) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {}",
            export_name, event_id, thread_id);
    }

    auto guard = timer->lock();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;
    timer->event_set = false;
    return SCE_KERNEL_OK;
}

SceInt32 timer_start(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID timer_handle) {
    TimerPtr timer = lock_and_find(timer_handle, kernel.timers, kernel.mutex);
    if (!timer)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID);

    auto guard = timer->lock();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;
    if (timer->is_started)
        return 1;
    timer->is_started = true;
    timer->time = get_current_time();

    if (timer->event_interval != 0)
        timer_schedule_event(timer);

    return SCE_KERNEL_OK;
}

SceInt32 timer_stop(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID timer_handle) {
    const TimerPtr timer = lock_and_find(timer_handle, kernel.timers, kernel.mutex);
    if (!timer)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID);

    auto timer_lock = timer->lock();
    if (!timer_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;
    bool was_stopped = !timer->is_started;
    timer->is_started = false;
    timer->time = get_current_time();
    timer->next_event = std::numeric_limits<uint64_t>::max();

    return static_cast<int>(was_stopped);
}

// *********
// * Mutex *
// *********

SceUID mutex_create(KernelState &kernel, MemState &mem, const char *export_name, const char *mutex_name, SceUID thread_id, SceUInt attr, int init_count, Ptr<SceKernelLwMutexWork> workarea, SyncWeight weight) {
    if (!mutex_name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(mutex_name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }
    if (init_count < 0) {
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
    }
    if (init_count > 1 && (attr & SCE_KERNEL_MUTEX_ATTR_RECURSIVE)) {
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_COUNT);
    }

    const MutexPtr mutex = std::make_shared<Mutex>(attr);
    const SceUID uid = kernel.get_next_uid();
    mutex->uid = uid;
    mutex->init_count = init_count;
    mutex->lock_count = init_count;
    mutex->workarea = workarea;
    strncpy(mutex->name, mutex_name, KERNELOBJECT_MAX_NAME_LENGTH);
    mutex->attr = attr;
    mutex->owner = nullptr;
    if (init_count > 0) {
        const ThreadStatePtr thread = kernel.get_thread(thread_id);
        if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
            return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
        mutex->owner = thread;
    }
    if (weight == SyncWeight::Light) {
        SceKernelLwMutexWork *workarea_mem = workarea.get(mem);
        workarea_mem->lockCount = init_count;
        if (workarea_mem->lockCount)
            workarea_mem->owner = thread_id;
        workarea_mem->attr = attr;
        workarea_mem->uid = uid;
    }

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    auto &mutexes = get_mutexes(kernel, weight);
    mutexes.emplace(uid, mutex);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} init_count: {}",
            export_name, uid, thread_id, mutex_name, attr, init_count);
    }

    return uid;
}

SceUID mutex_find(KernelState &kernel, const char *export_name, const char *pName) {
    if (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);

    if (LOG_SYNC_PRIMITIVES)
        LOG_DEBUG("{}: name: \"{}\"", export_name, pName);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);

    const auto it = std::find_if(kernel.mutexes.begin(), kernel.mutexes.end(), [=](const auto &mutex) {
        return strncmp(mutex.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != kernel.mutexes.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

inline static int mutex_lock_impl(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, int lock_count, MutexPtr &mutex, SyncWeight weight, SceUInt *timeout, bool only_try, WaitTarget target, bool callbacks, const ThreadStatePtr &caller = nullptr) {
    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} lock_count: {} timeout: {} waiting_threads: {}",
            export_name, mutex->uid, thread_id, mutex->name, mutex->attr, mutex->lock_count, timeout ? *timeout : 0,
            mutex->waiters.size());
    }

    const ThreadStatePtr thread = caller ? caller : kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    auto mutex_lock = mutex->lock();
    if (!mutex_lock)
        return unknown_mutex_id(export_name, weight);
    thread->set_wait_reason("mutex", mutex->uid, mutex->owner ? mutex->owner->id : 0);

    bool is_recursive = (mutex->attr & SCE_KERNEL_MUTEX_ATTR_RECURSIVE);

    // Already owned
    if (mutex->lock_count > 0) {
        // Owned by ourselves
        if (mutex->owner == thread) {
            if (is_recursive) {
                mutex->lock_count += lock_count;
                if (weight == SyncWeight::Light)
                    mutex->workarea.get(mem)->lockCount += lock_count;

                return SCE_KERNEL_OK;
            }
            if (weight == SyncWeight::Light)
                return RET_ERROR(SCE_KERNEL_ERROR_LW_MUTEX_RECURSIVE);

            return RET_ERROR(SCE_KERNEL_ERROR_MUTEX_RECURSIVE);
        }
        // Owned by someone else

        // Don't sleep if only_try is set
        if (only_try) {
            if (weight == SyncWeight::Light)
                return RET_ERROR(SCE_KERNEL_ERROR_LW_MUTEX_FAILED_TO_OWN);

            return RET_ERROR(SCE_KERNEL_ERROR_MUTEX_FAILED_TO_OWN);
        }

        // Sleep thread!
        const Deadline deadline = deadline_from(timeout);
        const WaitResult r = mutex->waiters.wait(mutex_lock, thread, target, { lock_count }, deadline, callbacks);
        writeback_timeout(timeout, deadline);

        // A deleted mutex has no owner and its work area may already be freed.
        if (weight == SyncWeight::Light && mutex->owner == thread) {
            mutex->workarea.get(mem)->lockCount = mutex->lock_count;
            mutex->workarea.get(mem)->owner = thread_id;
        }

        return guest_result(r);
    }
    // Not owned
    // Take ownership!

    mutex->lock_count += lock_count;
    mutex->owner = thread;

    if (weight == SyncWeight::Light) {
        mutex->workarea.get(mem)->lockCount = mutex->lock_count;
        if (mutex->owner == thread) {
            mutex->workarea.get(mem)->owner = thread_id;
        }
    }

    return SCE_KERNEL_OK;
}

int mutex_lock(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID mutexid, int lock_count, unsigned int *timeout, SyncWeight weight, bool callbacks) {
    assert(mutexid >= 0);

    MutexPtr mutex = find_mutex(kernel, mutexid, weight);
    if (!mutex)
        return unknown_mutex_id(export_name, weight);

    return mutex_lock_impl(kernel, mem, export_name, thread_id, lock_count, mutex, weight, timeout, false, { weight == SyncWeight::Light ? SCE_KERNEL_WAITTYPE_LW_MUTEX : SCE_KERNEL_WAITTYPE_MUTEX, mutexid }, callbacks);
}

int mutex_try_lock(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID mutexid, int lock_count, SyncWeight weight) {
    assert(mutexid >= 0);

    MutexPtr mutex = find_mutex(kernel, mutexid, weight);
    if (!mutex)
        return unknown_mutex_id(export_name, weight);

    // Never waits, so it has no wait target
    return mutex_lock_impl(kernel, mem, export_name, thread_id, lock_count, mutex, weight, nullptr, true, {}, false);
}

inline static int mutex_unlock_impl(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, int unlock_count, MutexPtr &mutex, const ThreadStatePtr &caller = nullptr) {
    const ThreadStatePtr current_thread = caller ? caller : kernel.get_thread(thread_id);
    if (!current_thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    auto mutex_lock = mutex->lock();
    if (!mutex_lock)
        return unknown_mutex_id(export_name, mutex->workarea ? SyncWeight::Light : SyncWeight::Heavy);

    if (current_thread == mutex->owner) {
        if (unlock_count > mutex->lock_count) {
            return RET_ERROR(SCE_KERNEL_ERROR_LW_MUTEX_UNLOCK_UDF);
        }

        mutex->lock_count -= unlock_count;

        if (mutex->lock_count == 0) {
            mutex->owner = nullptr;

            if (auto *waiter = mutex->waiters.front()) {
                mutex->lock_count += waiter->entry.lock_count;
                mutex->owner = waiter->thread;
                mutex->waiters.wake(*waiter);
            }
        }

        // keep the lwmutex workarea in sync
        if (mutex->workarea) {
            SceKernelLwMutexWork *workarea_mem = mutex->workarea.get(mem);
            workarea_mem->lockCount = mutex->lock_count;
            workarea_mem->owner = mutex->owner ? mutex->owner->id : 0;
        }
    }

    return SCE_KERNEL_OK;
}

int mutex_unlock(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID mutexid, int unlock_count, SyncWeight weight) {
    assert(mutexid >= 0);

    MutexPtr mutex = find_mutex(kernel, mutexid, weight);
    if (!mutex)
        return unknown_mutex_id(export_name, weight);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} lock_count: {} waiting_threads: {}",
            export_name, mutexid, thread_id, mutex->name, mutex->attr, mutex->lock_count, unlock_count,
            mutex->waiters.size());
    }

    return mutex_unlock_impl(kernel, mem, export_name, thread_id, unlock_count, mutex);
}

int mutex_delete(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID mutexid, SyncWeight weight) {
    return remove_sync_object(get_mutexes(kernel, weight), kernel.mutex, mutexid) ? SCE_KERNEL_OK : unknown_mutex_id(export_name, weight);
}

MutexPtr mutex_get(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID mutexid, SyncWeight weight) {
    assert(mutexid >= 0);

    MutexPtr mutex = find_mutex(kernel, mutexid, weight);
    if (!mutex)
        return nullptr;

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} lock_count: {} waiting_threads: {}",
            export_name, mutexid, thread_id, mutex->name, mutex->attr, mutex->lock_count,
            mutex->waiters.size());
    }
    return mutex;
}

// **************
// * RWLock *
// **************

SceUID rwlock_create(KernelState &kernel, MemState &mem, const char *export_name, const char *name, SceUID thread_id, SceUInt32 attr) {
    if (!name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }

    const RWLockPtr rwlock = std::make_shared<RWLock>(attr);
    const SceUID uid = kernel.get_next_uid();
    rwlock->uid = uid;
    strncpy(rwlock->name, name, KERNELOBJECT_MAX_NAME_LENGTH);
    rwlock->attr = attr;

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    kernel.rwlocks.emplace(uid, rwlock);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {}",
            export_name, uid, thread_id, name, attr);
    }

    return uid;
}

SceInt32 rwlock_lock(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID lock_id, uint32_t *timeout, bool is_write, bool callbacks) {
    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
    const RWLockPtr rwlock = lock_and_find(lock_id, kernel.rwlocks, kernel.mutex);

    if (!rwlock)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_RW_LOCK_ID);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} timeout: {} waiting_threads: {}",
            export_name, lock_id, thread_id, rwlock->name, rwlock->attr, timeout ? *timeout : 0,
            rwlock->waiters.size());
    }

    auto rwlock_lock = rwlock->lock();
    if (!rwlock_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_RW_LOCK_ID;

    // if it is a read lock, it is always recursive
    bool is_recursive = !is_write || (rwlock->attr & SCE_KERNEL_MUTEX_ATTR_RECURSIVE);

    // cases where we don't need to wait :
    if (rwlock->state == RWLockState::Unlocked // the lock is unlocked
        || (!is_write && rwlock->state == RWLockState::ReadLocked) // we want a read lock when the lock is readlocked
        || (is_recursive && rwlock->owners.contains(thread))) { // the thread asking has already locked this lock

        auto it = rwlock->owners.find(thread);
        if (it != rwlock->owners.end()) {
            // increase the count
            it->second++;
        } else {
            rwlock->owners.emplace(thread, 1);
        }

        rwlock->state = is_write ? RWLockState::WriteLocked : RWLockState::ReadLocked;

        return SCE_KERNEL_OK;
    } else if (!is_recursive && rwlock->owners.contains(thread)) {
        return RET_ERROR(SCE_KERNEL_ERROR_RW_LOCK_RECURSIVE);
    } else {
        // we need to wait
        const Deadline deadline = deadline_from(timeout);
        const WaitResult r = rwlock->waiters.wait(rwlock_lock, thread, { SCE_KERNEL_WAITTYPE_RW_LOCK, lock_id }, { is_write }, deadline, callbacks);
        writeback_timeout(timeout, deadline);
        return guest_result(r);
    }
}

SceInt32 rwlock_unlock(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID lock_id, bool is_write) {
    const ThreadStatePtr current_thread = kernel.get_thread(thread_id);
    if (!current_thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
    const RWLockPtr rwlock = lock_and_find(lock_id, kernel.rwlocks, kernel.mutex);

    if (!rwlock)
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_RW_LOCK_ID);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} waiting_threads: {}",
            export_name, lock_id, thread_id, rwlock->name, rwlock->attr,
            rwlock->waiters.size());
    }

    auto rwlock_lock = rwlock->lock();
    if (!rwlock_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_RW_LOCK_ID;

    auto it = rwlock->owners.find(current_thread);
    if (it == rwlock->owners.end()) {
        return RET_ERROR(SCE_KERNEL_ERROR_RW_LOCK_FAILED_TO_UNLOCK);
    }

    // decrease the lock count
    it->second--;
    if (it->second == 0)
        rwlock->owners.erase(current_thread);

    // if it is still locked
    if (!rwlock->owners.empty())
        return SCE_KERNEL_OK;

    rwlock->state = RWLockState::Unlocked;

    bool woke_writer = false;
    rwlock->waiters.wake_if([&](auto &waiter) {
        const bool waiting_is_write = waiter.entry.is_write;

        if (woke_writer || (rwlock->state == RWLockState::ReadLocked && waiting_is_write)) {
            // only awaken read threads
            return false;
        }

        rwlock->owners.emplace(waiter.thread, 1);

        if (waiting_is_write) {
            rwlock->state = RWLockState::WriteLocked;
            woke_writer = true;
        } else {
            rwlock->state = RWLockState::ReadLocked;
        }
        return true;
    });

    return SCE_KERNEL_OK;
}

SceInt32 rwlock_delete(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID lock_id) {
    return remove_sync_object(kernel.rwlocks, kernel.mutex, lock_id) ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_UNKNOWN_RW_LOCK_ID;
}

// **************
// * Semaphore *
// **************

SceUID semaphore_create(KernelState &kernel, const char *export_name, const char *name, SceUID thread_id, SceUInt attr, int init_val, int max_val) {
    if (!name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }

    const SemaphorePtr semaphore = std::make_shared<Semaphore>(attr);
    const SceUID uid = kernel.get_next_uid();
    semaphore->uid = uid;
    semaphore->init_val = init_val;
    semaphore->val = init_val;
    semaphore->max = max_val;
    semaphore->attr = attr;
    strncpy(semaphore->name, name, KERNELOBJECT_MAX_NAME_LENGTH);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} init_val: {} max_val: {}",
            export_name, uid, thread_id, name, attr, init_val, max_val);
    }

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    kernel.semaphores.emplace(uid, semaphore);

    return uid;
}

SceUID semaphore_find(KernelState &kernel, const char *export_name, const char *pName) {
    if (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);

    if (LOG_SYNC_PRIMITIVES)
        LOG_DEBUG("{}: name: \"{}\"", export_name, pName);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);

    const auto it = std::find_if(kernel.semaphores.begin(), kernel.semaphores.end(), [=](const auto &sema) {
        return strncmp(sema.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != kernel.semaphores.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

SceInt32 semaphore_wait(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID semaId, SceInt32 needCount, SceUInt32 *pTimeout, bool callbacks) {
    assert(semaId >= 0);

    // TODO Don't lock twice.
    const SemaphorePtr semaphore = lock_and_find(semaId, kernel.semaphores, kernel.mutex);
    if (!semaphore) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} val: {} timeout: {} waiting_threads: {}",
            export_name, semaphore->uid, thread_id, semaphore->name, semaphore->attr, semaphore->val,
            pTimeout ? *pTimeout : 0, semaphore->waiters.size());
    }

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    thread->set_wait_reason("sema", semaId, needCount);
    auto semaphore_lock = semaphore->lock();
    if (!semaphore_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID;

    if (semaphore->val < needCount) {
        const Deadline deadline = deadline_from(pTimeout);
        const WaitResult r = semaphore->waiters.wait(semaphore_lock, thread, { SCE_KERNEL_WAITTYPE_SEMAPHORE, semaId }, { needCount }, deadline, callbacks);
        writeback_timeout(pTimeout, deadline);
        return guest_result(r);
    } else {
        semaphore->val -= needCount;
    }

    return SCE_KERNEL_OK;
}

int semaphore_signal(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID semaid, int signal) {
    assert(semaid >= 0);

    // TODO Don't lock twice.
    const SemaphorePtr semaphore = lock_and_find(semaid, kernel.semaphores, kernel.mutex);
    if (!semaphore) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} val: {} signal: {} waiting_threads: {}",
            export_name, semaphore->uid, thread_id, semaphore->name, semaphore->attr, semaphore->val, signal,
            semaphore->waiters.size());
    }

    auto semaphore_lock = semaphore->lock();
    if (!semaphore_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID;

    if (semaphore->val + signal > semaphore->max) {
        return RET_ERROR(SCE_KERNEL_ERROR_SEMA_OVF);
    }
    semaphore->val += signal;

    while (auto *waiter = semaphore->waiters.front()) {
        const auto waiting_signal_count = waiter->entry.need_count;

        if (semaphore->val < waiting_signal_count)
            break;

        semaphore->val -= waiting_signal_count;
        semaphore->waiters.wake(*waiter);
    }

    return SCE_KERNEL_OK;
}

int semaphore_delete(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID semaid) {
    return remove_sync_object(kernel.semaphores, kernel.mutex, semaid) ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID;
}

int semaphore_cancel(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID semaid, SceInt32 setCount, SceUInt32 *pNumWaitThreads) {
    assert(semaid >= 0);

    // TODO: Don't lock twice
    const SemaphorePtr semaphore = lock_and_find(semaid, kernel.semaphores, kernel.mutex);
    if (!semaphore) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} val: {} waiting_threads: {}",
            export_name, semaphore->uid, thread_id, semaphore->name, semaphore->attr, semaphore->val,
            semaphore->waiters.size());
    }

    auto semaphore_lock = semaphore->lock();
    if (!semaphore_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID;
    if (setCount > semaphore->max) {
        return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
    }
    const auto nb_threads = static_cast<SceUInt32>(semaphore->waiters.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));

    if (setCount < 0) {
        semaphore->val = semaphore->init_val;
    } else {
        semaphore->val = setCount;
    }
    if (pNumWaitThreads)
        *pNumWaitThreads = nb_threads;
    return SCE_KERNEL_OK;
}

// **********************
// * Condition Variable *
// **********************

SceUID condvar_create(KernelState &kernel, MemState &mem, const char *export_name, const char *name, SceUID thread_id, SceUInt attr, SceUID assoc_mutexid, Ptr<SceKernelLwCondWork> workarea, SyncWeight weight) {
    if (!name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }
    MutexPtr assoc_mutex = find_mutex(kernel, assoc_mutexid, weight);
    if (!assoc_mutex)
        return unknown_mutex_id(export_name, weight);

    const SceUID uid = kernel.get_next_uid();

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} assoc_mutexid: {}",
            export_name, uid, thread_id, name, attr, assoc_mutexid);
    }

    const CondvarPtr condvar = std::make_shared<Condvar>(attr);
    condvar->uid = uid;
    condvar->attr = attr;
    condvar->associated_mutex = std::move(assoc_mutex);
    condvar->lightweight = weight == SyncWeight::Light;
    strncpy(condvar->name, name, KERNELOBJECT_MAX_NAME_LENGTH);

    if (weight == SyncWeight::Light)
        workarea.get(mem)->uid = uid;

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    auto &condvars = get_condvars(kernel, weight);
    condvars.emplace(uid, condvar);

    return uid;
}

int condvar_wait(KernelState &kernel, MemState &mem, const char *export_name, SceUID thread_id, SceUID condid, SceUInt *timeout, SyncWeight weight, bool callbacks) {
    assert(condid >= 0);

    const CondvarPtr condvar = find_condvar(kernel, condid, weight);
    if (!condvar)
        return unknown_cond_id(export_name, weight);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} name: \"{}\" attr: {} assoc_mutexid: {} timeout: {} waiting_threads: {}",
            export_name, condvar->uid, condvar->name, condvar->attr, condvar->associated_mutex->uid,
            timeout ? *timeout : 0, condvar->waiters.size());
    }

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    thread->set_wait_reason("cond", condvar->uid, condvar->associated_mutex ? condvar->associated_mutex->uid : 0);
    auto condition_variable_lock = condvar->lock();
    if (!condition_variable_lock)
        return unknown_cond_id(export_name, weight);

    if (auto error = mutex_unlock_impl(kernel, mem, export_name, thread_id, 1, condvar->associated_mutex, thread))
        return error;

    const Deadline deadline = deadline_from(timeout);
    const WaitResult r = condvar->waiters.wait(condition_variable_lock, thread, { weight == SyncWeight::Light ? SCE_KERNEL_WAITTYPE_LW_COND_SIGNAL : SCE_KERNEL_WAITTYPE_COND_SIGNAL, condid }, {}, deadline, callbacks);
    writeback_timeout(timeout, deadline);
    // A callback that exits the thread must not take back a mutex for it.
    if (!r || (*r != SCE_KERNEL_OK && *r != SCE_KERNEL_ERROR_WAIT_TIMEOUT))
        return guest_result(r);

    condition_variable_lock.unlock();
    // Preserve local mutex ownership after a condition-variable timeout.
    // Reacquisition is untimed, because the original deadline may have expired.
    const int lock_result = mutex_lock_impl(kernel, mem, export_name, thread_id, 1, condvar->associated_mutex, weight, nullptr, false, { weight == SyncWeight::Light ? SCE_KERNEL_WAITTYPE_LW_COND_LW_MUTEX : SCE_KERNEL_WAITTYPE_COND_MUTEX, condid }, callbacks, thread);
    if (lock_result == SCE_KERNEL_ERROR_WAIT_DELETE ||
        lock_result == SCE_KERNEL_ERROR_UNKNOWN_MUTEX_ID || lock_result == SCE_KERNEL_ERROR_UNKNOWN_LW_MUTEX_ID)
        return weight == SyncWeight::Light ? SCE_KERNEL_ERROR_WAIT_DELETE_LW_MUTEX : SCE_KERNEL_ERROR_WAIT_DELETE_MUTEX;
    if (lock_result == SCE_KERNEL_ERROR_WAIT_CANCEL)
        return SCE_KERNEL_ERROR_WAIT_CANCEL_MUTEX;
    return *r == SCE_KERNEL_OK ? lock_result : *r;
}

int condvar_signal(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID condid, Condvar::SignalTarget signal_target, SyncWeight weight) {
    assert(condid >= 0);

    const CondvarPtr condvar = find_condvar(kernel, condid, weight);
    if (!condvar)
        return unknown_cond_id(export_name, weight);

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} name: \"{}\" attr: {} assoc_mutexid: {} waiting_threads: {}",
            export_name, condvar->uid, condvar->name, condvar->attr, condvar->associated_mutex->uid,
            condvar->waiters.size());
    }

    const auto target_type = signal_target.type;

    auto condvar_lock = condvar->lock();
    if (!condvar_lock)
        return unknown_cond_id(export_name, weight);

    if (target_type == Condvar::SignalTarget::Type::Specific) {
        // Search for specified waiting thread
        auto *waiter = condvar->waiters.find_if([&](auto &w) { return w.thread->id == signal_target.thread_id; });
        if (waiter) {
            condvar->waiters.wake(*waiter);
        } else {
            LOG_ERROR("[SYNCLOST] {}: SignalCondTo target {} is NOT waiting on cv {}", export_name, signal_target.thread_id, condid);
            return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
        }
    } else {
        condvar->waiters.wake_all();
    }

    return SCE_KERNEL_OK;
}

int condvar_delete(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID condid, SyncWeight weight) {
    return remove_sync_object(get_condvars(kernel, weight), kernel.mutex, condid) ? SCE_KERNEL_OK : unknown_cond_id(export_name, weight);
}

// **************
// * Event Flag *
// **************

SceUID eventflag_clear(KernelState &kernel, const char *export_name, SceUID evfId, SceUInt32 bitPattern) {
    const EventFlagPtr event = lock_and_find(evfId, kernel.eventflags, kernel.mutex);
    if (!event) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVF_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} bitPattern: {:#b}",
            export_name, evfId, bitPattern);
    }

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVF_ID;

    event->flags &= bitPattern;
    evf_record(evfId, 0, 1, bitPattern, event->flags, 0);

    return SCE_KERNEL_OK;
}

SceUID eventflag_create(KernelState &kernel, const char *export_name, SceUID thread_id, const char *pName, SceUInt32 attr, SceUInt32 initPattern) {
    if (!pName)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if (((attr & 0x80) == 0x80) && (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }

    const SceUID uid = kernel.get_next_uid();

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} bitPattern: {:#b}",
            export_name, uid, thread_id, pName, attr, initPattern);
    }

    const EventFlagPtr event = std::make_shared<EventFlag>(attr);
    event->uid = uid;
    event->flags = initPattern;
    strncpy(event->name, pName, KERNELOBJECT_MAX_NAME_LENGTH);
    event->attr = attr;
    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    kernel.eventflags.emplace(uid, event);

    return uid;
}

SceUID eventflag_find(KernelState &kernel, const char *export_name, const char *pName) {
    if (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);

    if (LOG_SYNC_PRIMITIVES)
        LOG_DEBUG("{}: name: \"{}\"", export_name, pName);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);

    const auto it = std::find_if(kernel.eventflags.begin(), kernel.eventflags.end(), [=](const auto &evf) {
        return strncmp(evf.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != kernel.eventflags.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

static int eventflag_waitorpoll(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, unsigned int flags, unsigned int wait, unsigned int *outBits, SceUInt *timeout, bool dowait, bool callbacks) {
    assert(event_id >= 0);

    // TODO Don't lock twice.
    const EventFlagPtr event = lock_and_find(event_id, kernel.eventflags, kernel.mutex);
    if (!event) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVF_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} existing_flags: {:#b} wait_flags: {:#b} timeout: {}"
                  " waiting_threads: {}",
            export_name, event->uid, thread_id, event->name, event->attr, event->flags, flags, timeout ? *timeout : 0,
            event->waiters.size());
    }

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVF_ID;

    if ((event->attr & 0x1000) == 0 && !event->waiters.empty()) {
        return RET_ERROR(SCE_KERNEL_ERROR_EVF_MULTI);
    }

    bool condition;
    if (wait & SCE_EVENT_WAITOR) {
        condition = event->flags & flags;
    } else {
        condition = (event->flags & flags) == flags;
    }

    if (outBits) {
        *outBits = event->flags;
    }

    if (condition) {
        if (wait & SCE_EVENT_WAITCLEAR) {
            event->flags = 0;
        }

        if (wait & SCE_EVENT_WAITCLEAR_PAT) {
            event->flags &= ~flags;
        }
        evf_record(event_id, thread_id, 2, flags, event->flags, 0);

        return SCE_KERNEL_OK;
    } else if (dowait) {
        evf_record(event_id, thread_id, 3, flags, event->flags, static_cast<uint32_t>(wait));
        thread->set_wait_reason("evf", event->uid, flags);
        const Deadline deadline = deadline_from(timeout);
        const WaitResult r = event->waiters.wait(event_lock, thread, { SCE_KERNEL_WAITTYPE_EVENTFLAG, event_id }, { wait, flags, outBits }, deadline, callbacks);
        writeback_timeout(timeout, deadline);
        const SceInt32 err = guest_result(r);
        if ((err == SCE_KERNEL_ERROR_WAIT_TIMEOUT || err == SCE_KERNEL_ERROR_WAIT_DELETE) && outBits) {
            // set it only if a timeout occurs
            // otherwise set in eventflag_set or eventflag_cancel
            *outBits = event->flags;
        }

        return err;
    } else {
        return SCE_KERNEL_ERROR_EVF_COND;
    }
}

SceInt32 eventflag_wait(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID evfId, SceUInt32 bitPattern, SceUInt32 waitMode, SceUInt32 *pResultPat, SceUInt32 *pTimeout, bool callbacks) {
    return eventflag_waitorpoll(kernel, export_name, thread_id, evfId, bitPattern, waitMode, pResultPat, pTimeout, true, callbacks);
}

int eventflag_poll(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, unsigned int flags, unsigned int wait, unsigned int *outBits) {
    return eventflag_waitorpoll(kernel, export_name, thread_id, event_id, flags, wait, outBits, 0, false, false);
}

int KernelState::try_break_provable_evf_cycle(bool dry_run) {
    struct FlagInfo {
        EventFlagPtr event;
        std::set<SceUID> waiter_tids;
        uint32_t wanted_union = 0;
    };
    std::unordered_map<SceUID, FlagInfo> flags;
    std::unordered_map<SceUID, SceUID> thread_waits_on;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (const auto &[uid, event] : eventflags) {
            auto evlock = event->lock();
            if (!evlock || event->waiters.empty())
                continue;
            FlagInfo fi;
            fi.event = event;
            event->waiters.for_each([&](const auto &w) {
                if (!w.thread)
                    return;
                fi.waiter_tids.insert(w.thread->id);
                fi.wanted_union |= w.entry.pattern;
                thread_waits_on[w.thread->id] = uid;
            });
            flags.emplace(uid, std::move(fi));
        }
    }
    if (flags.empty())
        return 0;

    std::unordered_map<SceUID, std::set<SceUID>> setters_snapshot;
    {
        const std::lock_guard<std::mutex> lock(evf_setters_mutex);
        setters_snapshot = evf_setters;
    }

    int broken = 0;
    for (auto &[uid, fi] : flags) {
        const auto st = setters_snapshot.find(uid);
        if (st == setters_snapshot.end() || st->second.empty())
            continue; // nobody ever set it ?!
        bool all_setters_blocked_here = true;
        for (const SceUID setter : st->second) {
            const auto w = thread_waits_on.find(setter);
            if (w == thread_waits_on.end() || !flags.count(w->second)) {
                all_setters_blocked_here = false;
                break;
            }
        }
        if (!all_setters_blocked_here)
            continue;
        LOG_ERROR("[EVFCYCLE]{} flag {} '{}' looks PROVABLY dead: every historical setter is itself blocked on a flag with waiters - {} bits 0x{:X}",
            dry_run ? " (DRY-RUN)" : "", uid, fi.event->name, dry_run ? "would set" : "setting", fi.wanted_union);
        if (!dry_run)
            eventflag_set(*this, "provable_cycle_breaker", 0, uid, fi.wanted_union);
        broken++;
    }
    return broken;
}

void KernelState::log_eventflag_history() {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (const auto &[uid, event] : eventflags) {
            auto evlock = event->lock();
            if (!evlock || event->waiters.empty())
                continue;
            std::string waiters;
            event->waiters.for_each([&](const auto &w) {
                waiters += fmt::format(" [tid={} wants=0x{:X} mode=0x{:X}]", w.thread ? w.thread->id : -1, w.entry.pattern, w.entry.wait_mode);
            });
            LOG_ERROR("HANG EVF: flag {} '{}' current=0x{:X} waiters:{}", uid, event->name, event->flags, waiters);
        }
    }
    const uint64_t next = evf_ring_next.load(std::memory_order_relaxed);
    const uint64_t count = std::min<uint64_t>(next, EVF_RING_SIZE);
    static const char *op_names[] = { "SET", "CLEAR", "WAIT_OK", "WAIT_BLOCK", "CANCEL" };
    std::string hist;
    for (uint64_t k = next - count; k < next; k++) {
        const EvfOp &e = evf_ring[k % EVF_RING_SIZE];
        hist += fmt::format("{} ms={} evf={} tid={} bits=0x{:X} after=0x{:X} woken_or_mode={}\n",
            op_names[e.op <= 4 ? e.op : 4], e.ms, e.evf, e.thread, e.bits, e.flags_after, e.woken);
    }
    LOG_ERROR("HANG EVF HISTORY ({} op(s), oldest first):\n{}", count, hist);
}

int KernelState::try_break_frame_sync_deadlock(std::vector<SceUID> &already_nudged) {
    std::vector<std::pair<SceUID, SceUInt32>> nudges;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (auto &[uid, event] : eventflags) {
            if (std::find(already_nudged.begin(), already_nudged.end(), uid) != already_nudged.end())
                continue;
            auto ev_lock = event->lock();
            if (!ev_lock || event->waiters.empty())
                continue;
            bool any_satisfiable = false;
            SceUInt32 need = 0;
            int best_prio = 0x7fffffff;
            event->waiters.for_each([&](const auto &w) {
                const bool cond = (w.entry.wait_mode & SCE_EVENT_WAITOR)
                    ? ((event->flags & w.entry.pattern) != 0)
                    : ((event->flags & w.entry.pattern) == w.entry.pattern);
                if (cond)
                    any_satisfiable = true;
                if (w.priority < best_prio) {
                    best_prio = w.priority;
                    need = w.entry.pattern;
                }
            });
            if (!any_satisfiable && need != 0)
                nudges.emplace_back(uid, need);
        }
    }
    for (const auto &[uid, bits] : nudges) {
        LOG_ERROR("DEADLOCK BREAKER: event flag {} has blocked waiter(s) with no satisfiable condition; setting bits {:#x} to break a frame-sync deadlock (once per flag per stall)", uid, bits);
        already_nudged.push_back(uid);
        eventflag_set(*this, "deadlock_breaker", 0, uid, bits);
    }
    return static_cast<int>(nudges.size());
}

SceInt32 eventflag_set(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID evfId, SceUInt32 bitPattern) {
    assert(evfId >= 0);

    // TODO Don't lock twice.
    const EventFlagPtr event = lock_and_find(evfId, kernel.eventflags, kernel.mutex);
    if (!event) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVF_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} existing_flags: {:#b} set_flags: {:#b}"
                  " waiting_threads: {}",
            export_name, event->uid, thread_id, event->name, event->attr, event->flags, bitPattern,
            event->waiters.size());
    }

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVF_ID;
    event->flags |= bitPattern;
    const auto woken_count = event->waiters.wake_if([&](auto &waiter) {
        const EventFlag::WaitEntry &wait = waiter.entry;
        const SceUInt32 waiting_flags = wait.pattern;

        bool condition;
        if (wait.wait_mode & SCE_EVENT_WAITOR) {
            condition = event->flags & waiting_flags;
        } else {
            condition = (event->flags & waiting_flags) == waiting_flags;
        }

        if (!condition)
            return false;

        if (wait.out_bits) {
            *wait.out_bits = event->flags;
        }

        if (wait.wait_mode & SCE_EVENT_WAITCLEAR) {
            event->flags = 0;
        }

        if (wait.wait_mode & SCE_EVENT_WAITCLEAR_PAT) {
            event->flags &= ~waiting_flags;
        }

        return true;
    });
    evf_record(evfId, thread_id, 0, bitPattern, event->flags, static_cast<uint32_t>(woken_count));

    return 0;
}

SceInt32 eventflag_cancel(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id, SceUInt32 pattern, SceUInt32 *num_wait_threads) {
    assert(event_id >= 0);

    const EventFlagPtr event = lock_and_find(event_id, kernel.eventflags, kernel.mutex);
    if (!event) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_EVF_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {} existing_flags: {:#b} waiting_threads: {}",
            export_name, event->uid, thread_id, event->name, event->attr, event->flags, event->waiters.size());
    }

    auto event_lock = event->lock();
    if (!event_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_EVF_ID;

    const auto set_out_bits = [&](auto &waiter) {
        if (waiter.entry.out_bits)
            *waiter.entry.out_bits = pattern;
        return true;
    };
    const auto nb_threads = static_cast<SceUInt32>(event->waiters.wake_if(set_out_bits, SCE_KERNEL_ERROR_WAIT_CANCEL));

    event->flags = pattern;

    if (num_wait_threads)
        *num_wait_threads = nb_threads;

    return SCE_KERNEL_OK;
}

int eventflag_delete(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID event_id) {
    return remove_sync_object(kernel.eventflags, kernel.mutex, event_id) ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_UNKNOWN_EVF_ID;
}

// *************
// * Msg Pipe  *
// *************

SceUID msgpipe_create(KernelState &kernel, const char *export_name, const char *name, SceUID thread_id, SceUInt attr, SceSize bufSize) {
    if (!name)
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if ((strlen(name) > 31) && ((attr & 0x80) == 0x80)) {
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);
    }

    const SceUID uid = kernel.get_next_uid();

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" attr: {}",
            export_name, uid, thread_id, name, attr);
    }

    const MsgPipePtr msgpipe = std::make_shared<MsgPipe>(attr, bufSize);

    msgpipe->attr = attr;
    msgpipe->uid = uid;
    strncpy(msgpipe->name, name, KERNELOBJECT_MAX_NAME_LENGTH);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);
    kernel.msgpipes.emplace(uid, msgpipe);

    return uid;
}

SceUID msgpipe_find(KernelState &kernel, const char *export_name, const char *pName) {
    if (strlen(pName) > KERNELOBJECT_MAX_NAME_LENGTH)
        return RET_ERROR(SCE_KERNEL_ERROR_UID_NAME_TOO_LONG);

    if (LOG_SYNC_PRIMITIVES)
        LOG_DEBUG("{}: name: \"{}\"", export_name, pName);

    const std::lock_guard<std::mutex> kernel_lock(kernel.mutex);

    const auto it = std::find_if(kernel.msgpipes.begin(), kernel.msgpipes.end(), [=](const auto &msg_pipe) {
        return strncmp(msg_pipe.second->name, pName, KERNELOBJECT_MAX_NAME_LENGTH) == 0;
    });

    if (it != kernel.msgpipes.end())
        return it->first;

    return RET_ERROR(SCE_KERNEL_ERROR_UID_CANNOT_FIND_BY_NAME);
}

// Wakes the first waiter whose request now fits and that was not woken already, so it retries its transfer
static void wakeup_msgpipe_waiter(WaitQueue<MsgPipe::WaitEntry> &waiters, std::size_t available) {
    auto *waiter = waiters.find_if([&](auto &w) {
        return !w.entry.notified && w.entry.request_size <= available;
    });
    if (waiter) {
        waiter->entry.notified = true;
        WaitQueue<MsgPipe::WaitEntry>::notify(*waiter);
    }
}

SceSize msgpipe_recv(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID msgPipeId, SceUInt32 waitMode, void *pRecvBuf, SceSize recvSize, SceUInt32 *pTimeout, bool callbacks) {
    assert(msgPipeId >= 0);

    const bool ASAP = !(waitMode & SCE_KERNEL_MSG_PIPE_MODE_FULL);

    const MsgPipePtr msgpipe = lock_and_find(msgPipeId, kernel.msgpipes, kernel.mutex);
    if (!msgpipe) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_MSG_PIPE_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" pipe attr: {} wait_mode: {:#b} ({})"
                  " senders: {} receivers: {}",
            export_name, msgpipe->uid, thread_id, msgpipe->name, msgpipe->attr, waitMode, ASAP ? "ASAP" : "FULL",
            msgpipe->senders.size(), msgpipe->receivers.size());
    }

    if (recvSize > msgpipe->data_buffer.Capacity())
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_SIZE);

    const auto copyOut = [&] {
        if (waitMode & SCE_KERNEL_MSG_PIPE_MODE_DONT_REMOVE) {
            return msgpipe->data_buffer.Peek(pRecvBuf, recvSize);
        } else {
            return msgpipe->data_buffer.Remove(pRecvBuf, recvSize);
        }
    };

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // the thread is being torn down so fail its last import instead of crashing the process
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
    auto msgpipe_lock = msgpipe->lock();
    if (!msgpipe_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_MSG_PIPE_ID;

    const auto can_receive = [&] {
        const std::size_t availableSize = msgpipe->data_buffer.Used();
        return (availableSize >= recvSize) || (ASAP && availableSize >= 1);
    };

    if (!can_receive()) {
        if (waitMode & SCE_KERNEL_MSG_PIPE_MODE_DONT_WAIT)
            return 0;

        // sleep until we can read, if ASAP we can read as low as 1 byte
        const MsgPipe::WaitEntry entry{ .request_size = ASAP ? 1 : recvSize };
        const WaitResult r = msgpipe->receivers.wait_until_ready(msgpipe_lock, thread, { SCE_KERNEL_WAITTYPE_MSG_PIPE, msgPipeId }, entry, deadline_from(pTimeout), callbacks, [&](auto &waiter) {
            waiter.entry.notified = false;
            return can_receive();
        });
        if (!r || *r != SCE_KERNEL_OK)
            return guest_result(r);
    }

    const SceSize copied_size = (SceSize)copyOut();
    wakeup_msgpipe_waiter(msgpipe->senders, msgpipe->data_buffer.Free());
    return copied_size;
}

// FIXME this should be SendVector!
SceSize msgpipe_send(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID msgPipeId, SceUInt32 waitMode, const void *pSendBuf, SceSize sendSize, SceUInt32 *pTimeout, bool callbacks) {
    assert(msgPipeId >= 0);

    const bool ASAP = !(waitMode & SCE_KERNEL_MSG_PIPE_MODE_FULL);

    const MsgPipePtr msgpipe = lock_and_find(msgPipeId, kernel.msgpipes, kernel.mutex);
    if (!msgpipe) {
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_MSG_PIPE_ID);
    }

    if (LOG_SYNC_PRIMITIVES) {
        LOG_DEBUG("{}: uid: {} thread_id: {} name: \"{}\" pipe attr: {} wait_mode: {:#b}"
                  " senders: {} receivers: {}",
            export_name, msgpipe->uid, thread_id, msgpipe->name, msgpipe->attr, waitMode,
            msgpipe->senders.size(), msgpipe->receivers.size());
    }

    if (sendSize > msgpipe->data_buffer.Capacity())
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_SIZE);

    const ThreadStatePtr thread = kernel.get_thread(thread_id);
    if (!thread) // fail a final import during thread teardown instead of dereferencing null
        return RET_ERROR(SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID);
    auto msgpipe_lock = msgpipe->lock();
    if (!msgpipe_lock)
        return SCE_KERNEL_ERROR_UNKNOWN_MSG_PIPE_ID;

    // If ASAP and there's at least 1 free byte, or FULL and there's enough space, copy and return directly.
    const auto can_send = [&] {
        const std::size_t freeSize = msgpipe->data_buffer.Free();
        return (freeSize >= sendSize) || (ASAP && (freeSize >= 1));
    };

    if (!can_send()) {
        if (waitMode & SCE_KERNEL_MSG_PIPE_MODE_DONT_WAIT)
            return 0;

        // sleep until there's more space, if ASAP we can insert as low as 1 byte
        const MsgPipe::WaitEntry entry{ .request_size = ASAP ? 1 : sendSize };
        const WaitResult r = msgpipe->senders.wait_until_ready(msgpipe_lock, thread, { SCE_KERNEL_WAITTYPE_MSG_PIPE, msgPipeId }, entry, deadline_from(pTimeout), callbacks, [&](auto &waiter) {
            waiter.entry.notified = false;
            return can_send();
        });
        if (!r || *r != SCE_KERNEL_OK)
            return guest_result(r);
    }

    const SceSize copied_size = (SceSize)msgpipe->data_buffer.Insert(pSendBuf, sendSize);
    wakeup_msgpipe_waiter(msgpipe->receivers, msgpipe->data_buffer.Used());
    return copied_size;
}

SceInt32 msgpipe_delete(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID msgpipe_id) {
    return remove_sync_object(kernel.msgpipes, kernel.mutex, msgpipe_id) ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_UNKNOWN_MSG_PIPE_ID;
}

SceInt32 timer_delete(KernelState &kernel, const char *export_name, SceUID timer_id) {
    return remove_sync_object(kernel.timers, kernel.mutex, timer_id) ? SCE_KERNEL_OK : SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;
}

SceInt32 timer_cancel(KernelState &kernel, const char *export_name, SceUID timer_id, SceUInt32 *num_wait_threads) {
    const TimerPtr timer = lock_and_find(timer_id, kernel.timers, kernel.mutex);
    auto guard = timer ? timer->lock() : std::unique_lock<std::mutex>();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID;
    const auto count = static_cast<SceUInt32>(timer->waiters.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));
    timer->is_repeat = false;
    timer->is_pulse = false;
    timer->event_interval = 0;
    timer->next_event = std::numeric_limits<uint64_t>::max();
    if (num_wait_threads)
        *num_wait_threads = count;
    return SCE_KERNEL_OK;
}

SceInt32 simple_event_cancel(KernelState &kernel, const char *export_name, SceUID event_id, SceUInt32 *num_wait_threads) {
    const SimpleEventPtr event = lock_and_find(event_id, kernel.simple_events, kernel.mutex);
    if (!event) {
        const auto result = timer_cancel(kernel, export_name, event_id, num_wait_threads);
        return result == SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID ? SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID : result;
    }
    auto guard = event->lock();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID;
    const auto count = static_cast<SceUInt32>(event->waiters.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));
    if (num_wait_threads)
        *num_wait_threads = count;
    return SCE_KERNEL_OK;
}

SceInt32 mutex_cancel(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID mutex_id, SceInt32 new_count, SceUInt32 *num_wait_threads) {
    const auto thread = kernel.get_thread(thread_id);
    if (!thread)
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    const auto mutex = find_mutex(kernel, mutex_id, SyncWeight::Heavy);
    auto guard = mutex ? mutex->lock() : std::unique_lock<std::mutex>();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_MUTEX_ID;
    if (new_count == -1)
        new_count = mutex->init_count;
    if (new_count < 0 || (new_count > 1 && !(mutex->attr & SCE_KERNEL_MUTEX_ATTR_RECURSIVE)))
        return SCE_KERNEL_ERROR_ILLEGAL_COUNT;
    const auto count = static_cast<SceUInt32>(mutex->waiters.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));
    mutex->lock_count = new_count;
    mutex->owner = new_count > 0 ? thread : nullptr;
    if (num_wait_threads)
        *num_wait_threads = count;
    return SCE_KERNEL_OK;
}

SceInt32 rwlock_cancel(KernelState &kernel, const char *export_name, SceUID thread_id, SceUID rwlock_id, SceUInt32 *num_readers, SceUInt32 *num_writers, SceInt32 flag) {
    const auto thread = kernel.get_thread(thread_id);
    if (!thread)
        return SCE_KERNEL_ERROR_UNKNOWN_THREAD_ID;
    const auto rwlock = lock_and_find(rwlock_id, kernel.rwlocks, kernel.mutex);
    auto guard = rwlock ? rwlock->lock() : std::unique_lock<std::mutex>();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_RW_LOCK_ID;
    const auto writers = static_cast<SceUInt32>(rwlock->waiters.wake_if([](auto &waiter) { return waiter.entry.is_write; }, SCE_KERNEL_ERROR_WAIT_CANCEL));
    const auto readers = static_cast<SceUInt32>(rwlock->waiters.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));
    rwlock->owners.clear();
    if (flag & SCE_KERNEL_RW_LOCK_CANCEL_WITH_WRITE_LOCK) {
        rwlock->owners.emplace(thread, 1);
        rwlock->state = RWLockState::WriteLocked;
    } else {
        rwlock->state = RWLockState::Unlocked;
    }
    if (num_readers)
        *num_readers = readers;
    if (num_writers)
        *num_writers = writers;
    return SCE_KERNEL_OK;
}

SceInt32 msgpipe_cancel(KernelState &kernel, const char *export_name, SceUID msgpipe_id, SceUInt32 *num_senders, SceUInt32 *num_receivers) {
    const auto pipe = lock_and_find(msgpipe_id, kernel.msgpipes, kernel.mutex);
    auto guard = pipe ? pipe->lock() : std::unique_lock<std::mutex>();
    if (!guard)
        return SCE_KERNEL_ERROR_UNKNOWN_MSG_PIPE_ID;
    const auto senders = static_cast<SceUInt32>(pipe->senders.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));
    const auto receivers = static_cast<SceUInt32>(pipe->receivers.wake_all(SCE_KERNEL_ERROR_WAIT_CANCEL));
    pipe->data_buffer.Clear();
    if (num_senders)
        *num_senders = senders;
    if (num_receivers)
        *num_receivers = receivers;
    return SCE_KERNEL_OK;
}
