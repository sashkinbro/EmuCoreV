// Shared production wait cases; the Android target also exercises export forwarding.
#include <chrono>
#include <future>
#include <thread>

namespace {
ThreadStatePtr thread_end_thread(KernelThreadEndEnv &env, SceUID id, ThreadStatus status) {
    auto thread = std::make_shared<ThreadState>(id, env.kernel, env.mem);
    thread->priority = 100;
    thread->status = status;
    std::lock_guard lock(env.kernel.mutex);
    env.kernel.threads.emplace(id, thread);
    return thread;
}
} // namespace

TEST(KernelThreadEnd, finite_deadline_expires_and_cleans_the_queue_after_spurious_wakes) {
    using namespace std::chrono_literals;
    for (bool callbacks : { false, true }) {
        KernelThreadEndEnv env;
        auto waiter = thread_end_thread(env, 1, ThreadStatus::dormant);
        auto target = thread_end_thread(env, 2, ThreadStatus::running);
        SceInt32 status = 0x12345678;
        SceUInt32 timeout = 30000;
        auto waiting = std::async(std::launch::async, [&] { return thread_end_wait(env, waiter, target, &status, &timeout, callbacks); });
        // Continuous unrelated wakes must not keep extending the original deadline.
        const auto wake_until = std::chrono::steady_clock::now() + 200ms;
        while (waiting.wait_for(5ms) != std::future_status::ready && std::chrono::steady_clock::now() < wake_until)
            waiter->wake();
        const bool expired = waiting.wait_for(0ms) == std::future_status::ready;
        if (!expired)
            waiter->exit_delete(false);
        const auto result = waiting.get();
        EXPECT_TRUE(expired);
        EXPECT_EQ(result, SCE_KERNEL_ERROR_WAIT_TIMEOUT);
        EXPECT_EQ(timeout, 0u);
        EXPECT_EQ(status, 0x12345678);
        {
            std::lock_guard lock(target->mutex);
            target->returned_value = 99;
            target->update_status(ThreadStatus::dormant);
        }
        // A stale queue entry would write the later exit into this old buffer.
        EXPECT_EQ(status, 0x12345678);
    }
}

TEST(KernelThreadEnd, successful_join_reports_status_and_remaining_time) {
    using namespace std::chrono_literals;
    KernelThreadEndEnv env;
    auto waiter = thread_end_thread(env, 1, ThreadStatus::dormant);
    auto target = thread_end_thread(env, 2, ThreadStatus::running);
    SceInt32 status = 0;
    SceUInt32 timeout = 1000000;
    auto waiting = std::async(std::launch::async, [&] { return thread_end_wait(env, waiter, target, &status, &timeout, false); });
    const auto deadline = std::chrono::steady_clock::now() + 200ms;
    bool queued = false;
    while (!queued && std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard lock(waiter->mutex);
            queued = waiter->status == ThreadStatus::waiting && waiter->wait_target.id == target->id;
        }
        std::this_thread::yield();
    }
    {
        std::lock_guard lock(target->mutex);
        target->returned_value = 0xFEDCBA98;
        target->update_status(ThreadStatus::dormant);
    }
    EXPECT_TRUE(queued);
    EXPECT_EQ(waiting.get(), 0);
    EXPECT_EQ(status, static_cast<SceInt32>(0xFEDCBA98));
    EXPECT_GT(timeout, 0u);
    EXPECT_LT(timeout, 1000000u);
}

TEST(KernelThreadEnd, zero_timeout_expires_but_an_already_ended_thread_succeeds) {
    KernelThreadEndEnv env;
    auto waiter = thread_end_thread(env, 1, ThreadStatus::dormant);
    auto target = thread_end_thread(env, 2, ThreadStatus::running);
    SceInt32 status = 55;
    SceUInt32 timeout = 0;
    auto waiting = std::async(std::launch::async, [&] { return thread_end_wait(env, waiter, target, &status, &timeout, false); });
    using namespace std::chrono_literals;
    const bool expired = waiting.wait_for(200ms) == std::future_status::ready;
    if (!expired)
        waiter->exit_delete(false);
    EXPECT_EQ(waiting.get(), SCE_KERNEL_ERROR_WAIT_TIMEOUT);
    EXPECT_TRUE(expired);
    EXPECT_EQ(status, 55);
    {
        std::lock_guard lock(target->mutex);
        target->returned_value = 77;
        target->update_status(ThreadStatus::dormant);
    }
    EXPECT_EQ(thread_end_wait(env, waiter, target, &status, &timeout, true), 0);
    EXPECT_EQ(status, 77);
}
