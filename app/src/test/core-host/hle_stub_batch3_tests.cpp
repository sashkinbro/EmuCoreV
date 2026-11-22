// Host tests for the pure helpers behind the third HLE stub batch: kernel timer / stack / vector
// arithmetic, audio-out config queries, touch regions, AppUtil slot ordering and time zone values,
// and the ScePower battery derivation.
#include <audio/out_config.h>
#include <kernel/kernel_arith.h>
#include <touch/region.h>

#include <app_util_helpers.h>
#include <power_battery.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

// ---------------------------------------------------------------- kernel_arith

TEST(KernelArith, TimerElapsedAndSet) {
    std::uint64_t time = 1000, bias = 0;
    EXPECT_EQ(kernel_arith::timer_elapsed(1500, time, bias), 500u);
    // set the counter to 40 at now=2000: it then reads 40 + dt
    kernel_arith::timer_set_elapsed(2000, 40, time, bias);
    EXPECT_EQ(kernel_arith::timer_elapsed(2000, time, bias), 40u);
    EXPECT_EQ(kernel_arith::timer_elapsed(2250, time, bias), 290u);
    // a restore bias is cleared by SetTimerTime
    time = 10;
    bias = 5;
    EXPECT_EQ(kernel_arith::timer_elapsed(100, time, bias), 85u);
    kernel_arith::timer_set_elapsed(100, 0, time, bias);
    EXPECT_EQ(bias, 0u);
    EXPECT_EQ(kernel_arith::timer_elapsed(100, time, bias), 0u);
    EXPECT_EQ(kernel_arith::timer_elapsed(101, time, bias), 1u);
}

TEST(KernelArith, TimerSetElapsedLargerThanClockWraps) {
    std::uint64_t time = 0, bias = 0;
    // elapsed larger than "now": the base wraps but the counter still reads correctly
    kernel_arith::timer_set_elapsed(10, 1000, time, bias);
    EXPECT_EQ(kernel_arith::timer_elapsed(10, time, bias), 1000u);
    EXPECT_EQ(kernel_arith::timer_elapsed(30, time, bias), 1020u);
}

TEST(KernelArith, TimerEventRemaining) {
    EXPECT_EQ(kernel_arith::timer_event_remaining(100, UINT64_MAX), 0u);
    EXPECT_FALSE(kernel_arith::timer_event_armed(UINT64_MAX));
    EXPECT_TRUE(kernel_arith::timer_event_armed(5));
    EXPECT_EQ(kernel_arith::timer_event_remaining(100, 250), 150u);
    EXPECT_EQ(kernel_arith::timer_event_remaining(250, 250), 0u);
    EXPECT_EQ(kernel_arith::timer_event_remaining(300, 250), 0u);
}

TEST(KernelArith, StackFreeSize) {
    constexpr std::uint32_t base = 0x81000000, size = 0x10000;
    EXPECT_EQ(kernel_arith::stack_free_size(base, size, base + size), size);
    EXPECT_EQ(kernel_arith::stack_free_size(base, size, base + size - 0x100), size - 0x100);
    EXPECT_EQ(kernel_arith::stack_free_size(base, size, base + 0x40), 0x40u);
    EXPECT_EQ(kernel_arith::stack_free_size(base, size, base), 0u);
    EXPECT_EQ(kernel_arith::stack_free_size(base, size, base - 4), 0u); // overflowed
    EXPECT_EQ(kernel_arith::stack_free_size(base, size, base + size + 16), size); // above the stack
    // stack ending at the top of the address space does not overflow 32 bits
    EXPECT_EQ(kernel_arith::stack_free_size(0xFFFF0000u, 0x10000, 0xFFFFFFF0u), 0xFFF0u);
}

namespace {
struct Seg {
    std::uint32_t id;
    std::uint32_t size;
};
} // namespace

