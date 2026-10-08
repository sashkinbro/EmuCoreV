#pragma once
// Only the external kernel registry boundary is replaced. ThreadState, Callback,
// WaitQueue and all waiting/callback servicing methods are production code.
#include <atomic>
#include <kernel/callback.h>
#include <kernel/sync_primitives.h>
#include <map>
#include <unordered_map>
inline std::atomic<uint64_t> test_kernel_identity{ 1 };
struct KernelState {
    const uint64_t sync_cache_identity = test_kernel_identity++;
    std::atomic<uint64_t> sync_cache_generation{ 0 };
    std::mutex mutex;
    SimpleEventPtrs simple_events;
    TimerPtrs timers;
    SemaphorePtrs semaphores;
    MutexPtrs mutexes, lwmutexes;
    CondvarPtrs condvars, lwcondvars;
    RWLockPtrs rwlocks;
    EventFlagPtrs eventflags;
    MsgPipePtrs msgpipes;
    std::map<SceUID, ThreadStatePtr> threads;
    std::atomic<SceUID> next_uid{ 100 };
    SceUID get_next_uid() { return next_uid++; }
    ThreadStatePtr get_thread(SceUID id) {
        std::lock_guard guard(mutex);
        const auto it = threads.find(id);
        return it == threads.end() ? nullptr : it->second;
    }
    int try_break_provable_evf_cycle(bool dry_run = false);
    void log_eventflag_history();
    int try_break_frame_sync_deadlock(std::vector<SceUID> &);
    Address halt_instruction_pc = 0x100;
    std::atomic<uint64_t> thread_wake_counter{ 0 };
    std::map<SceUID, CallbackPtr> callbacks;
    bool delete_callback(SceUID id) {
        auto it = callbacks.find(id);
        if (it == callbacks.end())
            return false;
        it->second->mark_deleted();
        callbacks.erase(it);
        return true;
    }
};
