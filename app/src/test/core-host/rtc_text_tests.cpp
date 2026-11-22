// Host tests for the pure RTC helpers (rtc/rtc_text.h): civil/tick conversion, DOS and Win32 time,
// RFC 2822 / RFC 3339 formatting and parsing.
#include <rtc/rtc_text.h>

#include <gtest/gtest.h>

#include <cstring>
#include <string>

using namespace rtc_text;

namespace {
Civil make(int y, int mo, int d, int h = 0, int mi = 0, int s = 0, std::uint32_t us = 0) {
    Civil c;
    c.year = y;
    c.month = mo;
    c.day = d;
    c.hour = h;
    c.minute = mi;
    c.second = s;
    c.microsecond = us;
    return c;
}
std::string fmt2822(std::uint64_t t, int tz) {
    char b[32] = {};
    EXPECT_EQ(format_rfc2822(b, t, tz), 0u);
    return b;
}
std::string fmt3339(std::uint64_t t, int tz) {
    char b[32] = {};
    EXPECT_EQ(format_rfc3339(b, t, tz), 0u);
    return b;
}
} // namespace

TEST(RtcText, LeapYears) {
    EXPECT_TRUE(is_leap(2000));
    EXPECT_FALSE(is_leap(1900));
    EXPECT_TRUE(is_leap(2024));
    EXPECT_FALSE(is_leap(2023));
    EXPECT_FALSE(is_leap(2100));
    EXPECT_EQ(days_in_month(2024, 2), 29);
    EXPECT_EQ(days_in_month(2023, 2), 28);
    EXPECT_EQ(days_in_month(2023, 12), 31);
    EXPECT_EQ(days_in_month(2023, 11), 30);
}

TEST(RtcText, DayOfYear) {
    EXPECT_EQ(day_of_year(2023, 1, 1), 1);
    EXPECT_EQ(day_of_year(2023, 12, 31), 365);
    EXPECT_EQ(day_of_year(2024, 12, 31), 366);
    EXPECT_EQ(day_of_year(2024, 3, 1), 61);
    EXPECT_EQ(day_of_year(2023, 3, 1), 60);
    EXPECT_EQ(day_of_year(1900, 3, 1), 60);
    EXPECT_EQ(day_of_year(2000, 3, 1), 61);
}

TEST(RtcText, TickRoundTripAndEpoch) {
    // 1970-01-01 is 62135596800 s after 0001-01-01.
    const std::uint64_t unix_epoch = 62135596800ULL * TICKS_PER_SEC;
    const Civil e = civil_from_ticks(unix_epoch);
    EXPECT_EQ(e.year, 1970);
    EXPECT_EQ(e.month, 1);
    EXPECT_EQ(e.day, 1);
    EXPECT_EQ(ticks_from_civil(make(1970, 1, 1)), unix_epoch);
    const Civil first = civil_from_ticks(0);
    EXPECT_EQ(first.year, 1);
    EXPECT_EQ(first.month, 1);
    EXPECT_EQ(first.day, 1);

    const int years[] = { 1, 4, 100, 400, 1582, 1900, 1970, 1999, 2000, 2004, 2023, 2024, 2038, 2100, 2400, 9999 };
    for (int y : years) {
        for (int m = 1; m <= 12; m++) {
            const int dim = days_in_month(y, m);
            for (int d : { 1, 15, dim - 1, dim }) {
                const Civil c = make(y, m, d, 23, 59, 59, 999999);
                const Civil r = civil_from_ticks(ticks_from_civil(c));
                ASSERT_EQ(r.year, y);
                ASSERT_EQ(r.month, m);
                ASSERT_EQ(r.day, d);
                ASSERT_EQ(r.hour, 23);
                ASSERT_EQ(r.minute, 59);
                ASSERT_EQ(r.second, 59);
                ASSERT_EQ(r.microsecond, 999999u);
            }
        }
    }
}

