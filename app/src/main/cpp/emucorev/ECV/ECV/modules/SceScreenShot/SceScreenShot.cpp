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

#include <app/state.h>
#include <kernel/types.h>

#include <mutex>
#include <string>

static std::string screenshot_read_string(const MemState &mem, Ptr<const char> str) {
    if (!str || !str.valid(mem))
        return {};
    // bounded: the firmware limits these strings to a few hundred bytes
    constexpr size_t MAX_LEN = 1024;
    const char *const text = str.get(mem);
    size_t len = 0;
    while (len < MAX_LEN && text[len] != '\0')
        len++;
    return std::string(text, len);
}

EXPORT(int, sceScreenShotCapture) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceScreenShotDisable) {
    const std::lock_guard<std::mutex> lock(emuenv.app.services.mutex);
    emuenv.app.services.screenshot_enabled = false;
    return 0;
}

EXPORT(int, sceScreenShotDisableNotification) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceScreenShotEnable) {
    const std::lock_guard<std::mutex> lock(emuenv.app.services.mutex);
    emuenv.app.services.screenshot_enabled = true;
    return 0;
}

EXPORT(int, sceScreenShotEnableNotification) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceScreenShotGetParam) {
    return UNIMPLEMENTED();
}

// (path of the overlay image, offsetX, offsetY); a NULL path removes the overlay. The image is only
// recorded: the emulator has no system screenshot to draw it on.
EXPORT(int, sceScreenShotSetOverlayImage, Ptr<const char> path, int32_t offsetX, int32_t offsetY) {
    const std::lock_guard<std::mutex> lock(emuenv.app.services.mutex);
    if (!path) {
        emuenv.app.services.screenshot_overlay_path.clear();
        emuenv.app.services.screenshot_overlay_x = 0;
        emuenv.app.services.screenshot_overlay_y = 0;
        return 0;
    }
    if (!path.valid(emuenv.mem))
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    std::string value = screenshot_read_string(emuenv.mem, path);
    if (value.empty())
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    emuenv.app.services.screenshot_overlay_path = std::move(value);
    emuenv.app.services.screenshot_overlay_x = offsetX;
    emuenv.app.services.screenshot_overlay_y = offsetY;
    return 0;
}

// PROVISIONAL layout of the SDK's SceScreenShotParam: three UTF-8 string pointers (photo title, game
// title, game comment). Pointers are read as guest addresses; a NULL string clears that field.
struct SceScreenShotParam {
    Ptr<const char> photoTitle;
    Ptr<const char> gameTitle;
    Ptr<const char> gameComment;
};

EXPORT(int, sceScreenShotSetParam, Ptr<const SceScreenShotParam> param) {
    if (!param || !param.valid(emuenv.mem))
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);

    const SceScreenShotParam *const p = param.get(emuenv.mem);
    const std::lock_guard<std::mutex> lock(emuenv.app.services.mutex);
    emuenv.app.services.screenshot_param_set = true;
    emuenv.app.services.screenshot_photo_title = screenshot_read_string(emuenv.mem, p->photoTitle);
    emuenv.app.services.screenshot_game_title = screenshot_read_string(emuenv.mem, p->gameTitle);
    emuenv.app.services.screenshot_game_comment = screenshot_read_string(emuenv.mem, p->gameComment);
    return 0;
}
