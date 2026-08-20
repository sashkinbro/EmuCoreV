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

#include <ctrl/state.h>
#include <dialog/state.h>
#include <display/functions.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <kernel/state.h>
#include <touch/functions.h>
#include <touch/state.h>
#include <touch/touch.h>

#include <SDL3/SDL_events.h>

#include <cmath>
#include <cstring>

void set_rear_touchscreen(TouchState &state, bool is_back) {
    state.touchscreen_port = static_cast<SceTouchPortType>(is_back);
}

static void reset_pinch(TouchState &state) {
    state.pinch_dist = TouchState::initial_pinch_dist;
}

static bool is_common_dialog_running(const EmuEnvState &emuenv) {
    return emuenv.common_dialog.type != NO_DIALOG
        && emuenv.common_dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING;
}

static bool normalized_touch_to_report(float x, float y, SceTouchPortType port, SceTouchReport &report) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0.f || x > 1.f || y < 0.f || y > 1.f)
        return false;

    report.x = static_cast<SceInt16>(std::lround(x * 1919.f));
    report.y = port == SCE_TOUCH_PORT_FRONT
        ? static_cast<SceInt16>(std::lround(y * 1087.f))
        : static_cast<SceInt16>(108 + std::lround(y * 781.f));
    return true;
}

static SceTouchData recover_touch_events(const EmuEnvState &emuenv) {
    const auto &touch = emuenv.touch;
    SceTouchData touch_data;
    memset(&touch_data, 0, sizeof(touch_data));

    if (emuenv.display.viewport_w <= 0 || emuenv.display.viewport_h <= 0
        || emuenv.display.viewport_drawable_w <= 0 || emuenv.display.viewport_drawable_h <= 0) {
        return touch_data;
    }

    for (uint8_t i = 0; i < touch.finger_count; i++) {
        const float x = (touch.finger_buffer[i].x * emuenv.display.viewport_drawable_w - emuenv.display.viewport_x) / emuenv.display.viewport_w;
        const float y = (touch.finger_buffer[i].y * emuenv.display.viewport_drawable_h - emuenv.display.viewport_y) / emuenv.display.viewport_h;
        auto &report = touch_data.report[touch_data.reportNum];
        if (!normalized_touch_to_report(x, y, touch.touchscreen_port, report))
            continue;

        report.id = static_cast<uint8_t>(touch.finger_buffer[i].touchID);
        report.force = touch.force_touch_enabled[touch.touchscreen_port] ? 128 : 0;
        ++touch_data.reportNum;
    }

    return touch_data;
}

static SceTouchData recover_touchpad_events(const EmuEnvState &emuenv) {
    const auto &touch = emuenv.touch;
    SceTouchData touch_data;
    memset(&touch_data, 0, sizeof(touch_data));

    for (uint8_t i = 0; i < touch.touchpad_finger_count; i++) {
        auto &report = touch_data.report[touch_data.reportNum];
        if (!normalized_touch_to_report(touch.touchpad_buffer[i].x, touch.touchpad_buffer[i].y, touch.touchscreen_port, report))
            continue;

        report.id = static_cast<uint8_t>(touch.touchpad_buffer[i].which);
        report.force = touch.force_touch_enabled[touch.touchscreen_port] ? 128 : 0;
        ++touch_data.reportNum;
    }

    return touch_data;
}

