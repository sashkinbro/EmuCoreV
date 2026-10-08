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

#include <cpu/state.h>
#include <kernel/callback.h>
#include <kernel/thread/wait_queue.h>
#include <kernel/types.h>
#include <mem/block.h>
#include <mem/ptr.h>

#include <chrono>
#include <condition_variable>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

struct CPUContext;

struct ThreadState;

extern thread_local ThreadState *g_tls_guest_thread;

void guest_sched_set_cores(int cores);
void guest_sched_release_for_block();
void guest_sched_forget_cpu(CPUState *cpu);
CPUState *guest_sched_token_cpu();
struct ThreadParams;
struct KernelState;
struct SyncPrimitive;

typedef std::unique_ptr<CPUState, std::function<void(CPUState *)>> CPUStatePtr;
typedef std::function<void(CPUState &, uint32_t, SceUID)> CallImport;
typedef std::function<std::string(Address)> ResolveNIDName;

// Values are what sceKernelGetThreadInfo reports
enum class ThreadStatus : SceUInt32 {
    running = SCE_KERNEL_THREAD_STATUS_RUNNING,
    waiting = SCE_KERNEL_THREAD_STATUS_WAITING, // Waiting to be awaken by sync object or operation
    dormant = SCE_KERNEL_THREAD_STATUS_DORMANT, // Waiting for a job
    suspended = SCE_KERNEL_THREAD_STATUS_SUSPENDED, // Suspended by debugger
};

struct ThreadState {
    mutable std::mutex mutex;
    std::string name;
    SceUID id;

    uint32_t last_import_nid = 0;
    uint32_t last_import_lr = 0;
    CPUContext last_import_context;
    const char *wait_prim_kind = nullptr;
    SceUID wait_prim_uid = 0;
    uint32_t wait_extra = 0;
    void set_wait_reason(const char *kind, SceUID uid, uint32_t extra) {
        wait_prim_kind = kind;
        wait_prim_uid = uid;
        wait_extra = extra;
    }
    Address entry_point = 0;

    Block stack;
    int stack_size = 0;
    Block tls;

    int priority = SCE_KERNEL_DEFAULT_PRIORITY;
    SceInt32 affinity_mask = SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT;
    uint64_t start_tick = 0;
    uint64_t last_vblank_waited = 0;

    CPUStatePtr cpu;
    ThreadStatus status = ThreadStatus::dormant;
    // What the thread waits on while waiting, empty otherwise
    WaitTarget wait_target;

    std::condition_variable status_cond;
    uint32_t returned_value = 0;

    ThreadState() = delete;
    explicit ThreadState(SceUID id, KernelState &kernel, MemState &mem);
    ~ThreadState();

    int init(const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option);
    int start(SceSize arglen, const Ptr<void> argp, bool run_entry_callback = false);
    void exit(SceInt32 status);
    void exit_delete(bool exit = true);

    void update_status(ThreadStatus status, std::optional<ThreadStatus> expected = std::nullopt);
    bool is_delete_requested() const { return delete_requested; }
    Address stack_top() const;

    void run_loop();

    // this function must be called from the thread itself (inside a svc call)
    uint32_t run_callback(Address callback_address, const std::vector<uint32_t> &args, CallbackPurpose purpose = CallbackPurpose::direct, std::array<uint32_t, 4> completion = {}, uint32_t external_tag = 0);

    // this function is called from another thread when this one is dormant
    // it is only used for module loading and gxm display queue right now
    // args and argp are passed to thread->start as is
    uint32_t run_guest_function(Address callback_address, SceSize args = 0, const Ptr<void> argp = Ptr<void>{});
    // Nonblocking host-side entry used by durable module workers. A frozen
    // thread stays untouched so the host worker can return to its own gate.
    bool try_start_guest_function(Address callback_address, SceSize args, Address &previous_entry_point);
    bool guest_function_finished(std::chrono::milliseconds budget);
    bool finish_guest_function(Address previous_entry_point);

    // Blocks this thread until the deadline passes.
    [[nodiscard]] WaitResult delay_until(Deadline deadline, bool callbacks);
    // Blocks this thread until a signal is sent to it.
    [[nodiscard]] WaitResult wait_for_signal(bool callbacks);
    // Sends a signal to this thread. Fails if the previous one was not consumed yet.
    SceInt32 send_signal();
    // Blocks waiter until this thread becomes dormant, then writes its exit status to exit_status.
    [[nodiscard]] WaitResult wait_for_thread_end(const ThreadStatePtr &waiter, SceInt32 *exit_status, bool callbacks, SceUInt32 *timeout = nullptr);

