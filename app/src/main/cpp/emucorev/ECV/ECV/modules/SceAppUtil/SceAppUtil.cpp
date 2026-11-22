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

#include "SceAppUtil.h"
#include "app_util_helpers.h"
#include "save_data_write.h"

#include <app/state.h>
#include <emuenv/app_util.h>

#include <io/device.h>
#include <io/functions.h>
#include <io/io.h>
#include <io/vfs.h>

#include <packages/license.h>
#include <packages/sfo.h>

#include <util/safe_time.h>
#include <util/tracy.h>

#ifdef _WIN32
#include <winsock.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>

TRACY_MODULE_NAME(SceAppUtil);

template <>
std::string to_debug_str<SceSystemParamId>(const MemState &mem, SceSystemParamId type) {
    switch (type) {
    case SCE_SYSTEM_PARAM_ID_LANG:
        return "SCE_SYSTEM_PARAM_ID_LANG";
    case SCE_SYSTEM_PARAM_ID_ENTER_BUTTON:
        return "SCE_SYSTEM_PARAM_ID_ENTER_BUTTON";
    case SCE_SYSTEM_PARAM_ID_USER_NAME:
        return "SCE_SYSTEM_PARAM_ID_USER_NAME";
    case SCE_SYSTEM_PARAM_ID_DATE_FORMAT:
        return "SCE_SYSTEM_PARAM_ID_DATE_FORMAT";
    case SCE_SYSTEM_PARAM_ID_TIME_FORMAT:
        return "SCE_SYSTEM_PARAM_ID_TIME_FORMAT";
    case SCE_SYSTEM_PARAM_ID_TIME_ZONE:
        return "SCE_SYSTEM_PARAM_ID_TIME_ZONE";
    case SCE_SYSTEM_PARAM_ID_SUMMERTIME:
        return "SCE_SYSTEM_PARAM_ID_SUMMERTIME";
    case SCE_SYSTEM_PARAM_ID_MAX_VALUE:
        return "SCE_SYSTEM_PARAM_ID_MAX_VALUE";
    }
    return std::to_string(type);
}

EXPORT(int, sceAppUtilAddCookieWebBrowser) {
    TRACY_FUNC(sceAppUtilAddCookieWebBrowser);
    return UNIMPLEMENTED();
}

static bool is_addcont_exist(EmuEnvState &emuenv, const SceChar8 *path);

// Not in the vitasdk headers: the argument order mirrors sceAppUtilDrmOpen (dirName, mountPoint)
// and the mount point string is provisional. The content is reachable through the existing
// addcont0: device, which maps to ux0:addcont/<title id>.
EXPORT(int, sceAppUtilAddcontMount, const SceAppUtilDrmAddcontId *dirName, SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilAddcontMount, dirName, mountPoint);
    if (!dirName || !mountPoint)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (!app_util::addcont_dir_name_valid(reinterpret_cast<const char *>(dirName->data), sizeof(dirName->data)) || !is_addcont_exist(emuenv, dirName->data))
        return RET_ERROR(SCE_ERROR_ERRNO_ENOENT);

    std::memset(mountPoint->data, 0, sizeof(mountPoint->data));
    std::strncpy(reinterpret_cast<char *>(mountPoint->data), "addcont0:", sizeof(mountPoint->data) - 1);
    return 0;
}

EXPORT(int, sceAppUtilAddcontUmount, const SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilAddcontUmount, mountPoint);
    if (!mountPoint)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
    // Provisional: the addcont0: device is always reachable, only mount points handed out by
    // sceAppUtilAddcontMount are accepted.
    if (std::strncmp(reinterpret_cast<const char *>(mountPoint->data), "addcont0:", sizeof(mountPoint->data)) != 0)
        return RET_ERROR(SCE_APPUTIL_ERROR_NOT_MOUNTED);
    return 0;
}

