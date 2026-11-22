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

// Pure helpers for SceAppUtil: save data slot ordering, host time zone values and addcont
// directory-name validation. No emulator state; used by SceAppUtil.cpp and the host tests.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace app_util {

// Mirrors SceAppUtilSaveDataSlotSortKey / SceAppUtilSaveDataSlotSortType.
enum SlotSortKey : int {
    SORT_KEY_SLOT_ID = 0,
    SORT_KEY_USER_PARAM = 1,
    SORT_KEY_SIZE_KIB = 2,
    SORT_KEY_MODIFIED_TIME = 3,
};
enum SlotSortOrder : int {
    SORT_ASCENT = 0,
    SORT_DESCENT = 1,
};

struct SlotEntry {
    std::uint32_t id = 0;
    std::uint32_t status = 0;
    int user_param = 0;
    std::uint32_t size_kib = 0;
    // Monotonic key of the modification time, see modified_time_key().
    std::uint64_t modified_key = 0;
};

constexpr std::uint64_t modified_time_key(int year, int month, int day, int hour, int minute, int second, std::uint32_t microsecond) {
    std::uint64_t k = static_cast<std::uint64_t>(year);
    k = k * 13 + static_cast<std::uint64_t>(month);
    k = k * 32 + static_cast<std::uint64_t>(day);
    k = k * 24 + static_cast<std::uint64_t>(hour);
    k = k * 60 + static_cast<std::uint64_t>(minute);
    k = k * 60 + static_cast<std::uint64_t>(second);
    return k * 1000000 + microsecond;
}

// Orders slots by `key` in the requested direction. Ties are broken by ascending slot id in both
// directions so the result is deterministic. Unknown keys fall back to the slot id.
inline void sort_slots(std::vector<SlotEntry> &slots, int key, int order) {
    const bool desc = order == SORT_DESCENT;
    auto value = [key](const SlotEntry &s) -> std::int64_t {
        switch (key) {
        case SORT_KEY_USER_PARAM:
            return s.user_param;
        case SORT_KEY_SIZE_KIB:
            return s.size_kib;
        case SORT_KEY_MODIFIED_TIME:
            return static_cast<std::int64_t>(s.modified_key);
        default:
            return s.id;
        }
    };
    std::stable_sort(slots.begin(), slots.end(), [&](const SlotEntry &a, const SlotEntry &b) {
        const std::int64_t va = value(a), vb = value(b);
        if (va != vb)
            return desc ? va > vb : va < vb;
        return a.id < b.id;
    });
}

// ---- Host time zone for SCE_SYSTEM_PARAM_ID_TIME_ZONE / SUMMERTIME.
struct TimeZoneParams {
    int time_zone_minutes; // offset from UTC without daylight saving, minutes east
    int summertime; // 1 while daylight saving time is in effect
};

constexpr std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Offset (seconds east of UTC) of a broken-down local time that corresponds to the UTC instant
// `utc_seconds` (seconds since 1970-01-01).
constexpr std::int64_t utc_offset_seconds(std::int64_t utc_seconds, int year, int month, int day, int hour, int minute, int second) {
    const std::int64_t local = days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day)) * 86400 + hour * 3600 + minute * 60 + second;
    return local - utc_seconds;
}

// Provisional: the daylight saving step is assumed to be exactly one hour, so the standard
// offset is the current offset minus 60 minutes while DST is active.
constexpr TimeZoneParams time_zone_params(std::int64_t offset_seconds, bool is_dst) {
    const int total_minutes = static_cast<int>(offset_seconds / 60);
    return { is_dst ? total_minutes - 60 : total_minutes, is_dst ? 1 : 0 };
}

// ---- addcont directory names: a single path component that fits SceAppUtilDrmAddcontId
// (17 bytes including the terminator).
inline bool addcont_dir_name_valid(const char *name, std::size_t buf_size = 17) {
    if (!name)
        return false;
    std::size_t len = 0;
    while (len < buf_size && name[len] != '\0')
        len++;
    if (len == 0 || len >= buf_size)
        return false;
    if (std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0)
        return false;
    for (std::size_t i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == ':')
            return false;
    }
    return true;
}

// ---- SceAppUtilBootParam::appVersion from the SFO APP_VER string ("01.05" -> 0x0105).
// PROVISIONAL: the firmware's encoding is not recovered; the BCD packing mirrors how the version is written.
// Anything that is not "<digits>.<digits>" (up to two digits each) yields 0.
inline std::uint32_t boot_param_app_version(const std::string &app_ver) {
    const std::size_t dot = app_ver.find('.');
    if (dot == std::string::npos || dot == 0 || dot > 2 || app_ver.size() - dot - 1 == 0 || app_ver.size() - dot - 1 > 2)
        return 0;
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < app_ver.size(); i++) {
        if (i == dot)
            continue;
        if (app_ver[i] < '0' || app_ver[i] > '9')
            return 0;
        value = (value << 4) | static_cast<std::uint32_t>(app_ver[i] - '0');
    }
    return value;
}

} // namespace app_util
