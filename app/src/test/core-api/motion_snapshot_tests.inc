#include <emucorev/savestate/motion_state_io.h>
#include <motion/event_handler.h>
#include <motion/motion.h>
#include <SDL3/SDL_timer.h>

DECL_EXPORT(int, sceMotionStartSampling);
DECL_EXPORT(int, sceMotionStopSampling);
DECL_EXPORT(int, sceMotionGetState, SceMotionState *state);
DECL_EXPORT(int, sceMotionSetGyroBiasCorrection, SceBool enabled);
DECL_EXPORT(int, sceMotionSetDeadband, SceBool enabled);
DECL_EXPORT(int, sceMotionSetTiltCorrection, SceBool enabled);
DECL_EXPORT(int, sceMotionSetAngleThreshold, SceFloat angle);
DECL_EXPORT(int, sceMotionRotateYaw, float radians);

TEST(MotionSnapshot, RealGuestSamplingSettingsAndOrientationSurviveCodecRestore) {
    EmuEnvState env;
    ASSERT_EQ(export_sceMotionStartSampling(env, 0, "motion-test"), SCE_MOTION_OK);
    ASSERT_EQ(export_sceMotionSetGyroBiasCorrection(env, 0, "motion-test", false), SCE_MOTION_OK);
    ASSERT_EQ(export_sceMotionSetDeadband(env, 0, "motion-test", false), SCE_MOTION_OK);
    ASSERT_EQ(export_sceMotionSetTiltCorrection(env, 0, "motion-test", false), SCE_MOTION_OK);
    ASSERT_EQ(export_sceMotionSetAngleThreshold(env, 0, "motion-test", 13.0f), SCE_MOTION_OK);
    ASSERT_EQ(export_sceMotionRotateYaw(env, 0, "motion-test", 0.75f), SCE_MOTION_OK);
    env.ctrl.has_motion_support = true;
    SceMotionState before{};
    ASSERT_EQ(export_sceMotionGetState(env, 0, "motion-test", &before), SCE_MOTION_OK);
    const auto saved = env.motion.capture_snapshot();
    emucorev::savestate::BufferWriter encoded;
    emucorev::savestate::write_motion_snapshot(encoded, saved);
    ASSERT_EQ(encoded.size(), 198u);
    ASSERT_EQ(export_sceMotionStopSampling(env, 0, "motion-test"), SCE_MOTION_OK);
    ASSERT_EQ(export_sceMotionGetState(env, 0, "motion-test", &before), SCE_MOTION_ERROR_NOT_SAMPLING);
    ASSERT_EQ(export_sceMotionRotateYaw(env, 0, "motion-test", 1.0f), SCE_MOTION_OK);
    emucorev::savestate::BufferReader reader(encoded.data().data(), encoded.size());
    MotionState::Snapshot parsed;
    std::string error;
    ASSERT_TRUE(emucorev::savestate::read_motion_snapshot(reader, parsed, error)) << error;
    ASSERT_EQ(reader.remaining(), 0u);
    ASSERT_TRUE(env.motion.apply_snapshot(parsed, error)) << error;
    SceMotionState after{};
    ASSERT_EQ(export_sceMotionGetState(env, 0, "motion-test", &after), SCE_MOTION_OK);
    EXPECT_EQ(std::memcmp(&before.deviceQuat, &after.deviceQuat, sizeof(before.deviceQuat)), 0);
    EXPECT_EQ(env.motion.motion_data.GetAngleThreshold(), 13.0f);
    EXPECT_FALSE(env.motion.motion_data.IsGyroBiasEnabled());
    EXPECT_FALSE(env.motion.motion_data.IsDeadbandEnabled());
    EXPECT_FALSE(env.motion.motion_data.IsTiltCorrectionEnabled());
    EXPECT_EQ(export_sceMotionStartSampling(env, 0, "motion-test"), SCE_MOTION_ERROR_ALREADY_SAMPLING);
    parsed.is_sampling = false;
    ASSERT_TRUE(env.motion.apply_snapshot(parsed, error));
    EXPECT_EQ(export_sceMotionGetState(env, 0, "motion-test", &after), SCE_MOTION_ERROR_NOT_SAMPLING);
}

