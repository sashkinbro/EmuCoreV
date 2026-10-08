DECL_EXPORT(uint64_t, sceKernelGetTimerBaseWide, SceUID timer_handle);
DECL_EXPORT(uint64_t, sceKernelGetTimerTimeWide, SceUID timer_handle);
DECL_EXPORT(int, sceKernelSetTimerTimeWide, SceUID timer_handle, SceUInt64 time);
namespace {
uint64_t timer_test_now() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch())
            .count());
}
} // namespace
TEST(KernelTimerClock, raw_base_and_elapsed_survive_epoch_translation_running_stopped_and_guest_set) {
    for (const bool running : { false, true }) {
        for (const uint64_t raw_base : { uint64_t{ 0 }, uint64_t{ 7000 }, std::numeric_limits<uint64_t>::max() - 10 }) {
            Timer original(0);
            original.is_started = running;
            original.time = raw_base;
            const auto saved_elapsed = original.elapsed_time_at(100000);
            Timer restored(0);
            restored.is_started = running;
            restored.time = raw_base;
            restored.restore_elapsed_time(900000000, saved_elapsed);
            EXPECT_EQ(restored.time, raw_base);
            EXPECT_EQ(restored.elapsed_time_at(900000000), saved_elapsed);
            EXPECT_EQ(restored.elapsed_time_at(900000009), saved_elapsed + 9);
            restored.time = 1234; // Existing guest SetTimerTime contract: a raw base assignment.
            EXPECT_EQ(restored.elapsed_time_at(900000009), saved_elapsed + raw_base - 1234 + 9);
        }
    }
}
TEST(KernelTimerClock, actual_exports_preserve_guest_base_and_saved_elapsed_then_start_stop_reset_bias) {
    EmuEnvState env;
    const auto uid = timer_create(env.kernel, env.mem, "test", "timer", 1, 0);
    auto timer = env.kernel.timers.at(uid);
    timer->time = 7000;
    const auto before_restore = timer_test_now();
    timer->restore_elapsed_time(before_restore, 12345);
    EXPECT_EQ(export_sceKernelGetTimerBaseWide(env, 1, "test", uid), 7000u);
    const auto elapsed = export_sceKernelGetTimerTimeWide(env, 1, "test", uid);
    EXPECT_GE(elapsed, 12345u);
    EXPECT_LE(elapsed, 12345 + timer_test_now() - before_restore);
    EXPECT_EQ(export_sceKernelSetTimerTimeWide(env, 1, "test", uid, 1234), 7000);
    EXPECT_EQ(export_sceKernelGetTimerBaseWide(env, 1, "test", uid), 1234u);
    const auto after_set = export_sceKernelGetTimerTimeWide(env, 1, "test", uid);
    EXPECT_GE(after_set, elapsed + 7000 - 1234);
    EXPECT_EQ(timer_start(env.kernel, "test", 1, uid), SCE_KERNEL_OK);
    EXPECT_EQ(timer->elapsed_clock_bias, 0u);
    const auto started = timer_test_now();
    EXPECT_LE(export_sceKernelGetTimerTimeWide(env, 1, "test", uid), timer_test_now() - started + 100000);
    timer->restore_elapsed_time(timer_test_now(), 99);
    EXPECT_EQ(timer_stop(env.kernel, "test", 1, uid), SCE_KERNEL_OK);
    EXPECT_EQ(timer->elapsed_clock_bias, 0u);
    EXPECT_FALSE(timer->is_started);
}
