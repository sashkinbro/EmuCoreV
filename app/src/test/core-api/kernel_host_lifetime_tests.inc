// Same production process_exit barrier on host and Android, without guest JIT.
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace {
struct HostLifetimeTls {
    ThreadStatePtr owner;
    std::promise<void> *entered;
    std::shared_future<void> release;
    std::atomic<bool> *destroyed;
    ~HostLifetimeTls() {
        entered->set_value();
        release.wait();
        owner.reset();
        destroyed->store(true);
    }
};
} // namespace

TEST(KernelHostLifetime,
    process_exit_waits_for_tls_destructors_before_memory_can_be_replaced) {
    using namespace std::chrono_literals;
    KernelHostLifetimeEnv env;
    std::promise<void> tls_entered;
    auto tls_started = tls_entered.get_future();
    std::promise<void> release_tls;
    auto release = release_tls.get_future().share();
    std::atomic<bool> destroyed{ false }, stack_freed{ false };
    auto guest = std::make_shared<ThreadState>(1, env.kernel, env.mem);
    guest->stack = Block(0x100, [&](Address) { stack_freed.store(true); });
    {
        std::lock_guard lock(env.kernel.mutex);
        env.kernel.threads.emplace(guest->id, guest);
    }
    std::promise<void> registered;
    auto registration = registered.get_future();
    std::thread worker([&] {
        thread_local std::unique_ptr<HostLifetimeTls> tls;
        ThreadStatePtr owner;
        {
            std::lock_guard lock(env.kernel.mutex);
            owner = env.kernel.threads.at(1);
        }
        tls = std::make_unique<HostLifetimeTls>(owner, &tls_entered, release,
            &destroyed);
        registration.wait();
        while (true) {
            {
                std::lock_guard lock(owner->mutex);
                if (owner->is_delete_requested())
                    break;
            }
            std::this_thread::yield();
        }
        owner.reset();
        env.kernel.host_threads.finished(1);
        {
            std::lock_guard lock(env.kernel.mutex);
            env.kernel.threads.erase(1);
            env.kernel.thread_deleted_cond.notify_all();
        }
        // The registry is empty, but the TLS owner deliberately survives here.
    });
    env.kernel.host_threads.add(1, [&] { worker.join(); });
    registered.set_value();
    guest.reset();
    auto shutdown = std::async(std::launch::async, [&] { env.kernel.process_exit(); });
    const bool entered = tls_started.wait_for(1s) == std::future_status::ready;
    const auto while_tls_alive = shutdown.wait_for(20ms);
    EXPECT_FALSE(stack_freed.load());
    // Always release before assertions can abort, including on the old baseline.
    release_tls.set_value();
    shutdown.get();
    env.kernel.host_threads.join_all();
    EXPECT_TRUE(entered);
    EXPECT_EQ(while_tls_alive, std::future_status::timeout);
    EXPECT_TRUE(destroyed.load());
    EXPECT_TRUE(stack_freed.load());
    EXPECT_EQ(env.kernel.sync_cache_generation.load(), 1u);
}

TEST(KernelHostLifetime,
    shutdown_waits_for_a_finished_worker_claimed_by_another_reaper) {
    using namespace std::chrono_literals;
    KernelHostLifetimeEnv env;
    std::promise<void> tls_entered, release_tls, join_entered;
    auto started = tls_entered.get_future();
    auto joining = join_entered.get_future();
    auto release = release_tls.get_future().share();
    std::atomic<bool> destroyed{ false };
    auto owner = std::make_shared<ThreadState>(1, env.kernel, env.mem);
    std::thread worker([owner, &tls_entered, release, &destroyed] {
        thread_local std::unique_ptr<HostLifetimeTls> tls;
        tls = std::make_unique<HostLifetimeTls>(owner, &tls_entered, release,
            &destroyed);
    });
    env.kernel.host_threads.add(1, [&] {
        join_entered.set_value();
        worker.join();
    });
    env.kernel.host_threads.finished(1);
    auto reaper = std::async(std::launch::async,
        [&] { env.kernel.host_threads.join_finished(); });
    const bool entered = started.wait_for(1s) == std::future_status::ready;
    const bool claimed = joining.wait_for(1s) == std::future_status::ready;
    auto shutdown = std::async(std::launch::async, [&] { env.kernel.process_exit(); });
    const auto while_joining = shutdown.wait_for(20ms);
    release_tls.set_value();
    shutdown.get();
    reaper.get();
    EXPECT_TRUE(entered);
    EXPECT_TRUE(claimed);
    EXPECT_EQ(while_joining, std::future_status::timeout);
    EXPECT_TRUE(destroyed.load());
}
