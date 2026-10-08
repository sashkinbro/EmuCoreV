#include <emucorev/savestate/operation_mutex.h>
#include <gtest/gtest.h>
#include <functional>
#include <future>

namespace save_state_operation_test {
std::mutex *ui_mutex();
int read_and_pause_ui_session(const std::function<void()> &, const std::function<int()> &);
}

TEST(SaveStateOperation, SharesOneMutexAcrossStartupAndUiTranslationUnits) {
    EXPECT_EQ(&emucorev::savestate::save_state_operation_mutex(), save_state_operation_test::ui_mutex());
}

TEST(SaveStateOperation, UiReadAndPauseWaitForStartupRestoreOrFailureCleanup) {
    using namespace std::chrono_literals;
    for (const bool restore_succeeds : { true, false }) {
        std::unique_lock startup(emucorev::savestate::save_state_operation_mutex());
        int session_phase = 1; // load_and_run has published Running, restore is unfinished.
        bool ui_paused = false;
        std::promise<void> attempting;
        auto attempted = attempting.get_future();
        auto ui = std::async(std::launch::async, [&]() {
            return save_state_operation_test::read_and_pause_ui_session(
                [&]() { attempting.set_value(); }, [&]() { ui_paused = true; return session_phase; });
        });
        attempted.wait();
        const auto blocked = ui.wait_for(20ms);
        // The startup owner completes restoration or tears down the failed launch
        // while still owning the same lock used by JNI reads and pause requests.
        session_phase = restore_succeeds ? 2 : 0;
        startup.unlock();
        EXPECT_EQ(ui.get(), restore_succeeds ? 2 : 0);
        EXPECT_EQ(blocked, std::future_status::timeout);
        EXPECT_TRUE(ui_paused);
    }
}
