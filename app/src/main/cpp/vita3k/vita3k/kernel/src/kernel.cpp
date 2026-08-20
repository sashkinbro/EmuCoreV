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

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

#include <cpu/common.h>
#include <kernel/state.h>
#include <mem/functions.h>

#include <kernel/thread/thread_state.h>

#include <cpu/functions.h>
#include <mem/ptr.h>
#include <util/lock_and_find.h>
#include <util/log.h>

#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <chrono>
#include <fstream>
#include <iomanip>

int CorenumAllocator::new_corenum() {
    const std::lock_guard<std::mutex> guard(lock);

    uint32_t size = 1;
    return alloc.allocate_from(0, size);
}

void CorenumAllocator::free_corenum(const int num) {
    const std::lock_guard<std::mutex> guard(lock);
    alloc.free(num, 1);
}

void CorenumAllocator::set_max_core_count(const std::size_t max) {
    const std::lock_guard<std::mutex> guard(lock);
    alloc.set_maximum(max);
}

void set_current_thread_state(SceUID id, const ThreadStatePtr &thread);
void clear_current_thread_state();

// TODO implement cross platform debug thread name setter and eliminate SDL thread
struct ThreadParams {
    KernelState *kernel = nullptr;
    SceUID thid = SCE_KERNEL_ERROR_ILLEGAL_THREAD_ID;
    SDL_Semaphore *host_may_destroy_params = nullptr;
};

static int SDLCALL thread_function(void *data) {
    assert(data != nullptr);
    const ThreadParams params = *static_cast<const ThreadParams *>(data);
    SDL_SignalSemaphore(params.host_may_destroy_params);
    const ThreadStatePtr thread = params.kernel->get_thread(params.thid);
    set_current_thread_state(params.thid, thread);
#ifdef TRACY_ENABLE
    if (!thread->name.empty()) {
        tracy::SetThreadName(thread->name.c_str());
    } else {
        std::string th_name = "TID:" + std::to_string(thread->id);
        tracy::SetThreadName(th_name.c_str());
    }
#endif

    thread->run_loop();
    const uint32_t r0 = read_reg(*thread->cpu, 0);
    clear_current_thread_state();

    {
        std::lock_guard<std::mutex> lock(params.kernel->mutex);
        params.kernel->threads.erase(thread->id);
        params.kernel->corenum_allocator.free_corenum(get_processor_id(*thread->cpu));
        params.kernel->thread_deleted_cond.notify_all();
    }

    return r0;
}

KernelState::KernelState()
    : debugger(*this) {
}

bool KernelState::init(MemState &mem, const CallImportFunc &call_import, bool cpu_opt) {
    corenum_allocator.set_max_core_count(MAX_CORE_COUNT);

    start_tick = rtc_get_ticks(rtc_base_ticks());
    base_tick = { rtc_base_ticks() };
    this->call_import = call_import;
    this->cpu_opt = cpu_opt;

    // Generate halt instruction (NOP + WFI)
    halt_instruction = alloc_block(mem, 4, "halt_instruction");
    const auto halt_ptr = halt_instruction.get_ptr<uint16_t>().get(mem);
    halt_ptr[0] = 0xBF00; // NOP
    halt_ptr[1] = 0xBF30; // WFI
    halt_instruction_pc = halt_instruction.get() | 1; // thumb mode pc

    return true;
}

void KernelState::load_process_param(MemState &mem, Ptr<uint32_t> ptr) {
    const SceProcessParam *param = ptr.cast<SceProcessParam>().get(mem);
    if (param->version == 0) {
        // Homebrews built with old vitasdk
        process_param = nullptr;
        return;
    }
    process_param = ptr.cast<SceProcessParam>();
    // VAR_NID(__sce_libcparam, 0xDF084DFA)
    // no memory leak because we don't allocate memory for this variable intially
    export_nids[0xDF084DFA] = process_param.get(mem)->sce_libc_param.address();
}

void KernelState::set_memory_watch(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &thread : threads) {
        auto &cpu = *thread.second->cpu;
        if (enabled != get_log_mem(cpu)) {
            if (enabled)
                set_log_mem(cpu, true);
            else
                set_log_mem(cpu, false);
        }
    }
}

