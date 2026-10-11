#include <cheat/functions.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <fstream>

namespace cheat_lifecycle_regression {
class CheatLivePack : public ::testing::Test {
protected:
    MemState mem;
    CheatState state;
    Address scratch = 0;
    const std::string title = "PCSE00333";
    fs::path directory;
    void SetUp() override {
        ASSERT_TRUE(init(mem, false));
        scratch = alloc(mem, 0x1000, "cheat-live-pack-core");
        ASSERT_NE(scratch, 0u);
        std::memset(Ptr<uint8_t>(scratch).get(mem), 0, 0x1000);
        directory = fs::temp_directory_path() / fs::unique_path("cheat-live-pack-%%%%-%%%%");
        fs::create_directories(directory);
    }
    void TearDown() override {
        boost::system::error_code error;
        fs::remove_all(directory, error);
        deinit_mem(mem);
    }
    uint32_t &word(unsigned offset = 0) { return *Ptr<uint32_t>(scratch + offset).get(mem); }
    void load(const std::string &contents) {
        { std::ofstream file((directory / (title + ".psv")).c_str(), std::ios::binary); file << contents; ASSERT_TRUE(file.good()); }
        ASSERT_TRUE(cheat::load(state, directory, title));
        cheat::set_enabled(state, true, mem, {});
        cheat::set_all_cheats_enabled(state, true, mem, {});
        cheat::apply(state, mem, {});
    }
};
TEST_F(CheatLivePack, DeleteReimportRetainsLiveB200SegmentAndOrdinaryDataWrite) {
    CheatModule module;
    module.name = "main";
    module.segments[1] = { scratch, 0x1000 };
    cheat::add_module(state, module);
    load("_V1 inf.HP\n$B200 00000001 00000000\n$0200 00000010 00020000\n");
    ASSERT_EQ(word(0x10), 0x20000u);
    cheat::clear_file(state, mem, {});
    EXPECT_TRUE(cheat::snapshot(state).cheats.empty());
    EXPECT_EQ(word(0x10), 0x20000u);
    ASSERT_EQ(state.modules.size(), 1u);
    word(0x10) = 0;
    ASSERT_TRUE(cheat::reload(state, directory, title, mem, {}));
    cheat::apply(state, mem, {});
    EXPECT_EQ(word(0x10), 0x20000u);
    ASSERT_EQ(cheat::snapshot(state).cheats.size(), 1u);
    EXPECT_FALSE(cheat::snapshot(state).cheats[0].broken);
    cheat::unload(state);
    EXPECT_TRUE(state.modules.empty());
}
TEST_F(CheatLivePack, DeleteRestoresRecordedArmInstructionAndInvalidatesOnce) {
    word() = 0xE1A00000u;
    load(fmt::format("_V1 ARM patch\n$A200 {:08X} EA01D709\n", scratch));
    ASSERT_EQ(word(), 0xEA01D709u);
    unsigned invalidated = 0;
    cheat::clear_file(state, mem, [&](uint32_t address, size_t size) {
        EXPECT_EQ(address, scratch);
        EXPECT_EQ(size, 4u);
        ++invalidated;
    });
    EXPECT_EQ(word(), 0xE1A00000u);
    EXPECT_EQ(invalidated, 1u);
    EXPECT_TRUE(cheat::snapshot(state).cheats.empty());
}
}