TEST(RtcText, LeapDayBoundaries) {
    const std::uint64_t feb28 = ticks_from_civil(make(2024, 2, 28));
    EXPECT_EQ(civil_from_ticks(feb28 + TICKS_PER_DAY).day, 29);
    EXPECT_EQ(civil_from_ticks(feb28 + 2 * TICKS_PER_DAY).month, 3);
    const std::uint64_t f28 = ticks_from_civil(make(1900, 2, 28));
    EXPECT_EQ(civil_from_ticks(f28 + TICKS_PER_DAY).month, 3);
    EXPECT_EQ(civil_from_ticks(ticks_from_civil(make(2000, 2, 29))).day, 29);
}

TEST(RtcText, CheckValid) {
    EXPECT_EQ(check_valid(make(2024, 2, 29)), 0u);
    EXPECT_EQ(check_valid(make(2023, 2, 29)), ERR_INVALID_DAY);
    EXPECT_EQ(check_valid(make(2023, 13, 1)), ERR_INVALID_MONTH);
    EXPECT_EQ(check_valid(make(2023, 0, 1)), ERR_INVALID_MONTH);
    EXPECT_EQ(check_valid(make(2023, 4, 31)), ERR_INVALID_DAY);
    EXPECT_EQ(check_valid(make(2023, 4, 30, 24)), ERR_INVALID_HOUR);
    EXPECT_EQ(check_valid(make(2023, 4, 30, 0, 60)), ERR_INVALID_MINUTE);
    EXPECT_EQ(check_valid(make(2023, 4, 30, 0, 0, 60)), ERR_INVALID_SECOND);
    EXPECT_EQ(check_valid(make(2023, 4, 30, 0, 0, 0, 1000000)), ERR_INVALID_MICROSECOND);
}

TEST(RtcText, Weekday) {
    // 0001-01-01 Monday, 1970-01-01 Thursday, 2000-01-01 Saturday, 2024-02-29 Thursday.
    EXPECT_STREQ(WEEKDAY[weekday_index(0)], "Mon");
    EXPECT_STREQ(WEEKDAY[weekday_index(ticks_from_civil(make(1970, 1, 1)))], "Thu");
    EXPECT_STREQ(WEEKDAY[weekday_index(ticks_from_civil(make(2000, 1, 1)))], "Sat");
    EXPECT_STREQ(WEEKDAY[weekday_index(ticks_from_civil(make(2024, 2, 29)))], "Thu");
}

TEST(RtcText, Dos) {
    Civil c;
    // 2024-02-29 13:45:30 -> date=(44<<9)|(2<<5)|29, time=(13<<11)|(45<<5)|15
    const std::uint32_t date = (44u << 9) | (2u << 5) | 29u;
    const std::uint32_t time = (13u << 11) | (45u << 5) | 15u;
    ASSERT_EQ(civil_from_dos((date << 16) | time, c), 0u);
    EXPECT_EQ(c.year, 2024);
    EXPECT_EQ(c.month, 2);
    EXPECT_EQ(c.day, 29);
    EXPECT_EQ(c.hour, 13);
    EXPECT_EQ(c.minute, 45);
    EXPECT_EQ(c.second, 30);
    EXPECT_EQ(civil_from_dos(0, c), ERR_INVALID_VALUE); // month 0 / day 0
    // 2023-02-29 is rejected
    EXPECT_EQ(civil_from_dos((((43u << 9) | (2u << 5) | 29u) << 16), c), ERR_INVALID_VALUE);
    // minimum / maximum representable
    ASSERT_EQ(civil_from_dos((((0u << 9) | (1u << 5) | 1u) << 16), c), 0u);
    EXPECT_EQ(c.year, 1980);
    ASSERT_EQ(civil_from_dos(0xFF9FBF7D, c), 0u);
    EXPECT_EQ(c.year, 2107);
    EXPECT_EQ(c.month, 12);
    EXPECT_EQ(c.day, 31);
    EXPECT_EQ(c.second, 58);
    // seconds field 31 means 62 s
    EXPECT_EQ(civil_from_dos((date << 16) | (13u << 11) | 31u, c), ERR_INVALID_VALUE);
}

