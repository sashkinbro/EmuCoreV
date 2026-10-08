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
} // namespace
