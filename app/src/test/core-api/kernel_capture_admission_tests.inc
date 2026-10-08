// Exercises the real capture/creation boundary without allocating a guest CPU.
TEST(KernelCaptureAdmission, drains_initializers_then_blocks_later_host_creators) {
    using namespace std::chrono_literals;
    EmuEnvState env;
    ASSERT_TRUE(env.kernel.begin_thread_creation());
    std::promise<int> stopped;
    auto stopped_result = stopped.get_future();
    std::promise<void> release_capture;
    auto release = release_capture.get_future();
    auto capture = std::async(std::launch::async, [&] {
        stopped.set_value(env.kernel.stop_world(0, 1s));
        release.wait();
        env.kernel.resume_world();
    });
    const auto until = std::chrono::steady_clock::now() + 1s;
    while (!env.kernel.is_capture_active() && std::chrono::steady_clock::now() < until)
        std::this_thread::yield();
    const bool closed = env.kernel.is_capture_active();
    const auto while_initializing = stopped_result.wait_for(20ms);
    auto creator = std::async(std::launch::async, [&] {
        const bool admitted = env.kernel.begin_thread_creation();
        if (admitted)
            env.kernel.end_thread_creation();
        return admitted;
    });
    env.kernel.end_thread_creation();
    const auto result = stopped_result.get();
    const auto while_captured = creator.wait_for(20ms);
    release_capture.set_value();
    capture.get();
    EXPECT_TRUE(closed);
    EXPECT_EQ(while_initializing, std::future_status::timeout);
    EXPECT_EQ(result, 0);
    EXPECT_EQ(while_captured, std::future_status::timeout);
    EXPECT_TRUE(creator.get());
}

TEST(KernelCaptureAdmission, rejected_competing_owner_cannot_release_capture_or_admit_creators) {
    EmuEnvState env;
    ASSERT_EQ(env.kernel.stop_world(0, 1s), 0);
    const auto clock = env.kernel.capture_clock();
    auto competitor = std::async(std::launch::async, [&] {
        const int result = env.kernel.stop_world(0, 10ms);
        // Both production RAII scopes release after a rejected/partial stop.
        env.kernel.resume_world();
        return result;
    });
    EXPECT_EQ(competitor.get(), 1);
    EXPECT_TRUE(env.kernel.is_capture_active());
    EXPECT_EQ(env.kernel.capture_clock().steady, clock.steady);
    auto creator = std::async(std::launch::async, [&] {
        const bool admitted = env.kernel.begin_thread_creation();
        if (admitted) env.kernel.end_thread_creation();
        return admitted;
    });
    EXPECT_EQ(creator.wait_for(30ms), std::future_status::timeout);
    env.kernel.resume_world();
    EXPECT_TRUE(creator.get());
    EXPECT_FALSE(env.kernel.is_capture_active());
}

TEST(KernelCaptureAdmission, partial_stop_keeps_ownership_until_its_owner_cleans_up) {
    EmuEnvState env;
    ASSERT_TRUE(env.kernel.begin_thread_creation());
    const int result = env.kernel.stop_world(0, 1ms);
    EXPECT_EQ(result, 1);
    EXPECT_TRUE(env.kernel.is_capture_active());
    env.kernel.end_thread_creation();
    auto creator = std::async(std::launch::async, [&] {
        const bool admitted = env.kernel.begin_thread_creation();
        if (admitted) env.kernel.end_thread_creation();
        return admitted;
    });
    EXPECT_EQ(creator.wait_for(30ms), std::future_status::timeout);
    env.kernel.resume_world();
    EXPECT_TRUE(creator.get());
    EXPECT_FALSE(env.kernel.is_capture_active());
}
TEST(KernelCaptureAdmission, deletion_unwinds_a_guest_creator_parked_by_capture) {
    using namespace std::chrono_literals;
    EmuEnvState env;
    auto guest = std::make_shared<ThreadState>(1, env.kernel, env.mem);
    env.kernel.threads.emplace(1, guest);
    guest->last_import_nid = 0xC5C11EE7U;
    guest->last_import_context.set_sp(0x4000);
    guest->last_import_context.cpu_registers[0] = 0x1234;
    ASSERT_EQ(env.kernel.stop_world(0, 1s), 0);
    auto creator = std::async(std::launch::async, [&] {
        g_tls_guest_thread = guest.get();
        const bool admitted = env.kernel.begin_thread_creation();
        if (admitted)
            env.kernel.end_thread_creation();
        g_tls_guest_thread = nullptr;
        {
            std::lock_guard lock(env.kernel.mutex);
            env.kernel.threads.erase(1);
            env.kernel.thread_deleted_cond.notify_all();
        }
        return admitted;
    });
    const auto until = std::chrono::steady_clock::now() + 1s;
    bool parked = false;
    while (std::chrono::steady_clock::now() < until) {
        {
            std::lock_guard lock(guest->mutex);
            parked = guest->status == ThreadStatus::suspended;
        }
        if (parked)
            break;
        std::this_thread::yield();
    }
    const auto saved = guest->capture_snapshot();
    auto shutdown = std::async(std::launch::async, [&] { env.kernel.process_exit(); });
    const auto drained = shutdown.wait_for(1s);
    if (drained != std::future_status::ready)
        env.kernel.resume_world();
    shutdown.get();
    env.kernel.resume_world();
    EXPECT_TRUE(parked);
    ASSERT_EQ(saved.waits.size(), 1u);
    EXPECT_EQ(saved.waits[0].operation, WaitOperation::creation_import);
    EXPECT_EQ(saved.waits[0].args[0], 0xC5C11EE7U);
    EXPECT_EQ(saved.waits[0].context.get_sp(), 0x4000u);
    EXPECT_EQ(saved.waits[0].context.cpu_registers[0], 0x1234u);
    EXPECT_EQ(drained, std::future_status::ready);
    EXPECT_FALSE(creator.get());
}