TEST(KernelArith, VectorRunsAllSegments) {
    const Seg segs[] = { { 0, 4 }, { 1, 0 }, { 2, 8 }, { 3, 2 } };
    std::vector<std::uint32_t> visited;
    std::uint32_t total = 99;
    const auto res = kernel_arith::run_vector(segs, 4, [&](const Seg &s) -> std::uint32_t {
        visited.push_back(s.id);
        return s.size;
    },
        total);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(total, 14u);
    EXPECT_EQ(visited, (std::vector<std::uint32_t>{ 0, 2, 3 })); // empty segment skipped
}

TEST(KernelArith, VectorStopsAtShortTransfer) {
    const Seg segs[] = { { 0, 4 }, { 1, 8 }, { 2, 2 } };
    int calls = 0;
    std::uint32_t total = 0;
    const auto res = kernel_arith::run_vector(segs, 3, [&](const Seg &s) -> std::uint32_t {
        calls++;
        return s.id == 1 ? 3u : s.size;
    },
        total);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(total, 7u);
    EXPECT_EQ(calls, 2);
}

TEST(KernelArith, VectorErrorHandling) {
    const Seg segs[] = { { 0, 4 }, { 1, 8 } };
    std::uint32_t total = 0;
    // error on the first segment is reported
    auto res = kernel_arith::run_vector(segs, 2, [&](const Seg &) -> std::uint32_t { return 0x80028221u; }, total);
    EXPECT_EQ(static_cast<std::uint32_t>(res), 0x80028221u);
    EXPECT_EQ(total, 0u);
    // error after progress: the partial transfer is reported as success
    res = kernel_arith::run_vector(segs, 2, [&](const Seg &s) -> std::uint32_t { return s.id == 0 ? 4u : 0x80028221u; }, total);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(total, 4u);
    // non-blocking transfer that moves nothing
    res = kernel_arith::run_vector(segs, 2, [&](const Seg &) -> std::uint32_t { return 0; }, total);
    EXPECT_EQ(res, 0);
    EXPECT_EQ(total, 0u);
}

// ---------------------------------------------------------------- audio_out_config

TEST(AudioOutConfig, GetConfig) {
    using namespace audio_out_config;
    EXPECT_EQ(get_config(CONFIG_LEN, 256, 48000, 1), 256);
    EXPECT_EQ(get_config(CONFIG_FREQ, 256, 48000, 1), 48000);
    EXPECT_EQ(get_config(CONFIG_MODE, 256, 48000, 1), 1);
    EXPECT_EQ(get_config(CONFIG_MODE, 256, 48000, 0), 0);
    EXPECT_EQ(get_config(3, 256, 48000, 1), ERR_INVALID_CONF_TYPE);
    EXPECT_EQ(get_config(-1, 256, 48000, 1), ERR_INVALID_CONF_TYPE);
}

TEST(AudioOutConfig, Validation) {
    using namespace audio_out_config;
    EXPECT_EQ(check_alc_mode(0), 0);
    EXPECT_EQ(check_alc_mode(1), 0);
    EXPECT_NE(check_alc_mode(2), 0); // SCE_AUDIO_ALC_MODE_MAX is not a mode
    EXPECT_NE(check_alc_mode(-1), 0);
    for (int e = 0; e <= 4; e++)
        EXPECT_EQ(check_effect_type(e), 0);
    EXPECT_EQ(check_effect_type(5), ERR_INVALID_FX_TYPE);
    EXPECT_EQ(check_effect_type(-1), ERR_INVALID_FX_TYPE);
    EXPECT_EQ(check_compress_port_type(0), ERR_INVALID_PORT_TYPE); // MAIN
    EXPECT_EQ(check_compress_port_type(1), 0); // BGM
    EXPECT_EQ(check_compress_port_type(2), 0); // VOICE
}

// ---------------------------------------------------------------- touch_region

namespace {
struct Rep {
    std::int16_t x, y;
    int id;
};
} // namespace