TEST(MotionSnapshot, WarmIntegratorProducesIdenticalNextMotionFrame) {
    MotionInput original;
    original.SetAcceleration({ 0.2f, 0.3f, -0.9f });
    for (unsigned i = 0; i < 25; ++i) {
        original.SetGyroscope({ 0.1f, 0.2f, 0.3f });
        original.UpdateOrientation(16667);
        original.UpdateRotation(16667);
        original.UpdateBasicOrientation();
    }
    MotionState state;
    state.motion_data = original;
    const auto snapshot = state.capture_snapshot();
    MotionState restored;
    std::string error;
    ASSERT_TRUE(restored.apply_snapshot(snapshot, error)) << error;
    const auto next = [](MotionInput &input) {
        input.SetAcceleration({ 0.1f, 0.4f, -0.8f });
        input.SetGyroscope({ 0.2f, 0.1f, 0.4f });
        input.UpdateOrientation(16667);
        input.UpdateRotation(16667);
        input.UpdateBasicOrientation();
    };
    next(original);
    next(restored.motion_data);
    auto expected = snapshot;
    expected.data = original.capture_snapshot();
    emucorev::savestate::BufferWriter first, second;
    emucorev::savestate::write_motion_snapshot(first, expected);
    emucorev::savestate::write_motion_snapshot(second, restored.capture_snapshot());
    EXPECT_EQ(first.data(), second.data());
}

TEST(MotionSnapshot, InvalidClockOrNumericPayloadCannotMutateLiveState) {
    MotionState state;
    std::string error;
    const auto before = state.capture_snapshot();
    auto invalid = before;
    invalid.is_sampling = true;
    invalid.last_counter = 91;
    invalid.last_accel_timestamp = 20;
    invalid.last_updated_accel_timestamp = 19;
    EXPECT_FALSE(state.apply_snapshot(invalid, error));
    EXPECT_EQ(state.last_counter, before.last_counter);
    EXPECT_EQ(state.is_sampling, before.is_sampling);
    invalid = before;
    invalid.data.quat.w = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(state.apply_snapshot(invalid, error));
    emucorev::savestate::BufferWriter writer;
    emucorev::savestate::write_motion_snapshot(writer, before);
    auto bytes = writer.data();
    bytes[4] = 2;
    emucorev::savestate::BufferReader reader(bytes.data(), bytes.size());
    auto output = before;
    output.last_counter = 77;
    EXPECT_FALSE(emucorev::savestate::read_motion_snapshot(reader, output, error));
    EXPECT_EQ(output.last_counter, 77u);
}

TEST(MotionSnapshot, RestoredSensorClockRejectsOldEventsAndExcludesLoadDowntime) {
    EmuEnvState env;
    env.ctrl.has_motion_support = true;
    auto saved = MotionState::legacy_snapshot();
    saved.last_accel_timestamp = 100;
    saved.last_updated_accel_timestamp = 110;
    std::string error;
    ASSERT_TRUE(env.motion.apply_snapshot(saved, error));
    // Establish a nonzero host event epoch without requiring physical sensors.
    env.motion.minimum_event_timestamp = 1000;
    SDL_SensorEvent event{};
    event.type = SDL_EVENT_SENSOR_UPDATE;
    event.timestamp = 999;
    event.sensor_timestamp = 9000000000;
    event.data[0] = SDL_STANDARD_GRAVITY;
    handle_motion_event(env, SDL_SENSOR_ACCEL, event);
    EXPECT_EQ(env.motion.motion_data.GetAcceleration().x, 0);
    event.timestamp = 1001;
    handle_motion_event(env, SDL_SENSOR_ACCEL, event);
    EXPECT_EQ(env.motion.last_updated_accel_timestamp, 110u);
    EXPECT_EQ(env.motion.motion_data.GetAcceleration().x, -1);
    event.sensor_timestamp += 16667000;
    handle_motion_event(env, SDL_SENSOR_ACCEL, event);
    EXPECT_EQ(env.motion.last_updated_accel_timestamp, 16777u);
    EXPECT_EQ(env.motion.accel_clock.map(9000001, 16777), 16777u);
}

TEST(MotionSnapshot, CaptureAndApplyHonorTheMotionMutex) {
    MotionState state;
    std::unique_lock lock(state.mutex);
    std::promise<void> entered;
    auto ready = entered.get_future();
    auto capture = std::async(std::launch::async, [&] {
        entered.set_value();
        return state.capture_snapshot();
    });
    ready.wait();
    EXPECT_EQ(capture.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
    lock.unlock();
    ASSERT_EQ(capture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    auto snapshot = capture.get();
    snapshot.is_sampling = true;
    std::string error;
    EXPECT_TRUE(state.apply_snapshot(snapshot, error));
    EXPECT_TRUE(state.capture_snapshot().is_sampling);
}
