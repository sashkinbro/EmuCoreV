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

// Offline console: the service initialises, but any request needing the PlayStation Network is not served.
// PROVISIONAL: double init reuses the generic SCE_NP_ERROR_ALREADY_INITIALIZED (service-specific code unrecovered).
EXPORT(int, sceNpActivityInit) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (emuenv.np.offline.activity_inited)
        return RET_ERROR(SCE_NP_ERROR_ALREADY_INITIALIZED);
    emuenv.np.offline.activity_inited = true;
    return 0;
}

EXPORT(int, sceNpActivityPostAppStartupStatus) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpActivityPostStatus) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceNpActivityTerm) {
    const std::lock_guard<std::mutex> lock(emuenv.np.mutex);
    if (!emuenv.np.offline.activity_inited)
        return RET_ERROR(SCE_NP_ERROR_NOT_INITIALIZED);
    emuenv.np.offline.activity_inited = false;
    return 0;
}