EXPORT(int, sceAppUtilAppEventParseGameCustomData) {
    TRACY_FUNC(sceAppUtilAppEventParseGameCustomData);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseIncomingDialog) {
    TRACY_FUNC(sceAppUtilAppEventParseIncomingDialog);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseLiveArea) {
    TRACY_FUNC(sceAppUtilAppEventParseLiveArea);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseNearGift) {
    TRACY_FUNC(sceAppUtilAppEventParseNearGift);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseNpActivity) {
    TRACY_FUNC(sceAppUtilAppEventParseNpActivity);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseNpAppDataMessage) {
    TRACY_FUNC(sceAppUtilAppEventParseNpAppDataMessage);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseNpBasicJoinablePresence) {
    TRACY_FUNC(sceAppUtilAppEventParseNpBasicJoinablePresence);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseNpInviteMessage) {
    TRACY_FUNC(sceAppUtilAppEventParseNpInviteMessage);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseScreenShotNotification) {
    TRACY_FUNC(sceAppUtilAppEventParseScreenShotNotification);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseSessionInvitation) {
    TRACY_FUNC(sceAppUtilAppEventParseSessionInvitation);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseTeleport) {
    TRACY_FUNC(sceAppUtilAppEventParseTeleport);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseTriggerUtil) {
    TRACY_FUNC(sceAppUtilAppEventParseTriggerUtil);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilAppEventParseWebBrowser) {
    TRACY_FUNC(sceAppUtilAppEventParseWebBrowser);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceAppUtilAppParamGetInt, SceAppUtilAppParamId paramId, SceInt32 *value) {
    TRACY_FUNC(sceAppUtilAppParamGetInt, paramId, value);
    if (paramId != SCE_APPUTIL_APPPARAM_ID_SKU_FLAG)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (!value)
        return RET_ERROR(SCE_APPUTIL_ERROR_NOT_INITIALIZED);

    *value = emuenv.license.rif[emuenv.io.title_id].sku_flag;

    return 0;
}

EXPORT(int, sceAppUtilBgdlGetStatus) {
    TRACY_FUNC(sceAppUtilBgdlGetStatus);
    return UNIMPLEMENTED();
}

static bool is_addcont_exist(EmuEnvState &emuenv, const SceChar8 *path) {
    const auto drm_content_id_path{ emuenv.vita_fs_path / "ux0" / emuenv.io.device_paths.addcont0 / reinterpret_cast<const char *>(path) };
    return (fs::exists(drm_content_id_path) && (!fs::is_empty(drm_content_id_path)));
}

EXPORT(SceInt32, sceAppUtilDrmClose, const SceAppUtilDrmAddcontId *dirName, const SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilDrmClose, dirName, mountPoint);
    if (!dirName)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (!is_addcont_exist(emuenv, dirName->data))
        return RET_ERROR(SCE_APPUTIL_ERROR_NOT_MOUNTED);

    return 0;
}

EXPORT(SceInt32, sceAppUtilDrmOpen, const SceAppUtilDrmAddcontId *dirName, const SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilDrmOpen, dirName, mountPoint);
    if (!dirName)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (!is_addcont_exist(emuenv, dirName->data))
        return SCE_ERROR_ERRNO_ENOENT;

    return 0;
}

// Mirrors SceAppUtilInitParam / SceAppUtilBootParam of the SDK (0x40 / 0x28 bytes).
struct SceAppUtilInitParam {
    SceSize workBufSize;
    uint8_t reserved[60];
};
static_assert(sizeof(SceAppUtilInitParam) == 0x40, "SceAppUtilInitParam is an invalid size");

struct SceAppUtilBootParam {
    SceAppUtilBootAttribute attr;
    SceUInt32 appVersion;
    uint8_t reserved[32];
};
static_assert(sizeof(SceAppUtilBootParam) == 0x28, "SceAppUtilBootParam is an invalid size");

EXPORT(int, sceAppUtilInit, const SceAppUtilInitParam *initParam, SceAppUtilBootParam *bootParam) {
    TRACY_FUNC(sceAppUtilInit, initParam, bootParam);
    if (!initParam || !bootParam)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    const std::lock_guard<std::mutex> lock(emuenv.app.services.mutex);
    if (emuenv.app.services.apputil_inited)
        return RET_ERROR(SCE_APPUTIL_ERROR_BUSY);
    emuenv.app.services.apputil_inited = true;

    // The emulator always starts the title normally (attr 0): LiveArea launch attributes are never set,
    // and the app version is derived from APP_VER (provisional encoding, see boot_param_app_version).
    *bootParam = {};
    bootParam->attr = 0;
    bootParam->appVersion = app_util::boot_param_app_version(emuenv.app_info.app_version);
    return 0;
}

