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
