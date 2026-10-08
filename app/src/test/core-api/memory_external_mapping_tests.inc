#include <mem/functions.h>
#include <cstdlib>

TEST(GuestExternalMapping, UnmapCopiesBackWithNoCoveringProtectionEntry) {
    MemState mem;
    ASSERT_TRUE(init(mem, true));
    struct MemoryGuard {
        MemState &mem;
        ~MemoryGuard() { deinit_mem(mem); }
    } memory_guard{mem};
    const uint32_t size = std::max<uint32_t>(4096, mem.host_page_size);
    const Address address = alloc_aligned(mem, size, "unmap-test", size);
    ASSERT_NE(address, 0u);
    const auto release_external = [](uint8_t *pointer) { std::free(static_cast<void *>(pointer)); };
    std::unique_ptr<uint8_t, decltype(release_external)> external(
        static_cast<uint8_t *>(std::aligned_alloc(size, size)), release_external);
    ASSERT_NE(external, nullptr);
    auto *arena = mem.memory.get() + address;
    for (const bool unrelated_entry : {false, true}) {
        std::memset(arena, 0x31, size);
        if (unrelated_entry) {
            // The reverse-ordered tree's lower_bound still returns end when
            // every remaining protection starts above the unmapped range.
            mem.protect_tree.emplace(address + 2 * size, ProtectSegmentInfo(size, MemPerm::ReadOnly));
        }
        add_external_mapping(mem, address, size, external.get());
        EXPECT_EQ(Ptr<uint8_t>(address).get(mem), external.get());
        std::memset(external.get(), 0x72, size);
        remove_external_mapping(mem, external.get(), size);
        EXPECT_TRUE(mem.external_mapping.empty());
        EXPECT_EQ(Ptr<uint8_t>(address).get(mem), arena);
        EXPECT_EQ(mem.protect_tree.size(), unrelated_entry ? 1u : 0u);
        EXPECT_TRUE(std::all_of(arena, arena + size, [](uint8_t byte) { return byte == 0x72; }));
        mem.protect_tree.clear();
    }
}