EXPORT(int, sceAppUtilLaunchWebBrowser) {
    TRACY_FUNC(sceAppUtilLaunchWebBrowser);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilMusicMount) {
    TRACY_FUNC(sceAppUtilMusicMount);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilMusicUmount) {
    TRACY_FUNC(sceAppUtilMusicUmount);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilPhotoMount) {
    TRACY_FUNC(sceAppUtilPhotoMount);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilPhotoUmount) {
    TRACY_FUNC(sceAppUtilPhotoUmount);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilPspSaveDataGetDirNameList) {
    TRACY_FUNC(sceAppUtilPspSaveDataGetDirNameList);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilPspSaveDataLoad) {
    TRACY_FUNC(sceAppUtilPspSaveDataLoad);
    return UNIMPLEMENTED();
}

// The emulator does not generate app events (LiveArea launches, invitations, ...): report "no
// event" with the whole 0x404-byte structure cleared. Event type 0 is the "nothing" type.
struct SceAppUtilAppEventParam {
    SceAppUtilAppEventType type;
    uint8_t dat[1024];
};
static_assert(sizeof(SceAppUtilAppEventParam) == 0x404, "SceAppUtilAppEventParam is an invalid size");

EXPORT(int, sceAppUtilReceiveAppEvent, SceAppUtilAppEventParam *eventParam) {
    TRACY_FUNC(sceAppUtilReceiveAppEvent, eventParam);
    if (!eventParam)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
    std::memset(eventParam, 0, sizeof(*eventParam));
    return 0;
}

EXPORT(int, sceAppUtilResetCookieWebBrowser) {
    TRACY_FUNC(sceAppUtilResetCookieWebBrowser);
    return UNIMPLEMENTED();
}

std::string construct_savedata0_path(const std::string &data, const char *ext) {
    return device::construct_normalized_path(VitaIoDevice::savedata0, data, ext);
}

std::string construct_slotparam_path(const unsigned int data) {
    return construct_savedata0_path("SlotParam_" + std::to_string(data), "bin");
}

EXPORT(int, sceAppUtilSaveDataDataRemove, SceAppUtilSaveDataFileSlot *slot, SceAppUtilSaveDataRemoveItem *files, unsigned int fileNum, SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataDataRemove, slot, files, fileNum, mountPoint);

    if (slot && (slot->id >= SCE_APPUTIL_SAVEDATA_SLOT_MAX))
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    for (unsigned int i = 0; i < fileNum; i++) {
        const auto file = fs::path(construct_savedata0_path(files[i].dataPath.get(emuenv.mem)));
        if (fs::is_regular_file(file)) {
            remove_file(emuenv.io, file.string().c_str(), emuenv.vita_fs_path, export_name);
        } else
            remove_dir(emuenv.io, file.string().c_str(), emuenv.vita_fs_path, export_name);
    }

    if (slot && files[0].mode == SCE_APPUTIL_SAVEDATA_DATA_REMOVE_MODE_DEFAULT) {
        remove_file(emuenv.io, construct_slotparam_path(slot->id).c_str(), emuenv.vita_fs_path, export_name);
    }

    return 0;
}

