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

#include "power_battery.h"

#include <kernel/state.h>
#include <util/lock_and_find.h>
#include <util/tracy.h>
#include <util/types.h>

#include <SDL3/SDL_power.h>

#include <climits>
#include <mutex>

#define LOW_BATTERY_PERCENT 10

TRACY_MODULE_NAME(ScePower);

// Charge percentage reported by the host, or -1 when unknown.
static int host_battery_percent() {
    int percent = -1;
    SDL_GetPowerInfo(nullptr, &percent);
    return percent;
}

static power_battery::CallbackRegistry g_power_callbacks;
static std::mutex g_power_callbacks_mutex;

enum ScePowerErrorCode {
    SCE_POWER_ERROR_INVALID_VALUE = 0x802B0000,
    SCE_POWER_ERROR_ALREADY_REGISTERED = 0x802B0001,
    SCE_POWER_ERROR_CALLBACK_NOT_REGISTERED = 0x802B0002,
    SCE_POWER_ERROR_CANT_SUSPEND = 0x802B0003,
    SCE_POWER_ERROR_NO_BATTERY = 0x802B0100,
    SCE_POWER_ERROR_DETECTING = 0x802B0101
};

enum ScePowerBatteryRemainLevel {
    SCE_POWER_BATTERY_REMAIN_LEVEL_INVALID = 0,
    SCE_POWER_BATTERY_REMAIN_LEVEL_0_25_PERCENTS = 1,
    SCE_POWER_BATTERY_REMAIN_LEVEL_25_50_PERCENTS = 2,
    SCE_POWER_BATTERY_REMAIN_LEVEL_50_75_PERCENTS = 3,
    SCE_POWER_BATTERY_REMAIN_LEVEL_75_100_PERCENTS = 4
};

EXPORT(int, scePowerBatteryUpdateInfo) {
    TRACY_FUNC(scePowerBatteryUpdateInfo);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerCancelRequest) {
    TRACY_FUNC(scePowerCancelRequest);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerGetArmClockFrequency) {
    TRACY_FUNC(scePowerGetArmClockFrequency);
    return 444;
}

EXPORT(int, scePowerGetBatteryChargingStatus) {
    TRACY_FUNC(scePowerGetBatteryChargingStatus);
    return power_battery::charging_status(SDL_GetPowerInfo(nullptr, nullptr) == SDL_POWERSTATE_CHARGING);
}

EXPORT(int, scePowerGetBatteryCycleCount) {
    TRACY_FUNC(scePowerGetBatteryCycleCount);
    return power_battery::CYCLE_COUNT;
}

EXPORT(int, scePowerGetBatteryElec) {
    TRACY_FUNC(scePowerGetBatteryElec);
    // Provisional: no current measurement is available on the host.
    return 0;
}

EXPORT(int, scePowerGetBatteryFullCapacity) {
    TRACY_FUNC(scePowerGetBatteryFullCapacity);
    return power_battery::FULL_CAPACITY_MAH;
}

EXPORT(int, scePowerGetBatteryLifePercent) {
    TRACY_FUNC(scePowerGetBatteryLifePercent);
    int res;
    SDL_GetPowerInfo(NULL, &res);
    if (res == -1) {
        return 100;
    }
    return res;
}

EXPORT(int, scePowerGetBatteryLifeTime) {
    TRACY_FUNC(scePowerGetBatteryLifeTime);
    int res;
    SDL_GetPowerInfo(&res, NULL);
    // SDL reports seconds, the SDK returns minutes
    return power_battery::life_time_minutes(res);
}

EXPORT(int, scePowerGetBatteryRemainCapacity) {
    TRACY_FUNC(scePowerGetBatteryRemainCapacity);
    return power_battery::remain_capacity_mah(host_battery_percent());
}

