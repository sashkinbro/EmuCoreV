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

#pragma once

#include <atomic>
#include <kernel/thread/wait.h>
#include <kernel/thread/wait_continuation.h>
#include <kernel/types.h>

#include <algorithm>
#include <concepts>
#include <list>
#include <memory>
#include <mutex>
#include <optional>

// Only declared here so ThreadState can hold a WaitQueue. The template bodies need the full type.
struct ThreadState;
using ThreadStatePtr = std::shared_ptr<ThreadState>;
std::shared_ptr<void> take_restored_wait_node(const ThreadStatePtr &thread, WaitTarget target);
std::shared_ptr<WaitContinuation> get_wait_continuation(const ThreadStatePtr &thread);

inline std::atomic<uint64_t> next_wait_sequence{ 1 };

// Threads blocked on a sync object, in FIFO or thread priority order.
// Entry is the per-waiter data the object needs, such as a requested count.
// Every method must be called with the object's lock held.
template <typename Entry>
class WaitQueue {
public:
    struct Waiter {
        ThreadStatePtr thread;
        // Taken when the wait starts, the queue is not reordered if it changes
        int priority = 0;
        Entry entry;
        // Set by a waker that decided the outcome of the wait
        std::optional<SceInt32> result;
        std::weak_ptr<WaitContinuation> continuation;
        uint64_t sequence = 0;
        void set_result(SceInt32 value) {
            result = value;
            if (auto record = continuation.lock()) {
                const std::lock_guard guard(record->mutex);
                record->state.has_result = true;
                record->state.result = value;
            }
        }
    };

    WaitQueue() = default;
    explicit WaitQueue(SceUInt32 attr)
        : by_priority(attr & SCE_KERNEL_ATTR_TH_PRIO) {}

    // Blocks thread on target until a waker gives it a result, ready() holds, it is being deleted, or the deadline passes.
    // thread must be the guest thread making this HLE call. ready() is rechecked each time it is woken.
    // With callbacks, the thread runs its notified callbacks while it waits.
    [[nodiscard]] WaitResult wait_until_ready(std::unique_lock<std::mutex> &lock, const ThreadStatePtr &thread, WaitTarget target, Entry entry, Deadline deadline, bool callbacks, std::predicate<Waiter &> auto ready) {
        auto restored = std::static_pointer_cast<Waiter>(take_restored_wait_node(thread, target));
        Waiter local{ .thread = thread, .entry = std::move(entry) };
        Waiter &waiter = restored ? *restored : local;
        if (!restored) {
            waiter.priority = waiter.thread->priority;
            waiter.continuation = get_wait_continuation(thread);
            push(waiter);
        }
        while (true) {
            if (waiter.result)
                break;
            lock.unlock();
            const WaitResult r = waiter.thread->wait(target, deadline, callbacks);
            lock.lock();
            // A waker's result wins, it already handed over what the waiter asked for
            if (waiter.result)
                break;
            if (!r) {
                remove(waiter);
                return r;
            }
            if (ready(waiter))
                break;
            if (*r == SCE_KERNEL_ERROR_WAIT_TIMEOUT) {
                remove(waiter);
                return r;
            }
        }
        remove(waiter);
        return waiter.result.value_or(SCE_KERNEL_OK);
    }

    // Blocks thread on target until a waker gives it a result, it is being deleted, or the deadline passes.
    // thread must be the guest thread making this HLE call.
    // With callbacks, the thread runs its notified callbacks while it waits.
    [[nodiscard]] WaitResult wait(std::unique_lock<std::mutex> &lock, const ThreadStatePtr &thread, WaitTarget target, Entry entry, Deadline deadline, bool callbacks) {
        return wait_until_ready(lock, thread, target, std::move(entry), deadline, callbacks, [](Waiter &) { return false; });
    }

    bool empty() const {
        return waiters.empty();
    }

    std::size_t size() const {
        return waiters.size();
    }

    // Visits queued waiters for diagnostics. The object's lock must be held.
    void for_each(std::invocable<const Waiter &> auto visit) const {
        for (const Waiter *waiter : waiters)
            visit(*waiter);
    }

    // Returns the waiter that would be woken first, or null.
    Waiter *front() {
        return waiters.empty() ? nullptr : waiters.front();
    }

