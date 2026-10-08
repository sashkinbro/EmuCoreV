#include <util/tracy.h>
#include <io/types.h>
#include <io/state.h>
#include <emuenv/app_util.h>
#include <emuenv/state.h>
#include <SceAppUtil/SceAppUtil.h>
#include <touch/functions.h>
#include <touch/state.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <renderer/gxm_types.h>

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <future>
#include <thread>

namespace {
template <typename Predicate>
bool await_waiter(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    do {
        if (ready())
            return true;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
}

DECL_EXPORT(int, sceGxmColorSurfaceInit, SceGxmColorSurface *surface, SceGxmColorFormat colorFormat, SceGxmColorSurfaceType surfaceType, SceGxmColorSurfaceScaleMode scaleMode, SceGxmOutputRegisterSize outputRegisterSize, uint32_t width, uint32_t height, uint32_t strideInPixels, Ptr<void> data);
DECL_EXPORT(void, sceGxmColorSurfaceGetClip, const SceGxmColorSurface *surface, uint32_t *xMin, uint32_t *yMin, uint32_t *xMax, uint32_t *yMax);
DECL_EXPORT(void, sceGxmColorSurfaceSetClip, SceGxmColorSurface *surface, uint32_t xMin, uint32_t yMin, uint32_t xMax, uint32_t yMax);

TEST(GuestUserName, bounded_buffer_uses_the_configured_name_and_preserves_canaries) {
    EmuEnvState env;
    env.io.user_name = "Player";
    std::array<SceChar8, 64> buffer;
    buffer.fill(static_cast<SceChar8>(0x5A));
    EXPECT_EQ(export_sceAppUtilSystemParamGetString(env, 0, "test", SCE_SYSTEM_PARAM_ID_USER_NAME, buffer.data(), 3), 0);
    EXPECT_STREQ(reinterpret_cast<char *>(buffer.data()), "Pl");
    for (size_t index = 3; index < buffer.size(); ++index)
        EXPECT_EQ(buffer[index], 0x5A) << index;
}

TEST(GuestUserName, one_byte_buffer_is_a_terminated_empty_string) {
    EmuEnvState env;
    env.io.user_name = "Player";
    std::array<SceChar8, 64> buffer;
    buffer.fill(static_cast<SceChar8>(0x5A));
    EXPECT_EQ(export_sceAppUtilSystemParamGetString(env, 0, "test", SCE_SYSTEM_PARAM_ID_USER_NAME, buffer.data(), 1), 0);
    EXPECT_EQ(buffer[0], 0);
    EXPECT_EQ(buffer[1], 0x5A);
}

TEST(GuestUserName, empty_or_null_buffer_is_rejected_without_writes) {
    EmuEnvState env;
    SceChar8 sentinel = 0x5A;
    EXPECT_NE(export_sceAppUtilSystemParamGetString(env, 0, "test", SCE_SYSTEM_PARAM_ID_USER_NAME, &sentinel, 0), 0);
    EXPECT_EQ(sentinel, 0x5A);
    EXPECT_NE(export_sceAppUtilSystemParamGetString(env, 0, "test", SCE_SYSTEM_PARAM_ID_USER_NAME, nullptr, 1), 0);
}

TEST(GuestTouch, paused_sampling_peek_still_returns_one_sample) {
    EmuEnvState env;
    env.touch.touch_mode[SCE_TOUCH_PORT_FRONT] = SCE_TOUCH_SAMPLING_STATE_STOP;
    std::array<SceTouchData, 2> samples{};
    samples[1].reportNum = 0x5A;
    EXPECT_EQ(touch_get(0, env, SCE_TOUCH_PORT_FRONT, samples.data(), 1, true), 1);
    EXPECT_EQ(samples[1].reportNum, 0x5A);
}

TEST(GuestTouch, intercepted_input_returns_one_empty_sample_and_zero_count_is_safe) {
    EmuEnvState env;
    env.drop_inputs = true;
    SceTouchData sample;
    std::memset(&sample, 0x5A, sizeof(sample));
    EXPECT_EQ(touch_get(0, env, SCE_TOUCH_PORT_FRONT, &sample, 1, true), 1);
    EXPECT_EQ(sample.reportNum, 0);
    sample.reportNum = 0x5A;
    EXPECT_EQ(touch_get(0, env, SCE_TOUCH_PORT_FRONT, &sample, 0, false), 0);
    EXPECT_EQ(sample.reportNum, 0x5A);
}

TEST(GuestTouch, canceled_pointer_releases_only_its_contact_and_ignores_duplicate_cancel) {
    EmuEnvState env;
    SDL_TouchFingerEvent finger{};
    finger.type = SDL_EVENT_FINGER_DOWN;
    finger.fingerID = 11;
    handle_touch_event(env.touch, finger);
    finger.fingerID = 22;
    handle_touch_event(env.touch, finger);
    ASSERT_EQ(env.touch.finger_count, 2);
    const auto remaining_touch_id = env.touch.finger_buffer[1].touchID;

    finger.type = SDL_EVENT_FINGER_CANCELED;
    finger.fingerID = 11;
    handle_touch_event(env.touch, finger);
    ASSERT_EQ(env.touch.finger_count, 1);
    EXPECT_EQ(env.touch.finger_buffer[0].fingerID, 22);
    EXPECT_EQ(env.touch.finger_buffer[0].touchID, remaining_touch_id);
    handle_touch_event(env.touch, finger);
    EXPECT_EQ(env.touch.finger_count, 1);

    finger.fingerID = 22;
    handle_touch_event(env.touch, finger);
    EXPECT_EQ(env.touch.finger_count, 0);
}

TEST(GuestSimpleEvent, named_open_and_delete_leave_unrelated_event_flags_intact) {
    EmuEnvState env;
    const auto event = simple_event_create(env.kernel, env.mem, "test", "named-event", 0, 0, 1);
    ASSERT_GT(event, 0);
    const auto flag = std::make_shared<EventFlag>(0);
    env.kernel.eventflags.emplace(event, flag);
    EXPECT_EQ(simple_event_find(env.kernel, "test", "named-event"), event);
    EXPECT_EQ(simple_event_delete(env.kernel, "test", 0, event), 0);
    EXPECT_EQ(env.kernel.simple_events.count(event), 0u);
    EXPECT_EQ(env.kernel.eventflags.at(event), flag);
    EXPECT_LT(simple_event_find(env.kernel, "test", "named-event"), 0);
}

TEST(GuestSimpleEvent, invalid_names_fail_without_creating_objects) {
    EmuEnvState env;
    EXPECT_LT(simple_event_create(env.kernel, env.mem, "test", nullptr, 0, 0, 0), 0);
    EXPECT_LT(simple_event_find(env.kernel, "test", nullptr), 0);
    EXPECT_LT(simple_event_find(env.kernel, "test", "a-name-longer-than-thirty-one-characters"), 0);
    EXPECT_TRUE(env.kernel.simple_events.empty());
}

TEST(GuestSemaphore, cancel_validates_the_requested_count_and_preserves_state_on_failure) {
    EmuEnvState env;
    const auto id = semaphore_create(env.kernel, "test", "semaphore", 0, 0, 2, 4);
    ASSERT_GT(id, 0);
    const auto semaphore = env.kernel.semaphores.at(id);
    SceUInt32 waiters = 0x5A;
    EXPECT_EQ(semaphore_cancel(env.kernel, "test", 0, id, 5, &waiters), SCE_KERNEL_ERROR_ILLEGAL_COUNT);
    EXPECT_EQ(semaphore->val, 2);
    EXPECT_EQ(waiters, 0x5Au);
    EXPECT_EQ(semaphore_cancel(env.kernel, "test", 0, id, 4, &waiters), 0);
    EXPECT_EQ(semaphore->val, 4);
    EXPECT_EQ(waiters, 0u);
    EXPECT_EQ(semaphore_cancel(env.kernel, "test", 0, id, -1, nullptr), 0);
    EXPECT_EQ(semaphore->val, 2);
}

TEST(GuestSemaphore, real_wait_queue_hands_over_counts_and_delivers_cancellation) {
    EmuEnvState env;
    const auto thread = std::make_shared<ThreadState>(10001, env.kernel, env.mem);
    thread->priority = 100;
    env.kernel.threads.emplace(thread->id, thread);
    const auto id = semaphore_create(env.kernel, "test", "blocked-semaphore", 0, 0, 0, 4);
    ASSERT_GT(id, 0);
    const auto semaphore = env.kernel.semaphores.at(id);
    const auto queued = [&] {
        const std::lock_guard lock(semaphore->mutex);
        return semaphore->waiters.size() == 1;
    };

    SceUInt32 timeout = 1000000;
    auto waiting = std::async(std::launch::async, [&] {
        return semaphore_wait(env.kernel, "test", thread->id, id, 2, &timeout, false);
    });
    const bool entered = await_waiter(queued);
    EXPECT_EQ(semaphore_signal(env.kernel, "test", 0, id, 2), 0);
    EXPECT_TRUE(entered);
    EXPECT_EQ(waiting.get(), 0);
    EXPECT_EQ(semaphore->val, 0);
    EXPECT_GT(timeout, 0u);

    timeout = 1000000;
    waiting = std::async(std::launch::async, [&] {
        return semaphore_wait(env.kernel, "test", thread->id, id, 3, &timeout, false);
    });
    const bool queued_for_cancel = await_waiter(queued);
    SceUInt32 count = 0;
    EXPECT_EQ(semaphore_cancel(env.kernel, "test", 0, id, 1, &count), 0);
    EXPECT_TRUE(queued_for_cancel);
    EXPECT_EQ(waiting.get(), SCE_KERNEL_ERROR_WAIT_CANCEL);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(semaphore->val, 1);
}

TEST(GuestThreadEnd, actual_thread_wait_delivers_the_target_exit_status) {
    EmuEnvState env;
    const auto target = std::make_shared<ThreadState>(10001, env.kernel, env.mem);
    const auto waiter = std::make_shared<ThreadState>(10002, env.kernel, env.mem);
    waiter->priority = 100;
    target->status = ThreadStatus::running;
    SceInt32 exit_status = -77;
    auto waiting = std::async(std::launch::async, [&] {
        return target->wait_for_thread_end(waiter, &exit_status, false);
    });
    const bool entered = await_waiter([&] {
        const std::lock_guard lock(waiter->mutex);
        return waiter->status == ThreadStatus::waiting;
    });
    target->exit(73);
    {
        const std::lock_guard lock(target->mutex);
        target->update_status(ThreadStatus::dormant);
    }
    EXPECT_TRUE(entered);
    const auto result = waiting.get();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 0);
    EXPECT_EQ(exit_status, 73);
}

TEST(GuestColorSurface, initial_clip_covers_the_surface_and_changes_preserve_geometry) {
    EmuEnvState env;
    SceGxmColorSurface surface{};
    ASSERT_EQ(export_sceGxmColorSurfaceInit(env, 0, "test", &surface,
        SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
        SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
        960, 544, 960, Ptr<void>(0x10000)), 0);
    uint32_t xMin = 99, yMin = 99, xMax = 0, yMax = 0;
    export_sceGxmColorSurfaceGetClip(env, 0, "test", &surface, &xMin, &yMin, &xMax, &yMax);
    EXPECT_EQ(xMin, 0u);
    EXPECT_EQ(yMin, 0u);
    EXPECT_EQ(xMax, 959u);
    EXPECT_EQ(yMax, 543u);
    export_sceGxmColorSurfaceSetClip(env, 0, "test", &surface, 13, 17, 800, 500);
    export_sceGxmColorSurfaceGetClip(env, 0, "test", &surface, &xMin, &yMin, &xMax, &yMax);
    EXPECT_EQ(xMin, 13u);
    EXPECT_EQ(yMin, 17u);
    EXPECT_EQ(xMax, 800u);
    EXPECT_EQ(yMax, 500u);
    EXPECT_EQ(surface.width, 960u);
    EXPECT_EQ(surface.height, 544u);
    EXPECT_EQ(surface.data.address(), 0x10000u);
    export_sceGxmColorSurfaceGetClip(env, 0, "test", &surface, nullptr, nullptr, nullptr, nullptr);
}

TEST(GuestColorSurface, maximum_surface_has_representable_inclusive_clip_bounds) {
    EmuEnvState env;
    SceGxmColorSurface surface{};
    ASSERT_EQ(export_sceGxmColorSurfaceInit(env, 0, "test", &surface,
        SCE_GXM_COLOR_FORMAT_A8B8G8R8, SCE_GXM_COLOR_SURFACE_LINEAR,
        SCE_GXM_COLOR_SURFACE_SCALE_NONE, SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
        4096, 4096, 4096, Ptr<void>(0x10000)), 0);
    EXPECT_EQ(surface.clip_x_max, 4095u);
    EXPECT_EQ(surface.clip_y_max, 4095u);
}
