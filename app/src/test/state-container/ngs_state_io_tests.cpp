#include <emucorev/savestate/ngs_state_io.h>
#include <emucorev/savestate/state_io.h>
#include <gtest/gtest.h>

using namespace emucorev::savestate;

namespace {
TEST(NgsStateIo, AcceptsCompleteEmptySnapshotAndRejectsTrailingOrTruncatedBytes) {
    BufferWriter writer;
    writer.u32(0);
    writer.u32(0);
    writer.u32(0);
    NgsSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(parse_ngs_state(writer.data(), snapshot, error)) << error;
    EXPECT_TRUE(snapshot.systems.empty());
    EXPECT_TRUE(snapshot.racks.empty());
    for (size_t size = 0; size < writer.size(); ++size) {
        const std::vector<uint8_t> truncated(writer.data().begin(), writer.data().begin() + size);
        EXPECT_FALSE(parse_ngs_state(truncated, snapshot, error)) << size;
    }
    writer.u8(0);
    EXPECT_FALSE(parse_ngs_state(writer.data(), snapshot, error));
}

TEST(NgsStateIo, RejectsCountBombBeforeResizingAndPreservesPreviousOutput) {
    NgsSnapshot snapshot;
    snapshot.definitions = 0x1234;
    snapshot.systems.resize(1);
    snapshot.systems[0].address = 0x5678;
    std::string error;
    for (uint32_t count : {1u, 4096u, UINT32_MAX}) {
        BufferWriter writer;
        writer.u32(0);
        writer.u32(count);
        EXPECT_FALSE(parse_ngs_state(writer.data(), snapshot, error));
        EXPECT_EQ(snapshot.definitions, 0x1234u);
        ASSERT_EQ(snapshot.systems.size(), 1u);
        EXPECT_EQ(snapshot.systems[0].address, 0x5678u);
    }
}

TEST(NgsStateIo, RejectsNestedQueuesThatCannotFitInSection) {
    for (uint32_t count : {1u, 65536u, UINT32_MAX}) {
        BufferWriter writer;
        writer.u32(0);
        writer.u32(1);
        writer.u32(0x1000);
        writer.u32(0x1000);
        writer.u32(4096);
        writer.value(SceNgsSystemInitParams{});
        writer.u32(count);
        NgsSnapshot snapshot;
        std::string error;
        EXPECT_FALSE(parse_ngs_state(writer.data(), snapshot, error));
    }
}
} // namespace
