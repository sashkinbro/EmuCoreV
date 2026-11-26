// C5 build-out: bookkeeping of USSE buffer stores on the route without memory mapping.
#include <renderer/vulkan/buffer_store_model.h>

#include <gtest/gtest.h>

#include <array>
#include <vector>

using namespace renderer::vulkan::buffer_store;

TEST(BufferStoreBlocks, ASecondUploadOfTheSameBlockReplacesTheFirst) {
    std::vector<Block> blocks;
    note_block(blocks, { 0x1000, 64, 0, 0, Vertex, 0 });
    note_block(blocks, { 0x2000, 32, 64, 0, Vertex, 1 });
    note_block(blocks, { 0x3000, 16, 0, 0, Fragment, 0 });
    note_block(blocks, { 0x4000, 48, 0, 0, Vertex, 0 });
    ASSERT_EQ(blocks.size(), 3u);
    EXPECT_EQ(blocks[0].guest_address, 0x4000u);
    EXPECT_EQ(blocks[0].size, 48u);
    EXPECT_EQ(blocks[1].guest_address, 0x2000u);
    EXPECT_EQ(blocks[2].stage, Fragment);
}

TEST(BufferStoreRing, RegionSurvivesUntilTheCursorOvertakesIt) {
    EXPECT_TRUE(ring_data_intact(3, 1000, 64, 3, 5000)); // no wrap since
    EXPECT_TRUE(ring_data_intact(3, 1000, 64, 4, 800)); // wrapped once, cursor still below the region
    EXPECT_TRUE(ring_data_intact(3, 1000, 64, 4, 1000));
    EXPECT_FALSE(ring_data_intact(3, 1000, 64, 4, 1001)); // cursor reached into the region
    EXPECT_FALSE(ring_data_intact(3, 1000, 64, 5, 0)); // wrapped twice: certainly reused
    EXPECT_FALSE(ring_data_intact(3, 1000, 0, 4, 0)); // empty region
}

TEST(BufferStoreRing, RangeMustLieInsideTheBuffer) {
    EXPECT_TRUE(range_in_buffer(1024, 0, 1024));
    EXPECT_TRUE(range_in_buffer(1024, 1000, 24));
    EXPECT_FALSE(range_in_buffer(1024, 1000, 25));
    EXPECT_FALSE(range_in_buffer(1024, 0, 0));
    EXPECT_FALSE(range_in_buffer(1024, 0xFFFFFFF0u, 0x20)); // no 32 bit wrap
}

TEST(BufferStoreRuns, OnlyChangedBytesAreReported) {
    std::array<uint8_t, 16> before{}, after{};
    after[2] = 1;
    after[3] = 2;
    after[9] = 7;
    const auto runs = changed_runs(before.data(), after.data(), before.size());
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].offset, 2u);
    EXPECT_EQ(runs[0].length, 2u);
    EXPECT_EQ(runs[1].offset, 9u);
    EXPECT_EQ(runs[1].length, 1u);
    EXPECT_TRUE(changed_runs(before.data(), before.data(), before.size()).empty());
}

TEST(BufferStoreRuns, RunsAtTheEdgesAndTheWholeBuffer) {
    std::array<uint8_t, 8> before{}, after{};
    after[0] = 1;
    after[7] = 1;
    auto runs = changed_runs(before.data(), after.data(), 8);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].offset, 0u);
    EXPECT_EQ(runs[1].offset, 7u);
    after.fill(0xFF);
    runs = changed_runs(before.data(), after.data(), 8);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0].length, 8u);
    EXPECT_TRUE(changed_runs(before.data(), after.data(), 0).empty());
}

TEST(BufferStoreRuns, MergeGapJoinsNearbyRunsOnly) {
    std::array<uint8_t, 16> before{}, after{};
    after[1] = 1;
    after[4] = 1; // two equal bytes in between
    after[12] = 1;
    EXPECT_EQ(changed_runs(before.data(), after.data(), 16, 0).size(), 3u);
    const auto merged = changed_runs(before.data(), after.data(), 16, 2);
    ASSERT_EQ(merged.size(), 2u);
    EXPECT_EQ(merged[0].offset, 1u);
    EXPECT_EQ(merged[0].length, 4u);
    EXPECT_EQ(merged[1].offset, 12u);
    // a stale copy of an earlier store is never written: bytes that equal the upload are not in any run
    std::array<uint8_t, 4> upload = { 9, 9, 9, 9 }, shader = { 9, 5, 9, 9 };
    const auto only = changed_runs(upload.data(), shader.data(), 4);
    ASSERT_EQ(only.size(), 1u);
    EXPECT_EQ(only[0].offset, 1u);
}

namespace {
// minimal module: header, OpDecorate %container Binding 2, [OpDecorate %container NonWritable], OpVariable %ptr %container StorageBuffer
std::vector<uint32_t> module_with_container(const bool non_writable, const uint32_t storage_class = 12) {
    std::vector<uint32_t> m = { 0x07230203u, 0x00010000u, 0, 20, 0 };
    m.insert(m.end(), { (4u << 16) | 71u, 7u, 33u, 2u });
    if (non_writable)
        m.insert(m.end(), { (3u << 16) | 71u, 7u, 24u });
    m.insert(m.end(), { (4u << 16) | 59u, 6u, 7u, storage_class });
    return m;
}
} // namespace

TEST(BufferStoreModule, WritableContainerIsDetectedFromTheModule) {
    auto writable = module_with_container(false);
    auto read_only = module_with_container(true);
    EXPECT_TRUE(spirv_has_writable_storage_buffer(writable.data(), writable.size()));
    EXPECT_FALSE(spirv_has_writable_storage_buffer(read_only.data(), read_only.size()));
}

TEST(BufferStoreModule, OtherStorageClassesAndBrokenModulesAreNotContainers) {
    auto uniform = module_with_container(false, 2);
    EXPECT_FALSE(spirv_has_writable_storage_buffer(uniform.data(), uniform.size()));
    auto truncated = module_with_container(false);
    truncated.pop_back();
    EXPECT_FALSE(spirv_has_writable_storage_buffer(truncated.data(), truncated.size()));
    std::vector<uint32_t> bad_magic = module_with_container(false);
    bad_magic[0] = 0;
    EXPECT_FALSE(spirv_has_writable_storage_buffer(bad_magic.data(), bad_magic.size()));
    EXPECT_FALSE(spirv_has_writable_storage_buffer(nullptr, 0));
}
