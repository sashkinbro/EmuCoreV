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

#include <cpu/common.h>
#include <kernel/callback.h>
#include <kernel/debugger.h>
#include <kernel/host_threads.h>
#include <kernel/object_store.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <kernel/types.h>
#include <mem/allocator.h>
#include <mem/block.h>
#include <mem/ptr.h>
#include <mem/util.h>
#include <rtc/rtc.h>
#include <util/containers.h>
#include <util/types.h>

#include <emuenv/app_launch_request.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

struct ThreadState;
struct MemState;

struct CodecEngineBlock;

struct KernelModule {
    SceKernelModuleInfo info;
    Ptr<const uint8_t> info_segment_address;
    uint32_t info_offset;
};
typedef std::shared_ptr<KernelModule> SceKernelModulePtr;

typedef std::shared_ptr<ThreadState> ThreadStatePtr;
typedef std::map<SceUID, CodecEngineBlock> CodecEngineBlocks;
typedef std::map<SceUID, Ptr<Ptr<void>>> SlotToAddress;
typedef std::map<SceUID, ThreadStatePtr> ThreadStatePtrs;
typedef std::map<SceUID, SceKernelModulePtr> SceKernelModuleInfoPtrs;
typedef std::map<SceUID, CallbackPtr> CallbackPtrs;
typedef unordered_map_fast<uint32_t, Address> ExportNids;
typedef unordered_map_fast<uint64_t, Address> LibExportNids;
// The plain NID entry outlives taiHEN and HLE redirects, so its owner is tracked apart from its address.
typedef unordered_map_fast<uint32_t, uint32_t> ExportNidOwners;
constexpr uint64_t lib_export_key(uint32_t library_nid, uint32_t nid) {
    return (static_cast<uint64_t>(library_nid) << 32) | nid;
}

typedef std::map<Address, uint32_t> NotFoundVars;
typedef std::function<void(CPUState &cpu, uint32_t nid, SceUID thread_id)> CallImportFunc;

struct CodecEngineBlock {
    uint32_t size;
    int32_t vaddr;
};

using LoadedSysmodules = std::map<SceSysmoduleModuleId, std::vector<SceUID>>;
using LoadedInternalSysmodules = std::vector<SceSysmoduleInternalModuleId>;

struct CorenumAllocator {
    BitmapAllocator alloc;
    std::mutex lock;

    void set_max_core_count(const std::size_t max);

    int new_corenum();
    void free_corenum(const int num);
};

struct VarBindingInfo {
    void *entries;
    uint32_t size;
    uint32_t module_nid;
};

struct FuncBindingInfo {
    Address entry_address;
    uint32_t library_nid;
};

typedef std::multimap<uint32_t, VarBindingInfo> VarBindingInfos;
typedef std::multimap<uint32_t, FuncBindingInfo> FuncBindingInfos;

typedef std::map<uint32_t, uint32_t> ModuleUidByNid;

struct KernelState {
    KernelState();

    std::mutex mutex;
    HostThreadRegistry host_threads;
    const uint64_t sync_cache_identity;
    std::atomic<uint64_t> sync_cache_generation{ 0 };
    CodecEngineBlocks codec_blocks;

    bool accurate_thread_scheduling = false;

    Ptr<const void> tls_address = Ptr<const void>(0);
    unsigned int tls_psize = 0;
    unsigned int tls_msize = 0;

    Ptr<const void> thread_event_start = Ptr<const void>(0);
    Address thread_event_start_arg = 0;
    Ptr<const void> thread_event_end = Ptr<const void>(0);
    Address thread_event_end_arg = 0;

    SimpleEventPtrs simple_events;
    TimerPtrs timers;
    SemaphorePtrs semaphores;
    CondvarPtrs condvars;
    CondvarPtrs lwcondvars;
    MutexPtrs mutexes;
    MutexPtrs lwmutexes; // also Mutexes for now
    RWLockPtrs rwlocks;
    EventFlagPtrs eventflags;
    MsgPipePtrs msgpipes;
    struct WaitResumeHandler {
        std::function<bool(ThreadState &, const std::shared_ptr<WaitContinuation> &)> restore_queue;
        std::function<SceInt32(ThreadState &, const std::shared_ptr<WaitContinuation> &)> resume;
    };
    // Modules register typed host reconstruction/completion, never serialized closures.
    std::map<WaitOperation, WaitResumeHandler> wait_resume_handlers;
    // Register stable module completion tags at runtime, never serialize closures.
    std::map<uint32_t, std::function<void(ThreadState &, const CallbackContinuationSnapshot &)>> callback_resume_handlers;
    bool can_restore_callback_frame(const CallbackContinuationSnapshot &frame) const {
        return frame.purpose != CallbackPurpose::direct
            && (frame.purpose != CallbackPurpose::external || (frame.external_tag && callback_resume_handlers.contains(frame.external_tag)));
    }
    bool resume_external_callback(ThreadState &thread, const CallbackContinuationSnapshot &frame) {
        const auto it = callback_resume_handlers.find(frame.external_tag);
        if (it == callback_resume_handlers.end())
            return false;
        it->second(thread, frame);
        return true;
    }
    CallbackPtrs callbacks;
    struct SavedCallback {
        SceUID uid = 0;
        SceUID owner = 0;
        std::string name;
        Address function = 0;
        Address userdata = 0;
        Callback::Snapshot notification;
    };
    std::vector<SavedCallback> snapshot_callbacks;
    void restore_snapshot_callbacks();
    // Retained deleted objects are continuation-owned, not visible to UID APIs.
    std::map<SceUID, std::shared_ptr<SyncPrimitive>> snapshot_objects;
    // Exited lock owners retain guest allocation lifetime without a worker or UID entry.
    ThreadStatePtrs snapshot_thread_owners;

