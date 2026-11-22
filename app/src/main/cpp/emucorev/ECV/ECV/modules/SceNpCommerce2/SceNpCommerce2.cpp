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

#include <np/state.h>

#include <mutex>

// Generic NP codes (see SceNpManager).
enum : uint32_t {
    SCE_NP_ERROR_ALREADY_INITIALIZED = 0x80550001,
    SCE_NP_ERROR_NOT_INITIALIZED = 0x80550002,
    SCE_NP_ERROR_INVALID_ARGUMENT = 0x80550003,
};

EXPORT(int, sceNpCommerce2AbortReq) {
    return UNIMPLEMENTED();
}

// PROVISIONAL signature: (version, npId, handler, handler argument, out ctxId). The context only
// records its id; every request made on it needs the network, which this offline console lacks.
EXPORT(int, sceNpCommerce2CreateCtx, uint32_t version, Ptr<const void> npId, uint32_t handler, uint32_t arg, Ptr<int32_t> ctxId) {
    // The 5-argument layout is unconfirmed on the Vita (Crazy Market passed ctxId = 0x2C, which
    // crashed the host when it was written blindly): validate before writing and log the raw
    // arguments once so the real ABI can be confirmed.
    LOG_WARN_ONCE("sceNpCommerce2CreateCtx(version={:#x}, npId={:#x}, handler={:#x}, arg={:#x}, ctxId={:#x})",
        version, npId.address(), handler, arg, ctxId.address());
    if (!npId || !ctxId || !ctxId.valid(emuenv.mem))
        return RET_ERROR(SCE_NP_ERROR_INVALID_ARGUMENT);
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.commerce2_inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    const uint32_t id = emuenv.np.offline.commerce2_next_ctx_id++;
    emuenv.np.offline.commerce2_ctxs.insert(id);
    *ctxId.get(emuenv.mem) = static_cast<int32_t>(id);
    return 0;
}

EXPORT(int, sceNpCommerce2CreateSessionCreateReq) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2CreateSessionGetResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2CreateSessionStart) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2DestroyCtx, int32_t ctxId) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.commerce2_inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    if (emuenv.np.offline.commerce2_ctxs.erase(static_cast<uint32_t>(ctxId)) == 0)
        return RET_ERROR(SCE_NP_ERROR_INVALID_ARGUMENT);
    return 0;
}

EXPORT(int, sceNpCommerce2DestroyGetCategoryContentsResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2DestroyGetProductInfoListResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2DestroyGetProductInfoResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2DestroyReq) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetCategoryContentsCreateReq) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetCategoryContentsGetResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetCategoryContentsStart) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetCategoryInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetCategoryInfoFromContentInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetContentInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetContentRatingDescriptor) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetContentRatingInfoFromCategoryInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetContentRatingInfoFromGameProductInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetGameProductInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetGameProductInfoFromContentInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetGameProductInfoFromGetProductInfoListResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetGameSkuInfoFromGameProductInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetPrice) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetProductInfoCreateReq) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetProductInfoGetResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetProductInfoListCreateReq) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetProductInfoListGetResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetProductInfoListStart) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetProductInfoStart) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetSessionInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetShortfallOfLibhttpPool) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2GetShortfallOfLibsslPool) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2HidePsStoreIcon) {
    return UNIMPLEMENTED();
}

// Offline console: the service initialises, but any request needing the PlayStation Network is not served.
// PROVISIONAL: double init reuses the generic SCE_NP_ERROR_ALREADY_INITIALIZED (service-specific code unrecovered).
EXPORT(int, sceNpCommerce2Init) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (emuenv.np.offline.commerce2_inited)
        return RET_ERROR(SCE_NP_ERROR_ALREADY_INITIALIZED);
    emuenv.np.offline.commerce2_inited = true;
    return 0;
}

EXPORT(int, sceNpCommerce2InitGetCategoryContentsResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2InitGetProductInfoListResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2InitGetProductInfoResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2ShowPsStoreIcon) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2StartEmptyStoreCheck) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2StopEmptyStoreCheck) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCommerce2Term) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.commerce2_inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    emuenv.np.offline.commerce2_inited = false;
    emuenv.np.offline.commerce2_ctxs.clear();
    return 0;
}
