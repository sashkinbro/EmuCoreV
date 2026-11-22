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

#include <module/module.h>

#include <ctrl/ctrl.h>
#include <ctrl/functions.h>
#include <kernel/types.h>
#include <util/log.h>
#include <util/tracy.h>

#include <SDL3/SDL_haptic.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <mutex>

TRACY_MODULE_NAME(SceCtrl);

template <>
std::string to_debug_str<SceCtrlPadInputMode>(const MemState &mem, SceCtrlPadInputMode type) {
    switch (type) {
    case SCE_CTRL_MODE_DIGITAL:
        return "SCE_CTRL_MODE_DIGITAL";
    case SCE_CTRL_MODE_ANALOG:
        return "SCE_CTRL_MODE_ANALOG";
    case SCE_CTRL_MODE_ANALOG_WIDE:
        return "SCE_CTRL_MODE_ANALOG_WIDE";
    }
    return std::to_string(type);
}

EXPORT(int, sceCtrlClearRapidFire) {
    TRACY_FUNC(sceCtrlClearRapidFire);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlDisconnect) {
    TRACY_FUNC(sceCtrlDisconnect);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlGetAnalogStickCheckMode) {
    TRACY_FUNC(sceCtrlGetAnalogStickCheckMode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlGetAnalogStickCheckTarget) {
    TRACY_FUNC(sceCtrlGetAnalogStickCheckTarget);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlGetBatteryInfo) {
    TRACY_FUNC(sceCtrlGetBatteryInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlGetButtonIntercept) {
    TRACY_FUNC(sceCtrlGetButtonIntercept);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlGetControllerPortInfo, SceCtrlPortInfo *info) {
    TRACY_FUNC(sceCtrlGetControllerPortInfo, info);
    info->port[0] = emuenv.cfg.current_config.pstv_mode ? SCE_CTRL_TYPE_VIRT : SCE_CTRL_TYPE_PHY;
    for (int i = 0; i < SCE_CTRL_MAX_WIRELESS_NUM; i++) {
        info->port[i + 1] = (emuenv.cfg.current_config.pstv_mode && !emuenv.ctrl.free_ports[i]) ? get_type_of_controller(emuenv.ctrl, i) : SCE_CTRL_TYPE_UNPAIRED;
    }
    return 0;
}

EXPORT(int, sceCtrlGetProcessStatus) {
    TRACY_FUNC(sceCtrlGetProcessStatus);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlGetSamplingMode, SceCtrlPadInputMode *mode) {
    TRACY_FUNC(sceCtrlGetSamplingMode, mode);
    if (mode == nullptr) {
        return RET_ERROR(SCE_CTRL_ERROR_INVALID_ARG);
    }
    *mode = emuenv.ctrl.input_mode;
    return SCE_KERNEL_OK;
}

EXPORT(int, sceCtrlGetSamplingModeExt, SceCtrlPadInputMode *mode) {
    TRACY_FUNC(sceCtrlGetSamplingModeExt, mode);
    if (mode == nullptr) {
        return RET_ERROR(SCE_CTRL_ERROR_INVALID_ARG);
    }
    *mode = emuenv.ctrl.input_mode_ext;
    return SCE_KERNEL_OK;
}

EXPORT(int, sceCtrlGetWirelessControllerInfo, SceCtrlWirelessControllerInfo *pInfo) {
    TRACY_FUNC(sceCtrlGetWirelessControllerInfo, pInfo);
    memset(pInfo->connected, SCE_CTRL_WIRELESS_INFO_NOT_CONNECTED, sizeof(pInfo->connected));
    if (emuenv.cfg.current_config.pstv_mode) {
        CtrlState &state = emuenv.ctrl;
        for (auto i = 0; i < state.controllers_num; i++)
            pInfo->connected[i] = SCE_CTRL_WIRELESS_INFO_CONNECTED;
    }

    return 0;
}

EXPORT(bool, sceCtrlIsMultiControllerSupported) {
    TRACY_FUNC(sceCtrlIsMultiControllerSupported);
    return emuenv.cfg.current_config.pstv_mode;
}

EXPORT(int, sceCtrlPeekBufferNegative, int port, SceCtrlData *pad_data, int count) {
    TRACY_FUNC(sceCtrlPeekBufferNegative, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, reinterpret_cast<SceCtrlData2 *>(pad_data), count, true, true, false, false);
}

EXPORT(int, sceCtrlPeekBufferNegative2, int port, SceCtrlData2 *pad_data, int count) {
    TRACY_FUNC(sceCtrlPeekBufferNegative2, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, pad_data, count, true, true, true, false);
}

EXPORT(int, sceCtrlPeekBufferPositive, int port, SceCtrlData *pad_data, int count) {
    TRACY_FUNC(sceCtrlPeekBufferPositive, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, reinterpret_cast<SceCtrlData2 *>(pad_data), count, false, true, false, false);
}

EXPORT(int, sceCtrlPeekBufferPositive2, int port, SceCtrlData2 *pad_data, int count) {
    TRACY_FUNC(sceCtrlPeekBufferPositive2, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, pad_data, count, false, true, true, false);
}

EXPORT(int, sceCtrlPeekBufferPositiveExt, int port, SceCtrlData *pad_data, int count) {
    TRACY_FUNC(sceCtrlPeekBufferPositiveExt, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, reinterpret_cast<SceCtrlData2 *>(pad_data), count, false, true, false, true);
}

EXPORT(int, sceCtrlPeekBufferPositiveExt2, int port, SceCtrlData2 *pad_data, int count) {
    TRACY_FUNC(sceCtrlPeekBufferPositiveExt2, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, pad_data, count, false, true, true, true);
}

EXPORT(int, sceCtrlReadBufferNegative, int port, SceCtrlData *pad_data, int count) {
    TRACY_FUNC(sceCtrlReadBufferNegative, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, reinterpret_cast<SceCtrlData2 *>(pad_data), count, true, false, false, false);
}

EXPORT(int, sceCtrlReadBufferNegative2, int port, SceCtrlData2 *pad_data, int count) {
    TRACY_FUNC(sceCtrlReadBufferNegative2, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, pad_data, count, true, false, true, false);
}

EXPORT(int, sceCtrlReadBufferPositive, int port, SceCtrlData *pad_data, int count) {
    TRACY_FUNC(sceCtrlReadBufferPositive, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, reinterpret_cast<SceCtrlData2 *>(pad_data), count, false, false, false, false);
}

EXPORT(int, sceCtrlReadBufferPositive2, int port, SceCtrlData2 *pad_data, int count) {
    TRACY_FUNC(sceCtrlReadBufferPositive2, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, pad_data, count, false, false, true, false);
}

EXPORT(int, sceCtrlReadBufferPositiveExt, int port, SceCtrlData *pad_data, int count) {
    TRACY_FUNC(sceCtrlReadBufferPositiveExt, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, reinterpret_cast<SceCtrlData2 *>(pad_data), count, false, false, false, true);
}

EXPORT(int, sceCtrlReadBufferPositiveExt2, int port, SceCtrlData2 *pad_data, int count) {
    TRACY_FUNC(sceCtrlReadBufferPositiveExt2, port, pad_data, count);
    return ctrl_get(thread_id, emuenv, port, pad_data, count, false, false, true, true);
}

EXPORT(int, sceCtrlRegisterBdRMCCallback) {
    TRACY_FUNC(sceCtrlRegisterBdRMCCallback);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlResetLightBar) {
    TRACY_FUNC(sceCtrlResetLightBar);
    return UNIMPLEMENTED();
}

#ifdef __ANDROID__
// Phone vibrator, exposed by SDL's Android haptic driver. The Kotlin side applies the user's
// gamepad-vibration switch, strength and device-fallback setting to every request.
static void vibrate_phone(uint8_t small, uint8_t large) {
    static std::mutex mutex;
    static uint8_t last_strength = 0;
    static Uint64 last_play_ms = 0;
    const std::lock_guard lock(mutex);

    const uint8_t strength = std::max(small, large);
    const Uint64 now = SDL_GetTicks();
    // Games re-send the same state every frame; a pulse lasts 400 ms, so refresh only when it changes or expires.
    constexpr Uint32 PULSE_MS = 400;
    if (strength == last_strength && (strength == 0 || now - last_play_ms < PULSE_MS - 50))
        return;

    if (!SDL_WasInit(SDL_INIT_HAPTIC) && !SDL_InitSubSystem(SDL_INIT_HAPTIC))
        return;
    int count = 0;
    SDL_HapticID *ids = SDL_GetHaptics(&count);
    if (!ids)
        return;
    SDL_Haptic *haptic = nullptr;
    if (count > 0) {
        haptic = SDL_GetHapticFromID(ids[0]);
        if (!haptic) {
            haptic = SDL_OpenHaptic(ids[0]);
            if (haptic && !SDL_InitHapticRumble(haptic)) {
                SDL_CloseHaptic(haptic);
                haptic = nullptr;
            }
        }
    }
    SDL_free(ids);
    if (!haptic)
        return;

    last_strength = strength;
    last_play_ms = now;
    if (strength == 0)
        SDL_StopHapticRumble(haptic);
    else
        SDL_PlayHapticRumble(haptic, strength / 255.0f, PULSE_MS);
}
#endif

EXPORT(int, sceCtrlSetActuator, int port, const SceCtrlActuator *pState) {
    TRACY_FUNC(sceCtrlSetActuator, port, pState);
    // A handheld Vita has no actuators; DualShock rumble exists only on PS TV.
    if (!emuenv.cfg.current_config.pstv_mode) {
        return RET_ERROR(SCE_CTRL_ERROR_NOT_SUPPORTED);
    }
    if (!pState)
        return RET_ERROR(SCE_CTRL_ERROR_INVALID_ARG);

    // Both motor strengths are 8-bit (0-255, provisional: no SDK header available); SDL wants 0-65535.
    const auto to_sdl = [](uint8_t v) { return static_cast<Uint16>(v * 257); };

    CtrlState &state = emuenv.ctrl;
    const std::lock_guard lock(state.mutex);
    for (const auto &controller : state.controllers) {
        // sceCtrl ports are 1-based and SDL_Gamepad index is 0-based. Need to convert.
        if (controller.second.port + 1 == port) {
            // SDL caps the duration and re-sends it; a zero state stops the motors.
            if (!SDL_RumbleGamepad(controller.second.controller.get(), to_sdl(pState->small), to_sdl(pState->large), SDL_HAPTIC_INFINITY)) {
#ifdef __ANDROID__
                // This pad cannot rumble: use the phone instead (if the user allows it).
                vibrate_phone(pState->small, pState->large);
#endif
            }
            return 0;
        }
    }

#ifdef __ANDROID__
    // No pad on this port (touch-only / handheld-style play): the phone is the only actuator.
    if (state.controllers.empty() && port == 1) {
        vibrate_phone(pState->small, pState->large);
        return 0;
    }
#endif
    return RET_ERROR(SCE_CTRL_ERROR_NO_DEVICE);
}

EXPORT(int, sceCtrlSetAnalogStickCheckMode) {
    TRACY_FUNC(sceCtrlSetAnalogStickCheckMode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlSetAnalogStickCheckTarget) {
    TRACY_FUNC(sceCtrlSetAnalogStickCheckTarget);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlSetButtonIntercept) {
    TRACY_FUNC(sceCtrlSetButtonIntercept);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlSetButtonRemappingInfo) {
    TRACY_FUNC(sceCtrlSetButtonRemappingInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlSetLightBar) {
    TRACY_FUNC(sceCtrlSetLightBar);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlSetRapidFire) {
    TRACY_FUNC(sceCtrlSetRapidFire);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlSetSamplingMode, SceCtrlPadInputMode mode) {
    TRACY_FUNC(sceCtrlSetSamplingMode, mode);
    SceCtrlPadInputMode old = emuenv.ctrl.input_mode;
    if (mode < SCE_CTRL_MODE_DIGITAL || mode > SCE_CTRL_MODE_ANALOG_WIDE) {
        return RET_ERROR(SCE_CTRL_ERROR_INVALID_ARG);
    }
    emuenv.ctrl.input_mode = mode;
    return old;
}

EXPORT(int, sceCtrlSetSamplingModeExt, SceCtrlPadInputMode mode) {
    TRACY_FUNC(sceCtrlSetSamplingModeExt, mode);
    SceCtrlPadInputMode old = emuenv.ctrl.input_mode_ext;
    if (mode < SCE_CTRL_MODE_DIGITAL || mode > SCE_CTRL_MODE_ANALOG_WIDE) {
        return RET_ERROR(SCE_CTRL_ERROR_INVALID_ARG);
    }
    emuenv.ctrl.input_mode_ext = mode;
    return old;
}

EXPORT(int, sceCtrlSingleControllerMode) {
    TRACY_FUNC(sceCtrlSingleControllerMode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceCtrlUnregisterBdRMCCallback) {
    TRACY_FUNC(sceCtrlUnregisterBdRMCCallback);
    return UNIMPLEMENTED();
}