EXPORT(int, scePowerGetBatteryRemainLevel) {
    TRACY_FUNC(scePowerGetBatteryRemainLevel);
    int res;
    SDL_GetPowerInfo(NULL, &res);
    if (res >= 0) {
        if (res <= 25)
            return SCE_POWER_BATTERY_REMAIN_LEVEL_0_25_PERCENTS;
        else if (res <= 50)
            return SCE_POWER_BATTERY_REMAIN_LEVEL_25_50_PERCENTS;
        else if (res <= 75)
            return SCE_POWER_BATTERY_REMAIN_LEVEL_50_75_PERCENTS;
    }

    return SCE_POWER_BATTERY_REMAIN_LEVEL_75_100_PERCENTS;
}

EXPORT(int, scePowerGetBatteryRemainMaxLevel) {
    TRACY_FUNC(scePowerGetBatteryRemainMaxLevel);
    return SCE_POWER_BATTERY_REMAIN_LEVEL_75_100_PERCENTS;
}

EXPORT(int, scePowerGetBatterySOH) {
    TRACY_FUNC(scePowerGetBatterySOH);
    return power_battery::HEALTH_PERCENT;
}

EXPORT(int, scePowerGetBatteryTemp) {
    TRACY_FUNC(scePowerGetBatteryTemp);
    return power_battery::BATTERY_TEMP_CENTI_C;
}

EXPORT(int, scePowerGetBatteryVolt) {
    TRACY_FUNC(scePowerGetBatteryVolt);
    return power_battery::voltage_mv(host_battery_percent());
}

EXPORT(int, scePowerGetBusClockFrequency) {
    TRACY_FUNC(scePowerGetBusClockFrequency);
    return 222;
}

EXPORT(int, scePowerGetCaseTemp) {
    TRACY_FUNC(scePowerGetCaseTemp);
    return power_battery::CASE_TEMP_CENTI_C;
}

EXPORT(int, scePowerGetGpuClockFrequency) {
    TRACY_FUNC(scePowerGetGpuClockFrequency);
    return 222;
}

EXPORT(int, scePowerGetGpuXbarClockFrequency) {
    TRACY_FUNC(scePowerGetGpuXbarClockFrequency);
    return 166;
}

EXPORT(int, scePowerGetUsingWireless) {
    TRACY_FUNC(scePowerGetUsingWireless);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerIsBatteryCharging) {
    TRACY_FUNC(scePowerIsBatteryCharging);
    SDL_PowerState info = SDL_GetPowerInfo(NULL, NULL);
    return (info == SDL_POWERSTATE_CHARGING);
}

EXPORT(int, scePowerIsBatteryExist) {
    TRACY_FUNC(scePowerIsBatteryExist);
    SDL_PowerState info = SDL_GetPowerInfo(NULL, NULL);
    return (info != SDL_POWERSTATE_NO_BATTERY);
}

EXPORT(int, scePowerIsLowBattery) {
    TRACY_FUNC(scePowerIsLowBattery);
    int res;
    SDL_GetPowerInfo(NULL, &res);
    if (res <= LOW_BATTERY_PERCENT) {
        return SCE_TRUE;
    }
    return SCE_FALSE;
}

EXPORT(int, scePowerIsLowBatteryInhibitUpdateDownload) {
    TRACY_FUNC(scePowerIsLowBatteryInhibitUpdateDownload);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerIsLowBatteryInhibitUpdateReboot) {
    TRACY_FUNC(scePowerIsLowBatteryInhibitUpdateReboot);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerIsPowerOnline) {
    TRACY_FUNC(scePowerIsPowerOnline);
    SDL_PowerState info = SDL_GetPowerInfo(NULL, NULL);
    return ((info != SDL_POWERSTATE_UNKNOWN) && (info != SDL_POWERSTATE_ON_BATTERY));
}

EXPORT(int, scePowerIsRequest) {
    TRACY_FUNC(scePowerIsRequest);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerIsSuspendRequired) {
    TRACY_FUNC(scePowerIsSuspendRequired);
    return UNIMPLEMENTED();
}

