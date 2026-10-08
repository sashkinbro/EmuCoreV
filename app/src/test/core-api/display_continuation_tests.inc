#include <display/functions.h>
#include <display/state.h>
namespace {
bool display_test_queued(DisplayState &display) {
    const auto until = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < until) {
        {
            const std::lock_guard lock(display.mutex);
            if (!display.vblank_waiters.empty())
                return true;
        }
        std::this_thread::yield();
    }
    return false;
}
} // namespace
TEST(DisplayContinuation, queued_target_survives_restore_without_recomputing_vblank) {
    KernelLifecycleEnv original;
    DisplayState display;
    register_display_wait_continuations(display, original.kernel);
    auto guest = continuation_thread(original, 1);
    const uint64_t target = (uint64_t{ 1 } << 32) + 7;
    auto pending = lifecycle_async(guest, [&] { wait_vblank(display, guest, target, false); return 0; });
    ASSERT_TRUE(display_test_queued(display));
    guest->request_world_stop();
    const auto snapshot = guest->capture_snapshot();
    guest->exit_delete(false);
    pending.get();
    ASSERT_EQ(snapshot.waits.size(), 1u);
    EXPECT_EQ(snapshot.waits[0].operation, WaitOperation::display_vblank);
    KernelLifecycleEnv restored;
    DisplayState new_display;
    register_display_wait_continuations(new_display, restored.kernel);
    auto thread = continuation_restore(restored, snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    thread->activate_restored_continuations();
    auto resumed = lifecycle_async(thread, [&] { return continuation_resume(thread); });
    EXPECT_EQ(resumed.wait_for(20ms), std::future_status::timeout);
    {
        const std::lock_guard lock(new_display.mutex);
        ASSERT_FALSE(new_display.vblank_waiters.empty());
        EXPECT_EQ(new_display.vblank_waiters.front()->entry.target_vcount, target);
        new_display.vblank_count = target;
        new_display.vblank_waiters.wake_if([&](auto &waiter) { return waiter.entry.target_vcount <= new_display.vblank_count; });
    }
    EXPECT_EQ(lifecycle_result(resumed, thread), SCE_KERNEL_OK);
    EXPECT_TRUE(new_display.vblank_waiters.empty());
}
TEST(DisplayContinuation, producer_freeze_drains_active_iteration_and_blocks_next_iteration) {
    DisplayState display;
    ASSERT_TRUE(display.begin_vblank_update());
    auto frozen = std::async(std::launch::async, [&] { return display.freeze_vblank_producer(1s); });
    EXPECT_EQ(frozen.wait_for(20ms), std::future_status::timeout);
    display.end_vblank_update();
    ASSERT_TRUE(frozen.get());
    auto next = std::async(std::launch::async, [&] {
        if (!display.begin_vblank_update())
            return false;
        display.end_vblank_update();
        return true;
    });
    const auto while_frozen = next.wait_for(20ms);
    display.resume_vblank_producer();
    EXPECT_EQ(while_frozen, std::future_status::timeout);
    EXPECT_TRUE(next.get());
}
TEST(DisplayContinuation, failed_freeze_does_not_leave_producer_admission_closed) {
    DisplayState display;
    ASSERT_TRUE(display.begin_vblank_update());
    EXPECT_FALSE(display.freeze_vblank_producer(1ms));
    display.end_vblank_update();
    EXPECT_TRUE(display.begin_vblank_update());
    display.end_vblank_update();
}
TEST(DisplayContinuation, shutdown_wakes_and_joins_producer_parked_by_capture) {
    DisplayState display;
    ScopedVblankFreeze frozen(display, 1s);
    ASSERT_TRUE(static_cast<bool>(frozen));
    std::atomic<bool> exited = false;
    display.vblank_thread = std::make_unique<std::thread>([&] { EXPECT_FALSE(display.begin_vblank_update()); exited = true; });
    display.deinit();
    EXPECT_TRUE(exited.load());
    EXPECT_FALSE(display.vblank_thread);
}