void KernelState::invalidate_jit_cache(Address start, size_t length) {
    std::lock_guard<std::mutex> lock(mutex);
    for (const auto &[_, thread] : threads) {
        ::invalidate_jit_cache(*thread->cpu, start, length);
    }
}

static thread_local SceUID tls_self_id = 0;
static thread_local ThreadStatePtr tls_self;

void set_current_thread_state(SceUID id, const ThreadStatePtr &thread) {
    tls_self_id = id;
    tls_self = thread;
}

void clear_current_thread_state() {
    tls_self_id = 0;
    tls_self.reset();
}

ThreadStatePtr KernelState::get_thread(SceUID thread_id) {
    if (thread_id == tls_self_id && tls_self)
        return tls_self;
    return lock_and_find(thread_id, threads, mutex);
}

ThreadStatePtr KernelState::create_thread(MemState &mem, const char *name, Ptr<const void> entry_point) {
    return create_thread(mem, name, entry_point, SCE_KERNEL_DEFAULT_PRIORITY, SCE_KERNEL_THREAD_CPU_AFFINITY_MASK_DEFAULT, SCE_KERNEL_STACK_SIZE_USER_MAIN, nullptr);
}

ThreadStatePtr KernelState::create_thread(MemState &mem, const char *name, Ptr<const void> entry_point, int init_priority, SceInt32 affinity_mask, int stack_size, const SceKernelThreadOptParam *option) {
    ThreadStatePtr thread = std::make_shared<ThreadState>(get_next_uid(), *this, mem);
    if (thread->init(name, entry_point, init_priority, affinity_mask, stack_size, option) < 0)
        return nullptr;

    {
        const std::lock_guard<std::mutex> lock(mutex);
        threads.emplace(thread->id, thread);
    }

    ThreadParams params;
    params.kernel = this;
    params.thid = thread->id;

    params.host_may_destroy_params = SDL_CreateSemaphore(0);
    SDL_DetachThread(SDL_CreateThread(&thread_function, thread->name.c_str(), &params));
    SDL_WaitSemaphore(params.host_may_destroy_params);
    SDL_DestroySemaphore(params.host_may_destroy_params);

    return thread;
}

Ptr<Ptr<void>> KernelState::get_thread_tls_addr(MemState &mem, SceUID thread_id, int key) {
    Ptr<Ptr<void>> address(0);
    // magic numbers taken from decompiled source. There is 0x400 unused bytes of unknown usage
    if (key <= 0x100 && key >= 0) {
        const ThreadStatePtr thread = get_thread(thread_id);
        address = thread->tls.get_ptr<Ptr<void>>() + key;
    } else {
        LOG_ERROR("Wrong tls slot index. TID:{} index:{}", thread_id, key);
    }
    return address;
}

void KernelState::request_process_exit(int res, std::optional<AppLaunchRequest> relaunch) {
    if (process_exit_callback)
        process_exit_callback(res, std::move(relaunch));
}

void KernelState::process_exit() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto &[_, timer] : timers)
            timer->condvar.notify_all();
        for (auto &[_, thread] : threads)
            thread->exit_delete(false);
    }

    std::unique_lock<std::mutex> lock(mutex);
    thread_deleted_cond.wait(lock, [this] { return threads.empty(); });
}

void KernelState::pause_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, thread] : threads) {
        paused_threads_status[thread->id] = thread->status;
        if (thread->status == ThreadStatus::run)
            thread->suspend();
    }
}

void KernelState::resume_threads() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, thread] : threads) {
        if (paused_threads_status[thread->id] == ThreadStatus::run)
            thread->resume();
    }
    paused_threads_status.clear();
}

int KernelState::stop_world(SceUID except_id, std::chrono::milliseconds budget) {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        world_stopped_threads.clear();
        world_stopped_threads.reserve(threads.size());
        for (auto &[tid, thread] : threads) {
            if (tid != except_id)
                world_stopped_threads.push_back(thread);
        }
    }

    // Phase 1: flag + halt everyone first (halts latch, so none is lost)...
    for (const auto &thread : world_stopped_threads)
        thread->request_world_stop();

    // Phase 2: ...then wait for each to be provably outside the JIT, on a shared deadline.
    const auto deadline = std::chrono::steady_clock::now() + budget;
    int not_parked = 0;
    for (const auto &thread : world_stopped_threads) {
        if (!thread->wait_world_stopped(deadline))
            ++not_parked;
    }
    return not_parked;
}

