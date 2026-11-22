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

EXPORT(int, sceNpScoreAbortRequest) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreCensorComment) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreCensorCommentAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreChangeModeForOtherSaveDataOwners) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreCreateRequest) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreCreateTitleCtx) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreDeleteRequest) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreDeleteTitleCtx) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetBoardInfo) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetBoardInfoAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetFriendsRanking) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetFriendsRankingAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetGameData) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetGameDataAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetRankingByNpId) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetRankingByNpIdAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetRankingByNpIdPcId) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetRankingByNpIdPcIdAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetRankingByRange) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreGetRankingByRangeAsync) {
    return UNIMPLEMENTED();
}

// Offline console: the service initialises, but any request needing the PlayStation Network is not served.
// PROVISIONAL: double init reuses the generic SCE_NP_ERROR_ALREADY_INITIALIZED (service-specific code unrecovered).
EXPORT(int, sceNpScoreInit) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (emuenv.np.offline.score_inited)
        return RET_ERROR(SCE_NP_ERROR_ALREADY_INITIALIZED);
    emuenv.np.offline.score_inited = true;
    return 0;
}

EXPORT(int, sceNpScorePollAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreRecordGameData) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreRecordGameDataAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreRecordScore) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreRecordScoreAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreSanitizeComment) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreSanitizeCommentAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreSetPlayerCharacterId) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreSetTimeout) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpScoreTerm) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.score_inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    emuenv.np.offline.score_inited = false;
    return 0;
}

EXPORT(int, sceNpScoreWaitAsync) {
    return UNIMPLEMENTED();
}
