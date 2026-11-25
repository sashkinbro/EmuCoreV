// Host tests for the pure logic behind the first "stubs that must really work" wave:
// the codec engine unmap memory allocator and the sceNetEpollAbort wait/abort state machine.
#include <codec_engine_alloc.h>
#include <net/epoll_abort.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

TEST(CodecEngineAlloc, ReturnsPositiveDistinctAlignedAddressesInsideTheBlock) {
    int32_t cursor = 0;
    const auto a = codec_engine::alloc(7, 0x100000, cursor, 0x1000, 0x100);
    ASSERT_EQ(a.status, codec_engine::AllocStatus::Ok);
    EXPECT_EQ(a.vaddr, codec_engine::device_base(7));
    EXPECT_LT(static_cast<int32_t>(a.vaddr), 0x7FFFFFFF);
    EXPECT_GT(static_cast<int32_t>(a.vaddr), 0);

    const auto b = codec_engine::alloc(7, 0x100000, cursor, 0x20, 0x4000);
    ASSERT_EQ(b.status, codec_engine::AllocStatus::Ok);
    EXPECT_EQ(b.vaddr, codec_engine::device_base(7) + 0x4000u);
    EXPECT_EQ(b.vaddr % 0x4000u, codec_engine::device_base(7) % 0x4000u);

    const auto c = codec_engine::alloc(7, 0x100000, cursor, 0x10, 0);
    ASSERT_EQ(c.status, codec_engine::AllocStatus::Ok);
    EXPECT_EQ(c.vaddr, codec_engine::device_base(7) + 0x4020u);
    EXPECT_NE(codec_engine::device_base(7), codec_engine::device_base(8));
}

TEST(CodecEngineAlloc, RejectsBadArgumentsAndExhaustionWithoutMovingTheCursor) {
    int32_t cursor = 0x40;
    EXPECT_EQ(codec_engine::alloc(1, 0x1000, cursor, 0, 4).status, codec_engine::AllocStatus::InvalidSize);
    EXPECT_EQ(codec_engine::alloc(1, 0x1000, cursor, 16, 3).status, codec_engine::AllocStatus::InvalidValue);
    EXPECT_EQ(codec_engine::alloc(1, 0x1000, cursor, 0x1000, 4).status, codec_engine::AllocStatus::InvalidSize);
    EXPECT_EQ(cursor, 0x40);
    // exactly filling the rest of the block works, one byte more does not
    EXPECT_EQ(codec_engine::alloc(1, 0x1000, cursor, 0x1000 - 0x40 + 1, 1).status, codec_engine::AllocStatus::InvalidSize);
    EXPECT_EQ(codec_engine::alloc(1, 0x1000, cursor, 0x1000 - 0x40, 1).status, codec_engine::AllocStatus::Ok);
    EXPECT_EQ(cursor, 0x1000);
}

TEST(CodecEngineAlloc, SizeZeroBlockIsNotBounded) {
    int32_t cursor = 0;
    EXPECT_EQ(codec_engine::alloc(1, 0, cursor, 0x800000, 0x100).status, codec_engine::AllocStatus::Ok);
}

TEST(CodecEngineAlloc, OwnsChecksTheBlockRange) {
    const uint32_t base = codec_engine::device_base(3);
    EXPECT_TRUE(codec_engine::owns(3, 0x1000, base));
    EXPECT_TRUE(codec_engine::owns(3, 0x1000, base + 0xFFF));
    EXPECT_FALSE(codec_engine::owns(3, 0x1000, base + 0x1000));
    EXPECT_FALSE(codec_engine::owns(3, 0x1000, base - 1));
    EXPECT_FALSE(codec_engine::owns(3, 0x1000, 0xDEADBEAF));
}

TEST(EpollAbort, PollResultEndsTheWait) {
    epoll_abort::State state;
    int result = 0;
    int polls = 0;
    const auto outcome = epoll_abort::wait(
        state, 1000000, [&](int) { return ++polls == 3 ? 2 : 0; }, &result);
    EXPECT_EQ(outcome, epoll_abort::Outcome::Ready);
    EXPECT_EQ(result, 2);
    EXPECT_EQ(polls, 3);
    EXPECT_EQ(state.waiting.load(), 0);
}

TEST(EpollAbort, TimeoutIsSlicedAndExact) {
    epoll_abort::State state;
    int total = 0;
    int polls = 0;
    const int timeout = epoll_abort::SLICE_MICROSECONDS * 2 + 5;
    const auto outcome = epoll_abort::wait(
        state, timeout, [&](int slice) { total += slice; ++polls; return 0; }, nullptr);
    EXPECT_EQ(outcome, epoll_abort::Outcome::Timeout);
    EXPECT_EQ(total, timeout);
    EXPECT_EQ(polls, 3);
}

TEST(EpollAbort, ZeroTimeoutPollsOnceWithoutBlocking) {
    epoll_abort::State state;
    int polls = 0;
    int last_slice = -1;
    const auto outcome = epoll_abort::wait(
        state, 0, [&](int slice) { ++polls; last_slice = slice; return 0; }, nullptr);
    EXPECT_EQ(outcome, epoll_abort::Outcome::Timeout);
    EXPECT_EQ(polls, 1);
    EXPECT_EQ(last_slice, 0);
}

TEST(EpollAbort, AbortWakesAnInfiniteWait) {
    epoll_abort::State state;
    std::atomic<bool> started{ false };
    epoll_abort::Outcome outcome = epoll_abort::Outcome::Ready;
    std::thread waiter([&] {
        outcome = epoll_abort::wait(
            state, -1, [&](int) { started = true; std::this_thread::sleep_for(std::chrono::milliseconds(1)); return 0; }, nullptr);
    });
    while (!started)
        std::this_thread::yield();
    EXPECT_TRUE(state.abort(0));
    waiter.join();
    EXPECT_EQ(outcome, epoll_abort::Outcome::Aborted);
    EXPECT_EQ(state.waiting.load(), 0);
    // nobody was waiting any more and no preservation was asked: the next wait is unaffected
    int polls = 0;
    EXPECT_EQ(epoll_abort::wait(state, 0, [&](int) { ++polls; return 0; }, nullptr), epoll_abort::Outcome::Timeout);
}

TEST(EpollAbort, AbortWithoutWaiterIsLostUnlessPreserved) {
    epoll_abort::State state;
    EXPECT_TRUE(state.abort(0));
    EXPECT_EQ(epoll_abort::wait(state, 0, [](int) { return 0; }, nullptr), epoll_abort::Outcome::Timeout);

    EXPECT_TRUE(state.abort(epoll_abort::FLAG_PRESERVATION));
    EXPECT_EQ(epoll_abort::wait(state, 0, [](int) { return 0; }, nullptr), epoll_abort::Outcome::Aborted);
    // the preserved abort is consumed by exactly one wait
    EXPECT_EQ(epoll_abort::wait(state, 0, [](int) { return 0; }, nullptr), epoll_abort::Outcome::Timeout);
}

TEST(EpollAbort, UnknownFlagsAreRejected) {
    epoll_abort::State state;
    EXPECT_FALSE(state.abort(0x2));
    EXPECT_FALSE(state.abort(0x80000000));
    EXPECT_EQ(state.generation.load(), 0u);
    EXPECT_FALSE(state.preserved.load());
}
