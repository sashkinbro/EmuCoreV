// Host tests for the firmware installer (B030-B035, B037 native side).
//
//   fw_tests --core <CORE_PSP2UPDAT.PUP> --fonts <PSP2UPDAT.PUP> --work <scratch dir> [--reference <tree>]
//   fw_tests --install <pup> <vita_root>          (measurement mode: version, peak RSS, peak disk)
//
// The real 3.74 PUPs are not part of the repository. `--reference` is an optional tree produced by the pre-change
// vendored installer (install_pup of CORE then FONTS); without it the vendored install_pup linked into this binary
// (pup.cpp is unchanged) builds the reference.
#include "measure.h"

#include <emucorev/firmware_installer.h>
#include <packages/functions.h>
#include <util/fs.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fw = emucorev::firmware;

static int g_checks = 0, g_failures = 0;
static std::string g_test;
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            g_failures++;                                                                          \
            std::printf("  FAIL [%s] %s:%d: %s\n", g_test.c_str(), __FILE__, __LINE__, #cond);   \
        }                                                                                          \
    } while (0)
#define CHECK_STATUS(result, expected)                                                                                                       \
    do {                                                                                                                                     \
        g_checks++;                                                                                                                          \
        if ((result).status != (expected)) {                                                                                                 \
            g_failures++;                                                                                                                    \
            std::printf("  FAIL [%s] %s:%d: status %s (%s), expected %s\n", g_test.c_str(), __FILE__, __LINE__, fw::status_name((result).status), \
                (result).detail.c_str(), fw::status_name(expected));                                                                         \
        }                                                                                                                                    \
    } while (0)

struct FileInfo {
    uint64_t size;
    uint64_t hash;
    bool operator==(const FileInfo &o) const { return size == o.size && hash == o.hash; }
};
using Tree = std::map<std::string, FileInfo>;

// `strip_cr`: the vendored installer writes system-packages/*.version in text mode (CRLF on a Windows host, LF on Android).
static uint64_t hash_file(const fs::path &p, uint64_t &size, bool strip_cr = false) {
    std::ifstream in(p.string(), std::ios::binary);
    std::vector<char> buf(1 << 20);
    uint64_t h = 1469598103934665603ull;
    size = 0;
    while (in) {
        in.read(buf.data(), buf.size());
        const auto n = in.gcount();
        for (std::streamsize i = 0; i < n; i++) {
            if (strip_cr && buf[i] == '\r')
                continue;
            h = (h ^ static_cast<uint8_t>(buf[i])) * 1099511628211ull;
            size++;
        }
    }
    return h;
}

// Snapshot of every regular file below `root` (relative, '/'-separated); `skip_top` names top-level entries to ignore.
static Tree snapshot(const fs::path &root, const std::set<std::string> &skip_top = {}) {
    Tree t;
    if (!fs::exists(root))
        return t;
    const std::string prefix = root.generic_string() + "/";
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (!fs::is_regular_file(it->path()))
            continue;
        std::string rel = it->path().generic_string().substr(prefix.size());
        if (skip_top.count(rel.substr(0, rel.find('/'))))
            continue;
        FileInfo info{};
        info.hash = hash_file(it->path(), info.size, rel.rfind("system-packages/", 0) == 0);
        t[rel] = info;
    }
    return t;
}

static std::vector<std::string> top_level(const fs::path &root) {
    std::vector<std::string> names;
    if (fs::exists(root))
        for (fs::directory_iterator it(root), end; it != end; ++it)
            names.push_back(it->path().filename().string());
    std::sort(names.begin(), names.end());
    return names;
}

static bool no_leftovers(const fs::path &root) {
    return !fs::exists(root / "PUP_DEC") && !fs::exists(root / ".fw_stage") && !fs::exists(root / ".fw_old");
}

