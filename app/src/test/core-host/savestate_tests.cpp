// Host tests for the save-state fixes B013 (deferred GXM ring cursor), B019 (stale surface-cache
// entries), B018 (open savedata files missing on the loading machine) and the engine 20 layout change
// (older saves are rejected by version, never misread; GXM/GPU codec round trip; raw layout guard).
//
// They run the real validation/parse code of the adapter against real save files. Artifact and library
// directories are taken from EMUCOREV_LAB (default C:/Users/SBRO/emucorev-lab); a test whose input is
// absent is skipped, so the target also builds and runs on a machine without the lab data.
//
// savestate.cpp is included to reach the file-local write_io/read_io and parse_io_state.

#include <gtest/gtest.h>

#include "../../main/cpp/emucorev/src/savestate/savestate.cpp"

#include <atomic>
#include <cstdlib>
#include <thread>
#include <fstream>

namespace {

namespace ss = emucorev::savestate;
namespace iof = emucorev::savestate::io_files;

fs::path lab_root() {
    const char *env = std::getenv("EMUCOREV_LAB");
    return env ? fs::path(env) : fs::path("C:/Users/SBRO/emucorev-lab");
}

fs::path scratch_dir(const std::string &name) {
    fs::path dir = fs::temp_directory_path() / ("ecv-savestate-test-" + name);
    boost::system::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

// A save's GXM/GPU sections plus the saved guest memory index, parsed with the production code.
struct LoadedState {
    ss::Reader reader;
    std::unique_ptr<ss::MemoryImage> memory;
    ss::GxmSnapshot gxm;
    ss::GpuSnapshot gpu;
    std::string error;
    // Set when the file was written by another engine version: Load() rejects it before parsing.
    bool other_engine = false;

    bool load(const fs::path &path, const std::string &scratch) {
        if (!reader.open(path, error) || !reader.validate_sections(error))
            return false;
        std::vector<uint8_t> meta_data;
        ss::Meta meta;
        if (!reader.read_section(ss::SectionId::Meta, meta_data, error) || !ss::read_meta(meta_data, meta))
            return false;
        if (!ss::supports_engine_version(meta.engine_version)) {
            other_engine = true;
            error = ss::unsupported_engine_version_reason(meta.engine_version);
            return false;
        }
        memory = ss::MemoryImage::preflight(reader, scratch_dir(scratch), error);
        if (!memory)
            return false;
        std::vector<uint8_t> gxm_data, gpu_data;
        return reader.read_section(ss::SectionId::Gxm, gxm_data, error)
            && reader.read_section(ss::SectionId::Gpu, gpu_data, error)
            && ss::parse_gpu_state(gpu_data, gpu, error)
            && ss::parse_gxm_state(gxm_data, gxm, error);
    }
};

// What Load() does for the GXM/GPU part, in the same order.
bool validate_like_load(LoadedState &state, size_t *dropped = nullptr) {
    const size_t count = ss::drop_unmapped_surface_cache_entries(state.gpu.surfaces,
        [&](uint64_t address, uint64_t size) { return state.memory->contains(address, size); });
    if (dropped)
        *dropped = count;
    return ss::validate_gxm_state(state.gxm, *state.memory, state.error)
        && ss::validate_gpu_state(state.gpu, *state.memory, state.error);
}

} // namespace

// Artifacts recorded by an older engine document fixed bugs; their layout is no longer readable, so the
// version gate (tested in SaveStateVersion) is what Load() applies to them.
#define LOAD_OR_SKIP_OTHER_ENGINE(state, path, scratch)                                    \
    do {                                                                                   \
        const bool loaded_ = (state).load((path), (scratch));                              \
        if (!loaded_ && (state).other_engine)                                              \
            GTEST_SKIP() << (path).string() << ": " << (state).error;                      \
        ASSERT_TRUE(loaded_) << (path).string() << ": " << (state).error;                 \
    } while (0)

// ---- B013 -----------------------------------------------------------------------------------

TEST(SaveStateB013, TearawayArtifactsValidate) {
    for (const char *name : { "anbernic-gameplay1-restorefail.ecvs", "anbernic-gameplay1b-restorefail.ecvs" }) {
        const fs::path path = lab_root() / "saves-broken/PCSA00144" / name;
        if (!fs::exists(path))
            GTEST_SKIP() << "missing artifact " << path.string();
        LoadedState state;
        LOAD_OR_SKIP_OTHER_ENGINE(state, path, "b013");
        EXPECT_TRUE(validate_like_load(state)) << name << ": " << state.error;
    }
}

TEST(SaveStateB013, IdleDeferredContextKeepsCursorAfterRingWasReleased) {
    const fs::path path = lab_root() / "saves-broken/PCSA00144/anbernic-gameplay1-restorefail.ecvs";
    if (!fs::exists(path))
        GTEST_SKIP() << "missing artifact";
    LoadedState state;
    LOAD_OR_SKIP_OTHER_ENGINE(state, path, "b013b");
    // The root cause: deferred contexts that are not recording have ring size 0 but a nonzero cursor.
    size_t idle_with_cursor = 0;
    for (const auto &context : state.gxm.contexts)
        if (context.deferred && !context.state.active
            && context.state.vertex_ring_buffer_used > context.state.vertex_ring_buffer_size)
            ++idle_with_cursor;
    EXPECT_GE(idle_with_cursor, 1u);
    ASSERT_TRUE(validate_like_load(state)) << state.error;

    // An immediate context owns a fixed ring: a cursor past its end is still corruption.
    for (auto &context : state.gxm.contexts) {
        if (context.deferred)
            continue;
        context.state.vertex_ring_buffer_used = context.state.vertex_ring_buffer_size + 4;
        EXPECT_FALSE(ss::validate_gxm_state(state.gxm, *state.memory, state.error));
        EXPECT_EQ(state.error, "invalid saved GXM context references");
        context.state.vertex_ring_buffer_used = 0;
    }
    EXPECT_TRUE(ss::validate_gxm_state(state.gxm, *state.memory, state.error)) << state.error;
}

// ---- B019 -----------------------------------------------------------------------------------

TEST(SaveStateB019, KillzoneArtifactHadDanglingSurfaceEntries) {
    const fs::path path = lab_root() / "saves-broken/PCSA00107/lenovo-title.BAD-restore-fail.ecvs";
    if (!fs::exists(path))
        GTEST_SKIP() << "missing artifact " << path.string();
    LoadedState state;
    LOAD_OR_SKIP_OTHER_ENGINE(state, path, "b019");

    // Without the fix: the exact failure from the device log.
    ASSERT_TRUE(ss::validate_gxm_state(state.gxm, *state.memory, state.error)) << state.error;
    EXPECT_FALSE(ss::validate_gpu_state(state.gpu, *state.memory, state.error));
    EXPECT_EQ(state.error, "invalid GPU snapshot references: color surface is outside saved memory at 0x664F8000");

    // The two LRU entries (slots 2 and 3, last rendered thousands of frames ago) point at freed memory.
    size_t dropped = 0;
    ASSERT_TRUE(validate_like_load(state, &dropped)) << state.error;
    EXPECT_EQ(dropped, 1u); // only slot 3 is not fully mapped; slot 2 still lies inside a live block
    EXPECT_FALSE(state.gpu.surfaces.colors[3].active);
    EXPECT_TRUE(state.gpu.surfaces.colors[2].active);
    EXPECT_EQ(state.gpu.surfaces.last_written_color_slot, 1);
}

TEST(SaveStateB019, DropKeepsMappedAndResetsLastWritten) {
    renderer::vulkan::SurfaceCacheSnapshot surfaces;
    surfaces.colors.resize(renderer::vulkan::surface_cache_snapshot_slots);
    surfaces.depth_stencil.resize(renderer::vulkan::surface_cache_snapshot_slots);
    surfaces.colors[0].active = true;
    surfaces.colors[0].guest_address = 0x60000000;
    surfaces.colors[0].total_bytes = 0x1000;
    surfaces.colors[1].active = true;
    surfaces.colors[1].guest_address = 0x70000000;
    surfaces.colors[1].total_bytes = 0x1000;
    surfaces.last_written_color_slot = 1;
    surfaces.depth_stencil[4].active = true;
    surfaces.depth_stencil[4].surface.depth_data = Ptr<void>(0x70001000);
    surfaces.depth_stencil[5].active = true;
    surfaces.depth_stencil[5].surface.depth_data = Ptr<void>(0x70002000);
    surfaces.pending_depth_stencil_slot = 5;
    const auto mapped = [](uint64_t address, uint64_t size) {
        return address >= 0x60000000 && address + size <= 0x60100000;
    };
    EXPECT_EQ(ss::drop_unmapped_surface_cache_entries(surfaces, mapped), 2u);
    EXPECT_TRUE(surfaces.colors[0].active);
    EXPECT_FALSE(surfaces.colors[1].active);
    EXPECT_EQ(surfaces.last_written_color_slot, 0xFF);
    EXPECT_FALSE(surfaces.depth_stencil[4].active);
    // The pending depth scene is never dropped: that would leave a dangling association.
    EXPECT_TRUE(surfaces.depth_stencil[5].active);
    EXPECT_EQ(ss::drop_unmapped_surface_cache_entries(surfaces, mapped), 0u);
}

// ---- Library regression -----------------------------------------------------------------------

TEST(SaveStateLibrary, EveryRecordedSaveStillValidatesAndNothingIsDropped) {
    const fs::path root = lab_root() / "saves";
    if (!fs::exists(root))
        GTEST_SKIP() << "missing library " << root.string();
    size_t checked = 0, rejected = 0;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->path().extension() != ".ecvs")
            continue;
        // A save of another engine version must be refused up front with the version reason.
        const ss::Result inspected = ss::inspect_state(it->path());
        if (inspected.status == ss::Status::UnsupportedVersion) {
            EXPECT_NE(inspected.error.find("build (engine version"), std::string::npos) << it->path().string();
            ++rejected;
            continue;
        }
        LoadedState state;
        ASSERT_TRUE(state.load(it->path(), "library")) << it->path().string() << ": " << state.error;
        size_t dropped = 0;
        EXPECT_TRUE(validate_like_load(state, &dropped)) << it->path().string() << ": " << state.error;
        EXPECT_EQ(dropped, 0u) << it->path().string();
        ++checked;
    }
    std::printf("[library] %zu saves validated, %zu rejected by engine version\n", checked, rejected);
    EXPECT_GT(checked + rejected, 0u);
}

