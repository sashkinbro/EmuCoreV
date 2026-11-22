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

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct EmuEnvState;

namespace app {

struct User {
    std::string id;
    std::string name = "EmuCore";
    std::string theme_id = "default";
    bool use_theme_bg = true;
    std::string start_type = "default";
    std::string start_path;
    std::vector<std::string> backgrounds;
};

struct UserListState {
    std::map<std::string, User> users;
};

struct AppEntry {
    std::string app_ver;
    std::string category;
    std::string content_id;
    std::string addcont;
    std::string savedata;
    std::string parental_level;
    std::string stitle;
    std::string title;
    std::string title_id;
    std::string path;
    std::string icon_path;
};

struct AppTime {
    std::string app_path;
    std::time_t last_time_used{ 0 };
    int64_t time_used{ 0 };
};

struct AppsListState {
    std::vector<AppEntry> apps;

    std::map<std::string, std::vector<AppTime>> app_times;

    mutable std::mutex mutex;
};

// Session state of small HLE services that only keep a few values (no emulated hardware behind them).
struct ServiceState {
    std::mutex mutex;

    // sceAppUtilInit / sceAppUtilShutdown
    bool apputil_inited = false;

    // sceAppMgrAcquireBgmPort / sceAppMgrReleaseBgmPort: the application owns the BGM port.
    bool bgm_port_acquired = false;

    // sceScreenShot*: the emulator has no system screenshot UI, the values are only kept for queries.
    bool screenshot_enabled = true;
    bool screenshot_param_set = false;
    std::string screenshot_photo_title;
    std::string screenshot_game_title;
    std::string screenshot_game_comment;
    std::string screenshot_overlay_path;
    int screenshot_overlay_x = 0;
    int screenshot_overlay_y = 0;

    void reset() {
        const std::lock_guard<std::mutex> lock(mutex);
        apputil_inited = false;
        bgm_port_acquired = false;
        screenshot_enabled = true;
        screenshot_param_set = false;
        screenshot_photo_title.clear();
        screenshot_game_title.clear();
        screenshot_game_comment.clear();
        screenshot_overlay_path.clear();
        screenshot_overlay_x = 0;
        screenshot_overlay_y = 0;
    }
};

} // namespace app

struct AppState {
    app::AppsListState apps_list;
    app::UserListState user_list;
    app::ServiceState services;
};
