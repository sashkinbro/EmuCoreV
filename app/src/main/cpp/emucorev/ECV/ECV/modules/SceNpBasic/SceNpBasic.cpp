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

enum SceNpBasicError : uint32_t {
    SCE_NP_BASIC_ERROR_INVALID_ARGUMENT = 0x80551d02,
    // PROVISIONAL: the generic NP codes stand in for the Basic-specific ones (not recovered).
    SCE_NP_BASIC_ERROR_ALREADY_INITIALIZED = 0x80550001,
    SCE_NP_BASIC_ERROR_NOT_INITIALIZED = 0x80550002,
};

// Offline: there is never an event to dispatch to the registered handler.
EXPORT(int, sceNpBasicCheckCallback) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.basic_inited)
        return RET_ERROR(SCE_NP_BASIC_ERROR_NOT_INITIALIZED);
    return 0;
}

EXPORT(int, sceNpBasicCheckIfPlayerIsBlocked) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetBlockListEntries) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetBlockListEntryCount) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetFriendContextState) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetFriendListEntries) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetFriendListEntryCount, int *nb_friends) {
    if (!nb_friends)
        return RET_ERROR(SCE_NP_BASIC_ERROR_INVALID_ARGUMENT);

    STUBBED("No friends");
    *nb_friends = 0;
    return 0;
}

EXPORT(int, sceNpBasicGetFriendOnlineStatus) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetFriendRequestEntries) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetFriendRequestEntryCount) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetGameJoiningPresence) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetGamePresenceOfFriend) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetPlaySessionLog) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetPlaySessionLogSize) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetRequestedFriendRequestEntries) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicGetRequestedFriendRequestEntryCount) {
    return UNIMPLEMENTED();
}

// The emulator is an offline console: the NP Basic service initialises and keeps its handler, but no
// event (friends, presence, messages) is ever produced.
// PROVISIONAL: double-init / not-initialised reuse the generic SCE_NP_ERROR_* codes (Basic-specific values unrecovered).
EXPORT(int, sceNpBasicInit) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (emuenv.np.offline.basic_inited)
        return RET_ERROR(SCE_NP_BASIC_ERROR_ALREADY_INITIALIZED);
    emuenv.np.offline.basic_inited = true;
    return 0;
}

EXPORT(int, sceNpBasicJoinGameAckResponseSend) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicRecordPlaySessionLog) {
    return UNIMPLEMENTED();
}

// PROVISIONAL signature: (communication config, handler, handler argument).
EXPORT(int, sceNpBasicRegisterHandler, const void *commConfig, uint32_t handler, uint32_t arg) {
    if (!commConfig || !handler)
        return RET_ERROR(SCE_NP_BASIC_ERROR_INVALID_ARGUMENT);
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.basic_inited)
        return RET_ERROR(SCE_NP_BASIC_ERROR_NOT_INITIALIZED);
    emuenv.np.offline.basic_handler_pc = handler;
    emuenv.np.offline.basic_handler_arg = arg;
    return 0;
}

EXPORT(int, sceNpBasicRegisterInGameDataMessageHandler) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicRegisterJoinGameAckHandler) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicSendInGameDataMessage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicSetInGamePresence) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicTerm) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.basic_inited)
        return RET_ERROR(SCE_NP_BASIC_ERROR_NOT_INITIALIZED);
    emuenv.np.offline.basic_inited = false;
    emuenv.np.offline.basic_handler_pc = 0;
    emuenv.np.offline.basic_handler_arg = 0;
    return 0;
}

EXPORT(int, sceNpBasicUnregisterHandler) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.basic_inited)
        return RET_ERROR(SCE_NP_BASIC_ERROR_NOT_INITIALIZED);
    emuenv.np.offline.basic_handler_pc = 0;
    emuenv.np.offline.basic_handler_arg = 0;
    return 0;
}

EXPORT(int, sceNpBasicUnregisterInGameDataMessageHandler) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicUnregisterJoinGameAckHandler) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpBasicUnsetInGamePresence) {
    return UNIMPLEMENTED();
}