// ---- B018 -----------------------------------------------------------------------------------

namespace {

constexpr const char *kSaveFile = "ux0:/user/00/savedata/PCSE00537/JETPACK.SAV";

// EmuEnvState is far too large to construct on the host. write_io/read_io only touch `io` and
// `vita_fs_path`, so build exactly those two members inside zeroed storage.
struct FakeEnv {
    FakeEnv(const fs::path &vita_fs) {
        storage.assign(sizeof(EmuEnvState), 0);
        emuenv = reinterpret_cast<EmuEnvState *>(storage.data());
        new (&emuenv->vita_fs_path) fs::path(vita_fs);
        *reinterpret_cast<IOState **>(reinterpret_cast<char *>(emuenv) + offsetof(EmuEnvState, io)) = &io;
    }
    ~FakeEnv() { emuenv->vita_fs_path.~path(); }
    std::vector<char> storage;
    EmuEnvState *emuenv = nullptr;
    IOState io;
};

std::vector<uint8_t> bytes_of(const std::string &text) { return { text.begin(), text.end() }; }

std::vector<uint8_t> read_all(const fs::path &path) {
    std::vector<uint8_t> data;
    iof::read_host_file(path, data);
    return data;
}

fs::path host_file(const fs::path &vita_fs, const char *guest) {
    return vita_fs / "ux0" / (std::string(guest).substr(5));
}

// Save the IO section of a machine that has `path` open (positioned inside the content).
std::vector<uint8_t> save_io_with_open_file(const fs::path &device_a, const char *path, const std::string &content, int64_t position,
    int32_t mode = SCE_O_WRONLY | SCE_O_CREAT) {
    FakeEnv env(device_a);
    const SceUID fd = open_file(env.io, path, mode, device_a, "test");
    EXPECT_GE(fd, 0);
    auto &file = env.io.std_files.at(fd);
    file.write(content.data(), static_cast<SceSize>(content.size()), 1);
    file.seek(position, SCE_SEEK_SET);

    const fs::path out = device_a / "io.ecvs";
    ss::Writer writer;
    std::string error;
    EXPECT_TRUE(writer.open(out, error)) << error;
    EXPECT_TRUE(ss::write_io(*env.emuenv, writer, error)) << error;
    EXPECT_TRUE(writer.finalize(error)) << error;
    ss::Reader reader;
    std::vector<uint8_t> section;
    EXPECT_TRUE(reader.open(out, error)) << error;
    EXPECT_TRUE(reader.read_section(ss::SectionId::Io, section, error)) << error;
    env.io.std_files.clear();
    return section;
}

} // namespace