void touch_vsync_update(EmuEnvState &emuenv) {
    auto &touch = emuenv.touch;
    std::chrono::time_point<std::chrono::steady_clock> ts = std::chrono::steady_clock::now();
    uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(ts.time_since_epoch()).count();

    // disable mouse support on android because the touchscreen is considered as a mouse, and this creates a mess
#ifdef __ANDROID__
    constexpr bool on_android = true;
#else
    constexpr bool on_android = false;
#endif
    if (touch.finger_count > 0 || touch.touchpad_finger_count > 0 || on_android) {
        SceTouchData touch_data = touch.is_touchpad ? recover_touchpad_events(emuenv) : recover_touch_events(emuenv);
        touch_data.timeStamp = timestamp;

        SceTouchData *buffers = touch.touch_buffers[(touch.touch_buffer_idx + 1) % MAX_TOUCH_BUFFER_SAVED];
        for (int port = 0; port < 2; port++) {
            buffers[port].status = 0;
            buffers[port].reportNum = 0;
            buffers[port].timeStamp = timestamp;
        }
        buffers[touch.touchscreen_port] = touch_data;

    } else {
        const auto &ts = emuenv.touch;
        const bool allow_mouse_touch = ts.renderer_focused && !is_common_dialog_running(emuenv);

        float mouse_x = ts.mouse_x;
        float mouse_y = ts.mouse_y;
        bool left = ts.mouse_button_left;
        bool right = ts.mouse_button_right;
        left = left || touch.pinch_modifier_enabled;

        for (int port = 0; port < 2; port++) {
            // do it for both the front and the back touchscreen
            SceTouchData *data = &touch.touch_buffers[(touch.touch_buffer_idx + 1) % MAX_TOUCH_BUFFER_SAVED][port];
            memset(data, 0, sizeof(SceTouchData));
            data->timeStamp = timestamp;

            const bool pressed = (port == SCE_TOUCH_PORT_BACK) ? right : left;
            if (pressed && allow_mouse_touch) {
                if (!touch.is_touched[port]) {
                    touch.curr_touch_id[port]++;
                    // the touch id must be between 0 and 127
                    touch.curr_touch_id[port] %= 128;
                    touch.is_touched[port] = true;
                }

                const SceFVector2 touch_pos_viewport = {
                    (mouse_x - emuenv.display.viewport_x) / emuenv.display.viewport_w,
                    (mouse_y - emuenv.display.viewport_y) / emuenv.display.viewport_h
                };

                if ((touch_pos_viewport.x >= 0) && (touch_pos_viewport.y >= 0) && (touch_pos_viewport.x < 1) && (touch_pos_viewport.y < 1)) {
                    data->report[data->reportNum].x = static_cast<uint16_t>(touch_pos_viewport.x * 1920);
                    if (port == SCE_TOUCH_PORT_FRONT) {
                        data->report[data->reportNum].y = static_cast<uint16_t>(touch_pos_viewport.y * 1088);
                    } else {
                        data->report[data->reportNum].y = static_cast<uint16_t>(108 + touch_pos_viewport.y * 781);
                    }
                    data->report[data->reportNum].id = touch.curr_touch_id[port];
                    ++data->reportNum;

                    if (touch.pinch_modifier_enabled) {
                        if (touch.pinch_velocity != 0) {
                            pinch_move(touch, touch.pinch_velocity);
                        }

                        const SceInt16 baseTouchposX = data->report[data->reportNum - 1].x;

                        data->report[data->reportNum - 1].x = baseTouchposX - touch.pinch_dist;
                        data->report[data->reportNum].x = baseTouchposX + touch.pinch_dist;
                        data->report[data->reportNum].y = data->report[data->reportNum - 1].y;
                        data->report[data->reportNum].id = data->report[data->reportNum - 1].id + 1;
                        ++data->reportNum;

                        // QOL loop pinch when going too far, this is convenient when you want multiple consecutive pinches without releasing the modifier key
                        if (data->report[data->reportNum - 1].x < baseTouchposX || data->report[data->reportNum - 2].x < 0) {
                            reset_pinch(touch);
                        }
                    }
                }

                if (!emuenv.touch.touch_mode[port]) {
                    data->reportNum = 0;
                }
            } else {
                touch.is_touched[port] = false;
            }
        }
    }

    touch.touch_buffer_idx++;
    touch.touch_buffer_idx %= MAX_TOUCH_BUFFER_SAVED;
}

void pinch_modifier(TouchState &state, bool isHold) {
    state.pinch_modifier_enabled = isHold;

    // reset pinch state when modifier key is released
    if (!isHold) {
        reset_pinch(state);
    }
}

void pinch_move(TouchState &state, float velocity) {
    state.pinch_dist += velocity * state.pinch_speed;
}

void pinch_automove(TouchState &state, float velocity) {
    state.pinch_velocity = velocity;
}

int handle_touch_event(TouchState &state, SDL_TouchFingerEvent &finger) {
    switch (finger.type) {
    case SDL_EVENT_FINGER_DOWN: {
        for (uint8_t i = 0; i < state.finger_count; ++i) {
            if (finger.fingerID == state.finger_buffer[i].fingerID) {
                const SDL_TouchID touch_id = state.finger_buffer[i].touchID;
                state.finger_buffer[i] = finger;
                state.finger_buffer[i].touchID = touch_id;
                return 0;
            }
        }
        if (state.finger_count >= SCE_TOUCH_MAX_REPORT)
            return 0;

        state.finger_buffer[state.finger_count] = finger;
        state.finger_buffer[state.finger_count].touchID = state.next_touch_id;
        state.next_touch_id = (state.next_touch_id + 1) % 128;
        state.finger_count++;
        break;
    }
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        int finger_index = -1;
        for (int i = 0; i < state.finger_count; i++) {
            if (finger.fingerID == state.finger_buffer[i].fingerID) {
                finger_index = i;
                break;
            }
        }
        if (finger_index < 0)
            return 0;
        for (int i = finger_index + 1; i < state.finger_count; ++i)
            state.finger_buffer[i - 1] = state.finger_buffer[i];
        --state.finger_count;
        break;
    }

    case SDL_EVENT_FINGER_MOTION: {
        for (int i = 0; i < state.finger_count; i++) {
            if (finger.fingerID == state.finger_buffer[i].fingerID) {
                SDL_TouchID touch_id = state.finger_buffer[i].touchID;
                state.finger_buffer[i] = finger;
                state.finger_buffer[i].touchID = touch_id;
            }
        }
        break;
    }
    }

    return 0;
}