void KernelState::log_thread_hang_dump() {
    std::vector<ThreadStatePtr> snapshot;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (auto &[tid, t] : threads)
            snapshot.push_back(t);
    }

    std::string dump = fmt::format("HANG DUMP: {} guest thread(s)\n", snapshot.size());
    for (const auto &t : snapshot) {
        const ThreadStatus status = t->status;
        const char *status_str = (status == ThreadStatus::run) ? "run" : (status == ThreadStatus::wait) ? "wait"
            : (status == ThreadStatus::suspend)                                                         ? "suspend"
                                                                                                        : "dormant";
        std::string line;
        if (status == ThreadStatus::run) {
            line = fmt::format("thread {} ({}) status=run (executing or blocked inside an HLE import) last_import_nid=0x{:08X} import_lr=0x{:X}", t->name, t->id, t->last_import_nid, t->last_import_lr);
        } else {
            line = fmt::format("thread {} ({}) status={} PC=0x{:X} LR=0x{:X} last_import_nid=0x{:08X} import_lr=0x{:X} stack:\n{}", t->name, t->id, status_str, read_pc(*t->cpu), read_lr(*t->cpu), t->last_import_nid, t->last_import_lr, t->log_stack_traceback());
        }
        LOG_ERROR("HANG DUMP: {}", line);
        dump += line + "\n";
    }

    // vita3k.log is truncated on relaunch, so also persist the dump where a restart cannot eat it
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::ofstream hang_file("vita3k_hangdump.log", std::ios::app);
    if (hang_file)
        hang_file << "==== " << std::put_time(std::localtime(&now), "%Y-%m-%d %H:%M:%S") << " ====\n"
                  << dump << std::endl;
}

void KernelState::resume_world() {
    for (const auto &thread : world_stopped_threads)
        thread->resume_from_world();
    world_stopped_threads.clear();
}

void KernelState::deinit(MemState &mem) {
    process_exit();
    threads.clear();

    simple_events.clear();
    timers.clear();
    semaphores.clear();
    condvars.clear();
    lwcondvars.clear();
    mutexes.clear();
    lwmutexes.clear();
    rwlocks.clear();
    eventflags.clear();
    msgpipes.clear();
    callbacks.clear();

    loaded_modules.clear();
    loaded_sysmodules.clear();
    loaded_internal_sysmodules.clear();

    {
        std::lock_guard<std::mutex> lock(export_nids_mutex);
        export_nids.clear();
        func_binding_infos.clear();
        var_binding_infos.clear();
        module_uid_by_nid.clear();
    }

    corenum_allocator.alloc.reset();
    corenum_allocator.alloc.set_maximum(0);

    obj_store.clear();

    tls_address = Ptr<const void>(0);
    tls_psize = 0;
    tls_msize = 0;

    thread_event_start = Ptr<const void>(0);
    thread_event_start_arg = 0;
    thread_event_end = Ptr<const void>(0);
    thread_event_end_arg = 0;

    codec_blocks.clear();

    halt_instruction = nullptr;
    halt_instruction_pc = 0;

    process_param = nullptr;
    client_vtable = Ptr<void>(0);
    shellsvc_client = Ptr<Address>(0);
    libc_dso_handle_main = Ptr<void>(0);

    debugger.deinit();

    next_uid = 1;

    paused_threads_status.clear();
}

SceKernelModuleInfo *KernelState::find_module_by_addr(Address address) {
    const auto lock = std::lock_guard(mutex);
    for (auto &[_, mod] : loaded_modules) {
        for (auto &seg : mod->info.segments) {
            if (!seg.size)
                continue;
            if (seg.vaddr.address() <= address && address <= seg.vaddr.address() + seg.memsz) {
                return &mod->info;
            }
        }
    }
    return nullptr;
}