namespace {
// Writes a one-byte-marker state to `slot`; returns true when finalize succeeded.
bool write_marker_state(const fs::path &slot, uint8_t marker, size_t padding = 0) {
    ss::Writer writer;
    std::string error;
    std::vector<uint8_t> payload(1 + padding, marker);
    return writer.open(slot, error) && writer.write_section(ss::SectionId::Meta, payload.data(), payload.size(), false, error)
        && writer.finalize(error);
}
int marker_of(const fs::path &file) {
    ss::Reader reader;
    std::string error;
    std::vector<uint8_t> section;
    if (!reader.open(file, error) || !reader.read_section(ss::SectionId::Meta, section, error) || section.empty())
        return -1;
    return section[0];
}
size_t count_files(const fs::path &dir) {
    size_t files = 0;
    for (const auto &entry : fs::directory_iterator(dir)) {
        (void)entry;
        ++files;
    }
    return files;
}
struct BackupHooks { // restore the test hooks even when an assertion fails
    ~BackupHooks() {
        ss::backup_hard_link_disabled = false;
        ss::backup_copy_limit = 16ull << 20;
    }
};
} // namespace

TEST(SaveStateB149, FinalizeKeepsExactlyOnePreviousStateAsBak) {
    BackupHooks hooks;
    const fs::path dir = scratch_dir("b149-bak");
    const fs::path slot = dir / "quick.ecvs", bak = dir / "quick.ecvs.bak";
    ASSERT_TRUE(write_marker_state(slot, 1));
    EXPECT_FALSE(fs::exists(bak)); // nothing to keep on the first save
    ASSERT_TRUE(write_marker_state(slot, 2));
    ASSERT_TRUE(fs::exists(bak));
    EXPECT_EQ(marker_of(slot), 2);
    EXPECT_EQ(marker_of(bak), 1);
    ASSERT_TRUE(write_marker_state(slot, 3)); // the older .bak is replaced, never accumulated
    EXPECT_EQ(marker_of(slot), 3);
    EXPECT_EQ(marker_of(bak), 2);
    EXPECT_EQ(count_files(dir), 2u); // slot + .bak, no .tmp left behind
}

