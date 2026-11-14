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

#include "io/functions.h"
#include <io/state.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceFios2Kernel);

struct sceFiosKernelOverlayResolveWithRangeSync_opt {
    Ptr<char> pOutPath;
    SceSize maxPath;
    char loOrderFilter;
    char hiOrderFilter;
    char reserved1;
    char reserved2;
    int reserved3;
    int reserved4;
    int reserved5;
    int reserved6;
};

enum SceFiosKernelResult {
    SCE_FIOS_OK = 0,
    SCE_FIOS_ERROR_BAD_PTR = 0x80B4000B,
};

typedef SceUID SceFiosOverlayID;

// This module is the old (launch-era) FIOS2 kernel overlay API
EXPORT(int, _sceFiosKernelOverlayAdd, SceFiosProcessOverlay *pOverlay, SceFiosOverlayID *pOutID) {
    TRACY_FUNC(_sceFiosKernelOverlayAdd, pOverlay, pOutID);
    if (!pOverlay || !pOutID)
        return RET_ERROR(SCE_FIOS_ERROR_BAD_PTR);
    if (pOverlay->type != SCE_FIOS_OVERLAY_TYPE_OPAQUE)
        LOG_WARN("Using unimplemented overlay type {}.", fmt::underlying(pOverlay->type));

    *pOutID = create_overlay(emuenv.io, pOverlay);
    LOG_INFO("fios overlay #{} added: order {} type {} \"{}\" -> \"{}\"", *pOutID, pOverlay->order, fmt::underlying(pOverlay->type), pOverlay->dst, pOverlay->src);
    return SCE_FIOS_OK;
}

EXPORT(int, _sceFiosKernelOverlayAddForProcess, SceUID pid, SceFiosProcessOverlay *pOverlay, SceFiosOverlayID *pOutID) {
    TRACY_FUNC(_sceFiosKernelOverlayAddForProcess, pid, pOverlay, pOutID);
    if (!pOverlay || !pOutID)
        return RET_ERROR(SCE_FIOS_ERROR_BAD_PTR);
    if (pOverlay->type != SCE_FIOS_OVERLAY_TYPE_OPAQUE)
        LOG_WARN("Using unimplemented overlay type {}.", fmt::underlying(pOverlay->type));

    *pOutID = create_overlay(emuenv.io, pOverlay);
    LOG_INFO("fios overlay #{} added (pid {}): order {} type {} \"{}\" -> \"{}\"", *pOutID, pid, pOverlay->order, fmt::underlying(pOverlay->type), pOverlay->dst, pOverlay->src);
    return SCE_FIOS_OK;
}

EXPORT(int, _sceFiosKernelOverlayDHChstatSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayDHCloseSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayDHOpenSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayDHReadSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayDHStatSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayDHSyncSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayGetInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayGetInfoForProcess) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayGetList, SceUID pid, uint32_t minOrder, uint32_t maxOrder, SceFiosOverlayID *pOutIDs, SceUInt32 maxIDs, SceUInt32 *pActualIDs) {
    TRACY_FUNC(_sceFiosKernelOverlayGetList, pid, minOrder, maxOrder, pOutIDs, maxIDs, pActualIDs);
    const std::lock_guard<std::mutex> guard(emuenv.io.overlay_mutex);

    std::vector<SceFiosOverlayID> overlay_ids;
    for (const auto &overlay : emuenv.io.overlays) {
        if (overlay.order >= minOrder && overlay.order <= maxOrder)
            overlay_ids.push_back(overlay.id);
    }

    if (pActualIDs)
        *pActualIDs = static_cast<SceUInt32>(overlay_ids.size());
    if (pOutIDs)
        memcpy(pOutIDs, overlay_ids.data(), std::min<uint32_t>(overlay_ids.size(), maxIDs) * sizeof(SceFiosOverlayID));

    return SCE_FIOS_OK;
}

EXPORT(int, _sceFiosKernelOverlayGetRecommendedScheduler) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayModify) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayModifyForProcess) {
    return UNIMPLEMENTED();
}

static int remove_overlay_by_id(EmuEnvState &emuenv, SceFiosOverlayID id) {
    const std::lock_guard<std::mutex> guard(emuenv.io.overlay_mutex);
    for (auto it = emuenv.io.overlays.begin(); it != emuenv.io.overlays.end(); ++it) {
        if (it->id == id) {
            LOG_INFO("fios overlay #{} removed: \"{}\" -> \"{}\"", id, it->dst, it->src);
            emuenv.io.overlays.erase(it);
            return SCE_FIOS_OK;
        }
    }
    return SCE_FIOS_OK; // removing an unknown id is tolerated
}

EXPORT(int, _sceFiosKernelOverlayRemove, SceFiosOverlayID id) {
    TRACY_FUNC(_sceFiosKernelOverlayRemove, id);
    return remove_overlay_by_id(emuenv, id);
}

EXPORT(int, _sceFiosKernelOverlayRemoveForProcess, SceUID pid, SceFiosOverlayID id) {
    TRACY_FUNC(_sceFiosKernelOverlayRemoveForProcess, pid, id);
    return remove_overlay_by_id(emuenv, id);
}

EXPORT(int, _sceFiosKernelOverlayResolveSync, SceUID pid, int resolveFlag, const char *pInPath, Ptr<char> pOutPath, SceSize maxPath) {
    TRACY_FUNC(_sceFiosKernelOverlayResolveSync, pid, resolveFlag, pInPath, pOutPath, maxPath);
    if (!pInPath || !pOutPath)
        return RET_ERROR(SCE_FIOS_ERROR_BAD_PTR);

    const std::string resolved = resolve_path(emuenv.io, pInPath);
    strncpy(pOutPath.get(emuenv.mem), resolved.c_str(), maxPath);
    return SCE_FIOS_OK;
}

EXPORT(int, _sceFiosKernelOverlayResolveWithRangeSync, SceUID pid, int resolveFlag, const char *pInPath, sceFiosKernelOverlayResolveWithRangeSync_opt *opt) {
    TRACY_FUNC(_sceFiosKernelOverlayResolveWithRangeSync, pid, resolveFlag, pInPath, opt);
    if (!pInPath || !opt || !opt->pOutPath)
        return RET_ERROR(SCE_FIOS_ERROR_BAD_PTR);

    const SceUInt32 min_order = static_cast<uint8_t>(opt->loOrderFilter);
    const SceUInt32 max_order = (opt->hiOrderFilter == 0 && opt->loOrderFilter == 0) ? 0x7F : static_cast<uint8_t>(opt->hiOrderFilter);
    const std::string resolved = resolve_path(emuenv.io, pInPath, min_order, max_order);
    strncpy(opt->pOutPath.get(emuenv.mem), resolved.c_str(), opt->maxPath);
    return SCE_FIOS_OK;
}

EXPORT(int, _sceFiosKernelOverlayThreadIsDisabled) {
    return UNIMPLEMENTED();
}

EXPORT(int, _sceFiosKernelOverlayThreadSetDisabled) {
    return UNIMPLEMENTED();
}
