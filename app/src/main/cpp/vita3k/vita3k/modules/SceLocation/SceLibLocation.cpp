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

#include <kernel/state.h>
#include <util/tracy.h>

#include <map>
#include <mutex>
#include <string>

TRACY_MODULE_NAME(SceLibLocation);

typedef SceInt32 SceLocationHandle;

enum SceLocationErrorCode {
    SCE_LOCATION_INFO_LOCATION_NOT_AVAILABLE = 0x80101202,
    SCE_LOCATION_INFO_HEADING_NOT_AVAILABLE = 0x80101205,
    SCE_LOCATION_ERROR_INVALID_ADDRESS = 0x80101240,
    SCE_LOCATION_ERROR_INVALID_HANDLE = 0x80101241,
    SCE_LOCATION_ERROR_UNSUPPORTED = 0x801012FF
};

enum SceLocationPermissionStatus {
    SCE_LOCATION_PERMISSION_DENY = 0,
    SCE_LOCATION_PERMISSION_ALLOW = 1
};

enum SceLocationPermissionApplicationStatus {
    SCE_LOCATION_PERMISSION_APPLICATION_NONE = 0,
    SCE_LOCATION_PERMISSION_APPLICATION_INIT = 1,
    SCE_LOCATION_PERMISSION_APPLICATION_DENY = 2,
    SCE_LOCATION_PERMISSION_APPLICATION_ALLOW = 3
};

enum SceLocationDialogStatus {
    SCE_LOCATION_DIALOG_STATUS_IDLE = 0,
    SCE_LOCATION_DIALOG_STATUS_RUNNING = 1,
    SCE_LOCATION_DIALOG_STATUS_FINISHED = 2
};

enum SceLocationDialogResult {
    SCE_LOCATION_DIALOG_RESULT_NONE = 0,
    SCE_LOCATION_DIALOG_RESULT_DISABLE = 1,
    SCE_LOCATION_DIALOG_RESULT_ENABLE = 2
};

struct SceLocationPermissionInfo {
    SceUInt32 parentalstatus;
    SceUInt32 mainstatus;
    SceUInt32 applicationstatus;
    SceUInt32 confirm_required;
    SceUInt32 reserved;
};

struct SceLocationLocationInfo {
    SceDouble latitude;
    SceDouble longitude;
    SceDouble altitude;
    SceFloat accuracy;
    SceFloat reserve;
    SceFloat direction;
    SceFloat speed;
    SceUInt64 timestamp;
};

struct SceLocationHeadingInfo {
    SceFloat trueHeading;
    SceFloat headingVectorX;
    SceFloat headingVectorY;
    SceFloat headingVectorZ;
    SceFloat reserve;
    SceFloat reserve2;
    SceUInt64 timestamp;
};

struct LocationHandleState {
    SceUInt32 locate_method = 0;
    SceUInt32 heading_method = 0;
    SceUInt32 dialog_status = SCE_LOCATION_DIALOG_STATUS_IDLE;
    SceUInt32 dialog_result = SCE_LOCATION_DIALOG_RESULT_NONE;
};

struct LocationState {
    std::mutex mutex;
    std::map<SceLocationHandle, LocationHandleState> handles;
    SceLocationHandle next_handle = 1;
};

LIBRARY_INIT(SceLibLocation) {
    emuenv.kernel.obj_store.create<LocationState>();
}

static LocationHandleState *find_handle(LocationState *state, SceLocationHandle handle) {
    const auto it = state->handles.find(handle);
    return (it == state->handles.end()) ? nullptr : &it->second;
}

static void log_location(const char *what, SceLocationHandle handle, int result) {
    static std::mutex log_mutex;
    static std::map<std::string, uint64_t> totals;

    uint64_t n;
    {
        std::lock_guard<std::mutex> lock(log_mutex);
        n = ++totals[what];
    }
    if ((n > 32) && ((n % 500) != 0))
        return;
    LOG_INFO("[LOCATION] {} handle {} -> 0x{:X} (call {})", what, handle, static_cast<uint32_t>(result), n);
}

template <typename T>
static bool writable(const MemState &mem, const Ptr<T> &p) {
    return p && p.valid(mem);
}

EXPORT(int, sceLocationInit, SceUInt32 unk0, SceUInt32 unk1) {
    TRACY_FUNC(sceLocationInit, unk0, unk1);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->handles.clear();
        state->next_handle = 1;
    }
    log_location("sceLocationInit", 0, 0);
    return 0;
}