int handle_touchpad_event(TouchState &state, SDL_GamepadTouchpadEvent &touchpad) {
    switch (touchpad.type) {
    case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN:
        for (uint8_t i = 0; i < state.touchpad_finger_count; ++i) {
            if (touchpad.finger == state.touchpad_buffer[i].finger) {
                const auto touch_id = state.touchpad_buffer[i].which;
                state.touchpad_buffer[i] = touchpad;
                state.touchpad_buffer[i].which = touch_id;
                return 0;
            }
        }
        if (state.touchpad_finger_count >= SCE_TOUCH_MAX_REPORT)
            return 0;

        state.touchpad_buffer[state.touchpad_finger_count] = touchpad;
        state.touchpad_buffer[state.touchpad_finger_count].which = state.next_touch_id;
        state.next_touch_id = (state.next_touch_id + 1) % 128;
        state.touchpad_finger_count++;
        break;
    case SDL_EVENT_GAMEPAD_TOUCHPAD_UP: {
        int finger_index = -1;
        for (uint8_t i = 0; i < state.touchpad_finger_count; ++i) {
            if (touchpad.finger == state.touchpad_buffer[i].finger) {
                finger_index = i;
                break;
            }
        }
        if (finger_index < 0)
            return 0;
        for (int i = finger_index + 1; i < state.touchpad_finger_count; ++i)
            state.touchpad_buffer[i - 1] = state.touchpad_buffer[i];
        --state.touchpad_finger_count;
        break;
    }
    case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION:
        for (int i = 0; i < state.touchpad_finger_count; i++) {
            if (touchpad.finger == state.touchpad_buffer[i].finger) {
                const auto touch_id = state.touchpad_buffer[i].which;
                state.touchpad_buffer[i] = touchpad;
                state.touchpad_buffer[i].which = touch_id;
            }
        }
        break;
    }

    state.is_touchpad = state.touchpad_finger_count > 0;

    return 0;
}

std::vector<SceFVector2> get_touchpad_fingers_pos(const TouchState &state, SceTouchPortType &port) {
    if (!state.is_touchpad)
        return {};

    std::vector<SceFVector2> touchpad_fingers_pos;
    touchpad_fingers_pos.reserve(state.touchpad_finger_count);
    for (int i = 0; i < state.touchpad_finger_count; i++) {
        touchpad_fingers_pos.push_back({ state.touchpad_buffer[i].x, state.touchpad_buffer[i].y });
    }

    port = state.touchscreen_port;

    return touchpad_fingers_pos;
}

int toggle_touchscreen(TouchState &state) {
    if (state.touchscreen_port == SCE_TOUCH_PORT_FRONT) {
        state.touchscreen_port = SCE_TOUCH_PORT_BACK;
    } else {
        state.touchscreen_port = SCE_TOUCH_PORT_FRONT;
    }
    return 0;
}

int touch_get(const SceUID thread_id, EmuEnvState &emuenv, const SceUInt32 &port, SceTouchData *pData, SceUInt32 count, bool is_peek) {
    memset(pData, 0, sizeof(SceTouchData) * count);
    if (emuenv.drop_inputs || emuenv.ctrl.overlay_input_intercepted.load(std::memory_order_relaxed))
        return 0;

    const int port_idx = static_cast<int>(port);

    int nb_returned_data = 1;
    if (is_peek) {
        if (emuenv.touch.touch_mode[port])
            nb_returned_data = count;
        else
            nb_returned_data = 0;
    } else {
        const uint64_t current_vcount = emuenv.display.vblank_count.load();
        if (current_vcount <= emuenv.touch.last_vcount[port_idx]) {
            // sceTouchRead is blocking, wait for the next vsync for the buffer to be updated
            auto thread = emuenv.kernel.get_thread(thread_id);

            wait_vblank(emuenv.display, emuenv.kernel, thread, emuenv.touch.last_vcount[port_idx] + 1, false);
        }
        uint64_t vblank_count = emuenv.display.vblank_count.load();
        nb_returned_data = std::min<int>(count, vblank_count - emuenv.touch.last_vcount[port_idx]);
        emuenv.touch.last_vcount[port_idx] = vblank_count;
    }

    int corr_buffer_idx;
    if (is_peek) {
        corr_buffer_idx = emuenv.touch.touch_buffer_idx;
    } else {
        // give the oldest buffer first
        corr_buffer_idx = (emuenv.touch.touch_buffer_idx - nb_returned_data + 1 + MAX_TOUCH_BUFFER_SAVED) % MAX_TOUCH_BUFFER_SAVED;
    }
    for (int32_t i = 0; i < nb_returned_data; i++) {
        pData[i] = emuenv.touch.touch_buffers[corr_buffer_idx][port_idx];
        if (emuenv.touch.force_touch_enabled[port_idx]) {
            for (int32_t j = 0; j < pData[i].reportNum; j++) {
                pData[i].report[j].force = 128;
            }
        }
        // if peek, repeat the last buffer
        if (!is_peek) {
            corr_buffer_idx++;
            corr_buffer_idx %= MAX_TOUCH_BUFFER_SAVED;
        }
    }

    return nb_returned_data;
}

void touch_set_force_mode(TouchState &state, int port, bool mode) {
    state.force_touch_enabled[port] = mode;
}
