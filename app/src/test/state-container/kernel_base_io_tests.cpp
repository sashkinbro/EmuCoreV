#include <emucorev/savestate/kernel_base_io.h>
#include <gtest/gtest.h>

using namespace emucorev::savestate;
namespace {
void prefix(BufferWriter &writer) {
    writer.i32(100);
    writer.i32(1);
    writer.boolean(true);
    for (int i = 0; i < 7; ++i) writer.u32(0);
    writer.u64(123);
    writer.u64(456);
    for (int i = 0; i < 5 + KernelState::EXCEPTION_HANDLER_MAX; ++i) writer.u32(0);
}
void finish(BufferWriter &writer) {
    writer.boolean(false);
    writer.u32(0); // callbacks
}

TEST(KernelBaseIo, DecodesWithoutMutatingGuestAndRequiresEntireSection) {
    BufferWriter writer;
    prefix(writer);
    for (int i = 0; i < 11; ++i) writer.u32(0);
    finish(writer);
    KernelBaseSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(parse_kernel_base(writer.data(), snapshot, error)) << error;
    EXPECT_EQ(snapshot.next_uid, 100);
    EXPECT_EQ(snapshot.main_thread_id, 1);
    EXPECT_EQ(snapshot.start_tick, 123u);
    EXPECT_EQ(snapshot.base_tick, 456u);
    for (size_t size = 0; size < writer.size(); ++size) {
        std::vector<uint8_t> truncated(writer.data().begin(), writer.data().begin() + size);
        EXPECT_FALSE(parse_kernel_base(truncated, snapshot, error)) << size;
        EXPECT_EQ(snapshot.start_tick, 123u);
    }
    writer.u8(0xAA);
    EXPECT_FALSE(parse_kernel_base(writer.data(), snapshot, error));
}

TEST(KernelBaseIo, RejectsDuplicateUidAndCountBombsBeforePublishing) {
    KernelBaseSnapshot snapshot;
    snapshot.next_uid = 77;
    std::string error;
    BufferWriter duplicate;
    prefix(duplicate);
    duplicate.u32(2);
    for (int i = 0; i < 2; ++i) {
        duplicate.i32(9); duplicate.u32(4096); duplicate.u32(0x1000);
    }
    EXPECT_FALSE(parse_kernel_base(duplicate.data(), snapshot, error));
    EXPECT_EQ(snapshot.next_uid, 77);
    BufferWriter bomb;
    prefix(bomb);
    bomb.u32(UINT32_MAX);
    EXPECT_FALSE(parse_kernel_base(bomb.data(), snapshot, error));
    EXPECT_EQ(snapshot.next_uid, 77);
}

TEST(KernelBaseIo, RetainsMultipleBindingsForTheSameNidAndGuestAddresses) {
    BufferWriter writer;
    prefix(writer);
    for (int i = 0; i < 7; ++i) writer.u32(0);
    writer.u32(2); // function multimap
    for (uint32_t address : {0x1000u, 0x2000u}) {
        writer.u32(0x1234); writer.u32(address); writer.u32(0x5678);
    }
    writer.u32(2); // variable multimap
    for (uint32_t address : {0x3000u, 0x4000u}) {
        writer.u32(0x1234); writer.u32(address); writer.u32(20); writer.u32(0x5678);
    }
    writer.u32(0); // libraries
    writer.u32(0); // module uid map
    finish(writer);
    KernelBaseSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(parse_kernel_base(writer.data(), snapshot, error)) << error;
    EXPECT_EQ(snapshot.func_binding_infos.count(0x1234), 2u);
    EXPECT_EQ(snapshot.var_binding_infos.count(0x1234), 2u);
    EXPECT_EQ(snapshot.var_binding_infos.begin()->second.entries, 0x3000u);
}

TEST(KernelBaseIo, RebasesProcessElapsedTimeWithoutChangingRtcWallClockBase) {
    constexpr uint64_t base_tick = 62'135'596'800'000'000ull;
    constexpr uint64_t saved_host_time_us = 8'000'000'000ull;
    constexpr uint64_t elapsed_at_save_us = 45'400'000ull;
    constexpr uint64_t away_time_us = 220'600'000ull;
    constexpr uint64_t active_after_restore_us = 1'234'000ull;
    const uint64_t saved_start_tick = base_tick + saved_host_time_us - elapsed_at_save_us;

    uint64_t saved_elapsed_us = 0;
    ASSERT_TRUE(kernel_process_elapsed_at_capture(saved_start_tick, base_tick,
        saved_host_time_us, saved_elapsed_us));
    ASSERT_EQ(saved_elapsed_us, elapsed_at_save_us);

    uint64_t restored_start_tick = 0;
    const uint64_t restored_base_tick = base_tick;
    ASSERT_TRUE(rebase_kernel_process_start_tick(base_tick, saved_elapsed_us,
        saved_host_time_us + away_time_us, restored_start_tick));
    EXPECT_EQ(restored_base_tick, base_tick);
    EXPECT_EQ(base_tick + saved_host_time_us + away_time_us - restored_start_tick,
        elapsed_at_save_us);
    EXPECT_EQ(base_tick + saved_host_time_us + away_time_us + active_after_restore_us - restored_start_tick,
        elapsed_at_save_us + active_after_restore_us);
}

TEST(KernelBaseIo, RejectsInvalidProcessClockArithmetic) {
    uint64_t result = 123;
    EXPECT_FALSE(kernel_process_elapsed_at_capture(100, UINT64_MAX, 1, result));
    EXPECT_EQ(result, 123u);
    EXPECT_FALSE(kernel_process_elapsed_at_capture(200, 100, 50, result));
    EXPECT_EQ(result, 123u);
    EXPECT_FALSE(rebase_kernel_process_start_tick(UINT64_MAX, 0, 1, result));
    EXPECT_EQ(result, 123u);
    EXPECT_FALSE(rebase_kernel_process_start_tick(100, 101, 0, result));
    EXPECT_EQ(result, 123u);
}
} // namespace
