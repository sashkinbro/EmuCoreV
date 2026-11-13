// Local trophy-stream regression tests for ECV / EmuCoreV.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <np/trophy/context.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace {
struct TrophyStreamCopyFixture {
    np::trophy::Context context;
    std::vector<uint8_t> payload;
    size_t cursor = 0;
    size_t reads = 0;
    size_t seeks = 0;
    size_t fail_read_at = std::numeric_limits<size_t>::max();

    explicit TrophyStreamCopyFixture(size_t size = 147157) : payload(size) {
        for (size_t i = 0; i < payload.size(); ++i)
            payload[i] = static_cast<uint8_t>((i * 13 + 7) % 251);
        context.trophy_file.entries.push_back({ "ICON0.PNG", 0, size });
        context.trophy_file.seek_func = [this](int offset) {
            ++seeks;
            if (offset != 0)
                return false;
            cursor = 0;
            return true;
        };
        context.trophy_file.read_func = [this](void *out, uint32_t count) {
            ++reads;
            if (cursor >= fail_read_at || cursor > payload.size() || count > payload.size() - cursor)
                return false;
            std::memcpy(out, payload.data() + cursor, count);
            cursor += count;
            return true;
        };
    }
};

void expect_trophy_copy_prefix(SceSize payload_size, SceSize capacity) {
    TrophyStreamCopyFixture fixture(payload_size);
    constexpr size_t guard_size = 32;
    // Keep enough host memory for the old overwrite to be a canary failure.
    std::vector<uint8_t> bytes(guard_size + std::max(payload_size, capacity) + guard_size, 0xCD);
    SceSize size = capacity;
    ASSERT_EQ(fixture.context.copy_file_data_from_trophy_file("ICON0.PNG", bytes.data() + guard_size, &size), 0);
    const size_t copied = std::min(payload_size, capacity);
    EXPECT_EQ(size, copied);
    EXPECT_TRUE(std::equal(bytes.begin() + guard_size, bytes.begin() + guard_size + copied, fixture.payload.begin()));
    EXPECT_TRUE(std::all_of(bytes.begin(), bytes.begin() + guard_size, [](uint8_t b) { return b == 0xCD; }));
    EXPECT_TRUE(std::all_of(bytes.begin() + guard_size + copied, bytes.end(), [](uint8_t b) { return b == 0xCD; }));
}
} // namespace

TEST(TrophyStreamCopy, PartialMultiChunkEntryNeverWritesBeyondCallerCapacity) {
    // Real local game assets include 147157-byte game icons and 90279-byte SGU icons.
    for (SceSize length : { 147157u, 90279u, 169869u })
        for (SceSize capacity : { 17u, 32768u, 65535u, 65536u, 65537u, 100000u }) {
            SCOPED_TRACE(::testing::Message() << "entry=" << length << " capacity=" << capacity);
            expect_trophy_copy_prefix(length, capacity);
        }
}

TEST(TrophyStreamCopy, ExactAndOversizedCapacityPreserveTheWholeEntry) {
    expect_trophy_copy_prefix(147157, 147157);
    expect_trophy_copy_prefix(147157, 147174);
    expect_trophy_copy_prefix(65536, 65536);
}

TEST(TrophyStreamCopy, ZeroCapacityAndEmptyEntryDoNotTouchBufferOrStream) {
    for (const auto &[length, capacity] : std::array<std::pair<SceSize, SceSize>, 3>{
             std::pair<SceSize, SceSize>{147157, 0}, {0, 17}, {0, 0} }) {
        TrophyStreamCopyFixture fixture(length);
        std::array<uint8_t, 32> bytes;
        bytes.fill(0xCD);
        SceSize size = capacity;
        EXPECT_EQ(fixture.context.copy_file_data_from_trophy_file("ICON0.PNG", bytes.data(), &size), 0);
        EXPECT_EQ(size, 0u);
        EXPECT_EQ(fixture.reads, 0u);
        EXPECT_EQ(fixture.seeks, 0u);
        EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0xCD; }));
    }
}

TEST(TrophyStreamCopy, NullBufferQueriesFullEntrySizeWithoutReading) {
    TrophyStreamCopyFixture fixture;
    SceSize size = 1;
    EXPECT_EQ(fixture.context.copy_file_data_from_trophy_file("ICON0.PNG", nullptr, &size), 0);
    EXPECT_EQ(size, fixture.payload.size());
    EXPECT_EQ(fixture.reads, 0u);
    EXPECT_EQ(fixture.seeks, 0u);
}

TEST(TrophyStreamCopy, FilledCallerCapacityStopsFurtherEntryReads) {
    TrophyStreamCopyFixture fixture;
    std::array<uint8_t, 17> bytes{};
    SceSize size = bytes.size();
    ASSERT_EQ(fixture.context.copy_file_data_from_trophy_file("ICON0.PNG", bytes.data(), &size), 0);
    EXPECT_EQ(fixture.reads, 1u);
    EXPECT_EQ(fixture.cursor, 65536u);
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), fixture.payload.begin()));
}

TEST(TrophyStreamCopy, MissingEntryPreservesBufferAndInoutSizeWithoutStreamAccess) {
    TrophyStreamCopyFixture fixture;
    std::array<uint8_t, 32> bytes;
    bytes.fill(0xCD);
    SceSize size = 17;
    // Error-code policy is deliberately outside this capacity-only fix.
    (void)fixture.context.copy_file_data_from_trophy_file("MISSING.PNG", bytes.data(), &size);
    EXPECT_EQ(size, 17u);
    EXPECT_EQ(fixture.reads, 0u);
    EXPECT_EQ(fixture.seeks, 0u);
    EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](uint8_t b) { return b == 0xCD; }));
}

TEST(TrophyStreamCopy, FailedStreamReadCannotWritePastCapacityOrChangeExistingInoutSizePolicy) {
    for (size_t fail_at : {size_t(0), size_t(65536)}) {
        TrophyStreamCopyFixture fixture;
        fixture.fail_read_at = fail_at;
        std::vector<uint8_t> bytes(fixture.payload.size() + 32, 0xCD);
        SceSize size = 100000;
        (void)fixture.context.copy_file_data_from_trophy_file("ICON0.PNG", bytes.data(), &size);
        EXPECT_EQ(size, 100000u);
        EXPECT_TRUE(std::all_of(bytes.begin() + fail_at, bytes.end(), [](uint8_t b) { return b == 0xCD; }));
        EXPECT_TRUE(std::equal(bytes.begin(), bytes.begin() + fail_at, fixture.payload.begin()));
    }
}
