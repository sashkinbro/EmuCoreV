#include <emucorev/savestate/ngs_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>
#include <ngs/system.h>
#include <ngs/modules/player.h>
#include <cstring>

namespace {
class NgsPreflight : public testing::Test {
protected:
    emucorev::savestate::NgsSnapshot snapshot;
    std::vector<uint8_t> ram = std::vector<uint8_t>(0x20000);
    fs::path root;
    std::string error;
    uint32_t cursor = 0;

    template <typename T> void put(Address address, const T &value) {
        std::memcpy(ram.data() + address - 0x1000, &value, sizeof(value));
    }
    void SetUp() override {
        root = fs::temp_directory_path() / fs::unique_path("ngs-preflight-%%%%-%%%%");
        ASSERT_TRUE(fs::create_directory(root));
        snapshot.definitions = 0x1000;
        for (uint32_t i = 0; i < static_cast<uint32_t>(ngs::BussType::BUSS_MAX); ++i) {
            const bool two = i == 12 || i == 14 || i == 15 || i == 16;
            const bool four = i == 13 || i == 17;
            put(0x1000 + i * sizeof(ngs::VoiceDefinition), ngs::VoiceDefinition{static_cast<ngs::BussType>(i), four ? 4u : two ? 2u : 1u});
        }
        ngs::SystemInitInfo system;
        system.address = 0x2000;
        system.memspace = Ptr<void>(system.address);
        system.memspace_size = 0x1000;
        system.params = {4, 16, 64, 48000, 0};
        system.queued_voices = {{4, 0}}; // init_system's four null slots precede pushed racks.
        snapshot.systems.push_back(system);
        ngs::RackInitInfo rack;
        rack.address = 0x4000;
        rack.system_address = system.address;
        rack.system = Ptr<ngs::System>(system.address);
        rack.info = {Ptr<void>(rack.address), 0x10000};
        rack.description = {Ptr<ngs::VoiceDefinition>(0x1000), 1, 2, 1, 1, Ptr<void>()};
        snapshot.racks.push_back(rack);
        build_rack(ngs::BussType::BUSS_MASTER);
    }
    void build_rack(ngs::BussType type) {
        auto &saved = snapshot.racks.front();
        saved.description.definition = Ptr<ngs::VoiceDefinition>(0x1000 + static_cast<uint32_t>(type) * sizeof(ngs::VoiceDefinition));
        saved.blocks.clear();
        saved.voices.clear();
        cursor = 0;
        std::fill(ram.begin() + 0x3000, ram.begin() + 0x13000, 0);
        auto allocate = [&](uint32_t size) {
            const Address address = saved.address + cursor;
            if (size) {
                size = (size + 3) & ~3u;
                saved.blocks.push_back({size, cursor, false});
                cursor += size;
            }
            return address;
        };
        allocate(sizeof(ngs::Rack));
        ngs::VoiceInfo voice;
        voice.address = allocate(sizeof(ngs::Voice));
        voice.state = ngs::VOICE_STATE_ACTIVE;
        voice.patches.resize(ngs::MAX_OUTPUT_PORT, std::vector<Address>(1));
        ngs::VoiceDefinition definition{type, 1};
        std::vector<std::unique_ptr<ngs::Module>> modules;
        ngs::apply_voice_definition(&definition, modules);
        for (const auto &module : modules) {
            ngs::ModuleDataInfo data;
            data.parameters.resize(module->get_buffer_parameter_size());
            data.guest_state_data.resize(module->get_guest_state_size());
            ngs::ModuleData temporary;
            temporary.logical_state = module->create_logical_state();
            module->capture_logical_state(temporary, data.logical_state);
            allocate(data.parameters.size());
            allocate(data.parameters.size());
            voice.modules.push_back(std::move(data));
        }
        if (type == ngs::BussType::BUSS_MASTER)
            voice.modules[1].guest_state_data.resize(64 * 4); // Output's dynamic stereo S16 buffer.
        const Address patch_address = allocate(sizeof(ngs::Patch));
        ngs::Patch patch{};
        patch.source = Ptr<ngs::Voice>(voice.address);
        patch.dest = patch.source;
        patch.output_index = patch.output_sub_index = patch.dest_index = 0;
        put(patch_address, patch);
        voice.patches[0][0] = patch_address;
        saved.voices.push_back(std::move(voice));
        saved.blocks.push_back({saved.info.size - cursor, cursor, true});
    }
    void TearDown() override { fs::remove_all(root); }
    std::unique_ptr<emucorev::savestate::MemoryImage> image() {
        using namespace emucorev::savestate;
        BufferWriter bytes;
        bytes.u32(kMemoryImageFormatVersion);
        bytes.u32(ram.size());
        bytes.u32(1);
        bytes.u32(0x1000);
        bytes.u32(ram.size() / kMemoryImagePageSize);
        bytes.u64(0x1000);
        bytes.u32(ram.size());
        bytes.u32(static_cast<uint32_t>(MemoryChunkEncoding::Raw));
        bytes.u32(ram.size());
        bytes.u32(crc32_buffer(ram.data(), ram.size()));
        bytes.append(ram.data(), ram.size());
        Writer writer;
        const fs::path file = root / "memory.ecvs";
        if (!writer.open(file, error) || !writer.write_section(SectionId::Memory, bytes.data().data(), bytes.size(), false, error)
            || !writer.finalize(error)) return {};
        Reader reader;
        if (!reader.open(file, error) || !reader.validate_sections(error)) return {};
        return MemoryImage::preflight(reader, root, error);
    }
    bool validate() {
        auto memory = image();
        if (!memory) return false;
        return emucorev::savestate::validate_ngs_state(snapshot, *memory, error);
    }
};

TEST_F(NgsPreflight, AcceptsActualFactoryLayoutAndHistoricalQueueOffset) {
    EXPECT_TRUE(validate()) << error;
    snapshot.racks[0].description.channels_per_voice = 0; // Guest API explicitly accepts zero.
    EXPECT_TRUE(validate()) << error;
    snapshot.systems[0].queued_voices[0].first = 0;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, RejectsNegativeCountsAndDuplicateObjectStorage) {
    snapshot.racks[0].description.voice_count = -1;
    EXPECT_FALSE(validate());
    snapshot.racks[0].description.voice_count = 1;
    snapshot.systems.push_back(snapshot.systems[0]);
    EXPECT_FALSE(validate());
    snapshot.systems.pop_back();
    snapshot.systems[0].params.granularity = INT32_MAX;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, LockedParametersValidatePreviousSnapshotWhileGuestEditsLiveBytes) {
    build_rack(ngs::BussType::BUSS_SAS_EMULATION);
    auto &rack = snapshot.racks[0];
    auto &player = rack.voices[0].modules[0];
    player.last_info = player.parameters;
    player.flags = ngs::ModuleData::PARAMS_LOCK;
    SceNgsPlayerParams edit{};
    edit.playback_frequency = -1;
    std::memcpy(player.parameters.data(), &edit, sizeof(edit));
    const Address parameter_address = rack.voices[0].address + ((sizeof(ngs::Voice) + 3) & ~3u);
    put(parameter_address, edit);
    EXPECT_TRUE(validate()) << error;
    player.flags = 0;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, RetiredBuffersAreAllowedUntilTheyAreReachableByActivePlayback) {
    build_rack(ngs::BussType::BUSS_SAS_EMULATION);
    auto &voice = snapshot.racks[0].voices[0];
    auto &player = voice.modules[0];
    SceNgsPlayerParams retired{.buffer_params = {{Ptr<void>(0xffff0000), 16, 0, -1}}};
    std::memcpy(player.parameters.data(), &retired, sizeof(retired));
    const Address parameter_address = voice.address + ((sizeof(ngs::Voice) + 3) & ~3u);
    put(parameter_address, retired);
    voice.state = ngs::VOICE_STATE_AVAILABLE;
    snapshot.systems[0].queued_voices.clear();
    EXPECT_TRUE(validate()) << error;
    voice.state = ngs::VOICE_STATE_ACTIVE;
    snapshot.systems[0].queued_voices = {{4, 0}};
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, AcceptsEveryCurrentFactoryAndRejectsCorruptMatchingTypedPayload) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(ngs::BussType::BUSS_MAX); ++i) {
        build_rack(static_cast<ngs::BussType>(i));
        EXPECT_TRUE(validate()) << "buss=" << i << " " << error;
    }
    build_rack(ngs::BussType::BUSS_SAS_EMULATION);
    auto &player = snapshot.racks[0].voices[0].modules[0];
    ASSERT_GE(player.logical_state.size(), 9u);
    player.logical_state[4] = 2; // Unknown version with the correct Player ID.
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, RejectsDefinitionOverwrittenDuringRestoreAndParameterMemoryDisagreement) {
    const Address definition = snapshot.racks[0].address;
    put(definition, ngs::VoiceDefinition{ngs::BussType::BUSS_MASTER, 1});
    snapshot.racks[0].description.definition = Ptr<ngs::VoiceDefinition>(definition);
    EXPECT_FALSE(validate());
    build_rack(ngs::BussType::BUSS_SAS_EMULATION);
    snapshot.racks[0].voices[0].modules[0].parameters[0] = 1;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, RejectsUnmappedDefinitionsOverlappingPoolsAndWrongAssociation) {
    snapshot.definitions = 0xffff0000;
    EXPECT_FALSE(validate());
    snapshot.definitions = 0x1000;
    snapshot.racks[0].system_address += 4;
    EXPECT_FALSE(validate());
    snapshot.racks[0].system_address = 0x2000;
    snapshot.racks[0].info.data = Ptr<void>(0x2000);
    snapshot.racks[0].address = 0x2000;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, RejectsMisplacedVoiceAndMissingOwnedPoolBlock) {
    auto &rack = snapshot.racks[0];
    rack.voices[0].address += 4;
    EXPECT_FALSE(validate());
    rack.voices[0].address -= 4;
    rack.blocks[1].free = true;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, RejectsWrongModuleSizesSchemasFlagsAndCallbackReferences) {
    auto &module = snapshot.racks[0].voices[0].modules[1];
    module.guest_state_data.pop_back();
    EXPECT_FALSE(validate());
    module.guest_state_data.push_back(0);
    module.flags = 0x80;
    EXPECT_FALSE(validate());
    module.flags = 0;
    module.logical_state = {0xe6, 0x5c, 0, 0, 1, 0, 0, 0, 0};
    EXPECT_FALSE(validate()); // Valid Player blob is invalid for Output.
    module.logical_state.clear();
    module.callback = 0xfffffffd;
    EXPECT_FALSE(validate());
    module.callback = 0x1801;
    module.user_data = 0xdeadbeef; // User data is an opaque cookie, not necessarily mapped.
    EXPECT_TRUE(validate()) << error;
}