EXPORT(int, sceAppUtilSaveDataDataSave, SceAppUtilSaveDataFileSlot *slot, SceAppUtilSaveDataDataSaveItem *files, unsigned int fileNum, SceAppUtilMountPoint *mountPoint, SceSize *requiredSizeKiB) {
    TRACY_FUNC(sceAppUtilSaveDataDataSave, slot, files, fileNum, mountPoint, requiredSizeKiB);

    if (slot && (slot->id >= SCE_APPUTIL_SAVEDATA_SLOT_MAX))
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (fileNum && !files)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (requiredSizeKiB)
        // requiredSizeKiB must be set to 0 if there is enough space available
        *requiredSizeKiB = 0;

    // Adapter of the host I/O layer for app_util::save_data_write: one open, no truncation on open.
    struct FileOps {
        EmuEnvState &emuenv;
        const char *export_name;
        const std::string &path;
        SceUID fd = -1;

        bool open() {
            fd = open_file(emuenv.io, path.c_str(), SCE_O_WRONLY | SCE_O_CREAT, emuenv.vita_fs_path, export_name);
            return fd >= 0;
        }
        bool seek(std::uint64_t offset) {
            return seek_file(fd, static_cast<SceOff>(offset), SCE_SEEK_SET, emuenv.io, export_name) >= 0;
        }
        bool write(const void *data, std::uint64_t size) {
            return write_file(fd, data, static_cast<SceSize>(size), emuenv.io, export_name) == static_cast<int>(size);
        }
        bool truncate(std::uint64_t length) {
            return truncate_file(fd, length, emuenv.io, export_name) >= 0;
        }
        void close() {
            close_file(emuenv.io, fd, export_name);
        }
    };

    for (unsigned int i = 0; i < fileNum; i++) {
        if (!files[i].dataPath)
            return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
        const auto file_path = construct_savedata0_path(files[i].dataPath.get(emuenv.mem));
        switch (files[i].mode) {
        case SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_DIRECTORY:
            create_dir(emuenv.io, file_path.c_str(), 0777, emuenv.vita_fs_path, export_name);
            break;
        case SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_FILE_TRUNCATE:
        case SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_FILE:
        default: {
            const auto mode = files[i].mode == SCE_APPUTIL_SAVEDATA_DATA_SAVE_MODE_FILE_TRUNCATE ? app_util::SaveWriteMode::FileTruncate : app_util::SaveWriteMode::File;
            FileOps ops{ emuenv, export_name, file_path };
            switch (app_util::save_data_write(ops, mode, files[i].buf ? files[i].buf.get(emuenv.mem) : nullptr, files[i].bufSize, static_cast<std::uint64_t>(files[i].offset))) {
            case app_util::SaveWriteResult::Ok:
                break;
            case app_util::SaveWriteResult::BadParameter:
                return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
            case app_util::SaveWriteResult::IoError:
                return RET_ERROR(SCE_APPUTIL_ERROR_SAVEDATA_NO_SPACE_FS);
            }
            break;
        }
        }
    }

    if (slot) {
        SceAppUtilSaveDataSlotParam param{};
        auto *slot_param = slot->slotParam ? slot->slotParam.get(emuenv.mem) : &param;
        const auto slot_path = construct_slotparam_path(slot->id);

        if (!slot->slotParam) {
            // Keep the stored parameters; a missing or short SlotParam file starts from an empty one.
            const SceUID rfd = open_file(emuenv.io, slot_path.c_str(), SCE_O_RDONLY, emuenv.vita_fs_path, export_name);
            if (rfd >= 0) {
                const int read = read_file(slot_param, emuenv.io, rfd, sizeof(SceAppUtilSaveDataSlotParam), export_name);
                close_file(emuenv.io, rfd, export_name);
                if (read != static_cast<int>(sizeof(SceAppUtilSaveDataSlotParam)))
                    param = {};
            }
        }

        SceDateTime modified_time;
        std::time_t time = std::time(0);
        tm local = {};
        SAFE_LOCALTIME(&time, &local);
        modified_time.year = local.tm_year + 1900;
        modified_time.month = local.tm_mon + 1;
        modified_time.day = local.tm_mday;
        modified_time.hour = local.tm_hour;
        modified_time.minute = local.tm_min;
        modified_time.second = local.tm_sec;
        slot_param->modifiedTime = modified_time;

        const SceUID wfd = open_file(emuenv.io, slot_path.c_str(), SCE_O_WRONLY | SCE_O_CREAT, emuenv.vita_fs_path, export_name);
        if (wfd < 0)
            return RET_ERROR(SCE_APPUTIL_ERROR_SAVEDATA_NO_SPACE_FS);
        const int written = write_file(wfd, slot_param, sizeof(SceAppUtilSaveDataSlotParam), emuenv.io, export_name);
        close_file(emuenv.io, wfd, export_name);
        if (written != static_cast<int>(sizeof(SceAppUtilSaveDataSlotParam)))
            return RET_ERROR(SCE_APPUTIL_ERROR_SAVEDATA_NO_SPACE_FS);
    }

    return 0;
}

EXPORT(int, sceAppUtilSaveDataGetQuota, SceSize *quotaSizeKiB, SceSize *usedSizeKiB, const SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataGetQuota, quotaSizeKiB, usedSizeKiB, mountPoint);

    if (!quotaSizeKiB && !usedSizeKiB)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    // Quota from SFO
    if (quotaSizeKiB) {
        std::string savedata_max_size;

        if (!sfo::get_data_by_key(savedata_max_size, emuenv.sfo_handle, "SAVEDATA_MAX_SIZE"))
            savedata_max_size = "0";

        *quotaSizeKiB = static_cast<SceSize>(std::strtoul(savedata_max_size.c_str(), nullptr, 10));
    }

    // Used size from VFS
    if (usedSizeKiB) {
        *usedSizeKiB = vfs::get_directory_used_size(VitaIoDevice::ux0, emuenv.io.device_paths.savedata0, emuenv.vita_fs_path) / KiB(1);

        // Clamp used size to quota
        if (quotaSizeKiB && (*quotaSizeKiB > 0) && (*usedSizeKiB > *quotaSizeKiB))
            *usedSizeKiB = *quotaSizeKiB;
    }

    return 0;
}

