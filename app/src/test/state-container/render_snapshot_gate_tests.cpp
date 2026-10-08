#include <renderer/snapshot_gate.h>
#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <thread>

using namespace std::chrono_literals;

TEST(RenderSnapshotGate, RequiresDrainedCommandsAndReleasesWorker) {
    renderer::SnapshotGate gate;
    std::atomic<bool> drained{ false };
    std::atomic<bool> running{ true };
    std::atomic<unsigned> completed{ 0 };
    std::thread worker([&] {
        while (running) {
            gate.boundary(drained);
            ++completed;
            std::this_thread::yield();
        }
    });
    EXPECT_FALSE(gate.acquire(5ms));
    drained = true;
    const bool acquired = gate.acquire(2s);
    EXPECT_TRUE(acquired);
    if (acquired) {
        const auto count = completed.load();
        EXPECT_FALSE(gate.acquire(1ms));
        EXPECT_EQ(completed.load(), count);
    }
    running = false;
    gate.release();
    worker.join();
}

TEST(RenderSnapshotGate, ShutdownWakesBothAcquirerAndParkedWorker) {
    renderer::SnapshotGate gate;
    auto acquire = std::async(std::launch::async, [&] { return gate.acquire(2s); });
    gate.stop();
    EXPECT_FALSE(acquire.get());
    gate.reset();
    std::atomic<bool> running{ true };
    std::thread worker([&] {
        while (running) {
            gate.boundary(true);
            std::this_thread::yield();
        }
    });
    EXPECT_TRUE(gate.acquire(2s));
    running = false;
    gate.stop();
    worker.join();
}