    // Waits on target until woken by wake(), the thread exits or is deleted, or the deadline passes.
    // With callbacks, it also returns after running callbacks that were notified meanwhile.
    // A stale wake or callbacks can end it early, so callers must recheck their condition.
    [[nodiscard]] WaitResult wait(WaitTarget target, Deadline deadline, bool callbacks);
    // Wakes this thread from wait().
    void wake();

    // Runs the notified callbacks of this thread and returns how many ran. Called by the thread itself.
    SceUInt32 process_callbacks();
    // Tells this thread that one of its callbacks was notified, so a wait with callbacks runs it.
    void notify_callbacks();
    // Adds a callback this thread created. Called by the thread itself.
    void add_callback(const CallbackPtr &cb);

    void suspend();
    void suspend_and_wait();
    void resume(bool step = false);
    ThreadStatus pause_for_session();
    // Session pause is independent of the debugger, VM and world freeze.
    void resume_after_session_pause();
    void resume_if_suspended();

    // Stop-the-world support: distinct from suspend()/vm_suspended so they cannot cancel each other
    void request_world_stop();
    void park_creation_for_capture();
    void resume_creation_after_capture();
    bool wait_world_stopped(std::chrono::steady_clock::time_point deadline);
    bool resume_from_world();

    std::string log_stack_traceback() const;

    std::shared_ptr<WaitContinuation> begin_wait_continuation(WaitOperation operation,
        std::array<uint32_t, 8> args, SceUInt32 *timeout, bool callbacks, Deadline deadline);
    void end_wait_continuation(const std::shared_ptr<WaitContinuation> &record);
    std::shared_ptr<WaitContinuation> current_wait_continuation();
    template <class Waiter>
    std::shared_ptr<Waiter> take_restored_waiter(WaitTarget target) {
        auto record = current_wait_continuation();
        if (!record)
            return {};
        const std::lock_guard guard(record->mutex);
        if (record->state.target.type != target.type || record->state.target.id != target.id)
            return {};
        return std::static_pointer_cast<Waiter>(std::exchange(record->restored_node, {}));
    }
    void clear_wait_continuations();
    std::shared_ptr<void> restoring_wait_object(SceUID uid);
    std::vector<std::shared_ptr<WaitContinuation>> saved_wait_continuations();
    std::vector<std::shared_ptr<SyncPrimitive>> saved_sync_objects();
    bool restore_wait_queues();
    void activate_restored_continuations();
    SceInt32 resume_wait_continuation(const std::shared_ptr<WaitContinuation> &record);
    bool has_restored_continuations() const { return restored_continuations_pending; }
    void resume_restored_continuations();
    template <class T>
    Address guest_address(T *pointer) const {
        return pointer ? Ptr<T>(pointer, mem).address() : 0;
    }

    // Plain-data view of this thread used by the save-state writer/reader.
    struct Snapshot {
        SceUID id = 0;
        bool registered = true;
        std::string name;
        Address entry_point = 0;
        Address stack_addr = 0;
        int stack_size = 0;
        Address tls_addr = 0;
        int priority = 0;
        SceInt32 affinity_mask = 0;
        uint64_t start_tick = 0;
        uint64_t last_vblank_waited = 0;
        ThreadStatus status = ThreadStatus::dormant;
        uint32_t returned_value = 0;
        CPUContext context;
        CPUContext init_context;

        bool wake_pending = false;
        bool callbacks_pending = false;
        bool debugger_suspended = false;
        SceUID callback_cursor = 0;
        bool signal_pending = false;
        bool exit_requested = false;
        bool delete_requested = false;
        bool vm_suspended = false;
        bool single_stepping = false;
        bool run_start_callback = false;
        bool run_end_callback = false;
        bool is_processing_callbacks = false;
        int call_level = 0;
        std::vector<SceUID> callback_uids;
        std::vector<WaitContinuationSnapshot> waits;
        std::vector<CallbackContinuationSnapshot> callback_frames;
    };

    Snapshot capture_snapshot() const;
    void apply_private_snapshot(const Snapshot &snapshot);

private:
    int start_locked(SceSize arglen, const Ptr<void> argp, bool run_entry_callback);
    // Whether the thread is exiting or being deleted. Called with mutex held.
    bool exiting() const { return exit_requested || delete_requested; }