TEST_F(NgsPreflight, RejectsPatchToMissingVoiceOrFreeStorageAndDuplicateQueueEntries) {
    auto &voice = snapshot.racks[0].voices[0];
    ngs::Patch patch{};
    patch.source = Ptr<ngs::Voice>(voice.address);
    patch.dest = Ptr<ngs::Voice>(0x12345678);
    put(voice.patches[0][0], patch);
    EXPECT_FALSE(validate());
    patch.dest = patch.source;
    put(voice.patches[0][0], patch);
    snapshot.systems[0].queued_voices.push_back({4, 0});
    EXPECT_FALSE(validate());
    snapshot.systems[0].queued_voices.pop_back();
    snapshot.racks[0].blocks[snapshot.racks[0].blocks.size() - 2].free = true;
    EXPECT_FALSE(validate());
}

TEST_F(NgsPreflight, AcceptsLegacyZeroAllocatedRecordsOnlyAtRealBoundaries) {
    auto &blocks = snapshot.racks[0].blocks;
    blocks.push_back({0, 0, false});
    blocks.push_back({0, cursor, false});
    EXPECT_TRUE(validate()) << error;
    blocks.push_back({0, cursor + 4, false});
    EXPECT_FALSE(validate());
}

class NgsQueueRestore : public testing::Test {
protected:
    MemState memory;
    ngs::State state;
    void SetUp() override {
        // A flat bounded guest arena is enough for the real NGS pool/factory
        // paths; this test creates no CPUs, renderer, or audio device.
        memory.memory = Memory(new uint8_t[0x20000]{}, [](uint8_t *p) { delete[] p; });
        state.definitions = Ptr<ngs::VoiceDefinition>(0x1000);
        auto *definition = state.definitions.get(memory);
        *definition = {ngs::BussType::BUSS_MIXER, 1};
    }
    void TearDown() override { ngs::deinit(state, memory); }
};

