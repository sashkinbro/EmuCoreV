// Host tests for ObjectStore: absent types must be reported as nullptr (they are absent after a
// cold save-state restore) and create/get/erase must be safe against each other.
#include <kernel/object_store.h>

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

namespace {
struct Missing {
    int value = 0;
};
struct Present {
    explicit Present(int v = 7)
        : value(v) {}
    int value;
};
struct Churn {
    std::atomic<int> counter{ 0 };
};
// Models the Videodec use: a guest-created state touched by decode while Term erases it.
struct Tracked {
    static inline std::atomic<int> alive{ 0 };
    Tracked() { ++alive; }
    ~Tracked() { --alive; }
    int payload[64] = {};
};
} // namespace

TEST(ObjectStore, GetOnMissingTypeReturnsNull) {
    ObjectStore store;
    EXPECT_EQ(store.get<Missing>(), nullptr);
    EXPECT_EQ(store.get_shared<Missing>(), nullptr);
}

TEST(ObjectStore, GetAfterCreateAndErase) {
    ObjectStore store;
    ASSERT_TRUE(store.create<Present>(42));
    ASSERT_NE(store.get<Present>(), nullptr);
    EXPECT_EQ(store.get<Present>()->value, 42);
    store.erase<Present>();
    EXPECT_EQ(store.get<Present>(), nullptr);
    store.erase<Present>(); // erasing twice is a no-op
    EXPECT_EQ(store.get<Missing>(), nullptr);
}

TEST(ObjectStore, ClearDropsEverything) {
    ObjectStore store;
    store.create<Present>();
    store.clear();
    EXPECT_EQ(store.get<Present>(), nullptr);
}

TEST(ObjectStore, SharedHandleOutlivesErase) {
    ObjectStore store;
    store.create<Tracked>();
    ASSERT_EQ(Tracked::alive.load(), 1);
    auto held = store.get_shared<Tracked>();
    ASSERT_NE(held, nullptr);
    store.erase<Tracked>();
    EXPECT_EQ(store.get<Tracked>(), nullptr);
    EXPECT_EQ(Tracked::alive.load(), 1); // still alive: Term during Decode is not a use-after-free
    held->payload[63] = 1;
    held.reset();
    EXPECT_EQ(Tracked::alive.load(), 0);
}

TEST(ObjectStore, ConcurrentCreateGetEraseStress) {
    ObjectStore store;
    std::atomic<bool> stop{ false };
    std::atomic<uint64_t> hits{ 0 }, misses{ 0 };
    std::vector<std::thread> readers, writers;
    // Readers use get_shared (the pattern the modules use) and touch the object.
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                if (auto churn = store.get_shared<Churn>()) {
                    churn->counter.fetch_add(1, std::memory_order_relaxed);
                    hits.fetch_add(1, std::memory_order_relaxed);
                } else {
                    misses.fetch_add(1, std::memory_order_relaxed);
                }
                (void)store.get<Missing>(); // concurrent miss lookups
            }
        });
    }
    // Writers: create/erase/clear on the same and on other types.
    for (int i = 0; i < 2; ++i) {
        writers.emplace_back([&, i] {
            for (int n = 0; n < 20000; ++n) {
                store.create<Churn>();
                store.create<Present>(n);
                if ((n + i) % 3 == 0)
                    store.erase<Churn>();
                if (n % 5000 == 0)
                    store.clear();
                store.erase<Present>();
            }
        });
    }
    for (auto &t : writers)
        t.join();
    stop = true;
    for (auto &t : readers)
        t.join();
    EXPECT_GT(hits.load() + misses.load(), 0u);
    store.erase<Churn>();
    EXPECT_EQ(store.get<Churn>(), nullptr);
}