    // With mutex held, park before resuming guest work while either freeze reason is active.
    bool wait_for_guest_resume(std::unique_lock<std::mutex> &lock);
    // mutex stays held from callback notification acquisition through context preparation.
    uint32_t run_callback_locked(std::unique_lock<std::mutex> &lock, Address address, const std::vector<uint32_t> &args, CallbackPurpose purpose = CallbackPurpose::direct, SceUID callback_uid = 0, std::array<uint32_t, 4> completion = {}, uint32_t external_tag = 0);
    SceUInt32 continue_callbacks(bool fresh);

    void push_arguments(const std::vector<uint32_t> &args);
    void dispatch_abort(CPUState &cpu);

    KernelState &kernel;

    CPUContext init_cpu_ctx;
    CPUContext retained_cpu_context;
    // sceKernelExitThread (or top-level guest function return): park at dormant, thread reusable via start() / run_guest_function().
    bool exit_requested = false;
    // sceKernelExitDeleteThread (or external kill): will return from top-level run_loop(), then host thread joins.
    bool delete_requested = false;
    // Set by suspend(), consumed in run_loop() to transition to ThreadStatus::suspended.
    bool suspend_requested = false;
    // Debugger suspension is independent of VM/world freezes and only resume() clears it.
    bool debugger_suspended = false;
    // Transient UI/background/save pause, deliberately absent from Snapshot.
    bool session_suspended = false;
    // Suspended by sceKernelSuspendThreadForVM
    bool vm_suspended = false;
    // Stop-the-world
    bool world_stop_requested = false;
    bool world_stopped = false;
    // A wait/callback is parked at the freeze gate, including overlapping VM/world freezes.
    bool freeze_waiting = false;
    // Single stepping mode.
    bool single_stepping = false;

    // Number of active run_loop frames. The top-level host thread keeps one
    // frame alive (run_loop()) while parked dormant; callbacks add nested frames.
    int call_level = 0;

    // when calling sceKernelStartThread
    bool run_start_callback = false;
    // when calling sceKernelExitThread or sceKernelExitDeleteThread
    bool run_end_callback = false;

    std::vector<std::shared_ptr<WaitContinuation>> wait_continuations;
    std::shared_ptr<WaitContinuation> restoring_wait;
    std::shared_ptr<WaitContinuation> creation_wait;
    bool restored_continuations_pending = false;
    uint64_t next_continuation_sequence = 1;
    std::vector<std::shared_ptr<CallbackContinuationSnapshot>> callback_frames;
    SceUID callback_cursor = 0;
    bool skip_callback_dispatch_once = false;
    MemState &mem;

    // A sceKernelSendSignal is pending for this thread.
    bool signal_pending = false;
    // Set by wake() and consumed by the next wait().
    bool wake_pending = false;
    // Set by notify_callbacks() and cleared when the callbacks run.
    bool callbacks_pending = false;
    // Set while the thread runs its callbacks. They don't nest.
    bool is_processing_callbacks = false;
    // Callbacks this thread created, in creation order. The kernel owns them. Only this thread touches the list.
    struct CallbackRef {
        SceUID uid;
        std::weak_ptr<Callback> weak;
        CallbackPtr lock() const { return weak.lock(); }
    };
    std::list<CallbackRef> callbacks;

    // Notified under mutex whenever a condition a wait may be blocked on changes.
    std::condition_variable wait_cv;

    struct EndWaitEntry {
        // Where to write the exit status, or null
        SceInt32 *exit_status;
    };

    // Guards end_waiters. Taken after mutex when both are needed.
    std::mutex end_waiters_mutex;
    // Threads blocked in sceKernelWaitThreadEnd on this one.
    WaitQueue<EndWaitEntry> end_waiters;

public:
    MemState &get_mem() { return mem; }
};

typedef std::shared_ptr<ThreadState> ThreadStatePtr;

// The scope follows the logical HLE operation, including a condvar's second
// mutex phase. It outlives queue membership and therefore preserves grants.
struct WaitContinuationScope {
    ThreadState &thread;
    std::shared_ptr<WaitContinuation> record;
    bool resuming;
    WaitContinuationScope(ThreadState &thread, WaitOperation operation, std::array<uint32_t, 8> args,
        SceUInt32 *timeout, bool callbacks, Deadline deadline)
        : thread(thread)
        , record(thread.begin_wait_continuation(operation, args, timeout, callbacks, deadline))
        , resuming(record->resuming) {}
    ~WaitContinuationScope() { thread.end_wait_continuation(record); }
    Deadline deadline() const { return record->deadline; }
};