TEST(TouchRegion, HitTest) {
    const touch_region::Rect r{ 100, 200, 300, 400 };
    EXPECT_TRUE(touch_region::contains(r, 100, 200)); // inclusive corners
    EXPECT_TRUE(touch_region::contains(r, 300, 400));
    EXPECT_TRUE(touch_region::contains(r, 200, 300));
    EXPECT_FALSE(touch_region::contains(r, 99, 300));
    EXPECT_FALSE(touch_region::contains(r, 301, 300));
    EXPECT_FALSE(touch_region::contains(r, 200, 199));
    EXPECT_FALSE(touch_region::contains(r, 200, 401));
    EXPECT_TRUE(touch_region::rect_valid(r));
    EXPECT_FALSE(touch_region::rect_valid({ 5, 0, 4, 10 }));
    EXPECT_TRUE(touch_region::region_id_valid(0));
    EXPECT_TRUE(touch_region::region_id_valid(0xFF));
    EXPECT_FALSE(touch_region::region_id_valid(0x100));
    EXPECT_FALSE(touch_region::region_id_valid(-1));
}

TEST(TouchRegion, FilterKeepsOrderAndCompacts) {
    touch_region::Region reg;
    reg.defined = true;
    reg.rect = { 0, 0, 959, 543 };
    Rep reports[] = { { 10, 10, 1 }, { 1000, 10, 2 }, { 500, 500, 3 }, { 500, 600, 4 }, { 959, 543, 5 } };
    const auto kept = touch_region::filter_reports(reports, 5, reg);
    ASSERT_EQ(kept, 3u);
    EXPECT_EQ(reports[0].id, 1);
    EXPECT_EQ(reports[1].id, 3);
    EXPECT_EQ(reports[2].id, 5);
}

TEST(TouchRegion, UndefinedOrInactiveRegionKeepsEverything) {
    Rep reports[] = { { 0, 0, 1 }, { 4000, 4000, 2 } };
    touch_region::Region undefined;
    EXPECT_EQ(touch_region::filter_reports(reports, 2, undefined), 2u);
    touch_region::Region inactive;
    inactive.defined = true;
    inactive.active = false;
    inactive.rect = { 0, 0, 1, 1 };
    EXPECT_EQ(touch_region::filter_reports(reports, 2, inactive), 2u);
    touch_region::Region none;
    none.defined = true;
    none.rect = { 100, 100, 200, 200 };
    EXPECT_EQ(touch_region::filter_reports(reports, 2, none), 0u);
    EXPECT_EQ(touch_region::filter_reports(reports, 0, none), 0u);
}

TEST(TouchRegion, TableDefaults) {
    touch_region::Table table = {};
    EXPECT_EQ(table.size(), 256u);
    EXPECT_FALSE(table[0].defined);
    EXPECT_FALSE(touch_region::restricts(table[255]));
}

// ---------------------------------------------------------------- app_util

namespace {
app_util::SlotEntry slot(std::uint32_t id, int user, std::uint32_t size, std::uint64_t mod) {
    app_util::SlotEntry e;
    e.id = id;
    e.user_param = user;
    e.size_kib = size;
    e.modified_key = mod;
    return e;
}
std::vector<std::uint32_t> ids(const std::vector<app_util::SlotEntry> &v) {
    std::vector<std::uint32_t> r;
    for (const auto &e : v)
        r.push_back(e.id);
    return r;
}
} // namespace

TEST(AppUtilSlots, SortById) {
    std::vector<app_util::SlotEntry> v = { slot(5, 0, 0, 0), slot(1, 0, 0, 0), slot(3, 0, 0, 0) };
    app_util::sort_slots(v, app_util::SORT_KEY_SLOT_ID, app_util::SORT_ASCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 1, 3, 5 }));
    app_util::sort_slots(v, app_util::SORT_KEY_SLOT_ID, app_util::SORT_DESCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 5, 3, 1 }));
}