TEST_F(NgsQueueRestore, CyclicPatchDependenciesPreserveExactSavedQueueOrder) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    auto *system = Ptr<ngs::System>(0x2000).get(memory);
    SceNgsRackDescription description{state.definitions, 2, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo buffer{Ptr<void>(0x4000), 0x10000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &buffer, &description));
    auto *rack = buffer.data.cast<ngs::Rack>().get(memory);
    const Ptr<ngs::Voice> a = rack->voices[0], b = rack->voices[1];
    ASSERT_TRUE(a.get(memory)->patch(memory, 0, 0, 0, a, b));
    ASSERT_TRUE(b.get(memory)->patch(memory, 0, 0, 0, b, a));
    a.get(memory)->state = b.get(memory)->state = ngs::VOICE_STATE_ACTIVE;
    system->voice_scheduler.queue = {a.get(memory), b.get(memory)};
    Ptr<ngs::VoiceDefinition> definitions;
    std::vector<ngs::SystemInitInfo> systems;
    std::vector<ngs::RackInitInfo> racks;
    ASSERT_TRUE(ngs::capture_state(state, memory, definitions, systems, racks));
    const std::vector<uint8_t> ram(memory.memory.get(), memory.memory.get() + 0x20000);
    ngs::deinit(state, memory);
    std::memcpy(memory.memory.get(), ram.data(), ram.size());
    ASSERT_TRUE(ngs::restore_state(state, memory, definitions, systems, racks));
    system = Ptr<ngs::System>(0x2000).get(memory);
    ASSERT_EQ(system->voice_scheduler.queue.size(), 2u);
    EXPECT_EQ(system->voice_scheduler.queue[0], a.get(memory));
    EXPECT_EQ(system->voice_scheduler.queue[1], b.get(memory));
    EXPECT_EQ(a.get(memory)->patches[0][0].get(memory)->dest, b);
    EXPECT_EQ(b.get(memory)->patches[0][0].get(memory)->dest, a);
}
} // namespace