    ThreadStatePtrs threads;
    void *jni_env;
    void *jni_activity;

    SceKernelModuleInfoPtrs loaded_modules;
    LoadedSysmodules loaded_sysmodules;
    LoadedInternalSysmodules loaded_internal_sysmodules;

    // the variables in this block must be accessed by first locking export_nids_mutex
    std::mutex export_nids_mutex;
    ExportNids export_nids;
    LibExportNids export_nids_by_lib;
    ExportNidOwners export_nid_owners;
    FuncBindingInfos func_binding_infos;
    std::unordered_map<uint32_t, std::string> nid_libraries;
    VarBindingInfos var_binding_infos;
    ModuleUidByNid module_uid_by_nid;

    bool cpu_opt;
    CorenumAllocator corenum_allocator;
    CallImportFunc call_import;

    // Shared NOP+WFI sentinel used by the Dynarmic as the halt return address
    Block halt_instruction;
    Address halt_instruction_pc;

    ObjectStore obj_store;

    uint64_t start_tick;
    SceRtcTick base_tick;
    Ptr<SceProcessParam> process_param;
    Ptr<void> client_vtable = Ptr<void>(0);
    Ptr<Address> shellsvc_client = Ptr<Address>(0);
    Ptr<void> libc_dso_handle_main = Ptr<void>(0);

    Debugger debugger;

    // kubridge exception handlers (DABT=0, PABT=1, UNDEF=2)
    static constexpr int EXCEPTION_HANDLER_MAX = 3;
    std::atomic<Address> exception_handlers[EXCEPTION_HANDLER_MAX]{};
    std::condition_variable thread_deleted_cond;

    SceUID get_next_uid() {
        return next_uid++;
    }

    SceUID peek_next_uid() const {
        return next_uid.load();
    }

    void set_next_uid(SceUID value) {
        next_uid.store(value);
    }

    bool init(MemState &mem, const CallImportFunc &call_import, bool cpu_opt);
    void deinit(MemState &mem);
    void load_process_param(MemState &mem, Ptr<uint32_t> ptr);
    ThreadStatePtr create_thread(MemState &mem, const char *name, Ptr<const void> entry_point = Ptr<const void>(0));
    ThreadStatePtr create_thread(MemState &mem, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option);
    // Save-state support: recreate a thread from a snapshot without allocating its stack/TLS again.
    // With defer_start the thread is parked dormant until apply_private_snapshot() is called.
    ThreadStatePtr create_thread_from_snapshot(MemState &mem, const struct ThreadState::Snapshot &snapshot, bool defer_start = false);
    ThreadStatePtr create_retained_thread_from_snapshot(MemState &mem, const ThreadState::Snapshot &snapshot);

    ThreadStatePtr get_thread(SceUID thread_id);

    // Creates a callback owned by thread and returns its UID.
    SceUID create_callback(const ThreadStatePtr &thread, const char *name, Ptr<SceKernelCallbackFunction> func, Ptr<void> common);
    // Deletes the callback with this UID, so it never runs again. Returns false if there is none.
    bool delete_callback(SceUID id);
    Ptr<Ptr<void>> get_thread_tls_addr(MemState &mem, SceUID thread_id, int key);

    bool is_threads_paused() const { return session_pause_active.load(std::memory_order_acquire); }
    std::optional<ThreadStatus> session_paused_thread_status(SceUID id) {
        const std::lock_guard lock(mutex);
        const auto entry = paused_threads_status.find(id);
        return entry == paused_threads_status.end() ? std::nullopt : std::optional(entry->second);
    }
    void pause_threads();
    void resume_threads();
    // Save-state support: drop pause bookkeeping after guest threads were replaced.
    void clear_paused_threads_state();
    void reset_world_stop_state();

    // Ordinary creators cannot allocate guest RAM during a capture. The capture
    // owner may create restore shells, which inherit its world-stop gate.
    bool begin_thread_creation();
    void end_thread_creation();
    bool is_capture_active();
    struct CaptureClock {
        Deadline steady;
        uint64_t timer_us;
    };
    CaptureClock capture_clock();

    int stop_world(SceUID except_id, std::chrono::milliseconds budget);
    void resume_world();

    void log_thread_hang_dump();
    void log_eventflag_history();
    int try_break_provable_evf_cycle(bool dry_run = false);
    std::atomic<int64_t> last_world_stop_epoch_ms{ 0 };
    // Last resort recovery for a full deadlock (called by the hang watchdog)
    int try_break_frame_sync_deadlock(std::vector<SceUID> &already_nudged);

    std::atomic<uint64_t> thread_wake_counter{ 0 };

    // Kill all guest threads and block until they have exited. Must only be called from a host thread.
    void process_exit();
    std::function<void(int, std::optional<AppLaunchRequest>)> process_exit_callback;
    // Request process exit. Safe to call from a guest thread. Returns immediately.
    // The registered process_exit_callback is invoked to notify the host layer.
    void request_process_exit(int res, std::optional<AppLaunchRequest> relaunch = std::nullopt);

    void set_memory_watch(bool enabled);
    void invalidate_jit_cache(Address start, size_t length);
    SceKernelModuleInfo *find_module_by_addr(Address address);

private:
    std::condition_variable creation_changed;
    bool capture_active = false;
    std::optional<CaptureClock> captured_clock;
    std::thread::id capture_owner;
    size_t initializing_threads = 0;
    std::atomic<SceUID> next_uid{ 1 };
    std::map<SceUID, ThreadStatus> paused_threads_status;
    std::atomic<bool> session_pause_active{ false };
    std::vector<ThreadStatePtr> world_stopped_threads;
};
