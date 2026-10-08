#pragma once
#include <emucorev/savestate/gxm_state_io.h>
#include <kernel/thread/thread_state.h>
#include <map>
#include <set>

namespace emucorev::savestate {
inline bool validate_gxm_finish_continuations(const GxmSnapshot &gxm,
    const std::vector<ThreadState::Snapshot> &threads, std::string &error) {
    const auto fail = [&] { error = "invalid renderer Finish guest continuation"; return false; };
    if (!renderer::validate_finish_operations(gxm.finish_operations, gxm.commands, error)) return false;
    std::set<Address> contexts;
    for (const auto &context : gxm.contexts) contexts.insert(context.address);
    std::map<uint64_t, const renderer::FinishSnapshot *> operations;
    for (const auto &operation : gxm.finish_operations) {
        if (operation.context_address && !contexts.contains(operation.context_address)) return fail();
        operations.emplace(operation.id, &operation);
    }
    std::set<uint64_t> waiters;
    for (const auto &thread : threads) {
        for (const auto &wait : thread.waits) {
            if (wait.operation != WaitOperation::renderer_finish) continue;
            const auto &a = wait.args;
            const uint64_t id = uint64_t(a[0]) | (uint64_t(a[1]) << 32);
            const auto operation = operations.find(id);
            if (!thread.registered || thread.id <= 0 || operation == operations.end()
                || operation->second->owner != thread.id || !waiters.insert(id).second
                || a[2] != 1 || a[3] || a[4] || a[5] || a[6] || a[7]
                || !wait.infinite || wait.callbacks || wait.timeout_address || wait.queued
                || wait.has_result || wait.sequence || wait.phase != WaitPhase::queued
                || wait.target.type != SCE_KERNEL_WAITTYPE_EVENT || wait.target.id != 0) return fail();
        }
    }
    for (const auto &[id, operation] : operations)
        if ((operation->owner != 0) != waiters.contains(id)) return fail();
    return true;
}
}
