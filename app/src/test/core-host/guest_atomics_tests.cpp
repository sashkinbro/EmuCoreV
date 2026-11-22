// Host tests for the sceKernelAtomic* helpers: every operation at every width, wraparound,
// old/new return conventions and multi-threaded stress.
#include <guest_atomics.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

namespace ga = guest_atomic;

template <typename U>
class GuestAtomicTyped : public ::testing::Test {};
using Widths = ::testing::Types<uint8_t, uint16_t, uint32_t, uint64_t>;
TYPED_TEST_SUITE(GuestAtomicTyped, Widths);

TYPED_TEST(GuestAtomicTyped, NewValueOps) {
    using U = TypeParam;
    U v = 10;
    EXPECT_EQ(ga::add_and_get(&v, U(5)), U(15));
    EXPECT_EQ(v, U(15));
    EXPECT_EQ(ga::sub_and_get(&v, U(3)), U(12));
    EXPECT_EQ(v, U(12));
    EXPECT_EQ(ga::and_and_get(&v, U(0x0A)), U(0x08));
    EXPECT_EQ(v, U(0x08));
    EXPECT_EQ(ga::or_and_get(&v, U(0x03)), U(0x0B));
    EXPECT_EQ(v, U(0x0B));
    EXPECT_EQ(ga::xor_and_get(&v, U(0x0F)), U(0x04));
    EXPECT_EQ(v, U(0x04));
    v = 0xFF;
    EXPECT_EQ(ga::clear_and_get(&v, U(0x0F)), U(0xF0));
    EXPECT_EQ(v, U(0xF0));
}

TYPED_TEST(GuestAtomicTyped, OldValueOps) {
    using U = TypeParam;
    U v = 10;
    EXPECT_EQ(ga::get_and_add(&v, U(5)), U(10));
    EXPECT_EQ(v, U(15));
    EXPECT_EQ(ga::get_and_sub(&v, U(3)), U(15));
    EXPECT_EQ(v, U(12));
    EXPECT_EQ(ga::get_and_and(&v, U(0x0A)), U(12));
    EXPECT_EQ(v, U(0x08));
    EXPECT_EQ(ga::get_and_or(&v, U(0x03)), U(0x08));
    EXPECT_EQ(v, U(0x0B));
    EXPECT_EQ(ga::get_and_xor(&v, U(0x0F)), U(0x0B));
    EXPECT_EQ(v, U(0x04));
    v = 0xFF;
    EXPECT_EQ(ga::get_and_clear(&v, U(0x0F)), U(0xFF));
    EXPECT_EQ(v, U(0xF0));
    EXPECT_EQ(ga::get_and_set(&v, U(7)), U(0xF0));
    EXPECT_EQ(v, U(7));
    EXPECT_EQ(ga::set(&v, U(9)), U(7));
    EXPECT_EQ(v, U(9));
    v = 0xFF;
    EXPECT_EQ(ga::clear_mask(&v, U(0x3C)), U(0xFF));
    EXPECT_EQ(v, U(0xC3));
}

TYPED_TEST(GuestAtomicTyped, Wraparound) {
    using U = TypeParam;
    const U max = std::numeric_limits<U>::max();
    U v = max;
    EXPECT_EQ(ga::add_and_get(&v, U(1)), U(0));
    EXPECT_EQ(v, U(0));
    EXPECT_EQ(ga::sub_and_get(&v, U(1)), max);
    EXPECT_EQ(v, max);
    EXPECT_EQ(ga::get_and_add(&v, U(2)), max);
    EXPECT_EQ(v, U(1));
    EXPECT_EQ(ga::get_and_sub(&v, U(2)), U(1));
    EXPECT_EQ(v, max);
}

TYPED_TEST(GuestAtomicTyped, CompareAndSet) {
    using U = TypeParam;
    U v = 5;
    EXPECT_EQ(ga::compare_and_set(&v, U(4), U(9)), U(5)); // mismatch: unchanged, old returned
    EXPECT_EQ(v, U(5));
    EXPECT_EQ(ga::compare_and_set(&v, U(5), U(9)), U(5)); // match: stored, old returned
    EXPECT_EQ(v, U(9));
}

TYPED_TEST(GuestAtomicTyped, AddUnless) {
    using U = TypeParam;
    U v = 5;
    EXPECT_EQ(ga::add_unless(&v, U(3), U(5)), U(0)); // equals cmp: not added
    EXPECT_EQ(v, U(5));
    EXPECT_EQ(ga::add_unless(&v, U(3), U(6)), U(1)); // added
    EXPECT_EQ(v, U(8));
}

