#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/state_io.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <sys/wait.h>
#include <unistd.h>

using namespace emucorev::savestate;

namespace {
class StateArchive : public testing::Test {
protected:
    fs::path root;
    fs::path path;
    std::string error;

    void SetUp() override {
        const char *tmp = std::getenv("TMPDIR");
        root = fs::path(tmp ? tmp : ".") / fs::unique_path("state-container-%%%%-%%%%");
        ASSERT_TRUE(fs::create_directory(root));
        path = root / "slot.ecvs";
    }
    void TearDown() override { fs::remove_all(root); }
    void write(bool compressed = false) {
        Writer writer;
        ASSERT_TRUE(writer.open(path, error)) << error;
        constexpr std::array<uint8_t, 4> payload{1, 2, 3, 4};
        ASSERT_TRUE(writer.write_section(SectionId::Meta, payload.data(), payload.size(), compressed, error)) << error;
        ASSERT_TRUE(writer.write_section(SectionId::Memory, payload.data(), payload.size(), false, error)) << error;
        ASSERT_TRUE(writer.finalize(error)) << error;
    }
    std::vector<uint8_t> bytes() {
        std::ifstream file(path.string(), std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }
    void replace(const std::vector<uint8_t> &data) {
        std::ofstream file(path.string(), std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(data.data()), data.size());
    }
    template <typename Fn> void mutate_index(Fn fn) {
        auto data = bytes();
        Footer footer;
        std::memcpy(&footer, data.data() + data.size() - sizeof(Footer), sizeof(Footer));
        std::array<SectionIndexEntry, 2> index;
        std::memcpy(index.data(), data.data() + footer.index_offset, sizeof(index));
        fn(index);
        std::memcpy(data.data() + footer.index_offset, index.data(), sizeof(index));
        replace(data);
    }
};

TEST_F(StateArchive, ValidRawAndCompressedSectionsRoundTrip) {
    for (bool compressed : {false, true}) {
        write(compressed);
        Reader reader;
        ASSERT_TRUE(reader.open(path, error)) << error;
        std::vector<uint8_t> out;
        ASSERT_TRUE(reader.read_section(SectionId::Meta, out, error)) << error;
        EXPECT_EQ(out, (std::vector<uint8_t>{1, 2, 3, 4}));
    }
}

TEST_F(StateArchive, RejectsWrappingSectionExtent) {
    write();
    mutate_index([](auto &index) { index[0].offset = UINT64_MAX - 1; index[0].stored_size = 4; });
    Reader reader;
    EXPECT_FALSE(reader.open(path, error));
}

TEST_F(StateArchive, RejectsSectionInsideIndex) {
    write();
    mutate_index([](auto &index) { index[0].offset = 8; });
    Reader reader;
    EXPECT_FALSE(reader.open(path, error));
}

TEST_F(StateArchive, RejectsOverlappingSections) {
    write();
    mutate_index([](auto &index) { index[1].offset = index[0].offset; });
    Reader reader;
    EXPECT_FALSE(reader.open(path, error));
}

TEST_F(StateArchive, RejectsDuplicateSectionIds) {
    write();
    mutate_index([](auto &index) { index[1].id = index[0].id; });
    Reader reader;
    EXPECT_FALSE(reader.open(path, error));
}

TEST_F(StateArchive, RejectsUnknownFlagsAndRawSizeMismatch) {
    for (bool flags : {false, true}) {
        write();
        mutate_index([&](auto &index) { if (flags) index[0].flags = 2; else index[0].raw_size = 5; });
        Reader reader;
        EXPECT_FALSE(reader.open(path, error));
    }
}

TEST_F(StateArchive, RejectsCompressedPayloadBeyondItsSection) {
    write(true);
    auto data = bytes();
    uint64_t oversized = 20;
    std::memcpy(data.data() + 8, &oversized, 8);
    replace(data);
    Reader reader;
    ASSERT_TRUE(reader.open(path, error));
    std::vector<uint8_t> out;
    EXPECT_FALSE(reader.read_section(SectionId::Meta, out, error));
}

TEST_F(StateArchive, StreamingValidationDetectsCorruptMemoryBeforeAnyLoad) {
    write();
    auto data = bytes();
    data[4] ^= 0x40;
    replace(data);
    Reader reader;
    ASSERT_TRUE(reader.open(path, error));
    EXPECT_FALSE(reader.validate_sections(error));
    EXPECT_EQ(error, "section checksum mismatch");
}

TEST_F(StateArchive, StreamingValidationAcceptsCompleteRawAndCompressedArchive) {
    for (bool compressed : {false, true}) {
        write(compressed);
        Reader reader;
        ASSERT_TRUE(reader.open(path, error));
        EXPECT_TRUE(reader.validate_sections(error)) << error;
    }
}

TEST_F(StateArchive, RejectsCompressedAllocationBombBeforeResize) {
    write(true);
    mutate_index([](auto &index) { index[0].raw_size = UINT64_MAX; });
    Reader reader;
    EXPECT_FALSE(reader.open(path, error));
}

TEST_F(StateArchive, RejectsOversizedStoredDeflateBeforeAllocation) {
    Writer writer;
    ASSERT_TRUE(writer.open(path, error));
    std::array<uint8_t, 1024> noisy;
    uint32_t value = 123;
    for (auto &byte : noisy) {
        value = value * 1664525 + 1013904223;
        byte = value >> 24;
    }
    ASSERT_TRUE(writer.write_section(SectionId::Meta, noisy.data(), noisy.size(), true, error));
    ASSERT_TRUE(writer.write_section(SectionId::Memory, noisy.data(), noisy.size(), false, error));
    ASSERT_TRUE(writer.finalize(error));
    mutate_index([](auto &index) { index[0].raw_size = 4; });
    Reader reader;
    EXPECT_FALSE(reader.open(path, error));
}

TEST_F(StateArchive, WriterRejectsExcessiveBufferedDataBeforeReadingInput) {
    Writer writer;
    ASSERT_TRUE(writer.open(path, error));
    ASSERT_TRUE(writer.begin_section(SectionId::Meta, true, error));
    const uint8_t dummy = 0;
    EXPECT_FALSE(writer.append_section_data(&dummy, kMaxBufferedSectionSize + 1, error));
    EXPECT_EQ(error, "section exceeds buffered allocation limit");
}

TEST_F(StateArchive, WriterRejectsEmptyArchiveAndZeroIdentifier) {
    Writer writer;
    ASSERT_TRUE(writer.open(path, error));
    EXPECT_FALSE(writer.finalize(error));
    EXPECT_FALSE(writer.begin_section(static_cast<SectionId>(0), false, error));
}

TEST_F(StateArchive, WriterRefusesDuplicateSectionBeforePublishingInvalidSlot) {
    Writer writer;
    ASSERT_TRUE(writer.open(path, error));
    const uint8_t value = 0;
    ASSERT_TRUE(writer.write_section(SectionId::Meta, &value, 1, false, error));
    EXPECT_FALSE(writer.begin_section(SectionId::Meta, false, error));
    EXPECT_TRUE(writer.finalize(error));
    Reader reader;
    EXPECT_TRUE(reader.open(path, error));
}

TEST_F(StateArchive, FailedCommitPreservesPreviousSlot) {
    write();
    const auto previous = bytes();
    Writer writer;
    ASSERT_TRUE(writer.open(path, error));
    const uint8_t payload = 99;
    ASSERT_TRUE(writer.write_section(SectionId::Meta, &payload, 1, false, error));
    // Simulate a disappearing staging file before publication. On POSIX the
    // open file remains writable, so only the final rename reports failure.
    for (const auto &file : fs::directory_iterator(root)) {
        if (file.path().extension() == ".tmp")
            fs::remove(file.path());
    }
    EXPECT_FALSE(writer.finalize(error));
    EXPECT_EQ(bytes(), previous);
}

TEST_F(StateArchive, BufferReaderRejectsNoncanonicalBoolean) {
    const uint8_t raw = 2;
    BufferReader reader(&raw, 1);
    reader.boolean();
    EXPECT_FALSE(reader.ok());
}

TEST_F(StateArchive, BufferReaderRejectsOverflowingReadWithoutWriting) {
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        const std::array<uint8_t, 2> raw{1, 2};
        BufferReader reader(raw.data(), raw.size());
        reader.u8();
        uint8_t canary = 0xAB;
        const bool accepted = reader.bytes(&canary, std::numeric_limits<size_t>::max());
        std::_Exit(!accepted && canary == 0xAB ? 0 : 1);
    }
    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WIFEXITED(status) ? WEXITSTATUS(status) : -1, 0);
}
} // namespace
