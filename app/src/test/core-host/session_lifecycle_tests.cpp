// B151: per-process state must not leak from one emulator session into the next one in the
// same host process (game A closed to the library, game B started; or an exec switch).
// Host tests for the header-only pieces of that teardown.

#include <cpu/owner_lock.h>
#include <kernel/process_clock_pause.h>
#include <ngs/param_lock.h>
#include <power_battery.h>

#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

namespace {

// KernelState::process_time_us() with an injectable clock.
uint64_t process_time(ProcessClockPause &pause, uint64_t &start_tick, uint64_t now) {
    return now - pause.effective_start(start_tick, now);
}

} // namespace

TEST(SessionLifecycle, ClockPausedAtStopFreezesNextProcessWithoutReset) {
    // Session A: menu pause at t=1000, then the game is quit from the menu (no unpause).
    ProcessClockPause pause;
    uint64_t start_a = 0;
    pause.set_paused(true, 1000, start_a);
    ASSERT_TRUE(pause.paused());

    // Session B without the reset: its clock is stuck (the B151 hang on the first image).
    uint64_t start_b = 5000;
    const uint64_t t1 = process_time(pause, start_b, 6000);
    const uint64_t t2 = process_time(pause, start_b, 9000);
    EXPECT_EQ(t1, t2) << "a stale pause_start freezes the next process clock";
}

TEST(SessionLifecycle, ClockResetStartsNextProcessRunning) {
    ProcessClockPause pause;
    uint64_t start_a = 0;
    pause.set_paused(true, 1000, start_a);

    pause.reset(); // KernelState::init / deinit
    EXPECT_FALSE(pause.paused());
    uint64_t start_b = 5000;
    EXPECT_EQ(process_time(pause, start_b, 6000), 1000u);
    EXPECT_EQ(process_time(pause, start_b, 9000), 4000u);

    // Pausing still works in the new process.
    pause.set_paused(true, 9000, start_b);
    EXPECT_EQ(process_time(pause, start_b, 12000), 4000u);
    pause.set_paused(false, 12000, start_b);
    EXPECT_EQ(process_time(pause, start_b, 13000), 5000u);
}

TEST(SessionLifecycle, NgsParamLockHeldAtTeardownDoesNotBlockNextProcess) {
    ngs::system_param_lock_clear_all();
    int system_storage = 0; // the next process maps its system at the same host address
    const void *system = &system_storage;
    std::mutex m;
    std::condition_variable cv;
    {
        std::unique_lock lk(m);
        ngs::param_lock_acquire(ngs::system_param_lock(system), lk, cv, /*thread*/ 7, [] { return false; }, [] {});
    }
    // Session ends while thread 7 holds the lock (it is torn down, never unlocks).
    EXPECT_TRUE(ngs::system_param_lock(system).blocks(/*update thread*/ 3));

    ngs::system_param_lock_clear_all(); // ngs::deinit
    EXPECT_EQ(ngs::system_param_lock_entry_count(), 0u);
    EXPECT_FALSE(ngs::system_param_lock(system).blocks(3)) << "the next process's update would wait forever";

    // The update of the next process proceeds without blocking.
    std::unique_lock lk(m);
    bool blocked = false;
    ngs::param_lock_wait_for_update(ngs::system_param_lock(system), lk, cv, 3, [&] { blocked = true; });
    EXPECT_FALSE(blocked);
    ngs::system_param_lock_clear_all();
}

TEST(SessionLifecycle, OwnerLockReleasedByDestroyedOwnerFromAnotherThread) {
    OwnerLock lock;
    int cpu_a = 0, cpu_b = 0;
    std::thread guest([&] {
        ASSERT_TRUE(lock.acquire(&cpu_a, 1s));
        ASSERT_TRUE(lock.acquire(&cpu_a, 1s)); // recursive
        // thread exits while holding the lock (process exit tears it down)
    });
    guest.join();
    EXPECT_TRUE(lock.held());

    // The owner's CPU is destroyed on the teardown thread, not the thread that locked.
    lock.release_all(&cpu_a);
    EXPECT_FALSE(lock.held());

    // Next session: acquired at once, no timeout.
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_TRUE(lock.acquire(&cpu_b, 5s));
    EXPECT_LT(std::chrono::steady_clock::now() - t0, 1s);
    EXPECT_TRUE(lock.release(&cpu_b));
    EXPECT_FALSE(lock.held());
}

TEST(SessionLifecycle, OwnerLockSemantics) {
    OwnerLock lock;
    int a = 0, b = 0;
    EXPECT_FALSE(lock.release(&a)) << "unbalanced unlock is refused";
    ASSERT_TRUE(lock.acquire(&a, 1s));
    EXPECT_FALSE(lock.acquire(&b, 20ms)) << "another owner times out";
    EXPECT_FALSE(lock.release(&b));
    lock.release_all(&b); // not the owner: no effect
    EXPECT_TRUE(lock.held());

    std::thread waiter([&] { EXPECT_TRUE(lock.acquire(&b, 5s)); lock.release(&b); });
    std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(lock.release(&a));
    waiter.join();
    EXPECT_FALSE(lock.held());
}

TEST(SessionLifecycle, PowerCallbackRegistryIsPerProcess) {
    power_battery::CallbackRegistry registry;
    registry.bind_process(0);
    EXPECT_EQ(registry.add(5), power_battery::CallbackRegistry::Ok);
    EXPECT_EQ(registry.add(5), power_battery::CallbackRegistry::AlreadyRegistered);

    // Next guest process (kernel process epoch advanced), UIDs restart: same id registers again.
    registry.bind_process(1);
    EXPECT_EQ(registry.size(), 0u);
    EXPECT_EQ(registry.add(5), power_battery::CallbackRegistry::Ok);
    registry.bind_process(1); // same process: kept
    EXPECT_TRUE(registry.contains(5));
}