TYPED_TEST(GuestAtomicTyped, DecIfPositive) {
    using U = TypeParam;
    using S = std::make_signed_t<U>;
    U v = 2;
    EXPECT_EQ(ga::dec_if_positive(&v), U(1)); // returns old - 1
    EXPECT_EQ(v, U(1));
    EXPECT_EQ(ga::dec_if_positive(&v), U(0));
    EXPECT_EQ(v, U(0));
    EXPECT_EQ(static_cast<S>(ga::dec_if_positive(&v)), S(-1)); // zero: not stored, old - 1 returned
    EXPECT_EQ(v, U(0));
    v = static_cast<U>(S(-5)); // negative: not stored
    EXPECT_EQ(static_cast<S>(ga::dec_if_positive(&v)), S(-6));
    EXPECT_EQ(static_cast<S>(v), S(-5));
    v = static_cast<U>(std::numeric_limits<S>::max()); // largest positive value
    ga::dec_if_positive(&v);
    EXPECT_EQ(v, static_cast<U>(std::numeric_limits<S>::max() - 1));
}

// Export-level sign extension of the narrow results (what the EXPORT wrappers do).
TEST(GuestAtomic, NegativeNarrowValuesSignExtend) {
    uint8_t b = 0x7F;
    EXPECT_EQ(static_cast<int32_t>(static_cast<int8_t>(ga::add_and_get(&b, uint8_t(1)))), -128);
    uint16_t h = 0xFFFF;
    EXPECT_EQ(static_cast<int32_t>(static_cast<int16_t>(ga::get_and_add(&h, uint16_t(0)))), -1);
    h = 0;
    EXPECT_EQ(static_cast<int32_t>(static_cast<int16_t>(ga::sub_and_get(&h, uint16_t(1)))), -1);
    b = 0;
    EXPECT_EQ(static_cast<int32_t>(static_cast<int8_t>(ga::dec_if_positive(&b))), -1);
    int64_t w = 0;
    auto *wp = reinterpret_cast<uint64_t *>(&w);
    EXPECT_EQ(static_cast<int64_t>(ga::sub_and_get(wp, uint64_t(1))), -1);
}

TYPED_TEST(GuestAtomicTyped, NullPointerIsNoOp) {
    using U = TypeParam;
    EXPECT_EQ(ga::add_and_get<U>(nullptr, U(1)), U(0));
    EXPECT_EQ(ga::get_and_set<U>(nullptr, U(1)), U(0));
}

TYPED_TEST(GuestAtomicTyped, ConcurrentAddIsExact) {
    using U = TypeParam;
    alignas(8) U counter = 0;
    constexpr int kThreads = 4, kIters = 20000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < kIters; ++i)
                ga::add_and_get(&counter, U(1));
        });
    for (auto &t : threads)
        t.join();
    EXPECT_EQ(counter, static_cast<U>(kThreads * kIters)); // exact modulo the width
}

TEST(GuestAtomic, ConcurrentCompareAndSetSpinLock) {
    // Each thread takes a spin lock with CAS 0->1, bumps a plain counter, releases with set.
    alignas(4) uint32_t lock = 0;
    uint64_t plain = 0;
    constexpr int kThreads = 4, kIters = 20000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
        threads.emplace_back([&] {
            for (int i = 0; i < kIters; ++i) {
                while (ga::compare_and_set(&lock, 0u, 1u) != 0)
                    std::this_thread::yield();
                ++plain;
                ga::get_and_set(&lock, 0u);
            }
        });
    for (auto &t : threads)
        t.join();
    EXPECT_EQ(plain, uint64_t(kThreads) * kIters);
    EXPECT_EQ(lock, 0u);
}

TEST(GuestAtomic, MisalignedPointerFallsBackWithoutCrashing) {
    alignas(8) uint8_t buf[16] = {};
    auto *p = reinterpret_cast<uint32_t *>(buf + 1);
    EXPECT_EQ(ga::add_and_get(p, 0x01020304u), 0x01020304u);
    EXPECT_EQ(ga::get_and_add(p, 1u), 0x01020304u);
    uint32_t out;
    memcpy(&out, buf + 1, 4);
    EXPECT_EQ(out, 0x01020305u);
}
