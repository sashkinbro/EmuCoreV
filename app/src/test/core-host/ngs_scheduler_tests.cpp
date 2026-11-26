#include <gtest/gtest.h>

#include <ngs/scheduler.h>

namespace {

TEST(NgsSchedulerRelease, SystemReleaseOutsideUpdateIsImmediate) {
    ngs::VoiceScheduler scheduler;
    EXPECT_EQ(scheduler.system_release_action(7), ngs::SystemReleaseAction::Immediate);
}

TEST(NgsSchedulerRelease, SystemReleaseFromUpdateThreadIsRefused) {
    ngs::VoiceScheduler scheduler;
    scheduler.is_updating = true;
    scheduler.update_thread_id = 7;
    scheduler.set_callback_kind(ngs::UpdateCallbackKind::VoiceFinished);
    EXPECT_EQ(scheduler.system_release_action(7), ngs::SystemReleaseAction::RejectReentrant);
}

TEST(NgsSchedulerRelease, SystemReleaseFromOtherThreadWaits) {
    ngs::VoiceScheduler scheduler;
    scheduler.is_updating = true;
    scheduler.update_thread_id = 7;
    EXPECT_EQ(scheduler.system_release_action(8), ngs::SystemReleaseAction::Wait);
}

TEST(NgsSchedulerRelease, RackReleaseFromUpdateThreadNeverWaits) {
    ngs::VoiceScheduler scheduler;
    scheduler.is_updating = true;
    scheduler.update_thread_id = 7;
    const ngs::UpdateCallbackKind kinds[] = {
        ngs::UpdateCallbackKind::None,
        ngs::UpdateCallbackKind::ModuleProcess,
        ngs::UpdateCallbackKind::VoiceFinished,
        ngs::UpdateCallbackKind::RackReleaseCompletion,
    };
    for (const auto kind : kinds) {
        scheduler.set_callback_kind(kind);
        EXPECT_NE(scheduler.rack_release_action(7, false), ngs::RackReleaseAction::Wait);
        // With a completion callback the release is deferred past the update.
        EXPECT_EQ(scheduler.rack_release_action(7, true), ngs::RackReleaseAction::DeferCallback);
    }
    EXPECT_EQ(scheduler.rack_release_action(8, false), ngs::RackReleaseAction::Wait);
}

TEST(NgsSchedulerRelease, RackReleaseIsIdempotentWhilePending) {
    ngs::VoiceScheduler scheduler;
    scheduler.is_updating = true;
    scheduler.update_thread_id = 7;
    EXPECT_EQ(scheduler.rack_release_action(7, true, true), ngs::RackReleaseAction::RejectAlreadyPending);
}

} // namespace
