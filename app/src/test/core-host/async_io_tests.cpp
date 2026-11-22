// Host tests for the HLE async file I/O bookkeeping: results parked per fd at submit time and
// collected (once) by sceIoComplete, plus the stored I/O priorities.
#include <io/async_io.h>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

using async_io::AsyncOpTable;
using async_io::PriorityTable;

TEST(AsyncIo, CompleteReturnsParkedResultOnce) {
    AsyncOpTable table;
    table.push(5, 4096);
    EXPECT_EQ(table.pending(5), 1u);

    int64_t res = -1;
    ASSERT_TRUE(table.pop(5, res));
    EXPECT_EQ(res, 4096);
    EXPECT_EQ(table.pending(5), 0u);
    EXPECT_FALSE(table.pop(5, res)); // the op is deleted by completing it
}

TEST(AsyncIo, CompleteWithNothingPendingFails) {
    AsyncOpTable table;
    int64_t res = 1234;
    EXPECT_FALSE(table.pop(9, res));
    EXPECT_EQ(res, 1234); // untouched
    EXPECT_LT(async_io::ERR_NO_PENDING, 0);
}

TEST(AsyncIo, ResultsAreKeptPerFd) {
    AsyncOpTable table;
    table.push(1, 10);
    table.push(2, 20);
    int64_t res = 0;
    ASSERT_TRUE(table.pop(2, res));
    EXPECT_EQ(res, 20);
    ASSERT_TRUE(table.pop(1, res));
    EXPECT_EQ(res, 10);
}

TEST(AsyncIo, SameFdIsFifo) {
    AsyncOpTable table;
    table.push(3, 1);
    table.push(3, 2);
    table.push(3, 3);
    int64_t res = 0;
    for (int64_t expected = 1; expected <= 3; ++expected) {
        ASSERT_TRUE(table.pop(3, res));
        EXPECT_EQ(res, expected);
    }
    EXPECT_FALSE(table.pop(3, res));
}

TEST(AsyncIo, NegativeAndLargeResultsSurvive) {
    AsyncOpTable table;
    table.push(7, static_cast<int32_t>(0x80010016)); // an SCE error code
    table.push(7, 0x1'0000'0000LL); // a 64 bit lseek result
    int64_t res = 0;
    ASSERT_TRUE(table.pop(7, res));
    EXPECT_EQ(static_cast<int32_t>(res), static_cast<int32_t>(0x80010016));
    EXPECT_LT(res, 0);
    ASSERT_TRUE(table.pop(7, res));
    EXPECT_EQ(res, 0x1'0000'0000LL);
}

TEST(AsyncIo, PeekDoesNotConsume) {
    AsyncOpTable table;
    table.push(4, 99);
    int64_t res = 0;
    ASSERT_TRUE(table.peek(4, res));
    EXPECT_EQ(res, 99);
    EXPECT_EQ(table.pending(4), 1u);
    EXPECT_FALSE(table.peek(8, res));
}

TEST(AsyncIo, DiscardDropsEverythingForFd) {
    AsyncOpTable table;
    table.push(6, 1);
    table.push(6, 2);
    table.push(7, 3);
    table.discard(6);
    int64_t res = 0;
    EXPECT_FALSE(table.pop(6, res));
    EXPECT_TRUE(table.pop(7, res));
    table.push(1, 1);
    table.clear();
    EXPECT_EQ(table.pending(1), 0u);
}

TEST(AsyncIo, FireAndForgetIsBounded) {
    AsyncOpTable table;
    for (size_t i = 0; i < async_io::MAX_PENDING_PER_FD + 10; ++i)
        table.push(2, static_cast<int64_t>(i));
    EXPECT_EQ(table.pending(2), async_io::MAX_PENDING_PER_FD);
    int64_t res = 0;
    ASSERT_TRUE(table.pop(2, res));
    EXPECT_EQ(res, 10); // the oldest ones were dropped
}

TEST(AsyncIo, ConcurrentSubmitAndComplete) {
    AsyncOpTable table;
    constexpr int kThreads = 4;
    constexpr int kOps = 500;
    std::vector<std::thread> threads;
    std::atomic<int> completed{ 0 };
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < kOps; ++i) {
                table.push(t, i);
                int64_t res = -1;
                if (table.pop(t, res) && res == i)
                    ++completed;
            }
        });
    }
    for (auto &th : threads)
        th.join();
    EXPECT_EQ(completed.load(), kThreads * kOps);
}

TEST(IoPriority, StoreAndGetPerFd) {
    PriorityTable prio;
    EXPECT_EQ(prio.get(5), async_io::DEFAULT_PRIORITY);
    prio.set(5, 17);
    EXPECT_EQ(prio.get(5), 17);
    EXPECT_EQ(prio.get(6), async_io::DEFAULT_PRIORITY);
    prio.forget(5);
    EXPECT_EQ(prio.get(5), async_io::DEFAULT_PRIORITY);
}

TEST(IoPriority, DefaultsAreStoredAndFdFallsBackToThreadDefault) {
    PriorityTable prio;
    prio.set_process_default(3);
    prio.set_thread_default(8);
    EXPECT_EQ(prio.process_default(), 3);
    EXPECT_EQ(prio.thread_default(), 8);
    EXPECT_EQ(prio.get(1), 8);
}
