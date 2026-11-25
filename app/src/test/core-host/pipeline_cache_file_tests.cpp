// B148: the on-disk pipeline cache is only trusted when it was written by this build, translator, driver and GPU.
#include <renderer/vulkan/pipeline_cache_file.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan;

namespace {
PipelineCacheIdentity ident() {
    PipelineCacheIdentity id;
    id.binary_id = 0x1111;
    id.translator_version = 7;
    id.driver_version = 0x80300;
    id.api_version = (1u << 22) | (3u << 12);
    id.vendor_id = 0x5143;
    id.device_id = 0x43050a01;
    id.uuid = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    return id;
}
constexpr uint64_t blob = 4096, keys = 10;
uint64_t good_size() { return sizeof(PipelineCacheFileHeader) + keys * 8 + blob; }
PipelineCacheFileHeader header() { return make_header(ident(), blob, 0xABCD, keys); }
} // namespace

TEST(PipelineCacheFile, MatchingFileIsAccepted) {
    EXPECT_EQ(validate_header(header(), ident(), good_size()), CacheReject::None);
}

TEST(PipelineCacheFile, AnotherBuildOnTheSameDeviceIsDiscarded) {
    // the Lenovo case: same GPU, same driver, a different app build wrote the file
    PipelineCacheIdentity now = ident();
    now.binary_id = 0x2222;
    EXPECT_EQ(validate_header(header(), now, good_size()), CacheReject::Build);
}

TEST(PipelineCacheFile, TranslatorDriverAndDeviceChangesAreEachCaught) {
    PipelineCacheIdentity now = ident();
    now.translator_version = 8;
    EXPECT_EQ(validate_header(header(), now, good_size()), CacheReject::Translator);
    now = ident();
    now.driver_version += 1;
    EXPECT_EQ(validate_header(header(), now, good_size()), CacheReject::Driver);
    now = ident();
    now.api_version += 1;
    EXPECT_EQ(validate_header(header(), now, good_size()), CacheReject::Driver);
    now = ident();
    now.uuid[15] ^= 1;
    EXPECT_EQ(validate_header(header(), now, good_size()), CacheReject::Device);
    now = ident();
    now.device_id += 1;
    EXPECT_EQ(validate_header(header(), now, good_size()), CacheReject::Device);
}

TEST(PipelineCacheFile, OldFormatsAndGarbageNeverReachTheDriver) {
    PipelineCacheFileHeader h = header();
    h.magic = 0xBEEF4322; // the previous format
    EXPECT_EQ(validate_header(h, ident(), good_size()), CacheReject::Format);
    h = header();
    h.header_size += 8;
    EXPECT_EQ(validate_header(h, ident(), good_size()), CacheReject::Format);
    EXPECT_EQ(validate_header(header(), ident(), 8), CacheReject::TooSmall);
}

TEST(PipelineCacheFile, TruncatedAndHostileSizesAreRejectedWithoutOverflow) {
    EXPECT_EQ(validate_header(header(), ident(), good_size() - 1), CacheReject::Truncated);
    PipelineCacheFileHeader h = header();
    h.pipeline_count = ~0ull;
    EXPECT_EQ(validate_header(h, ident(), good_size()), CacheReject::Truncated);
    h = header();
    h.blob_size = ~0ull;
    EXPECT_EQ(validate_header(h, ident(), good_size()), CacheReject::Truncated);
    h = header();
    h.pipeline_count = (~0ull) / 8 + 1; // count * 8 overflows
    EXPECT_EQ(validate_header(h, ident(), good_size()), CacheReject::Truncated);
}

TEST(PipelineCacheFile, ReasonTextsAreStableForTheLabLog) {
    EXPECT_STREQ(to_string(CacheReject::Build), "app build changed");
    EXPECT_STREQ(to_string(CacheReject::Driver), "driver version changed");
    EXPECT_STREQ(to_string(CacheReject::Corrupt), "payload hash mismatch");
}