static std::string read_all(const fs::path &p) {
    std::ifstream in(p.string(), std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void copy_prefix(const fs::path &src, const fs::path &dst, uint64_t bytes) {
    std::ifstream in(src.string(), std::ios::binary);
    std::ofstream out(dst.string(), std::ios::binary | std::ios::trunc);
    std::vector<char> buf(1 << 20);
    while (bytes && in) {
        in.read(buf.data(), static_cast<std::streamsize>(std::min<uint64_t>(bytes, buf.size())));
        const auto n = in.gcount();
        out.write(buf.data(), n);
        bytes -= static_cast<uint64_t>(n);
    }
}

static void put(std::fstream &f, uint64_t offset, const void *data, size_t n) {
    f.seekp(static_cast<std::streamoff>(offset));
    f.write(static_cast<const char *>(data), static_cast<std::streamsize>(n));
}

// Full copy of `src` with `patch(file)` applied.
template <typename Patch>
static fs::path patched_copy(const fs::path &src, const fs::path &dst, Patch patch) {
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
    std::fstream f(dst.string(), std::ios::in | std::ios::out | std::ios::binary);
    patch(f);
    f.close();
    return dst;
}

static uint64_t record_field(const fs::path &pup, uint32_t index, int field_offset) {
    std::ifstream in(pup.string(), std::ios::binary);
    in.seekg(0x80 + index * 0x20 + field_offset);
    uint64_t v = 0;
    in.read(reinterpret_cast<char *>(&v), 8);
    return v;
}

struct Ctx {
    fs::path core, fonts, reference, work;
} ctx;

static fs::path fresh(const std::string &name) {
    const fs::path dir = ctx.work / name;
    boost::system::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

static void expect_clean_failure(const fs::path &root, const Tree &before, const std::set<std::string> &skip = {}) {
    CHECK(no_leftovers(root));
    CHECK(snapshot(root, skip) == before);
}

// ---------------------------------------------------------------------------------------------------------------------

static Tree g_reference; // vendored install_pup, CORE then FONTS (tree dirs only)

static void build_reference() {
    if (!ctx.reference.empty()) {
        g_reference = snapshot(ctx.reference);
        std::printf("reference: %zu files from %s\n", g_reference.size(), ctx.reference.string().c_str());
        return;
    }
    const fs::path ref = fresh("reference");
    install_pup(ref, ctx.core);
    install_pup(ref, ctx.fonts);
    g_reference = snapshot(ref);
    std::printf("reference: %zu files from the vendored install_pup\n", g_reference.size());
}

static void test_install_matches_reference() {
    const fs::path root = fresh("t_full");
    std::vector<fw::Progress> events;
    fw::Options opts;
    opts.progress = [&](const fw::Progress &p) { events.push_back(p); };
    auto r1 = fw::install(root, ctx.core, opts);
    CHECK_STATUS(r1, fw::Status::Ok);
    CHECK(r1.version == "3.74");
    CHECK(r1.files.count("vs0") && r1.files.count("os0") && !r1.files.count("sa0"));
    CHECK(no_leftovers(root));
    // progress: monotonic, ends at 100, covers the phases
    float last = -1;
    bool mono = true, saw_decrypt = false, saw_extract = false, saw_commit = false;
    for (const auto &e : events) {
        mono = mono && e.percent >= last;
        last = e.percent;
        saw_decrypt |= e.phase == fw::Phase::Decrypt;
        saw_extract |= e.phase == fw::Phase::Extract;
        saw_commit |= e.phase == fw::Phase::Commit;
    }
    CHECK(mono && saw_decrypt && saw_extract && saw_commit && last == 100.f);
    CHECK(events.size() > 20 && events.size() < 5000);
    CHECK(!events.empty() && events.back().files == 69 + 1473);

    fw::Options none;
    auto r2 = fw::install(root, ctx.fonts, none);
    CHECK_STATUS(r2, fw::Status::Ok);
    CHECK(r2.files.count("sa0") && !r2.files.count("vs0"));
    CHECK(no_leftovers(root));

    // key files
    CHECK(fs::is_regular_file(root / "vs0/vsh/shell/shell.self"));
    CHECK(fs::is_regular_file(root / "sa0/data/font/pvf/ltn0.pvf"));
    // marker
    const std::string marker = read_all(root / "fw.complete");
    CHECK(marker.rfind("emucorev-firmware-marker 1\n", 0) == 0);
    CHECK(marker.find("\nversion=3.74\n") != std::string::npos);
    CHECK(marker.find("\nvs0.files=1473\n") != std::string::npos);
    CHECK(marker.find("\nos0.files=69\n") != std::string::npos);
    CHECK(marker.find("\nsa0.files=178\n") != std::string::npos);

    // tree identical to the reference install (marker is new, everything else must match byte for byte)
    const Tree got = snapshot(root, { "fw.complete" });
    CHECK(got.size() == g_reference.size());
    CHECK(got == g_reference);
    if (got != g_reference) {
        int shown = 0;
        for (const auto &[k, v] : g_reference) {
            auto it = got.find(k);
            if ((it == got.end() || !(it->second == v)) && shown++ < 5)
                std::printf("    differs: %s\n", k.c_str());
        }
    }
    // top level exactly the partitions + marker + system-packages
    CHECK((top_level(root) == std::vector<std::string>{ "fw.complete", "os0", "sa0", "system-packages", "vs0" }));
}

static void test_reinstall_same_process() {
    // B033: the vendored make_filename() kept a function-static counter across installs; a second install in the
    // same process must give the identical tree.
    const fs::path root = fresh("t_reinstall");
    CHECK_STATUS(fw::install(root, ctx.core), fw::Status::Ok);
    CHECK_STATUS(fw::install(root, ctx.fonts), fw::Status::Ok);
    const Tree first = snapshot(root);
    CHECK_STATUS(fw::install(root, ctx.core), fw::Status::Ok);
    CHECK(no_leftovers(root));
    CHECK(snapshot(root) == first);
}

static void test_not_a_pup() {
    const fs::path root = fresh("t_notpup");
    const fs::path f = ctx.work / "notpup.bin";
    {
        std::ofstream o(f.string(), std::ios::binary);
        std::string junk(4096, '\0');
        o.write(junk.data(), junk.size());
    }
    auto r = fw::install(root, f);
    CHECK_STATUS(r, fw::Status::NotPup);
    CHECK(no_leftovers(root));
    // empty file
    { std::ofstream o(f.string(), std::ios::binary | std::ios::trunc); }
    CHECK_STATUS(fw::install(root, f), fw::Status::NotPup);
    // magic broken in an otherwise real package
    const fs::path bad = patched_copy(ctx.fonts, ctx.work / "badmagic.pup", [](std::fstream &s) { put(s, 0, "SCEXF", 5); });
    CHECK_STATUS(fw::install(root, bad), fw::Status::NotPup);
    // missing file
    CHECK_STATUS(fw::install(root, ctx.work / "does-not-exist.pup"), fw::Status::IoError);
    CHECK(no_leftovers(root));
    CHECK(top_level(root).empty());
    boost::system::error_code ec;
    fs::remove(bad, ec);
}

static void test_truncated() {
    const fs::path root = fresh("t_trunc");
    const uint64_t size = fs::file_size(ctx.core);
    for (uint64_t cut : { size * 6 / 10, size - 1, uint64_t(0x80 + 10), uint64_t(0x400) }) {
        const fs::path cutfile = ctx.work / "trunc.pup";
        copy_prefix(ctx.core, cutfile, cut);
        auto r = fw::install(root, cutfile);
        CHECK_STATUS(r, fw::Status::Truncated);
        CHECK(no_leftovers(root));
        CHECK(top_level(root).empty());
    }
    // only the first 5 bytes: still recognisably a PUP header start
    const fs::path five = ctx.work / "trunc5.pup";
    copy_prefix(ctx.core, five, 5);
    CHECK_STATUS(fw::install(root, five), fw::Status::Truncated);
}

static void test_bad_records() {
    const fs::path root = fresh("t_badrec");
    const uint64_t size = fs::file_size(ctx.fonts);
    const uint32_t victim = 5; // a deflated sa0 package record
    const uint64_t off_field = 0x80 + victim * 0x20 + 8, len_field = 0x80 + victim * 0x20 + 16;
    const fs::path p = ctx.work / "badrec.pup";

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = 0x10; put(s, off_field, &v, 8); });
    auto r = fw::install(root, p);
    CHECK_STATUS(r, fw::Status::Corrupt); // offset inside the header
    CHECK(no_leftovers(root) && top_level(root).empty());

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = size + 4096; put(s, off_field, &v, 8); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Truncated); // offset past the end

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = ~0ull - 100; put(s, off_field, &v, 8); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Truncated); // offset near 2^64 must not wrap

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = size; put(s, len_field, &v, 8); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Truncated); // offset + length past the end

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = ~0ull; put(s, len_field, &v, 8); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Truncated); // length overflow

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint32_t v = 0xFFFFFFFFu; put(s, 0x18, &v, 4); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Corrupt); // absurd record count (was a huge allocation/loop)

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint32_t v = 0; put(s, 0x18, &v, 4); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Corrupt);

    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint32_t v = 0x2000; put(s, 0x18, &v, 4); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Corrupt);

    // SCE header length of the package record (offset 16 inside the record's data) beyond the record
    const uint64_t rec_off = record_field(ctx.fonts, victim, 8);
    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = 0x7fffffffull; put(s, rec_off + 16, &v, 8); });
    CHECK_STATUS(fw::install(root, p), fw::Status::Corrupt);
    patched_copy(ctx.fonts, p, [&](std::fstream &s) { uint64_t v = 0x40; put(s, rec_off + 16, &v, 8); }); // too small for the metadata
    auto small = fw::install(root, p);
    CHECK(small.status == fw::Status::Corrupt);

    // damaged (encrypted) metadata: garbage after decryption must be caught by the bounds checks, not crash
    std::ifstream meta_in(ctx.fonts.string(), std::ios::binary);
    meta_in.seekg(static_cast<std::streamoff>(rec_off + 12));
    uint32_t metadata_offset = 0;
    meta_in.read(reinterpret_cast<char *>(&metadata_offset), 4);
    meta_in.close();
    for (uint64_t at : { uint64_t(metadata_offset + 48), uint64_t(metadata_offset + 48 + 64), uint64_t(metadata_offset + 48 + 100) }) {
        patched_copy(ctx.fonts, p, [&](std::fstream &s) {
            std::vector<char> junk(96);
            for (size_t i = 0; i < junk.size(); i++)
                junk[i] = static_cast<char>(0xA5 ^ (i * 37));
            put(s, rec_off + at, junk.data(), junk.size());
        });
        auto bad = fw::install(root, p);
        CHECK(bad.status == fw::Status::Corrupt || bad.status == fw::Status::Truncated);
        CHECK(no_leftovers(root) && top_level(root).empty());
    }

    // damaged deflate payload
    patched_copy(ctx.fonts, p, [&](std::fstream &s) {
        std::vector<char> junk(256, '\x5a');
        put(s, rec_off + 1152 + 4096, junk.data(), junk.size());
    });
    auto infl = fw::install(root, p);
    CHECK_STATUS(infl, fw::Status::Corrupt);
    CHECK(no_leftovers(root) && top_level(root).empty());
    boost::system::error_code ec;
    fs::remove(p, ec);
}