TEST(RtcText, Filetime) {
    Civil c;
    ASSERT_EQ(civil_from_filetime(0, c), 0u);
    EXPECT_EQ(c.year, 1601);
    EXPECT_EQ(c.month, 1);
    EXPECT_EQ(c.day, 1);
    // 2000-01-01 00:00:00 = 125911584000000000 (100 ns)
    ASSERT_EQ(civil_from_filetime(125911584000000000ULL, c), 0u);
    EXPECT_EQ(c.year, 2000);
    EXPECT_EQ(c.month, 1);
    EXPECT_EQ(c.day, 1);
    ASSERT_EQ(civil_from_filetime(125911584000000000ULL + 12345678ULL, c), 0u);
    EXPECT_EQ(c.second, 1);
    EXPECT_EQ(c.microsecond, 234567u);
    EXPECT_EQ(civil_from_filetime(UINT64_MAX, c), ERR_INVALID_VALUE);
    // The constant matches civil arithmetic.
    EXPECT_EQ(ticks_from_civil(make(1601, 1, 1)), FILETIME_EPOCH_TICKS);
}

TEST(RtcText, FormatRfc2822) {
    const std::uint64_t t = ticks_from_civil(make(2002, 10, 2, 13, 0, 0));
    EXPECT_EQ(fmt2822(t, 0), "Wed, 02 Oct 2002 13:00:00 +0000");
    EXPECT_EQ(fmt2822(t, 120), "Wed, 02 Oct 2002 15:00:00 +0200");
    EXPECT_EQ(fmt2822(t, -330), "Wed, 02 Oct 2002 07:30:00 -0530");
    // offset crossing a day / year / leap-day boundary
    EXPECT_EQ(fmt2822(ticks_from_civil(make(2023, 12, 31, 23, 30)), 60), "Mon, 01 Jan 2024 00:30:00 +0100");
    EXPECT_EQ(fmt2822(ticks_from_civil(make(2024, 2, 28, 23, 0)), 120), "Thu, 29 Feb 2024 01:00:00 +0200");
    EXPECT_EQ(fmt2822(ticks_from_civil(make(2024, 3, 1, 0, 30)), -60), "Thu, 29 Feb 2024 23:30:00 -0100");
    char b[32];
    EXPECT_EQ(format_rfc2822(b, t, 24 * 60), ERR_INVALID_VALUE);
    EXPECT_EQ(format_rfc2822(b, 0, -60), ERR_INVALID_VALUE); // before year 1
}

TEST(RtcText, FormatRfc3339) {
    const std::uint64_t t = ticks_from_civil(make(2002, 10, 2, 13, 0, 0, 250000));
    EXPECT_EQ(fmt3339(t, 0), "2002-10-02T13:00:00.25Z");
    EXPECT_EQ(fmt3339(t, 120), "2002-10-02T15:00:00.25+02:00");
    EXPECT_EQ(fmt3339(t, -330), "2002-10-02T07:30:00.25-05:30");
    EXPECT_EQ(fmt3339(ticks_from_civil(make(2024, 12, 31, 23, 59, 59, 990000)), 1), "2025-01-01T00:00:59.99+00:01");
    char b[32];
    EXPECT_EQ(format_rfc3339(b, t, -24 * 60), ERR_INVALID_VALUE);
}

