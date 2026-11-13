#include <cheat/functions.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <fstream>
#include <limits>

namespace cheat_execution_failure_regression {
// These cases use the linked core's real parser, interpreter and guest memory.
// Exercise both contiguous memory and page-table mode; no JNI/session seams.
class CheatExecutionFailure : public ::testing::TestWithParam<bool> {
protected:
    MemState mem;
    CheatState state;
    Address scratch = 0;
    fs::path directory;
    const std::string title = "PCSE00333";

    void SetUp() override {
        ASSERT_TRUE(init(mem, GetParam()));
        scratch = alloc(mem, 0x1000, "cheat-execution-failure");
        ASSERT_NE(scratch, 0u);
        ASSERT_LT(scratch, 0xFFFFE000u);
        std::memset(Ptr<uint8_t>(scratch).get(mem), 0, 0x1000);
        directory = fs::temp_directory_path() / fs::unique_path("cheat-failure-%%%%-%%%%");
        fs::create_directories(directory);
    }
    void TearDown() override {
        boost::system::error_code error;
        fs::remove_all(directory, error);
        deinit_mem(mem);
    }
    uint32_t &word(unsigned offset = 0) {
        return *Ptr<uint32_t>(scratch + offset).get(mem);
    }
    void load(const std::string &codes) {
        {
            std::ofstream file((directory / (title + ".psv")).c_str(), std::ios::binary | std::ios::trunc);
            file << "_V1 regression\n" << codes;
            ASSERT_TRUE(file.good());
        }
        ASSERT_TRUE(cheat::reload(state, directory, title, mem, {}));
        cheat::set_enabled(state, true, mem, {});
        const auto pack = cheat::snapshot(state);
        ASSERT_EQ(pack.cheats.size(), 1u);
        ASSERT_TRUE(pack.cheats[0].enabled);
    }
    void expect_broken() {
        const auto pack = cheat::snapshot(state);
        ASSERT_EQ(pack.cheats.size(), 1u);
        EXPECT_TRUE(pack.cheats[0].broken);
        EXPECT_FALSE(pack.cheats[0].enabled);
        EXPECT_EQ(cheat::enabled_cheat_count(state), 0u);
    }
};

TEST_P(CheatExecutionFailure, ValidByteHalfwordAndWordWritesRemainRunnable) {
    load(fmt::format("$0200 {:08X} 12345678\n$0100 {:08X} 0000ABCD\n$0000 {:08X} 000000EF\n",
        scratch, scratch + 4, scratch + 6));
    cheat::apply(state, mem, {});
    EXPECT_EQ(word(), 0x12345678u);
    EXPECT_EQ(word(4), 0x00EFABCDu);
    EXPECT_FALSE(cheat::snapshot(state).cheats[0].broken);
    EXPECT_EQ(cheat::enabled_cheat_count(state), 1u);
}

TEST_P(CheatExecutionFailure, NullAndOverflowingWritesFailBeforeLaterLines) {
    for (const uint32_t address : {0u, 0xFFFFFFFEu}) {
        SCOPED_TRACE(address);
        word() = 0x11223344u;
        load(fmt::format("$0200 {:08X} DEADBEEF\n$0200 {:08X} 00000077\n", address, scratch));
        cheat::apply(state, mem, {});
        expect_broken();
        EXPECT_EQ(word(), 0x11223344u);
    }
}

TEST_P(CheatExecutionFailure, RelativeBaseCannotWrapIntoMappedScratch) {
    CheatModule module;
    module.segments[0] = {0xFFFFF000u, 0x1000u};
    cheat::add_module(state, module);
    word() = 0x11223344u;
    load(fmt::format("$B200 00000000 00000000\n$0200 {:08X} DEADBEEF\n", scratch + 0x1000u));
    cheat::apply(state, mem, {});
    expect_broken();
    EXPECT_EQ(word(), 0x11223344u);
}

TEST_P(CheatExecutionFailure, RepeatedWriteOverflowStopsFollowingInstructions) {
    load(fmt::format("$4201 {:08X} 00000001\n$0002 {:08X} 00000000\n$0200 {:08X} 00000077\n",
        scratch, uint32_t(0u - scratch), scratch + 16));
    cheat::apply(state, mem, {});
    expect_broken();
    EXPECT_EQ(word(), 1u); // Earlier ordinary writes are intentionally retained.
    EXPECT_EQ(word(16), 0u);
}

TEST_P(CheatExecutionFailure, PointerReadWriteAndAdditionFailuresAreBroken) {
    load("$3201 00000000 00000000\n$3300 00000000 00000063\n");
    cheat::apply(state, mem, {});
    expect_broken();
    for (const uint32_t pointer : {0u, 0xFFFFFFFEu, 0xFFFFFFF0u}) {
        SCOPED_TRACE(pointer);
        word() = pointer;
        word(16) = 0x11223344u;
        const uint32_t offset = pointer == 0xFFFFFFF0u ? scratch + 32u : 0u;
        load(fmt::format("$3201 {:08X} {:08X}\n$3300 00000000 00000063\n", scratch, offset));
        cheat::apply(state, mem, {});
        expect_broken();
        EXPECT_EQ(word(16), 0x11223344u);
    }
}

TEST_P(CheatExecutionFailure, CopyRejectsNullSourceAndDestinationWithoutWriting) {
    for (const bool null_source : {false, true}) {
        SCOPED_TRACE(null_source);
        word() = 0x11223344u;
        load(fmt::format("$5200 {:08X} {:08X}\n", null_source ? scratch : 0u, null_source ? 0u : scratch));
        cheat::apply(state, mem, {});
        expect_broken();
        EXPECT_EQ(word(), 0x11223344u);
    }
}

TEST_P(CheatExecutionFailure, PointerCopyRejectsNullDestination) {
    word() = 0u;
    word(4) = scratch + 16;
    word(16) = 0x11223344u;
    load(fmt::format("$8201 {:08X} 00000000\n$8800 00000000 00000000\n$8601 {:08X} 00000000\n$8900 00000000 00000000\n",
        scratch, scratch + 4));
    cheat::apply(state, mem, {});
    expect_broken();
    EXPECT_EQ(word(16), 0x11223344u);
}

TEST_P(CheatExecutionFailure, LaterFailureRestoresArmBytesAndInvalidatesInSameApply) {
    word() = 0xE1A00000u;
    load(fmt::format("$A200 {:08X} EA01D709\n$0200 00000000 00000005\n", scratch));
    unsigned invalidations = 0;
    cheat::apply(state, mem, [&](uint32_t address, size_t size) {
        EXPECT_EQ(address, scratch);
        EXPECT_EQ(size, 4u);
        ++invalidations;
    });
    expect_broken();
    EXPECT_EQ(word(), 0xE1A00000u);
    EXPECT_TRUE(cheat::snapshot(state).cheats[0].saved_memory.empty());
    EXPECT_EQ(invalidations, 2u);
}

TEST_P(CheatExecutionFailure, SettersRejectStaleIndicesAndCannotReenableBrokenEntry) {
    EXPECT_FALSE(cheat::set_all_cheats_enabled(state, true, mem, {}));
    load("$0200 00000000 00000005\n");
    EXPECT_FALSE(cheat::set_cheat_enabled(state, 1, true, mem, {}));
    EXPECT_FALSE(cheat::set_cheat_enabled(state, std::numeric_limits<size_t>::max(), false, mem, {}));
    cheat::apply(state, mem, {});
    expect_broken();
    EXPECT_FALSE(cheat::set_cheat_enabled(state, 0, true, mem, {}));
    EXPECT_FALSE(cheat::set_all_cheats_enabled(state, true, mem, {}));
    expect_broken();
    // A real parser reload, rather than a toggle, is the supported retry path.
    load(fmt::format("$0200 {:08X} 00000007\n", scratch));
    cheat::apply(state, mem, {});
    EXPECT_EQ(word(), 7u);
    EXPECT_FALSE(cheat::snapshot(state).cheats[0].broken);
}

TEST_P(CheatExecutionFailure, GroupEnableSkipsBrokenEntriesAndRunsValidEntry) {
    load("$0200 00000000 00000005\n");
    cheat::apply(state, mem, {});
    expect_broken();
    {
        const std::lock_guard<std::mutex> lock(state.mutex);
        cheat::Cheat valid;
        valid.name = "valid";
        valid.lines = {{0x0200, scratch, 7}};
        state.file.cheats.push_back(valid);
    }
    ASSERT_TRUE(cheat::set_all_cheats_enabled(state, true, mem, {}));
    cheat::apply(state, mem, {});
    const auto pack = cheat::snapshot(state);
    ASSERT_EQ(pack.cheats.size(), 2u);
    EXPECT_TRUE(pack.cheats[0].broken);
    EXPECT_FALSE(pack.cheats[0].enabled);
    EXPECT_TRUE(pack.cheats[1].enabled);
    EXPECT_FALSE(pack.cheats[1].broken);
    EXPECT_EQ(cheat::enabled_cheat_count(state), 1u);
    EXPECT_EQ(word(), 7u);
}

INSTANTIATE_TEST_SUITE_P(RealGuestMemory, CheatExecutionFailure, ::testing::Bool());
} // namespace cheat_execution_failure_regression