// Callbacks are stored so that registration/unregistration pair up as on hardware, but they are
// never invoked: the emulated device has no power events to report.
EXPORT(int, scePowerRegisterCallback, SceUID cbid) {
    TRACY_FUNC(scePowerRegisterCallback, cbid);
    const CallbackPtr cb = lock_and_find(cbid, emuenv.kernel.callbacks, emuenv.kernel.mutex);
    if (!cb)
        return RET_ERROR(SCE_POWER_ERROR_INVALID_VALUE);

    const std::lock_guard<std::mutex> lock(g_power_callbacks_mutex);
    g_power_callbacks.bind_process(emuenv.kernel.sync_cache_generation.load(std::memory_order_acquire));
    // forget callbacks that were deleted since
    g_power_callbacks.prune([&](std::int32_t id) { return lock_and_find(id, emuenv.kernel.callbacks, emuenv.kernel.mutex) != nullptr; });
    if (g_power_callbacks.add(cbid) == power_battery::CallbackRegistry::AlreadyRegistered)
        return RET_ERROR(SCE_POWER_ERROR_ALREADY_REGISTERED);
    return 0;
}

EXPORT(int, scePowerRequestColdReset) {
    TRACY_FUNC(scePowerRequestColdReset);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerRequestDisplayOff) {
    TRACY_FUNC(scePowerRequestDisplayOff);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerRequestDisplayOn) {
    TRACY_FUNC(scePowerRequestDisplayOn);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerRequestStandby) {
    TRACY_FUNC(scePowerRequestStandby);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerRequestSuspend) {
    TRACY_FUNC(scePowerRequestSuspend);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerSetArmClockFrequency, int freq) {
    TRACY_FUNC(scePowerSetArmClockFrequency, freq);
    if (freq < 0) {
        return SCE_POWER_ERROR_INVALID_VALUE;
    }
    return 0;
}

EXPORT(int, scePowerSetBusClockFrequency, int freq) {
    TRACY_FUNC(scePowerSetBusClockFrequency, freq);
    if (freq < 0) {
        return SCE_POWER_ERROR_INVALID_VALUE;
    }
    return 0;
}

EXPORT(int, scePowerSetConfigurationMode, int mode) {
    TRACY_FUNC(scePowerSetConfigurationMode, mode);
    if (mode != 0x80 && mode != 0x800 && mode != 0x10880) {
        return SCE_POWER_ERROR_INVALID_VALUE;
    }
    return 0;
}

EXPORT(int, scePowerSetGpuClockFrequency, int freq) {
    TRACY_FUNC(scePowerSetGpuClockFrequency, freq);
    if (freq < 0) {
        return SCE_POWER_ERROR_INVALID_VALUE;
    }
    return 0;
}

EXPORT(int, scePowerSetGpuXbarClockFrequency, int freq) {
    TRACY_FUNC(scePowerSetGpuXbarClockFrequency, freq);
    if (freq < 0) {
        return SCE_POWER_ERROR_INVALID_VALUE;
    }
    return 0;
}

EXPORT(int, scePowerSetIdleTimerCount) {
    TRACY_FUNC(scePowerSetIdleTimerCount);
    return UNIMPLEMENTED();
}

EXPORT(int, scePowerSetUsingWireless, SceBool enabled) {
    TRACY_FUNC(scePowerSetUsingWireless, enabled);
    if (enabled != SCE_TRUE && enabled != SCE_FALSE) {
        return SCE_POWER_ERROR_INVALID_VALUE;
    }
    return 0;
}

EXPORT(int, scePowerUnregisterCallback, SceUID cbid) {
    TRACY_FUNC(scePowerUnregisterCallback, cbid);
    const std::lock_guard<std::mutex> lock(g_power_callbacks_mutex);
    g_power_callbacks.bind_process(emuenv.kernel.sync_cache_generation.load(std::memory_order_acquire));
    if (g_power_callbacks.remove(cbid) == power_battery::CallbackRegistry::NotRegistered)
        return RET_ERROR(SCE_POWER_ERROR_CALLBACK_NOT_REGISTERED);
    return 0;
}
