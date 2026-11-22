// Host tests for the sceAppUtilSaveDataDataSave write modes and guest path resolution.

#include <gtest/gtest.h>

#include <io/guest_path.h>
#include <save_data_write.h>

#include <string>

namespace {

// In-memory file standing in for the host I/O layer.
struct MemFile {
    std::string content;
    bool exists = false;
    bool fail_open = false;
    bool fail_write = false;
    bool fail_truncate = false;
    int opens = 0;
    int closes = 0;
    std::uint64_t pos = 0;

    bool open() {
        if (fail_open)
            return false;
        ++opens;
        exists = true; // CREAT, no truncation
        return true;
    }
    bool seek(std::uint64_t offset) {
        pos = offset;
        return true;
    }
    bool write(const void *data, std::uint64_t size) {
        if (fail_write)
            return false;
        if (content.size() < pos + size)
            content.resize(pos + size, '\0');
        content.replace(pos, size, static_cast<const char *>(data), size);
        pos += size;
        return true;
    }
    bool truncate(std::uint64_t length) {
        if (fail_truncate)
            return false;
        content.resize(length, '\0');
        return true;
    }
    void close() { ++closes; }
};

} // namespace

using app_util::SaveWriteMode;
using app_util::SaveWriteResult;

TEST(SaveDataWrite, FileModeWritesAtOffsetAndKeepsRest) {
    MemFile f;
    f.content = "AAAAAAAAAA";
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::File, "xyz", 3, 2), SaveWriteResult::Ok);
    EXPECT_EQ(f.content, "AAxyzAAAAA");
    EXPECT_EQ(f.opens, 1);
    EXPECT_EQ(f.closes, 1);
}

TEST(SaveDataWrite, FileModeCreatesAndZeroFillsGap) {
    MemFile f;
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::File, "ab", 2, 3), SaveWriteResult::Ok);
    EXPECT_EQ(f.content, std::string("\0\0\0ab", 5));
}

TEST(SaveDataWrite, TruncateModeKeepsWrittenDataAndCutsTail) {
    MemFile f;
    f.content = "0123456789";
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::FileTruncate, "AB", 2, 1), SaveWriteResult::Ok);
    // the written bytes must survive (they used to read back as zeros)
    EXPECT_EQ(f.content, "0AB");
    EXPECT_EQ(f.opens, 1);
}

TEST(SaveDataWrite, TruncateModeCanGrowFile) {
    MemFile f;
    f.content = "ab";
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::FileTruncate, "XY", 2, 4), SaveWriteResult::Ok);
    EXPECT_EQ(f.content, std::string("ab\0\0XY", 6));
}

TEST(SaveDataWrite, NullBufferNeedsTruncateMode) {
    MemFile f;
    f.content = "abcdef";
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::File, nullptr, 4, 0), SaveWriteResult::BadParameter);
    EXPECT_EQ(f.opens, 0);
    EXPECT_EQ(f.content, "abcdef");
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::FileTruncate, nullptr, 2, 1), SaveWriteResult::Ok);
    EXPECT_EQ(f.content, "abc");
}

TEST(SaveDataWrite, EmptyWriteInFileModeJustCreates) {
    MemFile f;
    EXPECT_EQ(app_util::save_data_write(f, SaveWriteMode::File, nullptr, 0, 0), SaveWriteResult::Ok);
    EXPECT_TRUE(f.exists);
    EXPECT_TRUE(f.content.empty());
}

TEST(SaveDataWrite, FailuresAreReportedAndFileClosed) {
    MemFile open_fail;
    open_fail.fail_open = true;
    EXPECT_EQ(app_util::save_data_write(open_fail, SaveWriteMode::File, "a", 1, 0), SaveWriteResult::IoError);
    EXPECT_EQ(open_fail.closes, 0);

    MemFile write_fail;
    write_fail.fail_write = true;
    EXPECT_EQ(app_util::save_data_write(write_fail, SaveWriteMode::FileTruncate, "a", 1, 0), SaveWriteResult::IoError);
    EXPECT_EQ(write_fail.closes, 1);

    MemFile trunc_fail;
    trunc_fail.fail_truncate = true;
    EXPECT_EQ(app_util::save_data_write(trunc_fail, SaveWriteMode::FileTruncate, "a", 1, 0), SaveWriteResult::IoError);
    EXPECT_EQ(trunc_fail.closes, 1);
}

TEST(GuestPath, ResolvesDotSegments) {
    using device::resolve_guest_subpath;
    EXPECT_EQ(resolve_guest_subpath("a/b/c"), "a/b/c");
    EXPECT_EQ(resolve_guest_subpath("/a/b"), "a/b");
    EXPECT_EQ(resolve_guest_subpath("a/./b"), "a/b");
    EXPECT_EQ(resolve_guest_subpath("a/b/../c"), "a/c");
    EXPECT_EQ(resolve_guest_subpath("a//b"), "a/b");
    EXPECT_EQ(resolve_guest_subpath("dir/"), "dir/");
    EXPECT_EQ(resolve_guest_subpath("a/b/.."), "a");
}

TEST(GuestPath, CannotEscapeDeviceRoot) {
    using device::resolve_guest_subpath;
    EXPECT_EQ(resolve_guest_subpath("../../etc/passwd"), "etc/passwd");
    EXPECT_EQ(resolve_guest_subpath("a/../../b"), "b");
    EXPECT_EQ(resolve_guest_subpath(".."), "");
    EXPECT_EQ(resolve_guest_subpath("/../.."), "");
    EXPECT_EQ(resolve_guest_subpath("/"), "");
}
