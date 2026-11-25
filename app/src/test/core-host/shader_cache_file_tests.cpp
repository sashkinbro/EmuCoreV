// The on-disk shader cache is only trusted when it was written by this translator and config, and verifies
// (docs/shader-cache.md): identity changes, truncation, corruption and atomic writes.
#include <renderer/shader_cache_file.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace renderer;
namespace fsx = std::filesystem;

namespace {
ShaderCacheIdentity ident() {
    ShaderCacheIdentity id;
    id.translator_hash = 0x1122334455667788ULL;
    id.translator_version = 32;
    id.features_mask = 0x1f05;
    return id;
}

std::vector<uint32_t> spirv() {
    std::vector<uint32_t> w(64);
    for (size_t i = 0; i < w.size(); ++i)
        w[i] = static_cast<uint32_t>(0x9e3779b9u * (i + 1));
    w[0] = spirv_magic_word;
    return w;
}

std::vector<uint8_t> good_entry() {
    const auto w = spirv();
    return encode_shader_cache_entry(shader_cache_identity_key(ident()), w.data(), w.size());
}

ShaderCacheReject decode(const std::vector<uint8_t> &bytes, uint64_t key, std::vector<uint32_t> *out = nullptr) {
    std::vector<uint32_t> words;
    const auto r = decode_shader_cache_entry(bytes.data(), bytes.size(), key, words);
    if (out)
        *out = words;
    return r;
}

std::vector<ShaderCacheManifestEntry> manifest_entries() {
    std::vector<ShaderCacheManifestEntry> e(5);
    for (size_t i = 0; i < e.size(); ++i) {
        e[i].frag.fill(static_cast<uint8_t>(i + 1));
        e[i].vert.fill(static_cast<uint8_t>(0x80 + i));
    }
    return e;
}

ShaderCacheReject decode_manifest(const std::vector<uint8_t> &b, const ShaderCacheIdentity &id, std::vector<ShaderCacheManifestEntry> *out = nullptr) {
    std::vector<ShaderCacheManifestEntry> e;
    const auto r = decode_shader_cache_manifest(b.data(), b.size(), id, e);
    if (out)
        *out = e;
    return r;
}

struct TempDir {
    fsx::path path;
    TempDir() {
        path = fsx::temp_directory_path() / ("ecv-shader-cache-test-" + std::to_string(reinterpret_cast<uintptr_t>(this)));
        fsx::remove_all(path);
        fsx::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fsx::remove_all(path, ec);
    }
};

size_t count_files(const fsx::path &dir) {
    size_t n = 0;
    for (const auto &e : fsx::directory_iterator(dir)) {
        (void)e;
        ++n;
    }
    return n;
}
} // namespace

TEST(ShaderCacheKey, ChangesWithTranslatorIdentity) {
    const uint64_t base = shader_cache_identity_key(ident());
    EXPECT_EQ(base, shader_cache_identity_key(ident()));

    auto a = ident();
    a.translator_hash ^= 1; // any translator source change
    EXPECT_NE(base, shader_cache_identity_key(a));

    auto b = ident();
    b.translator_version++;
    EXPECT_NE(base, shader_cache_identity_key(b));

    auto c = ident();
    c.features_mask ^= (1u << 12); // force-full-precision
    EXPECT_NE(base, shader_cache_identity_key(c));
    c = ident();
    c.features_mask ^= (1u << 13); // no-fp-contraction
    EXPECT_NE(base, shader_cache_identity_key(c));
}

TEST(ShaderCacheEntry, RoundTrips) {
    std::vector<uint32_t> out;
    EXPECT_EQ(decode(good_entry(), shader_cache_identity_key(ident()), &out), ShaderCacheReject::None);
    EXPECT_EQ(out, spirv());
}

TEST(ShaderCacheEntry, StaleIdentityIsRejected) {
    auto id = ident();
    id.translator_hash += 1;
    std::vector<uint32_t> out;
    EXPECT_EQ(decode(good_entry(), shader_cache_identity_key(id), &out), ShaderCacheReject::Identity);
    EXPECT_TRUE(out.empty());

    id = ident();
    id.features_mask ^= 1u << 12;
    EXPECT_EQ(decode(good_entry(), shader_cache_identity_key(id)), ShaderCacheReject::Identity);
}

