// Exercise the actual process_exit registry boundary without guest execution.
#include <chrono>
#include <future>
#include <thread>

TEST(KernelShutdown, a_thread_registered_after_the_exit_sweep_is_also_deleted) {
    using namespace std::chrono_literals;
    KernelShutdownEnv env;
    auto creator = std::make_shared<ThreadState>(1, env.kernel, env.mem);
    auto child = std::make_shared<ThreadState>(2, env.kernel, env.mem);
    {
        std::lock_guard lock(env.kernel.mutex);
        env.kernel.threads.emplace(creator->id, creator);
    }
    auto shutdown = std::async(std::launch::async, [&] { env.kernel.process_exit(); });
    const auto requested = [](const ThreadStatePtr &thread) {
        std::lock_guard lock(thread->mutex);
        return thread->is_delete_requested();
    };
    const auto until = [&](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        return predicate();
    };
    EXPECT_TRUE(until([&] { return requested(creator); }));
    {
        // create_thread can finish its initialization and register while the
        // creating guest is still returning from its HLE call during shutdown.
        std::lock_guard lock(env.kernel.mutex);
        env.kernel.threads.emplace(child->id, child);
        env.kernel.threads.erase(creator->id);
        env.kernel.thread_deleted_cond.notify_all();
    }
    const bool child_deleted = until([&] { return requested(child); });
    // Cleanup is unconditional, so the broken baseline fails without hanging.
    {
        std::lock_guard lock(env.kernel.mutex);
        env.kernel.threads.erase(child->id);
        env.kernel.thread_deleted_cond.notify_all();
    }
    EXPECT_TRUE(child_deleted);
    EXPECT_EQ(shutdown.wait_for(1s), std::future_status::ready);
    shutdown.get();
}