EXPORT(int, sceAppUtilSaveDataMount) {
    TRACY_FUNC(sceAppUtilSaveDataMount);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAppUtilSaveDataSlotCreate, unsigned int slotId, SceAppUtilSaveDataSlotParam *param, SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataSlotCreate, slotId, param, mountPoint);
    const auto fd = open_file(emuenv.io, construct_slotparam_path(slotId).c_str(), SCE_O_WRONLY | SCE_O_CREAT, emuenv.vita_fs_path, export_name);
    write_file(fd, param, sizeof(SceAppUtilSaveDataSlotParam), emuenv.io, export_name);
    close_file(emuenv.io, fd, export_name);
    return 0;
}

EXPORT(int, sceAppUtilSaveDataSlotDelete, unsigned int slotId, SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataSlotDelete, slotId, mountPoint);
    remove_file(emuenv.io, construct_slotparam_path(slotId).c_str(), emuenv.vita_fs_path, export_name);
    return 0;
}

EXPORT(int, sceAppUtilSaveDataSlotGetParam, unsigned int slotId, SceAppUtilSaveDataSlotParam *param, SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataSlotGetParam, slotId, param, mountPoint);
    const auto fd = open_file(emuenv.io, construct_slotparam_path(slotId).c_str(), SCE_O_RDONLY, emuenv.vita_fs_path, export_name);
    if (fd < 0)
        return RET_ERROR(SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND);
    read_file(param, emuenv.io, fd, sizeof(SceAppUtilSaveDataSlotParam), export_name);
    close_file(emuenv.io, fd, export_name);
    param->status = 0;
    return 0;
}

EXPORT(SceInt32, sceAppUtilSaveDataSlotSearch, SceAppUtilWorkBuffer *workBuf, const SceAppUtilSaveDataSlotSearchCond *cond,
    SceAppUtilSlotSearchResult *result, const SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataSlotSearch, workBuf, cond, result, mountPoint);

    if (!cond || !result)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    if (workBuf)
        result->slotList = Ptr<SceAppUtilSaveDataSlot>(workBuf->buf.address());

    // Collect the matching slots in id order, then order them by the requested key.
    std::vector<app_util::SlotEntry> hits;
    const SceUInt32 first = cond->from;
    const SceUInt32 last = static_cast<SceUInt32>(std::min<uint64_t>(static_cast<uint64_t>(cond->from) + cond->range, SCE_APPUTIL_SAVEDATA_SLOT_MAX));
    for (SceUInt32 i = first; i < last; i++) {
        const auto fd = open_file(emuenv.io, construct_slotparam_path(i).c_str(), SCE_O_RDONLY, emuenv.vita_fs_path, export_name);
        switch (cond->type) {
        case SCE_APPUTIL_SAVEDATA_SLOT_SEARCH_TYPE_EXIST_SLOT:
            if (fd > 0) {
                SceAppUtilSaveDataSlotParam param{};
                read_file(&param, emuenv.io, fd, sizeof(SceAppUtilSaveDataSlotParam), export_name);
                app_util::SlotEntry entry;
                entry.id = i;
                entry.status = param.status;
                entry.user_param = param.userParam;
                entry.size_kib = static_cast<std::uint32_t>(param.sizeKiB);
                entry.modified_key = app_util::modified_time_key(param.modifiedTime.year, param.modifiedTime.month, param.modifiedTime.day,
                    param.modifiedTime.hour, param.modifiedTime.minute, param.modifiedTime.second, param.modifiedTime.microsecond);
                hits.push_back(entry);
            }
            break;
        case SCE_APPUTIL_SAVEDATA_SLOT_SEARCH_TYPE_EMPTY_SLOT:
            if (fd < 0) {
                app_util::SlotEntry entry;
                entry.id = i;
                hits.push_back(entry);
            }
            break;
        default: break;
        }

        if (fd > 0)
            close_file(emuenv.io, fd, export_name);
    }

    // Empty slots carry no parameters: only the slot id order is meaningful for them.
    const int sort_key = cond->type == SCE_APPUTIL_SAVEDATA_SLOT_SEARCH_TYPE_EMPTY_SLOT ? app_util::SORT_KEY_SLOT_ID : static_cast<int>(cond->key);
    app_util::sort_slots(hits, sort_key, static_cast<int>(cond->order));

    result->hitNum = static_cast<SceUInt32>(hits.size());
    auto slotList = result->slotList.get(emuenv.mem);
    if (slotList) {
        for (size_t n = 0; n < hits.size(); n++) {
            slotList[n].id = hits[n].id;
            slotList[n].status = hits[n].status;
            slotList[n].userParam = hits[n].user_param;
            slotList[n].emptyParam = Ptr<SceAppUtilSaveDataSlotEmptyParam>(0);
        }
    }

    return 0;
}

