// Host tests for the save-state fixes B013 (deferred GXM ring cursor), B019 (stale surface-cache
// entries) and B018 (open savedata files missing on the loading machine).
//
// They run the real validation/parse code of the adapter against real save files. Artifact and library
// directories are taken from EMUCOREV_LAB (default C:/Users/SBRO/emucorev-lab); a test whose input is
// absent is skipped, so the target also builds and runs on a machine without the lab data.
//
// savestate.cpp is included to reach the file-local write_io/read_io and parse_io_state.

#include <gtest/gtest.h>

#include "../../main/cpp/emucorev/src/savestate/savestate.cpp"

#include <cstdlib>
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

    bool load(const fs::path &path, const std::string &scratch) {
        if (!reader.open(path, error) || !reader.validate_sections(error))
            return false;
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

// ---- B013 -----------------------------------------------------------------------------------

TEST(SaveStateB013, TearawayArtifactsValidate) {
    for (const char *name : { "anbernic-gameplay1-restorefail.ecvs", "anbernic-gameplay1b-restorefail.ecvs" }) {
        const fs::path path = lab_root() / "saves-broken/PCSA00144" / name;
        if (!fs::exists(path))
            GTEST_SKIP() << "missing artifact " << path.string();
        LoadedState state;
        ASSERT_TRUE(state.load(path, "b013")) << name << ": " << state.error;
        EXPECT_TRUE(validate_like_load(state)) << name << ": " << state.error;
    }
}

TEST(SaveStateB013, IdleDeferredContextKeepsCursorAfterRingWasReleased) {
    const fs::path path = lab_root() / "saves-broken/PCSA00144/anbernic-gameplay1-restorefail.ecvs";
    if (!fs::exists(path))
        GTEST_SKIP() << "missing artifact";
    LoadedState state;
    ASSERT_TRUE(state.load(path, "b013b")) << state.error;
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
    ASSERT_TRUE(state.load(path, "b019")) << state.error;

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
    size_t checked = 0;
    for (fs::recursive_directory_iterator it(root), end; it != end; ++it) {
        if (it->path().extension() != ".ecvs")
            continue;
        LoadedState state;
        ASSERT_TRUE(state.load(it->path(), "library")) << it->path().string() << ": " << state.error;
        size_t dropped = 0;
        EXPECT_TRUE(validate_like_load(state, &dropped)) << it->path().string() << ": " << state.error;
        EXPECT_EQ(dropped, 0u) << it->path().string();
        ++checked;
    }
    std::printf("[library] %zu saves validated\n", checked);
    EXPECT_GT(checked, 0u);
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

TEST(SaveStateB149, FinalizeKeepsExactlyOnePreviousStateAsBak) {
    const fs::path dir = scratch_dir("b149-bak");
    const fs::path slot = dir / "quick.ecvs", bak = dir / "quick.ecvs.bak";
    const auto write_state = [&](uint8_t marker) {
        ss::Writer writer;
        std::string error;
        ASSERT_TRUE(writer.open(slot, error)) << error;
        ASSERT_TRUE(writer.write_section(ss::SectionId::Meta, &marker, 1, false, error)) << error;
        ASSERT_TRUE(writer.finalize(error)) << error;
    };
    const auto marker_of = [](const fs::path &file) {
        ss::Reader reader;
        std::string error;
        std::vector<uint8_t> section;
        EXPECT_TRUE(reader.open(file, error)) << error;
        EXPECT_TRUE(reader.read_section(ss::SectionId::Meta, section, error)) << error;
        return section.empty() ? 0 : section[0];
    };
    write_state(1);
    EXPECT_FALSE(fs::exists(bak)); // nothing to keep on the first save
    write_state(2);
    ASSERT_TRUE(fs::exists(bak));
    EXPECT_EQ(marker_of(slot), 2);
    EXPECT_EQ(marker_of(bak), 1);
    write_state(3); // the older .bak is replaced, never accumulated
    EXPECT_EQ(marker_of(slot), 3);
    EXPECT_EQ(marker_of(bak), 2);
    size_t files = 0;
    for (const auto &entry : fs::directory_iterator(dir)) {
        (void)entry;
        ++files;
    }
    EXPECT_EQ(files, 2u); // slot + .bak, no .tmp left behind
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
    EXPECT_TRUE(ss::supports_engine_version(15) && ss::supports_engine_version(18) && ss::supports_engine_version(19));
    EXPECT_FALSE(ss::supports_engine_version(20));
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