TEST(RtcText, ParseRfc3339) {
    std::uint64_t t = 0;
    ASSERT_EQ(parse_iso("2002-10-02T13:00:00Z", true, t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13)));
    ASSERT_EQ(parse_iso("2002-10-02T15:00:00.25+02:00", true, t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13, 0, 0, 250000)));
    ASSERT_EQ(parse_iso("2002-10-02t07:30:00-0530", true, t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13)));
    ASSERT_EQ(parse_iso("2024-02-29T00:00:00.1234567Z", true, t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2024, 2, 29, 0, 0, 0, 123456)));
    // zone carries into previous day / year
    ASSERT_EQ(parse_iso("2024-01-01T00:30:00+01:00", true, t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2023, 12, 31, 23, 30)));
    // errors
    EXPECT_EQ(parse_iso("2002-10-02T13:00:00", true, t), ERR_BAD_PARSE); // zone required
    EXPECT_EQ(parse_iso("2002-10-02T13:00:00Zjunk", true, t), ERR_BAD_PARSE);
    EXPECT_EQ(parse_iso("2002-10-0213:00:00Z", true, t), ERR_BAD_PARSE);
    EXPECT_EQ(parse_iso("2023-02-29T00:00:00Z", true, t), ERR_INVALID_DAY);
    EXPECT_EQ(parse_iso("2023-13-01T00:00:00Z", true, t), ERR_INVALID_MONTH);
    EXPECT_EQ(parse_iso("2023-01-01T24:00:00Z", true, t), ERR_INVALID_HOUR);
    EXPECT_EQ(parse_iso("2023-01-01T00:61:00Z", true, t), ERR_INVALID_MINUTE);
    EXPECT_EQ(parse_iso("2023-01-01T00:00:61Z", true, t), ERR_INVALID_SECOND);
    EXPECT_EQ(parse_iso("0000-01-01T00:00:00Z", true, t), ERR_INVALID_YEAR);
    EXPECT_EQ(parse_iso("", true, t), ERR_BAD_PARSE);
    EXPECT_EQ(parse_iso(nullptr, true, t), ERR_BAD_PARSE);
    EXPECT_EQ(parse_iso("0001-01-01T00:00:00+01:00", true, t), ERR_INVALID_VALUE); // before tick 0
}

TEST(RtcText, ParseDateTime) {
    std::uint64_t t = 0;
    ASSERT_EQ(parse_datetime("2002-10-02 13:00:00", t), 0u); // no zone: UTC
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13)));
    ASSERT_EQ(parse_datetime("2002-10-02T13:00Z", t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13)));
    ASSERT_EQ(parse_datetime("Wed, 02 Oct 2002 15:00:00 +0200", t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13)));
    ASSERT_EQ(parse_datetime("2 Oct 2002 13:00 GMT", t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2002, 10, 2, 13)));
    ASSERT_EQ(parse_datetime("Thu, 29 Feb 2024 23:30:00 -0100", t), 0u);
    EXPECT_EQ(t, ticks_from_civil(make(2024, 3, 1, 0, 30)));
    EXPECT_EQ(parse_datetime("Thu, 30 Feb 2024 23:30:00 +0000", t), ERR_INVALID_DAY);
    EXPECT_EQ(parse_datetime("Wed, 02 Foo 2002 15:00:00", t), ERR_BAD_PARSE);
    EXPECT_EQ(parse_datetime("garbage", t), ERR_BAD_PARSE);
}

TEST(RtcText, FormatParseRoundTrip) {
    const int tzs[] = { 0, 60, -60, 330, -570, 840, -720 };
    for (int y : { 1970, 2000, 2024, 2100 }) {
        for (int tz : tzs) {
            const std::uint64_t t = ticks_from_civil(make(y, 2, 28, 22, 15, 7, 130000));
            std::uint64_t r = 0;
            ASSERT_EQ(parse_iso(fmt3339(t, tz).c_str(), true, r), 0u);
            EXPECT_EQ(r, t - (t % 10000)); // hundredths only
            ASSERT_EQ(parse_datetime(fmt2822(t, tz).c_str(), r), 0u);
            EXPECT_EQ(r, t - (t % TICKS_PER_SEC)); // whole seconds only
        }
    }
}
