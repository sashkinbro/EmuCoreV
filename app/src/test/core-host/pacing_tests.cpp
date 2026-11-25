#include <gtest/gtest.h>

#include <util/frame_pacing.h>

#include <algorithm>
#include <vector>

using namespace frame_pacing;

namespace {

// Feeds a perfectly regular vsync train and returns the refresh index (1-based) of every emitted tick.
std::vector<int> run_follower(VblankFollower &follower, int64_t panel_milli_hz, int refreshes, int64_t jitter_ns = 0) {
    const int64_t period = 1'000'000'000'000LL / panel_milli_hz;
    std::vector<int> tick_refreshes;
    int64_t t = 5'000'000'000LL;
    for (int i = 1; i <= refreshes; ++i) {
        t += period;
        const int64_t jitter = (i % 3 == 0) ? jitter_ns : (i % 3 == 1 ? -jitter_ns : 0);
        const uint32_t ticks = follower.on_vsync(t + jitter);
        for (uint32_t k = 0; k < ticks; ++k)
            tick_refreshes.push_back(i);
    }
    return tick_refreshes;
}

} // namespace

TEST(PresentModePolicy, VsyncOnIsAlwaysFifo) {
    EXPECT_EQ(choose_present_mode(true, { true, true, true }), PresentMode::Fifo);
    EXPECT_EQ(choose_present_mode(true, {}), PresentMode::Fifo);
}

TEST(PresentModePolicy, VsyncOffPrefersNonTearingMode) {
    EXPECT_EQ(choose_present_mode(false, { true, true, true }), PresentMode::Mailbox);
    EXPECT_EQ(choose_present_mode(false, { true, false, true }), PresentMode::Immediate);
    EXPECT_EQ(choose_present_mode(false, { true, false, false }), PresentMode::FifoRelaxed);
    EXPECT_EQ(choose_present_mode(false, {}), PresentMode::Fifo);
}

