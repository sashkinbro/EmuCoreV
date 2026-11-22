// Host tests for the pure rear-panel contact bookkeeping (touch/rear_contacts.h).

#include <touch/rear_contacts.h>

#include <gtest/gtest.h>

namespace {

struct IdSource {
    int next = 10;
    int operator()() { return next++; }
};

} // namespace

TEST(RearTouch, QuickTapIsReportedOnceAfterRelease) {
    touch_rear::Contacts c;
    IdSource ids;
    touch_rear::press(c, 0, 100, 200, ids);
    touch_rear::release(c, 0); // released before any vsync sampled it
    EXPECT_TRUE(c.any_active());

    SceTouchReport out[4]{};
    EXPECT_EQ(touch_rear::sample(c, out, 4, 0), 1u);
    EXPECT_EQ(out[0].x, 100);
    EXPECT_EQ(out[0].y, 200);
    touch_rear::end_sampling(c);

    EXPECT_FALSE(c.any_active());
    EXPECT_EQ(touch_rear::sample(c, out, 4, 0), 0u);
}

TEST(RearTouch, HeldContactStaysAfterSampling) {
    touch_rear::Contacts c;
    IdSource ids;
    touch_rear::press(c, 1, 5, 6, ids);
    SceTouchReport out[4]{};
    EXPECT_EQ(touch_rear::sample(c, out, 4, 128), 1u);
    EXPECT_EQ(out[0].force, 128);
    touch_rear::end_sampling(c);
    EXPECT_EQ(touch_rear::sample(c, out, 4, 0), 1u);
    touch_rear::release(c, 1);
    EXPECT_EQ(touch_rear::sample(c, out, 4, 0), 0u);
}

TEST(RearTouch, NewPressGetsNewIdMoveKeepsId) {
    touch_rear::Contacts c;
    IdSource ids;
    touch_rear::press(c, 0, 1, 1, ids);
    const int first = c.report[0].id;
    touch_rear::press(c, 0, 2, 2, ids); // move
    EXPECT_EQ(c.report[0].id, first);
    EXPECT_EQ(c.report[0].x, 2);
    EXPECT_EQ(ids.next, 11); // allocator called once
    touch_rear::release(c, 0);
    touch_rear::press(c, 0, 3, 3, ids);
    EXPECT_NE(c.report[0].id, first);
    EXPECT_TRUE(c.id_in_use(c.report[0].id));
}

TEST(RearTouch, ClearOnlyWhenAskedAndLimitHonoured) {
    touch_rear::Contacts c;
    IdSource ids;
    for (int i = 0; i < touch_rear::SLOTS; ++i) touch_rear::press(c, i, i, i, ids);
    SceTouchReport out[4]{};
    EXPECT_EQ(touch_rear::sample(c, out, 2, 0), 2u);
    c.clear();
    EXPECT_FALSE(c.any_active());
    touch_rear::press(c, 9, 0, 0, ids); // out of range ignored
    touch_rear::release(c, -1);
    EXPECT_FALSE(c.any_active());
}
