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

#include "SceNpManager.h"

#include "util/types.h"

#include <io/state.h>
#include <kernel/state.h>
#include <np/state.h>
#include <util/log.h>

#include <np/functions.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceNpManager);

enum SceNpErrorCode : uint32_t {
    SCE_NP_ERROR_ALREADY_INITIALIZED = 0x80550001,
    SCE_NP_ERROR_NOT_INITIALIZED = 0x80550002,
    SCE_NP_ERROR_INVALID_ARGUMENT = 0x80550003,
    SCE_NP_ERROR_UNKNOWN_PLATFORM_TYPE = 0x80550004
};

enum SceNpManagerErrorCode : uint32_t {
    SCE_NP_MANAGER_ERROR_ABORTED = 0x80550507,
    SCE_NP_MANAGER_ERROR_ALREADY_INITIALIZED = 0x80550501,
    SCE_NP_MANAGER_ERROR_OUT_OF_MEMORY = 0x80550504,
    SCE_NP_MANAGER_ERROR_NOT_INITIALIZED = 0x80550502,
    SCE_NP_MANAGER_ERROR_INVALID_ARGUMENT = 0x80550503,
    SCE_NP_MANAGER_ERROR_INVALID_STATE = 0x80550506,
    SCE_NP_MANAGER_ERROR_ID_NOT_AVAIL = 0x80550509
};

EXPORT(int, sceNpAuthAbortOAuthRequest) {
    TRACY_FUNC(sceNpAuthAbortOAuthRequest);
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpAuthCreateOAuthRequest) {
    TRACY_FUNC(sceNpAuthCreateOAuthRequest);
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpAuthDeleteOAuthRequest) {
    TRACY_FUNC(sceNpAuthDeleteOAuthRequest);
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpAuthGetAuthorizationCode) {
    TRACY_FUNC(sceNpAuthGetAuthorizationCode);
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpCheckCallback) {
    TRACY_FUNC(sceNpCheckCallback);

    std::vector<int> callback_ids;
    uint64_t generation;
    {
        const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
        if (!emuenv.np.inited)
            return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
        generation = emuenv.np.generation;
        for (const auto &[id, _] : emuenv.np.cbs)
            callback_ids.push_back(id);
    }

    const ThreadStatePtr thread = emuenv.kernel.get_thread(thread_id);
    const SceNpServiceState state = emuenv.cfg.current_config.psn_signed_in ? SCE_NP_SERVICE_STATE_SIGNED_IN : SCE_NP_SERVICE_STATE_SIGNED_OUT;
    for (const int id : callback_ids) {
        SceNpServiceStateCallback np_callback;
        {
            const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
            if (!emuenv.np.inited || emuenv.np.generation != generation)
                return 0;
            const auto it = emuenv.np.cbs.find(id);
            if (it == emuenv.np.cbs.end())
                continue;
            if (it->second.delivered && it->second.last_state == static_cast<uint32_t>(state))
                continue;
            it->second.delivered = true;
            it->second.last_state = static_cast<uint32_t>(state);
            np_callback = it->second;
        }
        // Guest code may unregister, terminate or reenter this pump.
        // No container iterator or service mutex may span the guest call.
        thread->run_callback(np_callback.pc, { static_cast<uint32_t>(state), 0, np_callback.data });
    }

    std::vector<SceNpMatching2ContextEvent> events;
    Address context_cb_pc = 0;
    Address context_cb_arg = 0;
    uint64_t context_generation = 0;
    {
        NpMatching2State &matching2 = emuenv.np.matching2;
        const std::scoped_lock lock(emuenv.np.mutex, matching2.mutex);
        if (!emuenv.np.inited || emuenv.np.generation != generation)
            return 0;
        if (!matching2.dispatching && matching2.context_cb_pc && !matching2.pending.empty()) {
            events.swap(matching2.pending);
            matching2.dispatching = true;
            context_cb_pc = matching2.context_cb_pc;
            context_cb_arg = matching2.context_cb_arg;
            context_generation = matching2.callback_generation;
        }
    }
    // Nested pumps leave newer events queued until this ordered batch finishes.
    struct MatchingDispatchScope {
        NpMatching2State &matching2;
        uint64_t generation;
        bool active;
        ~MatchingDispatchScope() {
            if (!active)
                return;
            const std::lock_guard<std::mutex> lock(matching2.mutex);
            if (matching2.callback_generation == generation)
                matching2.dispatching = false;
        }
    } dispatch_scope{ emuenv.np.matching2, context_generation, !events.empty() };
    for (const SceNpMatching2ContextEvent &event : events) {
        {
            NpMatching2State &matching2 = emuenv.np.matching2;
            const std::scoped_lock lock(emuenv.np.mutex, matching2.mutex);
            if (!emuenv.np.inited || emuenv.np.generation != generation || matching2.callback_generation != context_generation)
                break;
        }
        thread->run_callback(context_cb_pc, { event.ctx_id, event.event, event.cause, event.error_code, context_cb_arg });
    }

    return STUBBED("Stub");
}

