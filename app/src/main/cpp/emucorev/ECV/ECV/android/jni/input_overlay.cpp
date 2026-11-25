// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "android_state.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_log.h>
#include <ctrl/functions.h>
#include <jni.h>
#include <touch/functions.h>
#include <emucorev/savestate/operation_mutex.h>

#include <atomic>
#include <mutex>
#include <vector>

static int virtual_joystick_id = -1;
static SDL_Joystick *virtual_joystick = nullptr;

static bool refresh_overlay_controllers() {
    auto *emuenv = get_emuenv();
    auto *controller = get_app_session_controller();
    if (!emuenv || !controller || !controller->has_active_session() || !SDL_WasInit(SDL_INIT_GAMEPAD))
        return false;

    refresh_controllers(emuenv->ctrl, *emuenv);
    return true;
}

void detach_overlay_virtual_controller() {
    if (virtual_joystick)
        SDL_CloseJoystick(virtual_joystick);
    if (virtual_joystick_id >= 0)
        SDL_DetachVirtualJoystick(virtual_joystick_id);
    virtual_joystick = nullptr;
    virtual_joystick_id = -1;
    refresh_overlay_controllers();
}

bool attach_overlay_virtual_controller() {
    if (virtual_joystick)
        return refresh_overlay_controllers();

    detach_overlay_virtual_controller();

    if (!SDL_WasInit(SDL_INIT_GAMEPAD))
        return false;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "EmuCoreV Virtual Controller";

    virtual_joystick_id = SDL_AttachVirtualJoystick(&desc);
    if (virtual_joystick_id == 0) {
        SDL_Log("Could not create overlay virtual controller: %s", SDL_GetError());
        return false;
    }

    virtual_joystick = SDL_OpenJoystick(virtual_joystick_id);
    if (!virtual_joystick) {
        SDL_Log("Could not create virtual joystick: %s", SDL_GetError());
        detach_overlay_virtual_controller();
        return false;
    }

    return refresh_overlay_controllers();
}

bool overlay_virtual_controller_attached() {
    if (!virtual_joystick)
        return false;
    // An in-process relaunch (LoadExec, e.g. picking a game in the God of War Collection) resets
    // the ctrl runtime while our SDL joystick stays open: then the pad is no longer in the
    // controller list and every overlay press was ignored until the overlay was toggled. Report
    // "not attached" so the overlay re-attaches (which refreshes the controller list).
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return true;
    const std::lock_guard lock(emuenv->ctrl.mutex);
    return emuenv->ctrl.controllers.contains(static_cast<SDL_JoystickID>(virtual_joystick_id));
}

// Touch/rear-contact JNI calls come from the UI thread. They must never wait on the save-state operation mutex
// (a save/load can hold it for seconds): commands go into a small queue and are applied, in order, by whoever
// can take the mutex without waiting (the JNI caller via try_lock, otherwise the SDL main loop).
namespace {
struct PendingTouchCommand {
    enum class Kind { SetMode, Contact, Clear } kind;
    int a = 0; // mode or slot
    bool pressed = false;
    float x = 0.f;
    float y = 0.f;
};
constexpr size_t kMaxPendingTouchCommands = 512;
std::mutex pending_touch_mutex; // guards only the vector; never held across the save-state mutex
std::vector<PendingTouchCommand> pending_touch_commands;
std::atomic<bool> pending_touch_flag{ false };

void enqueue_touch_command(const PendingTouchCommand &command) {
    const std::lock_guard lock(pending_touch_mutex);
    auto &queue = pending_touch_commands;
    if (command.kind == PendingTouchCommand::Kind::Contact) {
        // Coalesce with the newest earlier command of the same slot (stop at a barrier).
        for (auto it = queue.rbegin(); it != queue.rend(); ++it) {
            if (it->kind != PendingTouchCommand::Kind::Contact)
                break;
            if (it->a != command.a)
                continue;
            if (it->pressed && command.pressed) {
                *it = command; // newer position replaces the pending one
                return;
            }
            if (!it->pressed && !command.pressed)
                return; // double release
            break;
        }
        if (command.pressed && queue.size() >= kMaxPendingTouchCommands)
            return; // overload: drop a press, never a release or clear
    } else if (command.kind == PendingTouchCommand::Kind::Clear) {
        // Pending contacts before a clear are moot; a pending mode change is not.
        std::erase_if(queue, [](const PendingTouchCommand &c) { return c.kind == PendingTouchCommand::Kind::Contact; });
    }
    queue.push_back(command);
    pending_touch_flag.store(true, std::memory_order_release);
}

// Caller owns save_state_operation_mutex().
void apply_pending_touch_commands_locked() {
    std::vector<PendingTouchCommand> commands;
    {
        const std::lock_guard lock(pending_touch_mutex);
        commands.swap(pending_touch_commands);
        pending_touch_flag.store(false, std::memory_order_release);
    }
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return; // no core: nothing stored, nothing to clear
    for (const auto &command : commands) {
        switch (command.kind) {
        case PendingTouchCommand::Kind::SetMode:
            set_touch_mode(emuenv->touch, command.a);
            break;
        case PendingTouchCommand::Kind::Clear:
            clear_touch_contacts(emuenv->touch);
            break;
        case PendingTouchCommand::Kind::Contact: {
            // A release is always applied so a contact can never stay stuck down; only new presses are gated.
            if (command.pressed) {
                auto *controller = get_app_session_controller();
                if (!controller || !controller->is_running())
                    continue;
                if (emuenv->drop_inputs || emuenv->ctrl.overlay_input_intercepted.load(std::memory_order_relaxed))
                    continue;
            }
            set_rear_touch_contact(emuenv->touch, command.a, command.pressed, command.x, command.y);
            break;
        }
        }
    }
}

void submit_touch_command(const PendingTouchCommand &command) {
    enqueue_touch_command(command);
    std::unique_lock lock(emucorev::savestate::save_state_operation_mutex(), std::try_to_lock);
    if (lock.owns_lock())
        apply_pending_touch_commands_locked();
}
} // namespace