TEST(SaveStateB149, MainFileIsNeverMissingWhileSaving) {
    BackupHooks hooks;
    const fs::path dir = scratch_dir("b149-atomic");
    const fs::path slot = dir / "quick.ecvs";
    ASSERT_TRUE(write_marker_state(slot, 1));
    std::atomic<bool> stop{false};
    std::atomic<int> missing{0};
#ifdef _WIN32
    // Windows refuses to replace a file another handle is probing, so the host run only exercises repeated
    // link + rename; the polling check runs where the harness is built for POSIX.
    std::thread poller([] {});
#else
    std::thread poller([&] {
        while (!stop.load()) {
            boost::system::error_code ec;
            if (!fs::is_regular_file(slot, ec))
                ++missing;
        }
    });
#endif
    for (int i = 2; i < 60; ++i)
        ASSERT_TRUE(write_marker_state(slot, static_cast<uint8_t>(i), 4096));
    stop = true;
    poller.join();
    EXPECT_EQ(missing.load(), 0);
}

TEST(SaveStateB149, BackupFallsBackToCopyWhenHardLinksAreUnavailable) {
    BackupHooks hooks;
    ss::backup_hard_link_disabled = true;
    const fs::path dir = scratch_dir("b149-copy");
    const fs::path slot = dir / "quick.ecvs", bak = dir / "quick.ecvs.bak";
    ASSERT_TRUE(write_marker_state(slot, 1));
    ASSERT_TRUE(write_marker_state(slot, 2));
    EXPECT_EQ(marker_of(slot), 2);
    EXPECT_EQ(marker_of(bak), 1);
}

TEST(SaveStateB149, BackupIsSkippedWhenLinkingFailsAndTheStateIsTooBigToCopy) {
    BackupHooks hooks;
    ss::backup_hard_link_disabled = true;
    ss::backup_copy_limit = 0;
    const fs::path dir = scratch_dir("b149-skip");
    const fs::path slot = dir / "quick.ecvs", bak = dir / "quick.ecvs.bak";
    ASSERT_TRUE(write_marker_state(slot, 1));
    ASSERT_TRUE(write_marker_state(slot, 2)); // still published
    EXPECT_EQ(marker_of(slot), 2);
    EXPECT_FALSE(fs::exists(bak));
}

TEST(SaveStateB149, DeleteStateLeavesTheBakForManualRecovery) {
    const fs::path dir = scratch_dir("b149-delete");
    const fs::path slot = dir / "quick.ecvs", bak = dir / "quick.ecvs.bak";
    std::ofstream(slot.string()) << "x";
    std::ofstream(bak.string()) << "y";
    EXPECT_EQ(ss::delete_state(slot).status, ss::Status::Ok);
    EXPECT_FALSE(fs::exists(slot));
    EXPECT_TRUE(fs::exists(bak));
}

TEST(SaveStateB018, MissingSavedataFileIsRecreatedFromTheState) {
    const fs::path device_a = scratch_dir("b018-a"), device_b = scratch_dir("b018-b");
    const std::string content = "JETPACK SAVE v3 coins=12345";
    const auto section = save_io_with_open_file(device_a, kSaveFile, content, 7);

    FakeEnv other(device_b); // another device: no savedata at all
    ASSERT_FALSE(fs::exists(host_file(device_b, kSaveFile)));
    std::string error;
    ASSERT_TRUE(ss::read_io(*other.emuenv, section, error)) << error;
    EXPECT_EQ(read_all(host_file(device_b, kSaveFile)), bytes_of(content));
    ASSERT_EQ(other.io.std_files.size(), 1u);
    const auto &entry = *other.io.std_files.begin();
    EXPECT_EQ(entry.second.tell(), 7);
    EXPECT_EQ(entry.second.get_translated_path(), kSaveFile);
    other.io.std_files.clear();
}

TEST(SaveStateB018, ExistingHostFileIsNeverOverwritten) {
    const fs::path device_a = scratch_dir("b018-c"), device_b = scratch_dir("b018-d");
    const auto section = save_io_with_open_file(device_a, kSaveFile, "OLD STATE CONTENT", 3);

    const fs::path target = host_file(device_b, kSaveFile);
    fs::create_directories(target.parent_path());
    {
        std::ofstream(target.string(), std::ios::binary) << "NEWER PROGRESS ON THIS DEVICE";
    }
    FakeEnv other(device_b);
    std::string error;
    ASSERT_TRUE(ss::read_io(*other.emuenv, section, error)) << error;
    EXPECT_EQ(read_all(target), bytes_of("NEWER PROGRESS ON THIS DEVICE"));
    other.io.std_files.clear();
}

