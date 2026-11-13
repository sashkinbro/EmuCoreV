// Local trophy stream callback lifetime regressions for ECV / EmuCoreV.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <io/state.h>
#include <np/trophy/context.h>

#include <memory>
#include <utility>
#include <vector>

namespace {
class TrophyStreamLifetime : public ::testing::Test {
protected:
    fs::path directory;
    IOState first_io;
    IOState second_io;
    np::CommunicationID communication_id{};

    void SetUp() override {
        directory = fs::temp_directory_path() / fs::unique_path("trophy-stream-life-%%%%-%%%%");
        fs::create_directories(directory);
        const auto first_path = directory / "first.bin";
        const auto second_path = directory / "second.bin";
        { fs::ofstream file(first_path, std::ios::binary); file << "ABCDEFGH"; }
        { fs::ofstream file(second_path, std::ios::binary); file << "abcdefgh"; }
        first_io.std_files.emplace(11, FileStats("ux0:first.bin", "first.bin", first_path, SCE_O_RDONLY));
        first_io.std_files.emplace(22, FileStats("ux0:first.bin", "first.bin", first_path, SCE_O_RDONLY));
        second_io.std_files.emplace(22, FileStats("ux0:second.bin", "second.bin", second_path, SCE_O_RDONLY));
        ASSERT_NE(first_io.std_files.at(11).get_file_pointer(), nullptr);
        ASSERT_NE(first_io.std_files.at(22).get_file_pointer(), nullptr);
        ASSERT_NE(second_io.std_files.at(22).get_file_pointer(), nullptr);
    }

    void TearDown() override {
        first_io.std_files.clear();
        second_io.std_files.clear();
        if (!directory.empty())
            fs::remove_all(directory);
    }

    void expect_stream_byte(np::trophy::Context &context, char expected) {
        char value = '?';
        // Positive seek preserves the existing integer-to-bool callback policy.
        ASSERT_TRUE(context.trophy_file.seek_func(2));
        ASSERT_TRUE(context.trophy_file.read_func(&value, 1));
        EXPECT_EQ(value, expected);
    }
};
} // namespace

TEST_F(TrophyStreamLifetime, MoveDoesNotFollowTheOriginalContextAddress) {
    np::trophy::Context original(communication_id, &first_io, 11, "");
    np::trophy::Context moved(std::move(original));
    original.io = &second_io;
    original.trophy_file_stream = 22;
    expect_stream_byte(moved, 'C');
    EXPECT_EQ(first_io.std_files.at(11).tell(), 3);
    EXPECT_EQ(second_io.std_files.at(22).tell(), 0);
}

TEST_F(TrophyStreamLifetime, CopyDoesNotFollowTheOriginalContextAddress) {
    np::trophy::Context original(communication_id, &first_io, 11, "");
    np::trophy::Context copied(original);
    original.io = &second_io;
    original.trophy_file_stream = 22;
    expect_stream_byte(copied, 'C');
    EXPECT_EQ(first_io.std_files.at(11).tell(), 3);
    EXPECT_EQ(second_io.std_files.at(22).tell(), 0);
}

TEST_F(TrophyStreamLifetime, CallbackOutlivesMovedFromContext) {
    std::unique_ptr<np::trophy::Context> moved;
    {
        np::trophy::Context original(communication_id, &first_io, 11, "");
        moved = std::make_unique<np::trophy::Context>(std::move(original));
    }
    expect_stream_byte(*moved, 'C');
}

TEST_F(TrophyStreamLifetime, VectorRelocationPreservesFileBackedCallbacks) {
    std::vector<np::trophy::Context> contexts;
    contexts.emplace_back(communication_id, &first_io, 11, "");
    const auto old_capacity = contexts.capacity();
    contexts.reserve(old_capacity + 1);
    ASSERT_GT(contexts.capacity(), old_capacity);
    expect_stream_byte(contexts[0], 'C');
}

TEST_F(TrophyStreamLifetime, RebindingChangesBothIOStateAndDescriptor) {
    np::trophy::Context context(communication_id, &first_io, 11, "");
    context.bind_trophy_stream(&second_io, 22);
    EXPECT_EQ(context.io, &second_io);
    EXPECT_EQ(context.trophy_file_stream, 22);
    expect_stream_byte(context, 'c');
    EXPECT_EQ(first_io.std_files.at(11).tell(), 0);
    EXPECT_EQ(first_io.std_files.at(22).tell(), 0);
    EXPECT_EQ(second_io.std_files.at(22).tell(), 3);
}

TEST_F(TrophyStreamLifetime, ReboundStreamRemainsBoundAfterMove) {
    np::trophy::Context original(communication_id, &first_io, 11, "");
    original.bind_trophy_stream(&second_io, 22);
    np::trophy::Context moved(std::move(original));
    original.bind_trophy_stream(&first_io, 11);
    expect_stream_byte(moved, 'c');
    EXPECT_EQ(first_io.std_files.at(11).tell(), 0);
}
