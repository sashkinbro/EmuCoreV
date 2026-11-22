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

// Pure (host independent, header-only) RTC calendar helpers: tick <-> civil conversion, DOS and
// Win32 FILETIME packing, RFC 2822 / RFC 3339 formatting and parsing. Ticks are microseconds since
// 0001-01-01T00:00:00 (proleptic Gregorian), as in SceRtcTick. Error values equal SceRtcErrorCode
// (rtc/rtc.h); they are repeated here so that the file has no dependencies.
//
// Text formats follow the vitasdk prototypes (vita-headers psp2/rtc.h). The exact strings produced by
// the firmware were not recovered: sub-second digits (hundredths) and the lenient parse grammar are
// provisional, modelled on the PSP sceRtc implementation.

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace rtc_text {

constexpr std::uint32_t ERR_INVALID_VALUE = 0x80251000;
constexpr std::uint32_t ERR_BAD_PARSE = 0x80251080;
constexpr std::uint32_t ERR_INVALID_YEAR = 0x80251081;
constexpr std::uint32_t ERR_INVALID_MONTH = 0x80251082;
constexpr std::uint32_t ERR_INVALID_DAY = 0x80251083;
constexpr std::uint32_t ERR_INVALID_HOUR = 0x80251084;
constexpr std::uint32_t ERR_INVALID_MINUTE = 0x80251085;
constexpr std::uint32_t ERR_INVALID_SECOND = 0x80251086;
constexpr std::uint32_t ERR_INVALID_MICROSECOND = 0x80251087;

constexpr std::uint64_t TICKS_PER_SEC = 1000000ULL;
constexpr std::uint64_t TICKS_PER_DAY = 86400ULL * TICKS_PER_SEC;
// Days between 0001-01-01 and 1970-01-01.
constexpr std::int64_t DAYS_TO_UNIX_EPOCH = 719162;
// Microseconds between 0001-01-01 and 1601-01-01 (Win32 FILETIME epoch).
constexpr std::uint64_t FILETIME_EPOCH_TICKS = 50491123200000000ULL;

struct Civil {
    int year = 1;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
    std::uint32_t microsecond = 0;
};

constexpr bool is_leap(int y) { return (y % 400 == 0) || (y % 100 != 0 && y % 4 == 0); }

constexpr int days_in_month(int y, int m) {
    constexpr int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

// 1-based day of the year, 1..366. Caller must pass a valid y/m/d.
constexpr int day_of_year(int y, int m, int d) {
    int n = d;
    for (int i = 1; i < m; i++)
        n += days_in_month(y, i);
    return n;
}

// Days since 1970-01-01 (Hinnant's days_from_civil).
constexpr std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

constexpr void civil_from_days(std::int64_t z, int &y, int &m, int &d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    y = static_cast<int>(yy + (m <= 2));
}

constexpr Civil civil_from_ticks(std::uint64_t ticks) {
    Civil c;
    const std::int64_t days = static_cast<std::int64_t>(ticks / TICKS_PER_DAY) - DAYS_TO_UNIX_EPOCH;
    std::uint64_t rem = ticks % TICKS_PER_DAY;
    civil_from_days(days, c.year, c.month, c.day);
    c.microsecond = static_cast<std::uint32_t>(rem % TICKS_PER_SEC);
    rem /= TICKS_PER_SEC;
    c.second = static_cast<int>(rem % 60);
    c.minute = static_cast<int>(rem / 60 % 60);
    c.hour = static_cast<int>(rem / 3600);
    return c;
}

// Fields must be valid; the result is meaningful for year >= 1.
constexpr std::uint64_t ticks_from_civil(const Civil &c) {
    const std::int64_t days = days_from_civil(c.year, static_cast<unsigned>(c.month), static_cast<unsigned>(c.day)) + DAYS_TO_UNIX_EPOCH;
    const std::uint64_t secs = static_cast<std::uint64_t>(c.hour) * 3600 + static_cast<std::uint64_t>(c.minute) * 60 + static_cast<std::uint64_t>(c.second);
    return static_cast<std::uint64_t>(days) * TICKS_PER_DAY + secs * TICKS_PER_SEC + c.microsecond;
}

// 0 if valid, else the SCE_RTC_ERROR_INVALID_* code (same order as sceRtcCheckValid).
constexpr std::uint32_t check_valid(const Civil &c) {
    if (c.month < 1 || c.month > 12)
        return ERR_INVALID_MONTH;
    if (c.day < 1 || c.day > days_in_month(c.year, c.month))
        return ERR_INVALID_DAY;
    if (c.hour < 0 || c.hour > 23)
        return ERR_INVALID_HOUR;
    if (c.minute < 0 || c.minute > 59)
        return ERR_INVALID_MINUTE;
    if (c.second < 0 || c.second > 59)
        return ERR_INVALID_SECOND;
    if (c.microsecond > 999999)
        return ERR_INVALID_MICROSECOND;
    return 0;
}

// ---- DOS time: high half = date ((y-1980)<<9 | m<<5 | d), low half = time (h<<11 | min<<5 | s/2).
// Returns 0 or ERR_INVALID_VALUE (fields out of range, or all-zero date).
constexpr std::uint32_t civil_from_dos(std::uint32_t dos, Civil &out) {
    Civil c;
    const std::uint32_t date = dos >> 16;
    const std::uint32_t time = dos & 0xFFFF;
    c.year = 1980 + static_cast<int>(date >> 9);
    c.month = static_cast<int>((date >> 5) & 0xF);
    c.day = static_cast<int>(date & 0x1F);
    c.hour = static_cast<int>(time >> 11);
    c.minute = static_cast<int>((time >> 5) & 0x3F);
    c.second = static_cast<int>(time & 0x1F) * 2;
    if (check_valid(c) != 0)
        return ERR_INVALID_VALUE;
    out = c;
    return 0;
}

// ---- Win32 FILETIME (100 ns units since 1601-01-01).
constexpr std::uint32_t civil_from_filetime(std::uint64_t ft, Civil &out) {
    // Largest representable tick: year 9999-12-31 23:59:59.999999; stay well inside uint64 maths.
    const std::uint64_t ticks = ft / 10 + FILETIME_EPOCH_TICKS;
    if (ft / 10 > (UINT64_MAX - FILETIME_EPOCH_TICKS))
        return ERR_INVALID_VALUE;
    out = civil_from_ticks(ticks);
    if (out.year > 9999)
        return ERR_INVALID_VALUE;
    return 0;
}

// ---- Formatting. `ticks` is UTC; tz_minutes is the offset east of UTC.
constexpr const char *WEEKDAY[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };
constexpr const char *MONTH[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

constexpr bool tz_in_range(int tz_minutes) { return tz_minutes > -24 * 60 && tz_minutes < 24 * 60; }

// 0001-01-01 was a Monday (proleptic Gregorian): index into WEEKDAY.
constexpr int weekday_index(std::uint64_t ticks) { return static_cast<int>(ticks / TICKS_PER_DAY % 7); }

// "Wed, 02 Oct 2002 13:00:00 +0200" (31 chars + NUL). `buf` must hold 32 bytes. Returns 0 or error.
inline std::uint32_t format_rfc2822(char *buf, std::uint64_t utc_ticks, int tz_minutes) {
    if (!tz_in_range(tz_minutes))
        return ERR_INVALID_VALUE;
    const std::int64_t local = static_cast<std::int64_t>(utc_ticks) + static_cast<std::int64_t>(tz_minutes) * 60 * static_cast<std::int64_t>(TICKS_PER_SEC);
    if (local < 0)
        return ERR_INVALID_VALUE;
    const auto lt = static_cast<std::uint64_t>(local);
    const Civil c = civil_from_ticks(lt);
    if (c.year > 9999)
        return ERR_INVALID_VALUE;
    const int a = tz_minutes < 0 ? -tz_minutes : tz_minutes;
    std::snprintf(buf, 32, "%s, %02d %s %04d %02d:%02d:%02d %c%02d%02d", WEEKDAY[weekday_index(lt)], c.day, MONTH[c.month - 1],
        c.year, c.hour, c.minute, c.second, tz_minutes < 0 ? '-' : '+', a / 60, a % 60);
    return 0;
}

// "2002-10-02T13:00:00.00+02:00" or "...Z" for a zero offset. `buf` must hold 32 bytes.
// Sub-second field: hundredths of a second (provisional, PSP behaviour).
inline std::uint32_t format_rfc3339(char *buf, std::uint64_t utc_ticks, int tz_minutes) {
    if (!tz_in_range(tz_minutes))
        return ERR_INVALID_VALUE;
    const std::int64_t local = static_cast<std::int64_t>(utc_ticks) + static_cast<std::int64_t>(tz_minutes) * 60 * static_cast<std::int64_t>(TICKS_PER_SEC);
    if (local < 0)
        return ERR_INVALID_VALUE;
    const Civil c = civil_from_ticks(static_cast<std::uint64_t>(local));
    if (c.year > 9999)
        return ERR_INVALID_VALUE;
    int n = std::snprintf(buf, 32, "%04d-%02d-%02dT%02d:%02d:%02d.%02u", c.year, c.month, c.day, c.hour, c.minute, c.second, c.microsecond / 10000);
    if (tz_minutes == 0) {
        std::snprintf(buf + n, 32 - n, "Z");
    } else {
        const int a = tz_minutes < 0 ? -tz_minutes : tz_minutes;
        std::snprintf(buf + n, 32 - n, "%c%02d:%02d", tz_minutes < 0 ? '-' : '+', a / 60, a % 60);
    }
    return 0;
}

// ---- Parsing.
struct Cursor {
    const char *p;
    bool digits(int n, int &out) {
        int v = 0;
        for (int i = 0; i < n; i++) {
            if (p[i] < '0' || p[i] > '9')
                return false;
            v = v * 10 + (p[i] - '0');
        }
        p += n;
        out = v;
        return true;
    }
    bool eat(char c) {
        if (*p != c)
            return false;
        p++;
        return true;
    }
    void skip_ws() {
        while (*p == ' ' || *p == '\t')
            p++;
    }
};

// Parses "[+-]hh:mm", "[+-]hhmm", "Z"/"z"/"UT"/"GMT". Returns false if absent or malformed.
inline bool parse_zone(Cursor &cur, int &tz_minutes) {
    if (*cur.p == 'Z' || *cur.p == 'z') {
        cur.p++;
        tz_minutes = 0;
        return true;
    }
    if (std::strncmp(cur.p, "GMT", 3) == 0 || std::strncmp(cur.p, "UTC", 3) == 0) {
        cur.p += 3;
        tz_minutes = 0;
        return true;
    }
    if (std::strncmp(cur.p, "UT", 2) == 0) {
        cur.p += 2;
        tz_minutes = 0;
        return true;
    }
    if (*cur.p != '+' && *cur.p != '-')
        return false;
    const bool neg = *cur.p == '-';
    cur.p++;
    int h = 0, m = 0;
    if (!cur.digits(2, h))
        return false;
    cur.eat(':');
    if (!cur.digits(2, m))
        return false;
    if (h > 23 || m > 59)
        return false;
    tz_minutes = (neg ? -1 : 1) * (h * 60 + m);
    return true;
}

// Applies "local fields at offset tz" -> UTC ticks. Returns error or 0.
inline std::uint32_t civil_to_utc_ticks(const Civil &c, int tz_minutes, std::uint64_t &out) {
    if (c.year < 1 || c.year > 9999)
        return ERR_INVALID_YEAR;
    if (const std::uint32_t e = check_valid(c))
        return e;
    const std::int64_t t = static_cast<std::int64_t>(ticks_from_civil(c)) - static_cast<std::int64_t>(tz_minutes) * 60 * static_cast<std::int64_t>(TICKS_PER_SEC);
    if (t < 0)
        return ERR_INVALID_VALUE;
    out = static_cast<std::uint64_t>(t);
    return 0;
}

// "YYYY-MM-DD[T| ]hh:mm[:ss[.f+]][zone]". `require_zone` for RFC 3339 (zone is mandatory there).
inline std::uint32_t parse_iso(const char *s, bool require_zone, std::uint64_t &utc_ticks) {
    if (!s)
        return ERR_BAD_PARSE;
    Cursor cur{ s };
    cur.skip_ws();
    Civil c;
    if (!cur.digits(4, c.year) || !cur.eat('-') || !cur.digits(2, c.month) || !cur.eat('-') || !cur.digits(2, c.day))
        return ERR_BAD_PARSE;
    if (!(cur.eat('T') || cur.eat('t') || cur.eat(' ')))
        return ERR_BAD_PARSE;
    if (!cur.digits(2, c.hour) || !cur.eat(':') || !cur.digits(2, c.minute))
        return ERR_BAD_PARSE;
    if (cur.eat(':')) {
        if (!cur.digits(2, c.second))
            return ERR_BAD_PARSE;
        if (cur.eat('.') || cur.eat(',')) {
            if (*cur.p < '0' || *cur.p > '9')
                return ERR_BAD_PARSE;
            std::uint32_t scale = 100000;
            while (*cur.p >= '0' && *cur.p <= '9') {
                c.microsecond += static_cast<std::uint32_t>(*cur.p - '0') * scale;
                scale /= 10; // digits beyond microseconds are truncated (scale reaches 0)
                cur.p++;
            }
        }
    }
    int tz = 0;
    cur.skip_ws();
    if (*cur.p) {
        if (!parse_zone(cur, tz))
            return ERR_BAD_PARSE;
    } else if (require_zone) {
        return ERR_BAD_PARSE;
    }
    cur.skip_ws();
    if (*cur.p)
        return ERR_BAD_PARSE;
    return civil_to_utc_ticks(c, tz, utc_ticks);
}

// "[Www,] D[D] Mon YYYY hh:mm[:ss] [zone]"
inline std::uint32_t parse_rfc2822(const char *s, std::uint64_t &utc_ticks) {
    if (!s)
        return ERR_BAD_PARSE;
    Cursor cur{ s };
    cur.skip_ws();
    if (((*cur.p >= 'A' && *cur.p <= 'Z') || (*cur.p >= 'a' && *cur.p <= 'z')) && cur.p[1] && cur.p[2] && cur.p[3] == ',') {
        cur.p += 4;
        cur.skip_ws();
    }
    Civil c;
    if (*cur.p < '0' || *cur.p > '9')
        return ERR_BAD_PARSE;
    c.day = 0;
    int nd = 0;
    while (*cur.p >= '0' && *cur.p <= '9' && nd < 2) {
        c.day = c.day * 10 + (*cur.p++ - '0');
        nd++;
    }
    cur.skip_ws();
    int mon = 0;
    for (int i = 0; i < 12 && !mon; i++) {
        bool match = true;
        for (int k = 0; k < 3; k++) {
            char ch = cur.p[k];
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch - 'A' + 'a');
            char want = static_cast<char>(MONTH[i][k] - 'A' + 'a');
            if (k == 0 ? ch != want : ch != MONTH[i][k])
                match = false;
        }
        if (match)
            mon = i + 1;
    }
    if (!mon)
        return ERR_BAD_PARSE;
    c.month = mon;
    cur.p += 3;
    cur.skip_ws();
    if (!cur.digits(4, c.year))
        return ERR_BAD_PARSE;
    cur.skip_ws();
    if (!cur.digits(2, c.hour) || !cur.eat(':') || !cur.digits(2, c.minute))
        return ERR_BAD_PARSE;
    if (cur.eat(':') && !cur.digits(2, c.second))
        return ERR_BAD_PARSE;
    int tz = 0;
    cur.skip_ws();
    if (*cur.p && !parse_zone(cur, tz))
        return ERR_BAD_PARSE;
    cur.skip_ws();
    if (*cur.p)
        return ERR_BAD_PARSE;
    return civil_to_utc_ticks(c, tz, utc_ticks);
}

// sceRtcParseDateTime: accepts the ISO/RFC 3339 shape (zone optional, taken as UTC) or RFC 2822.
inline std::uint32_t parse_datetime(const char *s, std::uint64_t &utc_ticks) {
    if (!s)
        return ERR_BAD_PARSE;
    const char *p = s;
    while (*p == ' ' || *p == '\t')
        p++;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] >= '0' && p[2] <= '9' && p[3] >= '0' && p[3] <= '9' && p[4] == '-')
        return parse_iso(s, false, utc_ticks);
    return parse_rfc2822(s, utc_ticks);
}

} // namespace rtc_text