TEST(SaveStateB018, InstalledContentIsNotEmbeddedAndStillMustExist) {
    const fs::path device_a = scratch_dir("b018-e"), device_b = scratch_dir("b018-f");
    const char *asset = "ux0:/app/PCSE00537/data/level.pak";
    const auto section = save_io_with_open_file(device_a, asset, "ASSET", 0);
    ss::IoSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(ss::parse_io_state(section, snapshot, error)) << error;
    EXPECT_TRUE(snapshot.embedded.empty());

    FakeEnv other(device_b);
    EXPECT_FALSE(ss::read_io(*other.emuenv, section, error));
    EXPECT_EQ(error, std::string("failed to reopen saved IO file: ") + asset);
}

TEST(SaveStateB018, MissingSavedataDirectoryIsRecreated) {
    const fs::path device_b = scratch_dir("b018-g");
    ss::BufferWriter buffer;
    buffer.i32(5);
    buffer.u32(0);
    buffer.u32(0);
    buffer.u32(1);
    buffer.i32(4);
    buffer.str("ux0:/user/00/savedata/PCSE00686");
    FakeEnv other(device_b);
    std::string error;
    ASSERT_TRUE(ss::read_io(*other.emuenv, buffer.data(), error)) << error;
    EXPECT_TRUE(fs::is_directory(device_b / "ux0/user/00/savedata/PCSE00686"));
    other.io.dir_entries.clear();
}

TEST(SaveStateB018, StatesWithoutTheExtensionStillParse) {
    // Engine 15-18 layout: tables only, nothing after the directory table.
    ss::BufferWriter buffer;
    buffer.i32(4);
    buffer.u32(0);
    buffer.u32(1);
    buffer.i32(3);
    buffer.str(kSaveFile);
    buffer.i32(SCE_O_RDONLY);
    buffer.i64(0);
    buffer.u32(0);
    ss::IoSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(ss::parse_io_state(buffer.data(), snapshot, error)) << error;
    EXPECT_TRUE(snapshot.embedded.empty());
    // Saves are not migrated across engine versions (engine 20 changed the GXM context layout).
    EXPECT_FALSE(ss::supports_engine_version(15) || ss::supports_engine_version(18) || ss::supports_engine_version(19));
    EXPECT_TRUE(ss::supports_engine_version(20));
    EXPECT_FALSE(ss::supports_engine_version(21));
}

TEST(SaveStateB018, ExtensionRejectsCorruptionAndStrayFiles) {
    const fs::path device_a = scratch_dir("b018-h");
    const auto good = save_io_with_open_file(device_a, kSaveFile, "DATA", 0);
    ss::IoSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(ss::parse_io_state(good, snapshot, error)) << error;
    ASSERT_EQ(snapshot.embedded.size(), 1u);

    auto truncated = good;
    truncated.pop_back();
    EXPECT_FALSE(ss::parse_io_state(truncated, snapshot, error));
    auto trailing = good;
    trailing.push_back(0);
    EXPECT_FALSE(ss::parse_io_state(trailing, snapshot, error));

    // An embedded file that is not an open descriptor, and a path outside user-data roots.
    for (const char *bad_path : { "ux0:/user/00/savedata/OTHER/x.bin", "ux0:/app/PCSE00537/x.bin" }) {
        ss::BufferWriter buffer;
        buffer.i32(4);
        buffer.u32(0);
        buffer.u32(1);
        buffer.i32(3);
        buffer.str(kSaveFile);
        buffer.i32(SCE_O_RDONLY);
        buffer.i64(0);
        buffer.u32(0);
        iof::write_extension(buffer, { { bad_path, bytes_of("x") } });
        EXPECT_FALSE(ss::parse_io_state(buffer.data(), snapshot, error)) << bad_path;
    }
}

TEST(SaveStateB018, EmbeddablePaths) {
    EXPECT_TRUE(iof::embeddable_path("ux0:/user/00/savedata/PCSE00537/JETPACK.SAV"));
    EXPECT_TRUE(iof::embeddable_path("ux0:/data/save.bin"));
    EXPECT_FALSE(iof::embeddable_path("ux0:/app/PCSE00537/eboot.bin"));
    EXPECT_FALSE(iof::embeddable_path("ux0:/patch/PCSE00537/x"));
    EXPECT_FALSE(iof::embeddable_path("ux0:/addcont/PCSE00537/x"));
    EXPECT_FALSE(iof::embeddable_path("vs0:/data/x"));
}

// ---- Engine 20: version gate, codec round trip, raw layout guard ---------------------------------