EXPORT(int, sceLocationTerm) {
    TRACY_FUNC(sceLocationTerm);
    log_location("sceLocationTerm", 0, 0);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    state->handles.clear();
    return 0;
}

EXPORT(int, sceLocationOpen, Ptr<SceLocationHandle> handle, SceUInt32 locateMethod, SceUInt32 headingMethod) {
    TRACY_FUNC(sceLocationOpen, handle, locateMethod, headingMethod);
    if (!writable(emuenv.mem, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);

    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);

    const SceLocationHandle id = state->next_handle++;
    LocationHandleState &opened = state->handles[id];
    opened.locate_method = locateMethod;
    opened.heading_method = headingMethod;
    *handle.get(emuenv.mem) = id;
    log_location("sceLocationOpen", id, 0);
    return 0;
}

EXPORT(int, sceLocationClose, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationClose, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    log_location("sceLocationClose", handle, 0);
    if (state->handles.erase(handle) == 0)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    return 0;
}

EXPORT(int, sceLocationReopen, SceLocationHandle handle, SceUInt32 locateMethod, SceUInt32 headingMethod) {
    TRACY_FUNC(sceLocationReopen, handle, locateMethod, headingMethod);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    LocationHandleState *opened = find_handle(state, handle);
    if (!opened)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    opened->locate_method = locateMethod;
    opened->heading_method = headingMethod;
    return 0;
}

EXPORT(int, sceLocationGetMethod, SceLocationHandle handle, Ptr<SceUInt32> locateMethod, Ptr<SceUInt32> headingMethod) {
    TRACY_FUNC(sceLocationGetMethod, handle, locateMethod, headingMethod);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    LocationHandleState *opened = find_handle(state, handle);
    if (!opened)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    if (!locateMethod && !headingMethod)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    if (locateMethod && !writable(emuenv.mem, locateMethod))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    if (headingMethod && !writable(emuenv.mem, headingMethod))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    if (locateMethod)
        *locateMethod.get(emuenv.mem) = opened->locate_method;
    if (headingMethod)
        *headingMethod.get(emuenv.mem) = opened->heading_method;
    return 0;
}

EXPORT(int, sceLocationGetLocation, SceLocationHandle handle, Ptr<SceLocationLocationInfo> locationInfo) {
    TRACY_FUNC(sceLocationGetLocation, handle, locationInfo);
    if (!writable(emuenv.mem, locationInfo))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);

    *locationInfo.get(emuenv.mem) = {};
    log_location("sceLocationGetLocation", handle, SCE_LOCATION_INFO_LOCATION_NOT_AVAILABLE);
    return RET_ERROR(SCE_LOCATION_INFO_LOCATION_NOT_AVAILABLE);
}

EXPORT(int, sceLocationGetLocationWithTimeout, SceLocationHandle handle, SceUInt32 timeout, Ptr<SceLocationLocationInfo> locationInfo) {
    TRACY_FUNC(sceLocationGetLocationWithTimeout, handle, timeout, locationInfo);
    return CALL_EXPORT(sceLocationGetLocation, handle, locationInfo);
}

EXPORT(int, sceLocationCancelGetLocation, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationCancelGetLocation, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    return 0;
}

EXPORT(int, sceLocationStartLocationCallback, SceLocationHandle handle, SceUInt32 distance, Ptr<void> callback, Ptr<void> userdata) {
    TRACY_FUNC(sceLocationStartLocationCallback, handle, distance, callback, userdata);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);

    log_location("sceLocationStartLocationCallback", handle, SCE_LOCATION_INFO_LOCATION_NOT_AVAILABLE);
    return RET_ERROR(SCE_LOCATION_INFO_LOCATION_NOT_AVAILABLE);
}

EXPORT(int, sceLocationStopLocationCallback, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationStopLocationCallback, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    return 0;
}

EXPORT(int, sceLocationGetHeading, SceLocationHandle handle, Ptr<SceLocationHeadingInfo> headingInfo) {
    TRACY_FUNC(sceLocationGetHeading, handle, headingInfo);
    if (!writable(emuenv.mem, headingInfo))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);

    *headingInfo.get(emuenv.mem) = {};
    log_location("sceLocationGetHeading", handle, SCE_LOCATION_INFO_HEADING_NOT_AVAILABLE);
    return RET_ERROR(SCE_LOCATION_INFO_HEADING_NOT_AVAILABLE);
}

