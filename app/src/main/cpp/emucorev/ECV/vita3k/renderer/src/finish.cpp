#include <renderer/finish.h>
#include <renderer/state.h>
#include <renderer/functions.h>
#include <renderer/command_snapshot.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <algorithm>
#include <limits>
#include <set>

namespace renderer {
namespace {
bool valid_snapshot(const FinishSnapshot &saved) {
    return saved.id && saved.id != UINT64_MAX && saved.owner >= 0 && saved.phase <= FinishPhase::Drain
        && (saved.phase != FinishPhase::Drain || saved.fence_done)
        && (!saved.drain_done || saved.phase == FinishPhase::Drain)
        && (saved.fence_done ? saved.result == 1 : saved.result == 0)
        && (saved.owner || (!saved.fence_done && saved.phase == FinishPhase::Fence));
}
std::shared_ptr<FinishOperation> lookup(State &state, uint64_t id) {
    const std::lock_guard lock(state.finish_operations_mutex);
    const auto it = state.finish_operations.find(id);
    return it == state.finish_operations.end() ? nullptr : it->second;
}
void wake(const std::weak_ptr<ThreadState> &waiter) {
    if (const auto thread = waiter.lock()) thread->wake();
}
void retire(State &state, const std::shared_ptr<FinishOperation> &operation, bool abandoned) {
    const std::lock_guard registry(state.finish_operations_mutex);
    const std::lock_guard lock(operation->mutex);
    operation->waiter.reset();
    if (abandoned && !operation->saved.fence_done) {
        operation->saved.owner = 0;
    } else {
        // A drain callback holds only a weak token; deleting the caller does
        // not leave a host stack pointer or a guest output behind.
        state.finish_operations.erase(operation->saved.id);
    }
}
bool await(State &state, ThreadState &thread, const std::shared_ptr<FinishOperation> &operation, bool drain) {
    while (true) {
        {
            const std::lock_guard lock(operation->mutex);
            if (operation->aborted || state.render_abort.load()) return false;
            if (drain ? operation->saved.drain_done : operation->saved.fence_done) return true;
        }
        if (!thread.wait({SCE_KERNEL_WAITTYPE_EVENT, 0}, Deadline::max(), false)) return false;
    }
}
}

SceInt32 finish_guest(State &state, ThreadState &thread, Context *context, Address context_address) {
    if (!state.kernel) return SCE_GXM_ERROR_DRIVER;
    const auto restoring = thread.current_wait_continuation();
    const bool resumed = restoring && restoring->resuming && restoring->state.operation == WaitOperation::renderer_finish;
    std::shared_ptr<FinishOperation> operation;
    if (resumed) {
        const auto &args = restoring->state.args;
        operation = lookup(state, uint64_t(args[0]) | (uint64_t(args[1]) << 32));
    } else {
        operation = std::make_shared<FinishOperation>();
        const std::lock_guard lock(state.finish_operations_mutex);
        if (!state.next_finish_id || state.next_finish_id == UINT64_MAX || state.finish_operations.size() >= 65536)
            return SCE_GXM_ERROR_DRIVER;
        operation->saved.id = state.next_finish_id++;
        operation->saved.owner = thread.id;
        operation->saved.context_address = context_address;
        state.finish_operations.emplace(operation->saved.id, operation);
    }
    if (!operation) return SCE_GXM_ERROR_DRIVER;
    const uint64_t id = operation->saved.id;
    WaitContinuationScope continuation(thread, WaitOperation::renderer_finish,
        {uint32_t(id), uint32_t(id >> 32), 1}, nullptr, false, Deadline::max());
    continuation.record->object = operation;
    const auto waiter = state.kernel->get_thread(thread.id);
    {
        const std::lock_guard lock(operation->mutex);
        operation->waiter = waiter;
    }
    if (!resumed) {
        auto *command = make_command(generic_command_allocate, generic_command_free, CommandOpcode::Nop, nullptr, int32_t(1));
        if (!command) { retire(state, operation, false); return SCE_GXM_ERROR_DRIVER; }
        command->flags |= Command::FLAG_FROM_HOST;
        command->completion_id = id;
        CommandList batch{command, command, context};
        submit_command_list(state, context, batch);
    }
    if (!await(state, thread, operation, false)) { retire(state, operation, true); return SCE_KERNEL_OK; }
    bool submit_drain = false;
    {
        const std::lock_guard lock(operation->mutex);
        operation->saved.phase = FinishPhase::Drain;
        if (!operation->saved.drain_done && !operation->drain_submitted) {
            operation->drain_submitted = true;
            submit_drain = true;
        }
    }
    if (submit_drain) {
        state.enqueue_finish_drain([weak = std::weak_ptr(operation)] {
            const auto token = weak.lock();
            if (!token) return;
            std::weak_ptr<ThreadState> waiter;
            {
                const std::lock_guard lock(token->mutex);
                token->saved.drain_done = true;
                waiter = token->waiter;
            }
            wake(waiter);
        });
    }
    const bool completed = await(state, thread, operation, true);
    retire(state, operation, !completed);
    // Both public Finish exports return zero, including renderer shutdown.
    return SCE_KERNEL_OK;
}

void complete_finish_command(State &state, uint64_t id, int result) {
    const auto operation = lookup(state, id);
    if (!operation) return;
    std::weak_ptr<ThreadState> waiter;
    bool abandoned;
    {
        const std::lock_guard lock(operation->mutex);
        operation->saved.result = result;
        operation->saved.fence_done = true;
        waiter = operation->waiter;
        abandoned = operation->saved.owner == 0;
    }
    wake(waiter);
    if (abandoned) retire(state, operation, false);
}

bool validate_finish_operations(const std::vector<FinishSnapshot> &operations,
    const std::vector<CommandSnapshot> &commands, std::string &error) {
    const auto fail = [&] { error = "invalid renderer Finish completion ownership"; return false; };
    if (operations.size() > 65536) return fail();
    std::map<uint64_t, const FinishSnapshot *> ids;
    std::map<uint64_t, size_t> pending;
    for (const auto &saved : operations) {
        if (!valid_snapshot(saved)
            || !ids.emplace(saved.id, &saved).second) return fail();
    }
    for (const auto &command : commands) {
        if (!command.completion_id) continue;
        const auto it = ids.find(command.completion_id);
        if (command.opcode != CommandOpcode::Nop || it == ids.end() || it->second->fence_done
            || ++pending[command.completion_id] != 1) return fail();
    }
    for (const auto &[id, saved] : ids)
        if (!saved->fence_done && pending[id] != 1) return fail();
    return true;
}

bool capture_finish_operations(State &state, std::vector<FinishSnapshot> &output, std::string &error) {
    std::vector<FinishSnapshot> saved;
    const std::lock_guard registry(state.finish_operations_mutex);
    for (const auto &[id, operation] : state.finish_operations) {
        const std::lock_guard lock(operation->mutex);
        if (operation->aborted) { error = "renderer Finish was aborted"; return false; }
        saved.push_back(operation->saved);
    }
    output = std::move(saved);
    return true;
}

bool restore_finish_operations(State &state, const std::vector<FinishSnapshot> &saved, std::string &error) {
    std::map<uint64_t, std::shared_ptr<FinishOperation>> staged;
    uint64_t next = 1;
    if (saved.size() > 65536) { error = "renderer Finish table exceeds limit"; return false; }
    for (const auto &value : saved) {
        if (!valid_snapshot(value)
            || !staged.emplace(value.id, std::make_shared<FinishOperation>()).second) {
            error = "invalid renderer Finish table"; return false;
        }
        staged.at(value.id)->saved = value;
        // GPU wait requests were drained at capture; an unfinished barrier is
        // harmless to reissue after all restored GPU resources are ready.
        staged.at(value.id)->drain_submitted = false;
        next = std::max(next, value.id + 1);
    }
    const std::lock_guard lock(state.finish_operations_mutex);
    state.finish_operations = std::move(staged);
    state.next_finish_id = next;
    return true;
}

void abort_finish_operations(State &state) {
    std::map<uint64_t, std::shared_ptr<FinishOperation>> retired;
    {
        const std::lock_guard lock(state.finish_operations_mutex);
        retired.swap(state.finish_operations);
    }
    for (const auto &[id, operation] : retired) {
        std::weak_ptr<ThreadState> waiter;
        {
            const std::lock_guard lock(operation->mutex);
            operation->aborted = true;
            waiter = operation->waiter;
        }
        wake(waiter);
    }
}

void register_finish_wait_handlers(KernelState &kernel, State &state) {
    state.kernel = &kernel;
    kernel.wait_resume_handlers[WaitOperation::renderer_finish] = {
        [&state, &kernel](ThreadState &thread, const std::shared_ptr<WaitContinuation> &record) {
            const auto &a = record->state.args;
            const auto operation = lookup(state, uint64_t(a[0]) | (uint64_t(a[1]) << 32));
            if (!operation) return false;
            const auto waiter = kernel.get_thread(thread.id);
            const std::lock_guard lock(operation->mutex);
            if (operation->saved.owner != thread.id || operation->aborted) return false;
            record->object = operation;
            operation->waiter = waiter;
            return true;
        },
        [&state](ThreadState &thread, const std::shared_ptr<WaitContinuation> &) {
            return finish_guest(state, thread, nullptr, 0);
        }};
}
}