TEST(ShaderCacheEntry, TruncatedEntryIsRejectedAtEveryLength) {
    const auto full = good_entry();
    const uint64_t key = shader_cache_identity_key(ident());
    for (size_t len = 0; len < full.size(); ++len) {
        std::vector<uint8_t> cut(full.begin(), full.begin() + len);
        std::vector<uint32_t> out;
        EXPECT_NE(decode(cut, key, &out), ShaderCacheReject::None) << "length " << len;
        EXPECT_TRUE(out.empty());
    }
    auto longer = full;
    longer.push_back(0); // trailing garbage
    EXPECT_NE(decode(longer, key), ShaderCacheReject::None);
}

TEST(ShaderCacheEntry, FlippedBitIsRejected) {
    const auto full = good_entry();
    const uint64_t key = shader_cache_identity_key(ident());
    for (size_t i = sizeof(ShaderCacheEntryHeader); i < full.size(); i += 7) {
        auto bad = full;
        bad[i] ^= 0x10;
        EXPECT_EQ(decode(bad, key), ShaderCacheReject::Corrupt) << "byte " << i;
    }
}

TEST(ShaderCacheEntry, OldUnframedSpirvIsRejectedAsOtherFormat) {
    // what the previous builds wrote: raw SPIR-V words
    const auto w = spirv();
    std::vector<uint8_t> raw(w.size() * 4);
    std::memcpy(raw.data(), w.data(), raw.size());
    EXPECT_EQ(decode(raw, shader_cache_identity_key(ident())), ShaderCacheReject::Format);
}

TEST(ShaderCacheEntry, EmptyAndHostileSizesAreRejectedWithoutOverflow) {
    const uint64_t key = shader_cache_identity_key(ident());
    EXPECT_EQ(decode({}, key), ShaderCacheReject::TooSmall);

    ShaderCacheEntryHeader h;
    h.identity_key = key;
    h.payload_size = ~0ULL; // would overflow header_size + payload_size
    std::vector<uint8_t> bytes(sizeof(h) + 16);
    std::memcpy(bytes.data(), &h, sizeof(h));
    EXPECT_EQ(decode(bytes, key), ShaderCacheReject::Truncated);

    h.payload_size = 0; // header only
    bytes.resize(sizeof(h));
    std::memcpy(bytes.data(), &h, sizeof(h));
    EXPECT_EQ(decode(bytes, key), ShaderCacheReject::Truncated);

    // intact hash but not SPIR-V
    std::vector<uint32_t> notspv(8, 0x12345678u);
    EXPECT_EQ(decode(encode_shader_cache_entry(key, notspv.data(), notspv.size()), key), ShaderCacheReject::Corrupt);
}

TEST(ShaderCacheManifest, RoundTrips) {
    std::vector<ShaderCacheManifestEntry> out;
    const auto bytes = encode_shader_cache_manifest(ident(), manifest_entries());
    ASSERT_EQ(decode_manifest(bytes, ident(), &out), ShaderCacheReject::None);
    ASSERT_EQ(out.size(), 5u);
    EXPECT_EQ(out[3].frag, manifest_entries()[3].frag);
    EXPECT_EQ(out[4].vert, manifest_entries()[4].vert);
    // an empty list is valid too
    EXPECT_EQ(decode_manifest(encode_shader_cache_manifest(ident(), {}), ident()), ShaderCacheReject::None);
}

TEST(ShaderCacheManifest, EachIdentityChangeHasItsOwnReason) {
    const auto bytes = encode_shader_cache_manifest(ident(), manifest_entries());
    auto id = ident();
    id.translator_hash ^= 0xff;
    EXPECT_EQ(decode_manifest(bytes, id), ShaderCacheReject::Translator);
    id = ident();
    id.translator_version = 33;
    EXPECT_EQ(decode_manifest(bytes, id), ShaderCacheReject::TranslatorVersion);
    id = ident();
    id.features_mask ^= 1u << 12;
    EXPECT_EQ(decode_manifest(bytes, id), ShaderCacheReject::Config);
}

