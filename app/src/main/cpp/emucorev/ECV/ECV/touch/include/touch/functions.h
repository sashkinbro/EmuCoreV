#pragma once

#include <touch/touch.h>

#include <vector>

struct EmuEnvState;
struct TouchState;

std::vector<SceFVector2> get_touchpad_fingers_pos(const TouchState &state, SceTouchPortType &port);
int handle_touchpad_event(TouchState &state, SDL_GamepadTouchpadEvent &touchpad);
void touch_vsync_update(EmuEnvState &emuenv);
void pinch_modifier(TouchState &state, bool isHold);
void pinch_move(TouchState &state, float velocity);
void pinch_automove(TouchState &state, float velocity);
int handle_touch_event(TouchState &state, SDL_TouchFingerEvent &finger);
int toggle_touchscreen(TouchState &state);
int touch_get(const SceUID thread_id, EmuEnvState &emuenv, const SceUInt32 &port, SceTouchData *pData, SceUInt32 count, bool is_peek);
void touch_set_force_mode(TouchState &state, int port, bool mode);
// Live host contacts are never restored from a guest snapshot. Preserve routing/sampling.
void clear_touch_contacts(TouchState &state);
void release_touchpad_device(TouchState &state, uint32_t device_id);
void set_rear_touch_contact(TouchState &state, int slot, bool pressed, float x, float y);
void set_touch_sampling_state(TouchState &state, int port, SceTouchSamplingState mode);
#ifdef __ANDROID__
void set_touch_mode(TouchState &state, int mode); // 0 = front, 1 = back, 2 = front + back mirrored
#endif
