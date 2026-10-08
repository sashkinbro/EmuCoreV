#include <gtest/gtest.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
ThreadState::ThreadState(SceUID id, KernelState &kernel, MemState &mem)
    : id(id)
    , kernel(kernel)
    , mem(mem) {}
ThreadState::~ThreadState() = default;
uint32_t ThreadState::run_callback_locked(std::unique_lock<std::mutex> &, Address, const std::vector<uint32_t> &) { return 0; }
void stop(CPUState &) {}
void guest_sched_release_for_block() {}
CPUState *guest_sched_token_cpu() { return nullptr; }
struct KernelLifecycleEnv {
    KernelState kernel;
    MemState mem;
};
#include "../core-api/kernel_lifecycle_tests.inc"

using KernelThreadEndEnv = KernelLifecycleEnv;
SceInt32 thread_end_wait(KernelThreadEndEnv &, const ThreadStatePtr &waiter, const ThreadStatePtr &target, SceInt32 *status, SceUInt32 *timeout, bool callbacks) {
    return guest_result(target->wait_for_thread_end(waiter, status, callbacks, timeout));
}
#include "../core-api/kernel_thread_end_tests.inc"

#include <kernel/uid_class.h>
using KernelUidClassEnv = KernelLifecycleEnv;
SceInt32 uid_class_query(KernelUidClassEnv &env, SceUID uid, bool) {
    return get_threadmgr_uid_class(env.kernel, uid);
}
#include "../core-api/kernel_uid_class_tests.inc"

#include "../core-api/kernel_mutex_cache_tests.inc"