TEST(AppUtilSlots, SortByUserParamWithTies) {
    std::vector<app_util::SlotEntry> v = { slot(0, 7, 0, 0), slot(1, -2, 0, 0), slot(2, 7, 0, 0), slot(3, 0, 0, 0) };
    app_util::sort_slots(v, app_util::SORT_KEY_USER_PARAM, app_util::SORT_ASCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 1, 3, 0, 2 }));
    app_util::sort_slots(v, app_util::SORT_KEY_USER_PARAM, app_util::SORT_DESCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 0, 2, 3, 1 })); // ties stay in ascending id order
}

TEST(AppUtilSlots, SortBySizeAndModifiedTime) {
    const auto t1 = app_util::modified_time_key(2023, 12, 31, 23, 59, 59, 0);
    const auto t2 = app_util::modified_time_key(2024, 1, 1, 0, 0, 0, 0);
    const auto t3 = app_util::modified_time_key(2024, 1, 1, 0, 0, 0, 1);
    EXPECT_LT(t1, t2);
    EXPECT_LT(t2, t3);
    EXPECT_LT(app_util::modified_time_key(2024, 2, 29, 0, 0, 0, 0), app_util::modified_time_key(2024, 3, 1, 0, 0, 0, 0));
    std::vector<app_util::SlotEntry> v = { slot(0, 0, 300, t3), slot(1, 0, 100, t1), slot(2, 0, 200, t2) };
    app_util::sort_slots(v, app_util::SORT_KEY_SIZE_KIB, app_util::SORT_ASCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 1, 2, 0 }));
    app_util::sort_slots(v, app_util::SORT_KEY_MODIFIED_TIME, app_util::SORT_DESCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 0, 2, 1 }));
    app_util::sort_slots(v, app_util::SORT_KEY_MODIFIED_TIME, app_util::SORT_ASCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 1, 2, 0 }));
}

TEST(AppUtilSlots, UnknownKeyFallsBackToId) {
    std::vector<app_util::SlotEntry> v = { slot(9, 1, 1, 1), slot(2, 2, 2, 2) };
    app_util::sort_slots(v, 77, app_util::SORT_ASCENT);
    EXPECT_EQ(ids(v), (std::vector<std::uint32_t>{ 2, 9 }));
    std::vector<app_util::SlotEntry> empty;
    app_util::sort_slots(empty, 0, 0);
    EXPECT_TRUE(empty.empty());
}

TEST(AppUtilTimeZone, OffsetFromBrokenDownTime) {
    // 1970-01-01 01:00:00 local at UTC instant 0 -> +1 h
    EXPECT_EQ(app_util::utc_offset_seconds(0, 1970, 1, 1, 1, 0, 0), 3600);
    // 2024-02-29 23:30 local at UTC 2024-02-29 18:00 (1709229600) -> +5:30
    EXPECT_EQ(app_util::utc_offset_seconds(1709229600, 2024, 2, 29, 23, 30, 0), 19800);
    // local date earlier than UTC date: UTC-8 around the new year
    EXPECT_EQ(app_util::utc_offset_seconds(1704067200, 2023, 12, 31, 16, 0, 0), -28800); // 2024-01-01T00:00Z
    // day rollover over a leap day: 2024-03-01 01:00 local at 2024-02-29 22:00Z -> +3h
    EXPECT_EQ(app_util::utc_offset_seconds(1709244000, 2024, 3, 1, 1, 0, 0), 10800);
}

TEST(AppUtilTimeZone, StandardOffsetAndSummertime) {
    auto p = app_util::time_zone_params(3600, false); // CET
    EXPECT_EQ(p.time_zone_minutes, 60);
    EXPECT_EQ(p.summertime, 0);
    p = app_util::time_zone_params(7200, true); // CEST
    EXPECT_EQ(p.time_zone_minutes, 60);
    EXPECT_EQ(p.summertime, 1);
    p = app_util::time_zone_params(-4 * 3600, true); // EDT
    EXPECT_EQ(p.time_zone_minutes, -300);
    EXPECT_EQ(p.summertime, 1);
    p = app_util::time_zone_params(-5 * 3600, false); // EST
    EXPECT_EQ(p.time_zone_minutes, -300);
    p = app_util::time_zone_params(0, false);
    EXPECT_EQ(p.time_zone_minutes, 0);
    p = app_util::time_zone_params(19800, false); // IST
    EXPECT_EQ(p.time_zone_minutes, 330);
    p = app_util::time_zone_params(0, true); // UTC with DST flag (e.g. Lisbon summer = +1, GMT+0 standard handled by caller)
    EXPECT_EQ(p.time_zone_minutes, -60);
}