EXPORT(int, sceNpGetServiceState, SceNpServiceState *state) {
    TRACY_FUNC(sceNpGetServiceState, state);
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    if (!state)
        return RET_ERROR(SCE_NP_ERROR_INVALID_ARGUMENT);
    *state = emuenv.cfg.current_config.psn_signed_in ? SCE_NP_SERVICE_STATE_SIGNED_IN : SCE_NP_SERVICE_STATE_SIGNED_OUT;

    return STUBBED("Stub");
}

EXPORT(int, sceNpInit, np::CommunicationConfig *comm_config, void *dontcare) {
    TRACY_FUNC(sceNpInit, comm_config, dontcare);
    const np::CommunicationID *comm_id = (comm_config) ? comm_config->comm_id.get(emuenv.mem) : nullptr;

    if (!init(emuenv.np, comm_id)) {
        return SCE_NP_ERROR_ALREADY_INITIALIZED;
    }

    return 0;
}

EXPORT(int, sceNpManagerGetAccountRegion) {
    TRACY_FUNC(sceNpManagerGetAccountRegion);
    return UNIMPLEMENTED();
}

constexpr size_t SCE_NP_AVATAR_URL_MAX_LENGTH = 127;

struct SceNpAvatarUrl {
    char data[SCE_NP_AVATAR_URL_MAX_LENGTH];
    char term;
};

struct SceNpManagerCacheParam {
    np::SceNpId npId;
    SceNpAvatarUrl avatarUrl;
};
static_assert(sizeof(SceNpManagerCacheParam) == 0xA4, "SceNpManagerCacheParam is an invalid size");

EXPORT(int, sceNpManagerGetCachedParam, Ptr<SceNpManagerCacheParam> param) {
    TRACY_FUNC(sceNpManagerGetCachedParam, param);
    if (!param || !param.valid(emuenv.mem))
        return RET_ERROR(SCE_NP_MANAGER_ERROR_INVALID_ARGUMENT);

    // the NP ID and avatar URL the system cached in flash at the last sign-in
    SceNpManagerCacheParam *const cache = param.get(emuenv.mem);
    memset(cache, 0, sizeof(*cache));
    return CALL_EXPORT(sceNpManagerGetNpId, &cache->npId);
}

EXPORT(int, sceNpManagerGetChatRestrictionFlag, SceInt *isRestricted) {
    TRACY_FUNC(sceNpManagerGetChatRestrictionFlag, isRestricted);
    *isRestricted = 0; // User is never restricted
    return 0;
}

EXPORT(int, sceNpManagerGetContentRatingFlag, SceInt *isRestricted, SceInt *age) {
    TRACY_FUNC(sceNpManagerGetContentRatingFlag, isRestricted, age);
    *isRestricted = 0; // User is never restricted
    *age = 21; // Assume user is 21 years old
    return STUBBED("isRestricted = 0; age = 21; return 0;");
}

EXPORT(int, sceNpManagerGetNpId, np::SceNpId *id) {
    TRACY_FUNC(sceNpManagerGetNpId, id);
    if (!id)
        return RET_ERROR(SCE_NP_MANAGER_ERROR_INVALID_ARGUMENT);
    if (emuenv.io.user_name.length() > SCE_NP_ONLINEID_MAX_LENGTH) {
        LOG_ERROR("Your online ID has over 16 characters, try again with shorter name");
        return SCE_NP_MANAGER_ERROR_ID_NOT_AVAIL;
    }
    // Fill the unused stuffs to 0 (prevent some weird things happen)
    memset(id, 0, sizeof(np::SceNpId));
    strcpy(id->handle.data, emuenv.io.user_name.c_str());
    id->isIdValid = true;
    id->opt.platformType[0] = 'p';
    id->opt.platformType[1] = 's';
    id->opt.platformType[2] = 'p';
    id->opt.platformType[3] = '2';
    return 0;
}

EXPORT(int, sceNpRegisterServiceStateCallback, Ptr<void> callback, Ptr<void> data) {
    TRACY_FUNC(sceNpRegisterServiceStateCallback, callback, data);
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    if (!callback)
        return RET_ERROR(SCE_NP_ERROR_INVALID_ARGUMENT);
    SceUID cid = emuenv.kernel.get_next_uid();
    SceNpServiceStateCallback sceNpServiceStateCallback{
        .pc = callback.address(),
        .data = data.address()
    };
    emuenv.np.cbs.emplace(cid, sceNpServiceStateCallback);
    emuenv.np.state_cb_id = cid;
    return 0;
}

EXPORT(void, sceNpTerm) {
    TRACY_FUNC(sceNpTerm);
    deinit(emuenv.np);
}

EXPORT(int, sceNpUnregisterServiceStateCallback) {
    TRACY_FUNC(sceNpUnregisterServiceStateCallback);
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    if (emuenv.np.state_cb_id) {
        emuenv.np.cbs.erase(emuenv.np.state_cb_id);
        emuenv.np.state_cb_id = 0;
    }
    return 0;
}