namespace {

// Meta sections copied byte for byte out of real engine-18 saves from the lab library (the full files
// are 15-30 MB). Regenerate with --gtest_also_run_disabled_tests --gtest_filter=*ExtractLegacyMeta*.
fs::path legacy_meta_dir() {
    return fs::path(__FILE__).parent_path() / "data" / "legacy-meta";
}

std::vector<uint8_t> meta_bytes(uint32_t engine_version, const std::string &title) {
    ss::BufferWriter meta;
    meta.u32(engine_version);
    meta.u32(0x1234);
    meta.u64(1764000000000ull);
    meta.u32(static_cast<uint32_t>(renderer::Backend::Vulkan));
    meta.u32(0);
    ss::write_string_field(meta, title);
    ss::write_string_field(meta, "test");
    ss::write_string_field(meta, "test");
    return meta.data();
}

bool write_meta_only_state(const fs::path &path, const std::vector<uint8_t> &meta) {
    ss::Writer writer;
    std::string error;
    return writer.open(path, error) && writer.write_section(ss::SectionId::Meta, meta.data(), meta.size(), false, error)
        && writer.finalize(error);
}

// What the launch path and an explicit load see for a save this build cannot read.
void expect_rejected_before_parse(const fs::path &path, const std::string &title, const char *direction) {
    const uintmax_t size_before = fs::file_size(path);
    const ss::Result inspected = ss::inspect_state(path);
    EXPECT_EQ(inspected.status, ss::Status::UnsupportedVersion) << path.string();
    EXPECT_EQ(inspected.meta.title_id, title) << path.string();
    EXPECT_NE(inspected.error.find(direction), std::string::npos) << inspected.error;

    FakeEnv env(scratch_dir("version-gate"));
    env.io.title_id = title;
    const ss::Result config = ss::prepare_launch_gpu_config(*env.emuenv, path);
    EXPECT_EQ(config.status, ss::Status::UnsupportedVersion) << config.error;
    const ss::Result loaded = ss::load_state(*env.emuenv, path, true);
    EXPECT_EQ(loaded.status, ss::Status::UnsupportedVersion) << loaded.error;
    EXPECT_TRUE(loaded.session_usable);
    EXPECT_EQ(loaded.error, inspected.error);

    // The file is kept untouched.
    ASSERT_TRUE(fs::exists(path));
    EXPECT_EQ(fs::file_size(path), size_before);
}

} // namespace

TEST(SaveStateVersion, RealOlderEngineSavesAreRejectedCleanly) {
    const fs::path dir = legacy_meta_dir();
    ASSERT_TRUE(fs::is_directory(dir)) << dir.string();
    size_t checked = 0;
    for (const auto &entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".ecvs")
            continue;
        const std::string title = entry.path().stem().string().substr(0, 9);
        const ss::Result inspected = ss::inspect_state(entry.path());
        EXPECT_LT(inspected.meta.engine_version, ss::kEngineVersion) << entry.path().string();
        expect_rejected_before_parse(entry.path(), title, "older build");
        ++checked;
    }
    EXPECT_GE(checked, 4u);
}

TEST(SaveStateVersion, NewerEngineIsRejectedToo) {
    const fs::path path = scratch_dir("version-newer") / "quick0.ecvs";
    ASSERT_TRUE(write_meta_only_state(path, meta_bytes(ss::kEngineVersion + 1, "PCSE00277")));
    expect_rejected_before_parse(path, "PCSE00277", "newer build");
}

TEST(SaveStateVersion, CurrentEngineIsAccepted) {
    const fs::path path = scratch_dir("version-current") / "quick0.ecvs";
    ASSERT_TRUE(write_meta_only_state(path, meta_bytes(ss::kEngineVersion, "PCSE00277")));
    const ss::Result inspected = ss::inspect_state(path);
    EXPECT_TRUE(inspected.ok()) << inspected.error;
    EXPECT_EQ(inspected.meta.engine_version, ss::kEngineVersion);
}

// Copies the Meta section of the lab's older-engine saves into data/legacy-meta (run on demand only).
TEST(SaveStateVersion, DISABLED_ExtractLegacyMetaFixtures) {
    const std::pair<const char *, const char *> sources[] = {
        { "PCSA00011", "anbernic-quick" }, // Gravity Rush: "invalid deferred GXM ranges"
        { "PCSA00144", "anbernic-cutscene1" }, // Tearaway: "invalid deferred GXM list table"
        { "PCSA00549", "anbernic-quick" }, // LittleBigPlanet: "invalid GXM sync object table"
        { "PCSE00277", "anbernic-quick" }, // Rayman Origins: "invalid renderer Finish table"
        { "PCSA00126", "anbernic-gameplay1" }, // God of War: "invalid renderer Finish table"
    };
    fs::create_directories(legacy_meta_dir());
    for (const auto &[title, scene] : sources) {
        const fs::path source = lab_root() / "saves" / title / (std::string(scene) + ".ecvs");
        ss::Reader reader;
        std::string error;
        std::vector<uint8_t> meta;
        ASSERT_TRUE(reader.open(source, error) && reader.read_section(ss::SectionId::Meta, meta, error)) << source.string() << ": " << error;
        ASSERT_TRUE(write_meta_only_state(legacy_meta_dir() / (std::string(title) + "-" + scene + ".ecvs"), meta));
    }
}