TEST(AppUtilAddcont, DirNameValidation) {
    EXPECT_TRUE(app_util::addcont_dir_name_valid("DLC0001"));
    EXPECT_TRUE(app_util::addcont_dir_name_valid("0123456789ABCDEF")); // 16 chars + NUL fits 17
    EXPECT_FALSE(app_util::addcont_dir_name_valid("0123456789ABCDEFG")); // 17 chars: no terminator room
    EXPECT_FALSE(app_util::addcont_dir_name_valid(""));
    EXPECT_FALSE(app_util::addcont_dir_name_valid(nullptr));
    EXPECT_FALSE(app_util::addcont_dir_name_valid("."));
    EXPECT_FALSE(app_util::addcont_dir_name_valid(".."));
    EXPECT_FALSE(app_util::addcont_dir_name_valid("a/b"));
    EXPECT_FALSE(app_util::addcont_dir_name_valid("a\\b"));
    EXPECT_FALSE(app_util::addcont_dir_name_valid("ux0:evil"));
    EXPECT_FALSE(app_util::addcont_dir_name_valid("../x"));
}

// ---------------------------------------------------------------- power_battery

TEST(PowerBattery, DerivedValues) {
    using namespace power_battery;
    EXPECT_EQ(normalize_percent(-1), 100); // unknown -> full
    EXPECT_EQ(normalize_percent(150), 100);
    EXPECT_EQ(normalize_percent(37), 37);
    EXPECT_EQ(remain_capacity_mah(100), FULL_CAPACITY_MAH);
    EXPECT_EQ(remain_capacity_mah(-1), FULL_CAPACITY_MAH);
    EXPECT_EQ(remain_capacity_mah(0), 0);
    EXPECT_EQ(remain_capacity_mah(50), FULL_CAPACITY_MAH / 2);
    EXPECT_EQ(voltage_mv(0), VOLT_EMPTY_MV);
    EXPECT_EQ(voltage_mv(100), VOLT_FULL_MV);
    EXPECT_EQ(voltage_mv(-1), VOLT_FULL_MV);
    EXPECT_LT(voltage_mv(40), voltage_mv(60));
    EXPECT_EQ(charging_status(true), 1);
    EXPECT_EQ(charging_status(false), 0);
    EXPECT_EQ(life_time_minutes(-1), 0x7FFFFFFF);
    EXPECT_EQ(life_time_minutes(3600), 60);
    EXPECT_EQ(life_time_minutes(59), 0);
}

TEST(PowerBattery, CallbackRegistry) {
    power_battery::CallbackRegistry reg;
    using R = power_battery::CallbackRegistry;
    EXPECT_EQ(reg.add(10), R::Ok);
    EXPECT_EQ(reg.add(11), R::Ok);
    EXPECT_EQ(reg.add(10), R::AlreadyRegistered);
    EXPECT_EQ(reg.size(), 2u);
    EXPECT_EQ(reg.remove(12), R::NotRegistered);
    EXPECT_EQ(reg.remove(10), R::Ok);
    EXPECT_FALSE(reg.contains(10));
    EXPECT_EQ(reg.remove(10), R::NotRegistered);
    EXPECT_EQ(reg.add(10), R::Ok); // can register again after removal
    reg.prune([](std::int32_t id) { return id == 11; });
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_TRUE(reg.contains(11));
}