// TODO: The shape of this must be varified and fixed before its ever used!
EXPORT(int, sceLocationStartHeadingCallback, SceLocationHandle handle, SceUInt32 difference, Ptr<void> callback, Ptr<void> userdata) {
    TRACY_FUNC(sceLocationStartHeadingCallback, handle, difference, callback, userdata);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    return RET_ERROR(SCE_LOCATION_INFO_HEADING_NOT_AVAILABLE);
}

EXPORT(int, sceLocationStopHeadingCallback, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationStopHeadingCallback, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    return 0;
}

EXPORT(int, sceLocationConfirm, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationConfirm, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    LocationHandleState *opened = find_handle(state, handle);
    if (!opened)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);

    // No dialog to run so its over and send FINISHED on the first poll or the caller loops forever
    opened->dialog_status = SCE_LOCATION_DIALOG_STATUS_FINISHED;
    opened->dialog_result = SCE_LOCATION_DIALOG_RESULT_ENABLE;
    log_location("sceLocationConfirm", handle, 0);
    return 0;
}

EXPORT(int, sceLocationConfirmGetStatus, SceLocationHandle handle, Ptr<SceUInt32> status) {
    TRACY_FUNC(sceLocationConfirmGetStatus, handle, status);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    LocationHandleState *opened = find_handle(state, handle);
    if (!opened)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    if (!writable(emuenv.mem, status))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    *status.get(emuenv.mem) = opened->dialog_status;
    log_location("sceLocationConfirmGetStatus", handle, opened->dialog_status);
    return 0;
}

EXPORT(int, sceLocationConfirmGetResult, SceLocationHandle handle, Ptr<SceUInt32> result) {
    TRACY_FUNC(sceLocationConfirmGetResult, handle, result);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    LocationHandleState *opened = find_handle(state, handle);
    if (!opened)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    if (!writable(emuenv.mem, result))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);
    *result.get(emuenv.mem) = opened->dialog_result;
    log_location("sceLocationConfirmGetResult", handle, opened->dialog_result);
    return 0;
}

EXPORT(int, sceLocationConfirmAbort, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationConfirmAbort, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    LocationHandleState *opened = find_handle(state, handle);
    if (!opened)
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    opened->dialog_status = SCE_LOCATION_DIALOG_STATUS_IDLE;
    opened->dialog_result = SCE_LOCATION_DIALOG_RESULT_NONE;
    return 0;
}

EXPORT(int, sceLocationGetPermission, SceLocationHandle handle, Ptr<SceLocationPermissionInfo> info) {
    TRACY_FUNC(sceLocationGetPermission, handle, info);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    if (!info)
        return 0;
    if (!writable(emuenv.mem, info))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_ADDRESS);

    // confirm_required must stay 0! A 1 is what sends a guest into the confirm loop regardless of status
    SceLocationPermissionInfo *out = info.get(emuenv.mem);
    out->parentalstatus = SCE_LOCATION_PERMISSION_ALLOW;
    out->mainstatus = SCE_LOCATION_PERMISSION_ALLOW;
    out->applicationstatus = SCE_LOCATION_PERMISSION_APPLICATION_ALLOW;
    out->confirm_required = 0;
    out->reserved = 0;
    log_location("sceLocationGetPermission", handle, 0);
    return 0;
}

EXPORT(int, sceLocationDenyApplication, SceLocationHandle handle) {
    TRACY_FUNC(sceLocationDenyApplication, handle);
    LocationState *state = emuenv.kernel.obj_store.get<LocationState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    if (!find_handle(state, handle))
        return RET_ERROR(SCE_LOCATION_ERROR_INVALID_HANDLE);
    return 0;
}

EXPORT(int, sceLocationSetGpsEmulationFile, Ptr<char> filename) {
    TRACY_FUNC(sceLocationSetGpsEmulationFile, filename);
    return RET_ERROR(SCE_LOCATION_ERROR_UNSUPPORTED);
}

EXPORT(int, sceLocationSetThreadParameter, SceUInt32 priority, SceUInt32 affinity) {
    TRACY_FUNC(sceLocationSetThreadParameter, priority, affinity);
    return 0;
}