TEST(ShaderCacheManifest, TruncatedOrCorruptIsRejected) {
    const auto full = encode_shader_cache_manifest(ident(), manifest_entries());
    for (size_t len = 0; len < full.size(); ++len) {
        std::vector<uint8_t> cut(full.begin(), full.begin() + len);
        EXPECT_NE(decode_manifest(cut, ident()), ShaderCacheReject::None) << "length " << len;
    }
    auto bad = full;
    bad[sizeof(ShaderCacheManifestHeader) + 10] ^= 1;
    EXPECT_EQ(decode_manifest(bad, ident()), ShaderCacheReject::Corrupt);

    // the previous builds wrote a manifest that started with a 64-bit count
    std::vector<uint8_t> old(8 + 4 + 4 + 64 * 3, 0);
    old[0] = 3;
    EXPECT_EQ(decode_manifest(old, ident()), ShaderCacheReject::Format);

    // an absurd entry count must not overflow the size check
    ShaderCacheManifestHeader h;
    h.header_size = sizeof(h);
    h.identity = ident();
    h.entry_count = ~0ULL / 32;
    std::vector<uint8_t> hostile(sizeof(h));
    std::memcpy(hostile.data(), &h, sizeof(h));
    EXPECT_EQ(decode_manifest(hostile, ident()), ShaderCacheReject::Truncated);
}

TEST(ShaderCacheReason, StringsAreStable) {
    // the lab greps "shader cache: discarded (<reason>)"
    EXPECT_STREQ(to_string(ShaderCacheReject::Translator), "shader translator changed");
    EXPECT_STREQ(to_string(ShaderCacheReject::Config), "shader config changed");
    EXPECT_STREQ(to_string(ShaderCacheReject::Truncated), "file truncated");
    EXPECT_STREQ(to_string(ShaderCacheReject::Corrupt), "payload hash mismatch");
}

TEST(ShaderCacheAtomicWrite, ReplacesTheOldFileCompletelyAndLeavesNoTemp) {
    TempDir dir;
    const auto path = dir.path / "vk32-abc.spv";
    const auto first = good_entry();
    ASSERT_TRUE(write_file_atomic(path, first));
    EXPECT_EQ(count_files(dir.path), 1u);

    auto w = spirv();
    w.resize(200, 7);
    const auto second = encode_shader_cache_entry(shader_cache_identity_key(ident()), w.data(), w.size());
    ASSERT_TRUE(write_file_atomic(path, second));
    EXPECT_EQ(count_files(dir.path), 1u);

    std::vector<uint8_t> read;
    ASSERT_TRUE(read_file_bytes(path, read));
    EXPECT_EQ(read, second);
}

TEST(ShaderCacheAtomicWrite, FailureLeavesTheOldFileUntouched) {
    TempDir dir;
    const auto path = dir.path / "entry.spv";
    const auto first = good_entry();
    ASSERT_TRUE(write_file_atomic(path, first));

    // the directory vanished (cache wiped while the game runs): the write fails and creates nothing
    EXPECT_FALSE(write_file_atomic(dir.path / "missing-dir" / "entry.spv", first));
    EXPECT_FALSE(fsx::exists(dir.path / "missing-dir"));

    // the final name is a non-empty directory: the rename fails and the temp file is cleaned up
    fsx::create_directories(dir.path / "blocked.spv" / "x");
    EXPECT_FALSE(write_file_atomic(dir.path / "blocked.spv", first));
    EXPECT_EQ(count_files(dir.path), 2u); // entry.spv + blocked.spv/

    std::vector<uint8_t> read;
    ASSERT_TRUE(read_file_bytes(path, read));
    EXPECT_EQ(read, first);
}

