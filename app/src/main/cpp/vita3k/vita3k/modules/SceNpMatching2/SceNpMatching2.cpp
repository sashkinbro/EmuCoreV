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

#include <emuenv/state.h>
#include <np/state.h>
#include <util/tracy.h>

TRACY_MODULE_NAME(SceNpMatching2);

enum SceNpMatching2ErrorCode {
    SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED = 0x80550C08
};

constexpr uint32_t SCE_NP_MATCHING2_CONTEXT_EVENT_START = 2;
constexpr uint32_t MATCHING2_NO_SERVER_ERROR = 0x80410123;

EXPORT(int, sceNpMatching2AbortContextStart) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2AbortRequest) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2ContextStart, SceUInt32 ctxId, SceUInt32 pad, SceUInt32 timeout_lo, SceUInt32 timeout_hi) {
    TRACY_FUNC(sceNpMatching2ContextStart, ctxId, pad, timeout_lo, timeout_hi);
    NpMatching2State &matching2 = emuenv.np.matching2;
    std::lock_guard<std::mutex> lock(matching2.mutex);
    if (!matching2.context_cb_pc) {
        // Nobody is listening, so there is no way to report the failure asynchronously
        return RET_ERROR(SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED);
    }

    matching2.pending.push_back({ ctxId, SCE_NP_MATCHING2_CONTEXT_EVENT_START, 0, MATCHING2_NO_SERVER_ERROR });
    return 0;
}

EXPORT(int, sceNpMatching2ContextStop, SceUInt32 ctxId) {
    TRACY_FUNC(sceNpMatching2ContextStop, ctxId);
    return RET_ERROR(SCE_NP_MATCHING2_ERROR_CONTEXT_NOT_STARTED);
}

EXPORT(int, sceNpMatching2CreateContext, Ptr<void> npId, Ptr<void> commId, Ptr<void> passphrase, SceUInt16 *ctxId) {
    TRACY_FUNC(sceNpMatching2CreateContext, npId, commId, passphrase, ctxId);
    if (!ctxId)
        return UNIMPLEMENTED();

    NpMatching2State &matching2 = emuenv.np.matching2;
    std::lock_guard<std::mutex> lock(matching2.mutex);
    *ctxId = static_cast<SceUInt16>(matching2.next_ctx_id++);
    return 0;
}

EXPORT(int, sceNpMatching2CreateJoinRoom) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2DestroyContext) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetLobbyInfoList) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetMemoryInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetRoomDataExternalList) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetRoomDataInternal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetRoomMemberDataExternalList) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetRoomMemberDataInternal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetRoomMemberIdListLocal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetRoomPasswordLocal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetServerLocal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetSignalingOptParamLocal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetUserInfoList) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GetWorldInfoList) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2GrantRoomOwner) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2Init) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2JoinLobby) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2JoinRoom) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2KickoutRoomMember) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2LeaveLobby) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2LeaveRoom) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2RegisterContextCallback, Ptr<void> cbFunc, Ptr<void> cbFuncArg) {
    TRACY_FUNC(sceNpMatching2RegisterContextCallback, cbFunc, cbFuncArg);
    NpMatching2State &matching2 = emuenv.np.matching2;
    std::lock_guard<std::mutex> lock(matching2.mutex);
    matching2.context_cb_pc = cbFunc.address();
    matching2.context_cb_arg = cbFuncArg.address();
    return 0;
}

EXPORT(int, sceNpMatching2RegisterLobbyEventCallback) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2RegisterLobbyMessageCallback) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2RegisterRoomEventCallback) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2RegisterRoomMessageCallback) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2RegisterSignalingCallback) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SearchRoom) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SendLobbyChatMessage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SendRoomChatMessage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SendRoomMessage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SetDefaultRequestOptParam) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SetRoomDataExternal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SetRoomDataInternal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SetRoomMemberDataInternal) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SetSignalingOptParam) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SetUserInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingCancelPeerNetInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingGetConnectionInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingGetConnectionStatus) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingGetLocalNetInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingGetPeerNetInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingGetPeerNetInfoResult) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2SignalingGetPingInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpMatching2Term) {
    TRACY_FUNC(sceNpMatching2Term);
    NpMatching2State &matching2 = emuenv.np.matching2;
    std::lock_guard<std::mutex> lock(matching2.mutex);
    matching2.context_cb_pc = 0;
    matching2.context_cb_arg = 0;
    matching2.pending.clear();
    return 0;
}