EXPORT(SceInt32, sceAppUtilSaveDataSlotSetParam, SceAppUtilSaveDataSlotId slotId, SceAppUtilSaveDataSlotParam *param, SceAppUtilMountPoint *mountPoint) {
    TRACY_FUNC(sceAppUtilSaveDataSlotSetParam, slotId, param, mountPoint);
    const auto fd = open_file(emuenv.io, construct_slotparam_path(slotId).c_str(), SCE_O_WRONLY, emuenv.vita_fs_path, export_name);
    if (fd < 0)
        return RET_ERROR(SCE_APPUTIL_ERROR_SAVEDATA_SLOT_NOT_FOUND);
    write_file(fd, param, sizeof(SceAppUtilSaveDataSlotParam), emuenv.io, export_name);
    close_file(emuenv.io, fd, export_name);
    return 0;
}

EXPORT(int, sceAppUtilSaveDataUmount) {
    TRACY_FUNC(sceAppUtilSaveDataUmount);
    return UNIMPLEMENTED();
}

static SceInt32 SafeMemory(EmuEnvState &emuenv, void *buf, SceSize bufSize, SceOff offset, const char *export_name, bool save) {
    std::vector<char> safe_mem(SCE_APPUTIL_SAFEMEMORY_MEMORY_SIZE);
    const auto safe_mem_path = construct_savedata0_path("sce_sys/safemem", "dat");
    SceInt32 res = 0;

    // Open file when it exist
    const auto fd = open_file(emuenv.io, safe_mem_path.c_str(), SCE_O_RDONLY, emuenv.vita_fs_path, export_name);
    if (fd > 0) {
        // Read file for set data inside safe mem when it exist
        res = read_file(safe_mem.data(), emuenv.io, fd, SCE_APPUTIL_SAFEMEMORY_MEMORY_SIZE, export_name);
        close_file(emuenv.io, fd, export_name);
    }

    if ((fd < 0) || save) {
        // When safe mem no exist or in save mode, write it with set buffer inside data
        const auto fd = open_file(emuenv.io, safe_mem_path.c_str(), SCE_O_WRONLY | SCE_O_CREAT, emuenv.vita_fs_path, export_name);
        memcpy(&safe_mem[offset], buf, bufSize);
        write_file(fd, safe_mem.data(), SCE_APPUTIL_SAFEMEMORY_MEMORY_SIZE, emuenv.io, export_name);
        close_file(emuenv.io, fd, export_name);
    } else
        memcpy(buf, &safe_mem[offset], bufSize);

    return res;
}

EXPORT(SceInt32, sceAppUtilLoadSafeMemory, void *buf, SceSize bufSize, SceOff offset) {
    TRACY_FUNC(sceAppUtilLoadSafeMemory, buf, bufSize, offset);
    if (!buf || (offset + bufSize > SCE_APPUTIL_SAFEMEMORY_MEMORY_SIZE))
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    const auto res = SafeMemory(emuenv, buf, bufSize, offset, export_name, false);

    // Load can return 0 when file no exist
    return res > 0 ? bufSize : 0;
}

EXPORT(SceInt32, sceAppUtilSaveSafeMemory, const void *buf, SceSize bufSize, SceOff offset) {
    TRACY_FUNC(sceAppUtilSaveSafeMemory, buf, bufSize, offset);
    if (!buf || (offset + bufSize > SCE_APPUTIL_SAFEMEMORY_MEMORY_SIZE))
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    SafeMemory(emuenv, const_cast<void *>(buf), bufSize, offset, export_name, true);

    return bufSize;
}