TEST(ShaderCacheAtomicWrite, AnInterruptedWriteIsNeverSeenAsAnEntryAndIsSwept) {
    TempDir dir;
    const auto path = dir.path / "vk32-abc.spv";
    const auto full = good_entry();
    // a crash after the first bytes of the temp file, before the rename
    {
        std::ofstream os(dir.path / ("vk32-abc.spv" + std::string(shader_cache_temp_marker) + "1-0"), std::ios::binary);
        os.write(reinterpret_cast<const char *>(full.data()), static_cast<std::streamsize>(full.size() / 2));
    }
    std::vector<uint8_t> read;
    EXPECT_FALSE(read_file_bytes(path, read)); // the entry itself does not exist: a plain cache miss
    ASSERT_TRUE(write_file_atomic(dir.path / "vk32-keep.spv", full));

    EXPECT_EQ(remove_stale_shader_cache_temp_files(dir.path), 1u);
    EXPECT_EQ(count_files(dir.path), 1u);
    EXPECT_TRUE(fsx::exists(dir.path / "vk32-keep.spv"));
}

TEST(ShaderCacheAtomicWrite, ATruncatedFileOnDiskIsRejectedWhenRead) {
    // what a torn write on a file system without ordered data would leave behind
    TempDir dir;
    const auto path = dir.path / "vk32-abc.spv";
    const auto full = good_entry();
    ASSERT_TRUE(write_file_atomic(path, full));
    fsx::resize_file(path, full.size() - 5);
    std::vector<uint8_t> read;
    ASSERT_TRUE(read_file_bytes(path, read));
    EXPECT_EQ(decode(read, shader_cache_identity_key(ident())), ShaderCacheReject::Truncated);
}

TEST(ShaderCacheWarmupGuard, SkipsAfterTwoUncleanRunsAndRecoversWhenCleared) {
    TempDir dir;
    const auto marker = dir.path / shader_cache_warmup_marker_name;
    uint32_t unclean = 99;
    EXPECT_TRUE(begin_shader_cache_warmup(marker, &unclean)); // first run, no marker
    EXPECT_EQ(unclean, 0u);
    EXPECT_TRUE(begin_shader_cache_warmup(marker, &unclean)); // the first one never finished
    EXPECT_EQ(unclean, 1u);
    EXPECT_FALSE(begin_shader_cache_warmup(marker, &unclean)); // two unclean runs: disabled
    EXPECT_EQ(unclean, 2u);
    EXPECT_FALSE(begin_shader_cache_warmup(marker)); // stays disabled
    // wiping the cache (translator change) removes the marker
    end_shader_cache_warmup(marker);
    EXPECT_TRUE(begin_shader_cache_warmup(marker));
}

TEST(ShaderCacheWarmupGuard, CleanRunsNeverAccumulate) {
    TempDir dir;
    const auto marker = dir.path / shader_cache_warmup_marker_name;
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(begin_shader_cache_warmup(marker));
        end_shader_cache_warmup(marker);
    }
    EXPECT_EQ(count_files(dir.path), 0u);
}

TEST(ShaderCacheWarmupGuard, GarbageMarkerCountsAsClean) {
    TempDir dir;
    const auto marker = dir.path / shader_cache_warmup_marker_name;
    const uint8_t junk[8] = { 9, 0, 0, 0, 1, 2, 3, 4 };
    ASSERT_TRUE(write_file_atomic(marker, junk, sizeof(junk)));
    EXPECT_TRUE(begin_shader_cache_warmup(marker));
}

TEST(ShaderCacheWriter, WritesInTheBackgroundAndFlushWaits) {
    TempDir dir;
    {
        ShaderCacheWriter writer;
        for (int i = 0; i < 50; ++i)
            writer.enqueue(dir.path / ("e" + std::to_string(i) + ".spv"), good_entry());
        writer.flush();
        EXPECT_EQ(count_files(dir.path), 50u);
        EXPECT_EQ(writer.failed_writes(), 0u);
        writer.enqueue(dir.path / "missing" / "x.spv", good_entry()); // counted, never throws
        writer.flush();
        EXPECT_EQ(writer.failed_writes(), 1u);
        writer.enqueue(dir.path / "last.spv", good_entry()); // the destructor drains the queue
    }
    EXPECT_EQ(count_files(dir.path), 51u);
}