static void test_failure_keeps_old_tree() {
    const fs::path root = fresh("t_oldtree");
    CHECK_STATUS(fw::install(root, ctx.core), fw::Status::Ok);
    CHECK_STATUS(fw::install(root, ctx.fonts), fw::Status::Ok);
    const Tree before = snapshot(root);

    // (a) exception thrown by the progress callback in the middle of the extraction
    {
        fw::Options o;
        int extract_events = 0;
        o.progress = [&](const fw::Progress &p) {
            if (p.phase == fw::Phase::Extract && ++extract_events == 20)
                throw std::logic_error("injected failure");
        };
        auto r = fw::install(root, ctx.core, o);
        CHECK_STATUS(r, fw::Status::Internal);
        expect_clean_failure(root, before);
    }
    // (b) user cancel during decrypt / extract / just before the swap
    for (fw::Phase phase : { fw::Phase::Decrypt, fw::Phase::Extract, fw::Phase::Commit }) {
        std::atomic<bool> cancel{ false };
        fw::Options o;
        o.progress = [&](const fw::Progress &p) {
            if (p.phase == phase && (phase != fw::Phase::Decrypt || p.percent > 20.f) && (phase != fw::Phase::Extract || p.percent > 60.f))
                cancel = true;
        };
        o.cancelled = [&] { return cancel.load(); };
        auto r = fw::install(root, ctx.core, o);
        CHECK_STATUS(r, fw::Status::Cancelled);
        expect_clean_failure(root, before);
    }
    // (c) a write error in the middle of the extraction (a directory squats on an output file name)
    {
        fw::Options o;
        bool armed = false;
        o.progress = [&](const fw::Progress &p) {
            if (p.phase != fw::Phase::Extract || armed)
                return;
            armed = true;
            for (const auto &[rel, info] : g_reference) {
                if (rel.rfind("os0/", 0) == 0) {
                    boost::system::error_code ec;
                    fs::create_directories(root / ".fw_stage" / rel, ec);
                }
            }
        };
        auto r = fw::install(root, ctx.core, o);
        CHECK_STATUS(r, fw::Status::IoError);
        expect_clean_failure(root, before);
    }
    // (d) failure after the partitions were swapped (the marker cannot be replaced): everything is rolled back
    {
        const fs::path root2 = fresh("t_oldtree_commit");
        CHECK_STATUS(fw::install(root2, ctx.core), fw::Status::Ok);
        const Tree snap = snapshot(root2);
        fs::remove(root2 / "fw.complete");
        fs::create_directories(root2 / "fw.complete");
        { std::ofstream(( root2 / "fw.complete" / "blocker").string()) << "x"; }
        const Tree with_blocker = snapshot(root2);
        auto r = fw::install(root2, ctx.core);
        CHECK(r.status == fw::Status::IoError);
        CHECK(no_leftovers(root2));
        CHECK(snapshot(root2) == with_blocker);
    }
    // (e) not enough space (injected): nothing is written
    {
        fw::Options o;
        o.available_bytes = [] { return uint64_t(10) << 20; };
        auto r = fw::install(root, ctx.core, o);
        CHECK_STATUS(r, fw::Status::NoSpace);
        CHECK(r.required_bytes > (100ull << 20));
        expect_clean_failure(root, before);
    }
    // (f) a stale staging dir / legacy PUP_DEC from an earlier crash is cleaned up by the next install
    {
        fs::create_directories(root / "PUP_DEC" / "PUP");
        fs::create_directories(root / ".fw_stage" / "vs0");
        CHECK_STATUS(fw::install(root, ctx.fonts), fw::Status::Ok);
        CHECK(no_leftovers(root));
        CHECK(snapshot(root) == before);
    }
}