EXPORT(int, sceAppUtilShutdown) {
    TRACY_FUNC(sceAppUtilShutdown);
    const std::lock_guard<std::mutex> lock(emuenv.app.services.mutex);
    if (!emuenv.app.services.apputil_inited)
        return RET_ERROR(SCE_APPUTIL_ERROR_NOT_INITIALIZED);
    emuenv.app.services.apputil_inited = false;
    return 0;
}

EXPORT(int, sceAppUtilStoreBrowse) {
    TRACY_FUNC(sceAppUtilStoreBrowse);
    return UNIMPLEMENTED();
}

EXPORT(SceInt32, sceAppUtilSystemParamGetInt, SceSystemParamId paramId, SceInt32 *value) {
    TRACY_FUNC(sceAppUtilSystemParamGetInt, paramId, value);
    if (!value)
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);

    switch (paramId) {
    case SCE_SYSTEM_PARAM_ID_LANG:
        *value = (SceSystemParamLang)emuenv.cfg.current_config.sys_lang;
        LOG_INFO("sceAppUtilSystemParamGetInt(LANG) -> {} (per-game={}, global={})",
            *value, emuenv.cfg.current_config.sys_lang, emuenv.cfg.sys_lang);
        return 0;
    case SCE_SYSTEM_PARAM_ID_ENTER_BUTTON:
        *value = (SceSystemParamEnterButtonAssign)emuenv.cfg.current_config.sys_button;
        return 0;
    case SCE_SYSTEM_PARAM_ID_DATE_FORMAT:
        *value = (SceSystemParamDateFormat)emuenv.cfg.current_config.sys_date_format;
        return 0;
    case SCE_SYSTEM_PARAM_ID_TIME_FORMAT:
        *value = (SceSystemParamTimeFormat)emuenv.cfg.current_config.sys_time_format;
        return 0;
    case SCE_SYSTEM_PARAM_ID_TIME_ZONE:
    case SCE_SYSTEM_PARAM_ID_SUMMERTIME: {
        // Generic: derived from the host clock (current offset from UTC and its DST flag).
        const std::time_t now = std::time(nullptr);
        tm local = {};
        SAFE_LOCALTIME(&now, &local);
        const auto offset = app_util::utc_offset_seconds(static_cast<std::int64_t>(now), local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
            local.tm_hour, local.tm_min, local.tm_sec);
        const auto params = app_util::time_zone_params(offset, local.tm_isdst > 0);
        *value = paramId == SCE_SYSTEM_PARAM_ID_TIME_ZONE ? params.time_zone_minutes : params.summertime;
        return 0;
    }
    default:
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
    }
}

EXPORT(int, sceAppUtilSystemParamGetString, unsigned int paramId, SceChar8 *buf, SceSize bufSize) {
    TRACY_FUNC(sceAppUtilSystemParamGetString, paramId, buf, bufSize);
    switch (paramId) {
    case SCE_SYSTEM_PARAM_ID_USER_NAME: {
        if (buf == nullptr || bufSize == 0) {
            return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
        }
        // copy user name into buf with size SCE_SYSTEM_PARAM_USERNAME_MAXSIZE or bufSize, whichever is smaller
        const auto copySize = std::min(bufSize, static_cast<unsigned int>(SCE_SYSTEM_PARAM_USERNAME_MAXSIZE));
        std::strncpy(reinterpret_cast<char *>(buf), emuenv.io.user_name.c_str(), copySize - 1);
        buf[copySize - 1] = '\0';
        // fallback to hostname
        if (buf[0] == '\0') {
            char devname[SCE_SYSTEM_PARAM_USERNAME_MAXSIZE];
            if (gethostname(devname, SCE_SYSTEM_PARAM_USERNAME_MAXSIZE) == 0) {
                std::strncpy(reinterpret_cast<char *>(buf), devname, copySize - 1);
                buf[copySize - 1] = '\0';
            }
            // double fallback to the default player name
            if (buf[0] == '\0') {
                std::strncpy(reinterpret_cast<char *>(buf), "EmuCore", copySize - 1);
                buf[copySize - 1] = '\0';
            }
        }
        break;
    }
    default:
        return RET_ERROR(SCE_APPUTIL_ERROR_PARAMETER);
    }
    return 0;
}