    // Returns the first waiter accepted by pick, or null.
    Waiter *find_if(std::predicate<Waiter &> auto pick) {
        const auto it = std::find_if(waiters.begin(), waiters.end(), [&](Waiter *w) { return pick(*w); });
        return it == waiters.end() ? nullptr : *it;
    }

    // Ends a wait with result and takes the waiter off the queue.
    void wake(Waiter &waiter, SceInt32 result = SCE_KERNEL_OK) {
        waiter.set_result(result);
        remove(waiter);
        waiter.thread->wake();
    }

    // Ends the wait of every waiter accepted by pick, in queue order. Returns how many were woken.
    std::size_t wake_if(std::predicate<Waiter &> auto pick, SceInt32 result = SCE_KERNEL_OK) {
        std::size_t woken = 0;
        for (auto it = waiters.begin(); it != waiters.end();) {
            Waiter &waiter = **it;
            if (!pick(waiter)) {
                ++it;
                continue;
            }
            it = waiters.erase(it);
            mark_queued(waiter, false);
            waiter.set_result(result);
            waiter.thread->wake();
            ++woken;
        }
        return woken;
    }

    // Ends the wait of every waiter with result. Returns how many were woken.
    std::size_t wake_all(SceInt32 result = SCE_KERNEL_OK) {
        return wake_if([](Waiter &) { return true; }, result);
    }

    // Wakes a waiter so it rechecks its condition, leaving it queued.
    static void notify(Waiter &waiter) {
        waiter.thread->wake();
    }

    // Wakes every waiter so they recheck their condition, leaving them queued.
    void notify_all() {
        for (Waiter *waiter : waiters)
            notify(*waiter);
    }

    // Queues a waiter that the caller will block itself. Used by waits that need their own loop.
    void push(Waiter &waiter) {
        if (!waiter.sequence)
            waiter.sequence = next_wait_sequence.fetch_add(1, std::memory_order_relaxed);
        auto next = next_wait_sequence.load(std::memory_order_relaxed);
        while (next <= waiter.sequence && !next_wait_sequence.compare_exchange_weak(next, waiter.sequence + 1, std::memory_order_relaxed)) {
        }
        mark_queued(waiter, true);
        auto pos = waiters.end();
        if (by_priority) {
            // Lower value is higher priority, equal priorities keep FIFO order
            pos = std::find_if(waiters.begin(), waiters.end(), [&](const Waiter *w) {
                return w->priority > waiter.priority || (w->priority == waiter.priority && w->sequence > waiter.sequence);
            });
        } else {
            pos = std::find_if(waiters.begin(), waiters.end(), [&](const Waiter *w) { return w->sequence > waiter.sequence; });
        }
        waiters.insert(pos, &waiter);
    }

    // Takes a waiter off the queue if it is still on it.
    void remove(Waiter &waiter) {
        std::erase(waiters, &waiter);
        mark_queued(waiter, false);
    }

    // Install before any restored host worker is admitted to guest execution.
    void restore(const ThreadStatePtr &thread, Entry entry, const std::shared_ptr<WaitContinuation> &record) {
        auto waiter = std::make_shared<Waiter>();
        waiter->thread = thread;
        waiter->entry = std::move(entry);
        waiter->continuation = record;
        bool queued;
        {
            const std::lock_guard guard(record->mutex);
            waiter->priority = record->state.priority;
            waiter->sequence = record->state.sequence;
            if (record->state.has_result)
                waiter->result = record->state.result;
            queued = record->state.queued;
            record->restored_node = waiter;
            record->detach_restored_waiter = [this, weak = std::weak_ptr<Waiter>(waiter)] {
                if (auto node = weak.lock())
                    remove(*node);
            };
        }
        if (queued)
            push(*waiter);
    }

private:
    static void mark_queued(Waiter &waiter, bool queued) {
        if (auto record = waiter.continuation.lock()) {
            const std::lock_guard guard(record->mutex);
            record->state.queued = queued;
            record->state.priority = waiter.priority;
            record->state.sequence = waiter.sequence;
        }
    }
    bool by_priority = false;
    std::list<Waiter *> waiters;
};
