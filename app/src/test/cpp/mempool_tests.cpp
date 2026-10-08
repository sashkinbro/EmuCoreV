#include <mem/mempool.h>
#include <gtest/gtest.h>
#include <limits>

namespace {
void expect_partition(const MemspaceBlockAllocator &allocator, uint32_t capacity) {
    uint64_t next = 0;
    for (const auto &block : allocator.blocks) {
        EXPECT_GT(block.size, 0u);
        EXPECT_EQ(block.offset, next);
        next += block.size;
    }
    EXPECT_EQ(next, capacity);
}

TEST(MemspacePool, SplittingAndFreeingRetainOneContiguousOrderedPartition) {
    MemspaceBlockAllocator allocator(256);
    std::vector<uint32_t> offsets;
    for (int i = 0; i < 8; ++i) {
        offsets.push_back(allocator.alloc(5));
        EXPECT_EQ(offsets.back(), static_cast<uint32_t>(i * 8));
        expect_partition(allocator, 256);
    }
    EXPECT_TRUE(allocator.free(offsets[3]));
    EXPECT_EQ(allocator.alloc(8), offsets[3]);
    expect_partition(allocator, 256);
}

TEST(MemspacePool, InvalidSizesDoNotAllocateOrChangeThePool) {
    MemspaceBlockAllocator allocator(256);
    for (uint32_t size : {0u, UINT32_MAX, UINT32_MAX - 1, UINT32_MAX - 2, 257u}) {
        EXPECT_EQ(allocator.alloc(size), UINT32_MAX) << size;
        ASSERT_EQ(allocator.blocks.size(), 1u);
        EXPECT_TRUE(allocator.blocks.front().free);
        expect_partition(allocator, 256);
    }
    EXPECT_EQ(allocator.alloc(256), 0u);
    EXPECT_EQ(allocator.alloc(1), UINT32_MAX);
    expect_partition(allocator, 256);
}

TEST(MemspacePool, ReinitializationDiscardsPreviousPartition) {
    MemspaceBlockAllocator allocator(256);
    ASSERT_EQ(allocator.alloc(16), 0u);
    allocator.init(128);
    ASSERT_EQ(allocator.blocks.size(), 1u);
    EXPECT_TRUE(allocator.blocks.front().free);
    expect_partition(allocator, 128);
    EXPECT_EQ(allocator.alloc(128), 0u);
}
} // namespace