// Called by the SDL main loop each iteration; takes the mutex only when something is queued.
void apply_pending_touch_commands() {
    if (!pending_touch_flag.load(std::memory_order_acquire))
        return;
    const std::lock_guard operation(emucorev::savestate::save_state_operation_mutex());
    apply_pending_touch_commands_locked();
}

extern "C" {

JNIEXPORT void JNICALL
ECV_NativeBridge_overlay_InputOverlay_attachController(JNIEnv *env, jobject thiz) {
    attach_overlay_virtual_controller();
}

JNIEXPORT jboolean JNICALL
ECV_NativeBridge_overlay_InputOverlay_isControllerAttached(JNIEnv *env, jobject thiz) {
    return overlay_virtual_controller_attached() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
ECV_NativeBridge_overlay_InputOverlay_detachController(JNIEnv *env, jobject thiz) {
    detach_overlay_virtual_controller();
}

JNIEXPORT void JNICALL
ECV_NativeBridge_overlay_InputOverlay_setAxis(JNIEnv *env, jobject thiz, jint axis, jshort value) {
    if (!virtual_joystick)
        return;

    SDL_SetJoystickVirtualAxis(virtual_joystick, axis, value);
}

JNIEXPORT void JNICALL
ECV_NativeBridge_overlay_InputOverlay_setButton(JNIEnv *env, jobject thiz, jint button, jboolean value) {
    if (!virtual_joystick)
        return;

    if (button == -SDL_GAMEPAD_AXIS_LEFT_TRIGGER || button == -SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
        // l2/r2
        SDL_SetJoystickVirtualAxis(virtual_joystick, -button, value ? SDL_MAX_SINT16 : SDL_MIN_SINT16);
    else if (button >= 0 && button < SDL_GAMEPAD_BUTTON_COUNT)
        SDL_SetJoystickVirtualButton(virtual_joystick, button, value);
}

JNIEXPORT void JNICALL
ECV_NativeBridge_overlay_InputOverlay_setTouchState(JNIEnv *env, jobject thiz, jint mode) {
    submit_touch_command({ PendingTouchCommand::Kind::SetMode, static_cast<int>(mode) });
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_input_RearTouchNative_setContact(JNIEnv *, jobject, jint slot, jboolean pressed, jfloat x, jfloat y) {
    submit_touch_command({ PendingTouchCommand::Kind::Contact, static_cast<int>(slot), pressed == JNI_TRUE, x, y });
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_input_RearTouchNative_clearContacts(JNIEnv *, jobject) {
    submit_touch_command({ PendingTouchCommand::Kind::Clear });
}

} // extern "C"