TEST(PanelRate, SnapsMeasurementNoiseOnly) {
    EXPECT_EQ(snap_milli_hz(119'999), 120000);
    EXPECT_EQ(snap_milli_hz(60'020), 60000);
    EXPECT_EQ(snap_milli_hz(143'950), 144000);
    EXPECT_EQ(snap_milli_hz(119'880), 120000); // within 0.3 %
    EXPECT_EQ(snap_milli_hz(99'000), 99000); // a real 99 Hz panel stays 99 Hz
    EXPECT_EQ(snap_milli_hz(100'400), 100400);
}

// Cadence of a 60 fps guest (flip every vblank) and a 30 fps guest (flip every 2nd vblank).
TEST(Cadence, SixtyFpsGuest) {
    EXPECT_TRUE(simulate_cadence(60000, 1).even());
    EXPECT_EQ(simulate_cadence(60000, 1).min_refreshes, 1u);

    const Cadence c120 = simulate_cadence(120000, 1);
    EXPECT_TRUE(c120.even());
    EXPECT_EQ(c120.min_refreshes, 2u);

    // 90 Hz cannot show 60 fps evenly: 2,1,2,1 is the best a fixed-rate panel can do.
    const Cadence c90 = simulate_cadence(90000, 1);
    EXPECT_FALSE(c90.even());
    EXPECT_EQ(c90.min_refreshes, 1u);
    EXPECT_EQ(c90.max_refreshes, 2u);
    EXPECT_NEAR(c90.mean_refreshes, 1.5, 0.01);

    // 144 Hz: 2 and 3 refreshes, spread as evenly as possible (mean 2.4).
    const Cadence c144 = simulate_cadence(144000, 1);
    EXPECT_FALSE(c144.even());
    EXPECT_EQ(c144.min_refreshes, 2u);
    EXPECT_EQ(c144.max_refreshes, 3u);
    EXPECT_NEAR(c144.mean_refreshes, 2.4, 0.01);
}

TEST(Cadence, ThirtyFpsGuest) {
    const Cadence c60 = simulate_cadence(60000, 2);
    EXPECT_TRUE(c60.even());
    EXPECT_EQ(c60.min_refreshes, 2u);

    const Cadence c90 = simulate_cadence(90000, 2);
    EXPECT_TRUE(c90.even());
    EXPECT_EQ(c90.min_refreshes, 3u);

    const Cadence c120 = simulate_cadence(120000, 2);
    EXPECT_TRUE(c120.even());
    EXPECT_EQ(c120.min_refreshes, 4u);

    // 144 Hz: 30 fps = 4.8 refreshes, so 4 and 5.
    const Cadence c144 = simulate_cadence(144000, 2);
    EXPECT_FALSE(c144.even());
    EXPECT_EQ(c144.min_refreshes, 4u);
    EXPECT_EQ(c144.max_refreshes, 5u);
    EXPECT_NEAR(c144.mean_refreshes, 4.8, 0.01);
}

TEST(VblankFollower, TicksOnEveryRefreshAt60Hz) {
    VblankFollower follower;
    const auto ticks = run_follower(follower, 60000, 120);
    EXPECT_EQ(follower.milli_hz(), 60000);
    ASSERT_GT(ticks.size(), 100u);
    for (size_t i = 1; i < ticks.size(); ++i)
        EXPECT_EQ(ticks[i] - ticks[i - 1], 1);
}

TEST(VblankFollower, EveryOtherRefreshAt120Hz) {
    VblankFollower follower;
    const auto ticks = run_follower(follower, 120000, 480);
    EXPECT_EQ(follower.milli_hz(), 120000);
    ASSERT_GT(ticks.size(), 200u);
    for (size_t i = 1; i < ticks.size(); ++i)
        EXPECT_EQ(ticks[i] - ticks[i - 1], 2); // phase never flips between 1 and 3
}

TEST(VblankFollower, RateIsExactly60HzAt90And144Hz) {
    for (const int64_t hz : { 90000, 144000, 120000, 60000 }) {
        VblankFollower follower;
        const int refreshes = static_cast<int>(hz / 1000) * 20; // 20 s
        const auto ticks = run_follower(follower, hz, refreshes);
        // After the rate is learned: exactly 60 ticks per second of panel time (10 s window).
        const int from = static_cast<int>(hz / 1000) * 2, to = static_cast<int>(hz / 1000) * 12;
        const long in_window = std::count_if(ticks.begin(), ticks.end(), [&](int r) { return r > from && r <= to; });
        EXPECT_EQ(in_window, 600) << hz;
    }
}

TEST(VblankFollower, GapsStayWithinOneRefreshAt90And144Hz) {
    for (const int64_t hz : { 90000, 144000 }) {
        VblankFollower follower;
        const auto ticks = run_follower(follower, hz, 1440);
        int lo = 100, hi = 0;
        for (size_t i = 1; i < ticks.size(); ++i) {
            lo = std::min(lo, ticks[i] - ticks[i - 1]);
            hi = std::max(hi, ticks[i] - ticks[i - 1]);
        }
        EXPECT_LE(hi - lo, 1) << hz;
    }
}

TEST(VblankFollower, ToleratesTimestampJitter) {
    VblankFollower follower;
    const auto ticks = run_follower(follower, 120000, 480, 100'000); // +-0.1 ms
    for (size_t i = 1; i < ticks.size(); ++i)
        EXPECT_EQ(ticks[i] - ticks[i - 1], 2);
}

TEST(VblankFollower, MissedVsyncDoesNotLoseTime) {
    VblankFollower follower;
    const int64_t period = 1'000'000'000'000LL / 120000;
    int64_t t = 1'000'000'000LL;
    for (int i = 0; i < 60; ++i) {
        t += period;
        follower.on_vsync(t);
    }
    t += 7 * period; // seven vsyncs skipped
    const uint32_t due = follower.on_vsync(t); // 3.5 ticks of guest time elapsed; the caller drops all but one
    EXPECT_GE(due, 3u);
    EXPECT_LE(due, 4u);
}

TEST(VblankFollower, FollowsRefreshRateChange) {
    VblankFollower follower;
    run_follower(follower, 120000, 240);
    EXPECT_EQ(follower.milli_hz(), 120000);
    // Panel dropped to 60 Hz (system picked another mode): estimate follows within a window.
    const int64_t period = 1'000'000'000'000LL / 60000;
    int64_t t = 100'000'000'000LL;
    follower.resync();
    uint32_t ticks = 0;
    for (int i = 0; i < 60; ++i) {
        t += period;
        ticks += follower.on_vsync(t);
    }
    EXPECT_EQ(follower.milli_hz(), 60000);
    EXPECT_GE(ticks, 44u);
}

TEST(IntervalHistogram, BinsAndMean) {
    IntervalHistogram h;
    h.add(16.6);
    h.add(16.7);
    h.add(33.3);
    h.add(500.0);
    EXPECT_EQ(h.count(), 4u);
    EXPECT_EQ(h.bin(8), 2u);
    EXPECT_EQ(h.bin(16), 1u);
    EXPECT_EQ(h.bin(IntervalHistogram::bins - 1), 1u);
}
