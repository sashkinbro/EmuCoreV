#pragma once

#include <app/session_controller.h>

namespace emucorev::savestate {

class ScopedSaveStateDrain {
public:
    explicit ScopedSaveStateDrain(app::AppSessionController *controller) {
        if (controller && controller->set_save_state_draining(true))
            controller_ = controller;
    }
    ~ScopedSaveStateDrain() { finish(); }
    void finish() {
        if (controller_) {
            controller_->set_save_state_draining(false);
            controller_ = nullptr;
        }
    }
    explicit operator bool() const { return controller_ != nullptr; }
    ScopedSaveStateDrain(const ScopedSaveStateDrain &) = delete;
    ScopedSaveStateDrain &operator=(const ScopedSaveStateDrain &) = delete;

private:
    app::AppSessionController *controller_ = nullptr;
};

// Keep guest threads and audio paused throughout Save/Load reconstruction.
// Releasing this reason preserves independent menu/background/user pauses.
class ScopedSaveStatePause {
public:
    explicit ScopedSaveStatePause(app::AppSessionController *controller) {
        if (controller && controller->set_pause_reason(app::AppSessionPauseReason::SaveState, true))
            controller_ = controller;
    }

    ~ScopedSaveStatePause() {
        if (controller_)
            controller_->set_pause_reason(app::AppSessionPauseReason::SaveState, false);
    }

    explicit operator bool() const { return controller_ != nullptr; }
    ScopedSaveStatePause(const ScopedSaveStatePause &) = delete;
    ScopedSaveStatePause &operator=(const ScopedSaveStatePause &) = delete;

private:
    app::AppSessionController *controller_ = nullptr;
};

} // namespace emucorev::savestate