namespace {

ss::GxmSnapshot sample_gxm_snapshot() {
    ss::GxmSnapshot snapshot;
    snapshot.params.flags = 0;
    snapshot.params.displayQueueMaxPendingCount = 2;
    snapshot.global_timestamp = 77;
    snapshot.last_display_global = 75;
    snapshot.notification_region = 0x81000000;
    snapshot.last_immediate_context = 0x81100000;
    snapshot.renderer_context_address = 0x81100000;
    snapshot.display_queue_thread = 0x40010003;
    snapshot.display_worker_phase = 2;
    snapshot.display_previous_entry = 0x81200000;

    gxm::ContextSnapshot context{}; // value-initialized: Ptr<> members have no default
    context.address = 0x81100000;
    context.deferred = true;
    context.state.type = SCE_GXM_CONTEXT_TYPE_DEFERRED;
    context.state.visibility_enable = true;
    context.state.visibility_index = 3;
    context.state.visibility_back_enable = true;
    context.state.visibility_back_index = 9;
    context.state.visibility_back_is_increment = true;
    context.state.active = true;
    context.renderer_record.depth_bias_unit = -4;
    context.renderer_record.depth_bias_slope = 2;
    context.command_next_free_pos = 128;
    context.command_allocator_size = 64;
    context.pending_first = 1;
    context.pending_last = 2;
    context.yuv_profile[0][0] = 1;
    context.yuv_profile[0][1] = 3;
    context.yuv_profile[1][0] = 2;
    context.yuv_profile[1][1] = 0;
    gxm::DeferredListSnapshot list;
    list.guest_address = 0x81300000;
    list.identity = 0xABCDEF;
    list.first = 1;
    list.last = 2;
    list.current = true;
    list.ranges = { { 0x81400000, 0x81400100 }, { 0x81500000, 0x81500040 } };
    context.deferred_lists.push_back(list);
    snapshot.contexts.push_back(context);

    snapshot.batches.push_back({ 0x81100000, 1, 2 });
    renderer::FinishSnapshot finish;
    finish.id = 5;
    finish.owner = 0x40010005;
    finish.context_address = 0x81100000;
    finish.fence_done = true;
    finish.result = -1;
    snapshot.finish_operations.push_back(finish);
    snapshot.sync_objects.push_back({ 0x81600000, 10, 11, 9, 8 });
    gxm::RenderTargetSnapshot target;
    target.address = 0x81700000;
    target.width = 960;
    target.height = 544;
    target.scenes_per_frame = 1;
    target.driver_mem_block = 0x40010007;
    snapshot.render_targets.push_back(target);

    const auto source = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{ 'G', 'X', 'P', 0, 1, 2, 3 });
    gxm::FragmentProgramSnapshot fragment;
    fragment.address = 0x81800000;
    fragment.program = Ptr<const SceGxmProgram>(0x81810000);
    fragment.program_source = source;
    fragment.has_blend = true;
    snapshot.fragment_programs.push_back(fragment);
    gxm::VertexProgramSnapshot vertex;
    vertex.address = 0x81900000;
    vertex.program = Ptr<const SceGxmProgram>(0x81910000);
    vertex.program_source = std::make_shared<std::vector<uint8_t>>(std::vector<uint8_t>{ 'G', 'X', 'P', 0, 9 });
    vertex.attributes.resize(2);
    vertex.streams.resize(1);
    vertex.key_hash = 0x1122334455667788ull;
    snapshot.vertex_programs.push_back(vertex);
    snapshot.shader_patchers.push_back({ 0x81A00000, {} });
    snapshot.display_entries.resize(1);
    PendingDisplaySubmission pending;
    pending.callback.data = 0x81B00000;
    pending.has_prediction = true;
    pending.prediction.pitch = 960;
    snapshot.display_submissions.emplace_back(0x40010009, pending);
    snapshot.regions[0x81000000] = MemoryMapInfo{ 0, 0x100000, 3 };
    return snapshot;
}

} // namespace