static void test_recover_interrupted() {
    const fs::path root = fresh("t_recover");
    CHECK_STATUS(fw::install(root, ctx.core), fw::Status::Ok);
    const Tree before = snapshot(root);
    // simulate a crash between "old vs0 moved away" and "new vs0 moved in"
    fs::create_directories(root / ".fw_old");
    fs::rename(root / "vs0", root / ".fw_old" / "vs0");
    fw::recover_interrupted(root);
    CHECK(no_leftovers(root));
    CHECK(snapshot(root) == before);
    // crash after the swap completed but before cleanup: new trees stay
    fs::create_directories(root / ".fw_old" / "vs0");
    { std::ofstream((root / ".fw_old" / ".committed").string()) << "1\n"; }
    fw::recover_interrupted(root);
    CHECK(no_leftovers(root));
    CHECK(snapshot(root) == before);
}

static int measure_mode(const char *pup, const char *root_arg) {
    const fs::path root = fs_utils::utf8_to_path(root_arg);
    fs::create_directories(root);
    DiskPeakSampler sampler(root);
    const auto r = fw::install(root, fs_utils::utf8_to_path(pup));
    std::printf("status=%s\nversion=%s\ndetail=%s\n", fw::status_name(r.status), r.version.c_str(), r.detail.c_str());
    std::printf("peak_rss_bytes=%llu\npeak_disk_bytes=%llu\n", peak_rss_bytes(), static_cast<unsigned long long>(sampler.stop()));
    return r.status == fw::Status::Ok ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc >= 4 && std::string(argv[1]) == "--install")
        return measure_mode(argv[2], argv[3]);
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--core") ctx.core = fs_utils::utf8_to_path(argv[i + 1]);
        else if (a == "--fonts") ctx.fonts = fs_utils::utf8_to_path(argv[i + 1]);
        else if (a == "--work") ctx.work = fs_utils::utf8_to_path(argv[i + 1]);
        else if (a == "--reference") ctx.reference = fs_utils::utf8_to_path(argv[i + 1]);
    }
    if (ctx.core.empty() || ctx.fonts.empty() || ctx.work.empty()) {
        std::fprintf(stderr, "usage: fw_tests --core <pup> --fonts <pup> --work <dir> [--reference <tree>]\n");
        return 2;
    }
    fs::create_directories(ctx.work);

    struct T {
        const char *name;
        void (*fn)();
    } tests[] = {
        { "install_matches_reference", test_install_matches_reference },
        { "reinstall_same_process", test_reinstall_same_process },
        { "not_a_pup", test_not_a_pup },
        { "truncated", test_truncated },
        { "bad_records", test_bad_records },
        { "failure_keeps_old_tree", test_failure_keeps_old_tree },
        { "recover_interrupted", test_recover_interrupted },
    };
    build_reference();
    for (const auto &t : tests) {
        g_test = t.name;
        const int before = g_failures;
        t.fn();
        std::printf("%s %s\n", g_failures == before ? "PASS" : "FAIL", t.name);
        std::fflush(stdout);
    }
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
