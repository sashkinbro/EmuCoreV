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

#pragma once

// State machine for the common dialogs that have no UI here (store checkout, NP profile/friend list/message,
// photo import/review, camera import). They behave like a dialog the user dismissed at once: Init
// succeeds, the dialog finishes with RESULT_USER_CANCELED, Term returns to NONE.
// Values mirror psp2/common_dialog.h (vita-headers); the module static_asserts the equality with the
// SceCommonDialog* enums. No emulator dependencies so it can be unit tested on the host.

#include <cstdint>

namespace stub_dialog {

enum class Kind : uint8_t {
    None,
    StoreCheckout,
    NpProfile,
    NpFriendList,
    NpFriendList2,
    NpMessage,
    PhotoImport,
    PhotoReview,
    CameraImport
};

constexpr int32_t STATUS_NONE = 0;
constexpr int32_t STATUS_RUNNING = 1;
constexpr int32_t STATUS_FINISHED = 2;

constexpr int32_t RESULT_OK = 0;
constexpr int32_t RESULT_USER_CANCELED = 1;
constexpr int32_t RESULT_ABORTED = 2;

constexpr int32_t ERR_BUSY = static_cast<int32_t>(0x80020401);
constexpr int32_t ERR_NULL = static_cast<int32_t>(0x80020402);
constexpr int32_t ERR_NOT_RUNNING = static_cast<int32_t>(0x80020404);
constexpr int32_t ERR_NOT_FINISHED = static_cast<int32_t>(0x80020410);
constexpr int32_t ERR_NOT_IN_USE = static_cast<int32_t>(0x80020411);

struct Core {
    Kind kind = Kind::None;
    int32_t status = STATUS_NONE;
    int32_t result = RESULT_OK;
};

// other_dialog_active: any other common dialog (or this one) currently owns the dialog layer.
inline int32_t init(Core &core, Kind kind, bool other_dialog_active) {
    if (other_dialog_active || core.kind != Kind::None)
        return ERR_BUSY;
    core.kind = kind;
    core.status = STATUS_RUNNING;
    core.result = RESULT_USER_CANCELED;
    return 0;
}

// The per-frame hook (GetStatus / sceCommonDialogUpdate): a running dialog finishes.
inline void update(Core &core) {
    if (core.kind != Kind::None && core.status == STATUS_RUNNING)
        core.status = STATUS_FINISHED;
}

inline int32_t get_status(const Core &core, Kind kind) {
    return core.kind == kind ? core.status : STATUS_NONE;
}

inline int32_t abort(Core &core, Kind kind) {
    if (core.kind != kind)
        return ERR_NOT_IN_USE;
    if (core.status == STATUS_RUNNING) {
        core.status = STATUS_FINISHED;
        core.result = RESULT_ABORTED;
    }
    return 0; // an already finished dialog keeps its result
}

inline int32_t get_result(const Core &core, Kind kind, int32_t &result) {
    if (core.kind != kind)
        return ERR_NOT_IN_USE;
    if (core.status != STATUS_FINISHED)
        return ERR_NOT_FINISHED;
    result = core.result;
    return 0;
}

inline int32_t term(Core &core, Kind kind) {
    if (core.kind != kind)
        return ERR_NOT_IN_USE;
    if (core.status != STATUS_FINISHED)
        return ERR_NOT_FINISHED;
    core = Core{};
    return 0;
}

} // namespace stub_dialog