TEST(SaveStateRoundTrip, GxmSectionKeepsEngine20Fields) {
    const ss::GxmSnapshot original = sample_gxm_snapshot();
    ss::BufferWriter first;
    std::string error;
    ASSERT_TRUE(ss::encode_gxm_state(original, first, error)) << error;

    ss::GxmSnapshot parsed;
    ASSERT_TRUE(ss::parse_gxm_state(first.data(), parsed, error)) << error;
    ASSERT_EQ(parsed.contexts.size(), 1u);
    const gxm::ContextSnapshot &context = parsed.contexts[0];
    EXPECT_TRUE(context.state.visibility_back_enable);
    EXPECT_EQ(context.state.visibility_back_index, 9u);
    EXPECT_TRUE(context.state.visibility_back_is_increment);
    EXPECT_TRUE(context.state.active);
    EXPECT_EQ(context.renderer_record.depth_bias_unit, -4);
    EXPECT_EQ(std::memcmp(context.yuv_profile, original.contexts[0].yuv_profile, sizeof(context.yuv_profile)), 0);
    ASSERT_EQ(context.deferred_lists.size(), 1u);
    EXPECT_EQ(context.deferred_lists[0].ranges, original.contexts[0].deferred_lists[0].ranges);
    ASSERT_EQ(parsed.finish_operations.size(), 1u);
    EXPECT_EQ(parsed.finish_operations[0].result, -1);
    ASSERT_EQ(parsed.sync_objects.size(), 1u);
    EXPECT_EQ(parsed.sync_objects[0].last_operation_global, 8u);
    ASSERT_EQ(parsed.vertex_programs.size(), 1u);
    EXPECT_EQ(*parsed.vertex_programs[0].program_source, *original.vertex_programs[0].program_source);
    ASSERT_EQ(parsed.display_submissions.size(), 1u);
    EXPECT_EQ(parsed.display_submissions[0].second.prediction.pitch, 960u);
    EXPECT_EQ(parsed.regions.size(), 1u);

    // Encoding what was parsed gives a section of the same layout that parses to the same values
    // (raw structs carry their padding bytes, which a memberwise copy need not preserve, so the
    // bytes themselves are not compared).
    ss::BufferWriter second;
    ASSERT_TRUE(ss::encode_gxm_state(parsed, second, error)) << error;
    EXPECT_EQ(first.size(), second.size());
    ss::GxmSnapshot reparsed;
    ASSERT_TRUE(ss::parse_gxm_state(second.data(), reparsed, error)) << error;
    ASSERT_EQ(reparsed.contexts.size(), 1u);
    EXPECT_EQ(reparsed.contexts[0].state.visibility_back_index, 9u);
    EXPECT_EQ(std::memcmp(reparsed.contexts[0].yuv_profile, original.contexts[0].yuv_profile, sizeof(context.yuv_profile)), 0);
    EXPECT_EQ(reparsed.contexts[0].deferred_lists[0].identity, 0xABCDEFu);
    EXPECT_EQ(reparsed.render_targets.size(), 1u);
    EXPECT_EQ(reparsed.regions.size(), 1u);

    // A YUV profile outside the table and a truncated section are refused.
    ss::GxmSnapshot bad = original;
    bad.contexts[0].yuv_profile[1][1] = renderer::yuv::profile_count;
    ss::BufferWriter bad_bytes;
    ASSERT_TRUE(ss::encode_gxm_state(bad, bad_bytes, error)) << error;
    EXPECT_FALSE(ss::parse_gxm_state(bad_bytes.data(), parsed, error));
    EXPECT_EQ(error, "invalid saved YUV profile");
    auto truncated = first.data();
    truncated.resize(truncated.size() - 1);
    EXPECT_FALSE(ss::parse_gxm_state(truncated, parsed, error));
}

TEST(SaveStateRoundTrip, GpuSectionKeepsBackFaceVisibilityQuery) {
    ss::GpuSnapshot original;
    original.mapping_method = 1;
    original.resolution = 2.0f;
    original.surfaces.colors.resize(renderer::vulkan::surface_cache_snapshot_slots);
    original.surfaces.depth_stencil.resize(renderer::vulkan::surface_cache_snapshot_slots);
    for (uint32_t slot = 0; slot < renderer::vulkan::surface_cache_snapshot_slots; ++slot) {
        original.surfaces.color_mru_order.push_back(static_cast<uint8_t>(slot));
        original.surfaces.depth_stencil_mru_order.push_back(static_cast<uint8_t>(slot));
    }
    renderer::vulkan::VKContextSnapshot context;
    context.context_address = 0x81100000;
    context.visibility_buffer_address = 0x82000000;
    context.visibility_stride = 64;
    context.visibility_query_index = 3;
    context.visibility_query_increment = false;
    context.visibility_back_query_index = 9;
    context.visibility_back_query_increment = true;
    original.contexts.push_back(context);

    std::vector<uint8_t> first;
    std::string error;
    ASSERT_TRUE(ss::encode_gpu_state(original, first, error)) << error;
    ss::GpuSnapshot parsed;
    ASSERT_TRUE(ss::parse_gpu_state(first, parsed, error)) << error;
    ASSERT_EQ(parsed.contexts.size(), 1u);
    EXPECT_EQ(parsed.contexts[0].visibility_query_index, 3);
    EXPECT_FALSE(parsed.contexts[0].visibility_query_increment);
    EXPECT_EQ(parsed.contexts[0].visibility_back_query_index, 9);
    EXPECT_TRUE(parsed.contexts[0].visibility_back_query_increment);
    std::vector<uint8_t> second;
    ASSERT_TRUE(ss::encode_gpu_state(parsed, second, error)) << error;
    EXPECT_EQ(first, second);

    // A back-face index outside the visibility buffer is refused like the front one.
    original.contexts[0].visibility_back_query_index = 16;
    ASSERT_TRUE(ss::encode_gpu_state(original, first, error)) << error;
    EXPECT_FALSE(ss::parse_gpu_state(first, parsed, error));
}

namespace {
struct UnregisteredRawStruct {
    uint32_t a = 0;
    uint16_t b = 0;
};
} // namespace

TEST(SaveStateLayout, RawStructsMustBeRegisteredWithTheirSize) {
    // The guard value<T>() enforces at compile time (see serialized_layout.h).
    static_assert(ss::raw_layout_registered<uint32_t>());
    static_assert(ss::raw_layout_registered<GxmContextState>());
    static_assert(ss::SerializedLayout<GxmContextState>::size == 1032);
    static_assert(ss::raw_layout_registered<renderer::GxmRecordState>());
    static_assert(!ss::raw_layout_registered<UnregisteredRawStruct>());
    SUCCEED();
}
