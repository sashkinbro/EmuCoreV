// Host tests for the state machine of the UI-less common dialogs (store checkout, NP, photo, camera):
// Init -> FINISHED (canceled) -> GetResult -> Term, one dialog at a time.
#include <dialog/stub_dialog.h>

#include <gtest/gtest.h>

using namespace stub_dialog;

TEST(StubDialog, InitMakesDialogRunningThenUpdateFinishesIt) {
    Core core;
    ASSERT_EQ(init(core, Kind::StoreCheckout, false), 0);
    EXPECT_EQ(get_status(core, Kind::StoreCheckout), STATUS_RUNNING);
    update(core);
    EXPECT_EQ(get_status(core, Kind::StoreCheckout), STATUS_FINISHED);
    update(core); // idempotent
    EXPECT_EQ(get_status(core, Kind::StoreCheckout), STATUS_FINISHED);
}

TEST(StubDialog, ResultIsCanceledAndNotAvailableWhileRunning) {
    Core core;
    ASSERT_EQ(init(core, Kind::PhotoImport, false), 0);
    int32_t result = 77;
    EXPECT_EQ(get_result(core, Kind::PhotoImport, result), ERR_NOT_FINISHED);
    EXPECT_EQ(result, 77);
    update(core);
    ASSERT_EQ(get_result(core, Kind::PhotoImport, result), 0);
    EXPECT_EQ(result, RESULT_USER_CANCELED);
}

TEST(StubDialog, TermReturnsToNone) {
    Core core;
    ASSERT_EQ(init(core, Kind::NpMessage, false), 0);
    EXPECT_EQ(term(core, Kind::NpMessage), ERR_NOT_FINISHED); // still running
    update(core);
    EXPECT_EQ(term(core, Kind::NpMessage), 0);
    EXPECT_EQ(get_status(core, Kind::NpMessage), STATUS_NONE);
    EXPECT_EQ(core.kind, Kind::None);
    // the dialog layer is free again
    EXPECT_EQ(init(core, Kind::NpProfile, false), 0);
}

TEST(StubDialog, OnlyOneDialogAtATime) {
    Core core;
    EXPECT_EQ(init(core, Kind::NpProfile, true), ERR_BUSY); // another dialog type owns the layer
    EXPECT_EQ(core.kind, Kind::None);
    ASSERT_EQ(init(core, Kind::NpProfile, false), 0);
    EXPECT_EQ(init(core, Kind::NpProfile, false), ERR_BUSY); // same dialog twice
    EXPECT_EQ(init(core, Kind::CameraImport, false), ERR_BUSY); // another stub dialog
}

TEST(StubDialog, CallsOnAnotherDialogKindAreNotInUse) {
    Core core;
    ASSERT_EQ(init(core, Kind::NpFriendList, false), 0);
    update(core);
    int32_t result = 0;
    EXPECT_EQ(get_status(core, Kind::NpFriendList2), STATUS_NONE);
    EXPECT_EQ(get_result(core, Kind::NpFriendList2, result), ERR_NOT_IN_USE);
    EXPECT_EQ(abort(core, Kind::NpFriendList2), ERR_NOT_IN_USE);
    EXPECT_EQ(term(core, Kind::NpFriendList2), ERR_NOT_IN_USE);
    EXPECT_EQ(get_status(core, Kind::NpFriendList), STATUS_FINISHED);
}

TEST(StubDialog, AbortWhileRunningReportsAborted) {
    Core core;
    ASSERT_EQ(init(core, Kind::PhotoReview, false), 0);
    EXPECT_EQ(abort(core, Kind::PhotoReview), 0);
    EXPECT_EQ(get_status(core, Kind::PhotoReview), STATUS_FINISHED);
    int32_t result = 0;
    ASSERT_EQ(get_result(core, Kind::PhotoReview, result), 0);
    EXPECT_EQ(result, RESULT_ABORTED);
}

TEST(StubDialog, AbortAfterFinishKeepsResult) {
    Core core;
    ASSERT_EQ(init(core, Kind::CameraImport, false), 0);
    update(core);
    EXPECT_EQ(abort(core, Kind::CameraImport), 0);
    int32_t result = 0;
    ASSERT_EQ(get_result(core, Kind::CameraImport, result), 0);
    EXPECT_EQ(result, RESULT_USER_CANCELED);
}

TEST(StubDialog, StatusOfUnusedDialogIsNone) {
    Core core;
    EXPECT_EQ(get_status(core, Kind::StoreCheckout), STATUS_NONE);
    int32_t result = 0;
    EXPECT_EQ(get_result(core, Kind::StoreCheckout, result), ERR_NOT_IN_USE);
    EXPECT_EQ(term(core, Kind::StoreCheckout), ERR_NOT_IN_USE);
    update(core); // no effect without a dialog
    EXPECT_EQ(core.status, STATUS_NONE);
}

TEST(StubDialog, ErrorCodesMatchCommonDialogHeader) {
    // psp2/common_dialog.h (vita-headers)
    EXPECT_EQ(static_cast<uint32_t>(ERR_BUSY), 0x80020401u);
    EXPECT_EQ(static_cast<uint32_t>(ERR_NULL), 0x80020402u);
    EXPECT_EQ(static_cast<uint32_t>(ERR_NOT_RUNNING), 0x80020404u);
    EXPECT_EQ(static_cast<uint32_t>(ERR_NOT_FINISHED), 0x80020410u);
    EXPECT_EQ(static_cast<uint32_t>(ERR_NOT_IN_USE), 0x80020411u);
}
