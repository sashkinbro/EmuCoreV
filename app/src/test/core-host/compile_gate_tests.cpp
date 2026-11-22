#include <gtest/gtest.h>

#include <renderer/compile_gate.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using Gate = renderer::CompileGate<int>;

namespace {
void wait_until(std::atomic<int> &counter, int expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (counter.load() != expected && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
}
} // namespace

TEST(CompileGate, FirstCallerOwnsThenResultIsShared) {
    Gate gate;
    int out = 0;
    EXPECT_FALSE(gate.try_get(out));
    EXPECT_EQ(gate.acquire().status, Gate::Status::Owner);
    gate.finish(42);
    const auto again = gate.acquire();
    EXPECT_EQ(again.status, Gate::Status::Ready);
    EXPECT_EQ(again.value, 42);
    EXPECT_TRUE(gate.try_get(out));
    EXPECT_EQ(out, 42);
}

TEST(CompileGate, WaitersReceiveTheOwnerResult) {
    Gate gate;
    ASSERT_EQ(gate.acquire().status, Gate::Status::Owner);

    std::atomic<int> arrived{ 0 }, ready{ 0 }, wrong{ 0 };
    std::vector<std::thread> waiters;
    for (int i = 0; i < 6; ++i) {
        waiters.emplace_back([&]() {
            ++arrived;
            const auto r = gate.acquire();
            if (r.status == Gate::Status::Ready && r.value == 7)
                ++ready;
            else
                ++wrong;
        });
    }
    wait_until(arrived, 6);
    // give the waiters time to block, they must not complete before finish()
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(ready.load(), 0);

    gate.finish(7);
    for (auto &t : waiters)
        t.join();
    EXPECT_EQ(ready.load(), 6);
    EXPECT_EQ(wrong.load(), 0);
}

TEST(CompileGate, PermanentFailureWakesWaitersAndLaterCallers) {
    Gate gate;
    ASSERT_EQ(gate.acquire().status, Gate::Status::Owner);

    std::atomic<int> arrived{ 0 }, failed{ 0 };
    std::vector<std::thread> waiters;
    for (int i = 0; i < 4; ++i) {
        waiters.emplace_back([&]() {
            ++arrived;
            if (gate.acquire().status == Gate::Status::Failed)
                ++failed;
        });
    }
    wait_until(arrived, 4);
    gate.fail(true);
    for (auto &t : waiters)
        t.join();
    EXPECT_EQ(failed.load(), 4);
    EXPECT_EQ(gate.acquire().status, Gate::Status::Failed);
}

TEST(CompileGate, TransientFailureLetsAWaiterRetry) {
    Gate gate;
    ASSERT_EQ(gate.acquire().status, Gate::Status::Owner);

    std::atomic<int> arrived{ 0 }, owners{ 0 }, ready{ 0 };
    std::vector<std::thread> waiters;
    for (int i = 0; i < 3; ++i) {
        waiters.emplace_back([&]() {
            ++arrived;
            const auto r = gate.acquire();
            if (r.status == Gate::Status::Owner) {
                ++owners;
                gate.finish(9);
            } else if (r.status == Gate::Status::Ready && r.value == 9) {
                ++ready;
            }
        });
    }
    wait_until(arrived, 3);
    gate.fail(false);
    for (auto &t : waiters)
        t.join();
    // exactly one waiter retried and compiled, the others got its result
    EXPECT_EQ(owners.load(), 1);
    EXPECT_EQ(ready.load(), 2);
}
