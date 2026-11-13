#include <emucorev/savestate/ngs_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>
#include <ngs/system.h>
#include <ngs/modules/player.h>
#include <chrono>
#include <cstring>
#include <future>
#include <mutex>

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
        memory.allocator.set_maximum(0x20000 / 0x1000);
        ASSERT_EQ(memory.allocator.allocate_at(0, 0x20000 / 0x1000), 0);
        state.definitions = Ptr<ngs::VoiceDefinition>(0x1000);
        auto *definition = state.definitions.get(memory);
        *definition = {ngs::BussType::BUSS_MIXER, 1};
    }
    void TearDown() override { ngs::deinit(state, memory); }
};

struct ModuleCallbackReleaseProbe {
    ngs::State *state = nullptr;
    MemState *memory = nullptr;
    ngs::System *system = nullptr;
    ngs::Rack *rack = nullptr;
    SceNgsRackDescription description{};
    SceNgsBufferInfo buffer{};
    ngs::RackReleaseAction action = ngs::RackReleaseAction::Immediate;
    bool released = false;
    bool reinitialized = false;
    std::optional<uint64_t> old_instance;
};

class ReleaseRackDuringModuleProcess final : public ngs::Module {
    ModuleCallbackReleaseProbe &probe;

public:
    explicit ReleaseRackDuringModuleProcess(ModuleCallbackReleaseProbe &probe)
        : probe(probe) {}

    bool process(KernelState &, const MemState &, SceUID thread_id,
        ngs::ModuleData &, std::unique_lock<std::recursive_mutex> &scheduler_lock,
        std::unique_lock<std::mutex> &voice_lock) override {
        ModuleCallbackReleaseProbe *const probe_ptr = &probe;
        voice_lock.unlock();
        scheduler_lock.unlock();
        std::unique_lock<std::recursive_mutex> callback_lock(probe_ptr->system->voice_scheduler.mutex);
        auto &scheduler = probe_ptr->system->voice_scheduler;
        probe_ptr->action = scheduler.rack_release_action(thread_id, false, scheduler.has_pending_rack_release(probe_ptr->rack));
        if (probe_ptr->action != ngs::RackReleaseAction::ReleaseModuleCallback)
            return false;
        ngs::release_rack(*probe_ptr->state, *probe_ptr->memory, probe_ptr->system, probe_ptr->rack);
        SceNgsBufferInfo reused_buffer = probe_ptr->buffer;
        probe_ptr->reinitialized = ngs::init_rack(*probe_ptr->state, *probe_ptr->memory, probe_ptr->system,
            &reused_buffer, &probe_ptr->description);
        scheduler.request_update_abort_after_callback();
        probe_ptr->released = true;
        return false;
    }

    uint32_t get_buffer_parameter_size() const override { return 0x20; }
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

TEST_F(NgsQueueRestore, RackReleaseWaitPredicateIgnoresSpuriousNotifications) {
    ngs::VoiceScheduler scheduler;
    scheduler.is_updating = true;
    std::promise<void> entered_wait;
    auto waiter = std::async(std::launch::async, [&] {
        std::unique_lock<std::recursive_mutex> lock(scheduler.mutex);
        entered_wait.set_value();
        scheduler.wait_for_update_to_finish(lock);
    });
    entered_wait.get_future().wait();
    {
        std::lock_guard<std::recursive_mutex> lock(scheduler.mutex);
        scheduler.condvar.notify_all();
    }
    EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    {
        std::lock_guard<std::recursive_mutex> lock(scheduler.mutex);
        scheduler.is_updating = false;
    }
    scheduler.condvar.notify_all();
    ASSERT_EQ(waiter.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    waiter.get();
}

TEST_F(NgsQueueRestore, FinishedCallbackReleaseRequestsOneShotUpdateAbort) {
    ngs::VoiceScheduler scheduler;
    EXPECT_EQ(scheduler.rack_release_action(42, false), ngs::RackReleaseAction::Immediate);
    scheduler.is_updating = true;
    scheduler.update_thread_id = 42;
    EXPECT_EQ(scheduler.rack_release_action(42, false), ngs::RackReleaseAction::RejectReentrant);
    {
        ngs::VoiceScheduler::CallbackContextScope finished_callback(scheduler, ngs::UpdateCallbackKind::VoiceFinished);
        EXPECT_EQ(scheduler.rack_release_action(42, false), ngs::RackReleaseAction::ReleaseFinishedCallback);
        scheduler.request_update_abort_after_callback();
    }
    EXPECT_TRUE(scheduler.consume_update_abort_after_callback());
    EXPECT_FALSE(scheduler.consume_update_abort_after_callback());
}

TEST_F(NgsQueueRestore, ModuleProcessCallbackReleaseSelectsAbortableAction) {
    ngs::VoiceScheduler scheduler;
    scheduler.is_updating = true;
    scheduler.update_thread_id = 42;
    {
        ngs::VoiceScheduler::CallbackContextScope module_callback(scheduler, ngs::UpdateCallbackKind::ModuleProcess);
        EXPECT_EQ(scheduler.rack_release_action(42, false), ngs::RackReleaseAction::ReleaseModuleCallback);
    }
    EXPECT_EQ(scheduler.rack_release_action(42, false), ngs::RackReleaseAction::RejectReentrant);
}

TEST_F(NgsQueueRestore, ReleaseCompletionCanReinitializeAndSynchronouslyReleaseNewRackGeneration) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    auto *system = Ptr<ngs::System>(0x2000).get(memory);
    SceNgsRackDescription description{state.definitions, 1, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo released_buffer{Ptr<void>(0x4000), 0x5000};
    SceNgsBufferInfo other_buffer{Ptr<void>(0xD000), 0x5000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &released_buffer, &description));
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &other_buffer, &description));
    auto *released_rack = released_buffer.data.cast<ngs::Rack>().get(memory);
    auto *other_rack = other_buffer.data.cast<ngs::Rack>().get(memory);
    const auto old_instance = state.rack_instance_id(released_buffer.data.address());
    ASSERT_TRUE(old_instance.has_value());

    auto &scheduler = system->voice_scheduler;
    std::lock_guard<std::recursive_mutex> scheduler_lock(scheduler.mutex);
    ngs::OperationPending current_release{};
    current_release.type = ngs::PendingType::ReleaseRack;
    current_release.system = system;
    current_release.release_data.state = &state;
    current_release.release_data.rack = released_rack;
    current_release.release_data.callback = 0x100;
    scheduler.operations_pending.push(current_release);
    ngs::OperationPending later_release = current_release;
    later_release.release_data.rack = other_rack;
    scheduler.operations_pending.push(later_release);

    // Match the production drain: take a value copy and remove the current
    // operation before invoking the guest completion callback.
    const ngs::OperationPending completed = scheduler.operations_pending.front();
    scheduler.operations_pending.pop();
    ASSERT_FALSE(scheduler.has_pending_rack_release(released_rack));
    ASSERT_TRUE(scheduler.has_pending_rack_release(other_rack));
    ngs::release_rack(state, memory, system, completed.release_data.rack);

    scheduler.is_updating = true;
    scheduler.update_thread_id = 42;
    {
        ngs::VoiceScheduler::CallbackContextScope completion_callback(scheduler, ngs::UpdateCallbackKind::RackReleaseCompletion);
        ASSERT_TRUE(ngs::init_rack(state, memory, system, &released_buffer, &description));
        auto *new_rack = released_buffer.data.cast<ngs::Rack>().get(memory);
        const auto new_instance = state.rack_instance_id(released_buffer.data.address());
        ASSERT_TRUE(new_instance.has_value());
        EXPECT_NE(new_instance, old_instance);
        EXPECT_FALSE(scheduler.has_pending_rack_release(new_rack));
        EXPECT_TRUE(scheduler.has_pending_rack_release(other_rack));
        EXPECT_EQ(scheduler.rack_release_action(42, false, scheduler.has_pending_rack_release(new_rack)),
            ngs::RackReleaseAction::Immediate);
        ngs::release_rack(state, memory, system, new_rack);
    }
    scheduler.is_updating = false;
    scheduler.update_thread_id = 0;
    scheduler.operations_pending.pop();
    EXPECT_FALSE(state.rack_instance_id(released_buffer.data.address()).has_value());
}

TEST_F(NgsQueueRestore, ModuleCallbackReleaseUnwindsBeforeStaleQueueAndMasterPatchUse) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    auto *system = Ptr<ngs::System>(0x2000).get(memory);
    const Address master_definition_address = state.definitions.address()
        + static_cast<uint32_t>(ngs::BussType::BUSS_MASTER) * sizeof(ngs::VoiceDefinition);
    *Ptr<ngs::VoiceDefinition>(master_definition_address).get(memory) = {ngs::BussType::BUSS_MASTER, 1};

    SceNgsRackDescription source_description{state.definitions, 1, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo source_buffer{Ptr<void>(0x4000), 0x8000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &source_buffer, &source_description));
    auto *source_rack = source_buffer.data.cast<ngs::Rack>().get(memory);
    ngs::Voice *source_voice = source_rack->voices[0].get(memory);
    const Address source_voice_address = Ptr<void>(source_voice, memory).address();

    SceNgsRackDescription master_description{Ptr<ngs::VoiceDefinition>(master_definition_address), 1, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo master_buffer{Ptr<void>(0xD000), 0x8000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &master_buffer, &master_description));
    auto *master_rack = master_buffer.data.cast<ngs::Rack>().get(memory);
    ngs::Voice *master_voice = master_rack->voices[0].get(memory);
    ASSERT_TRUE(master_voice->patch(memory, 0, 0, 0, Ptr<ngs::Voice>(master_voice, memory), Ptr<ngs::Voice>(source_voice, memory)));
    source_voice->state = master_voice->state = ngs::VOICE_STATE_ACTIVE;
    system->voice_scheduler.queue = {source_voice, master_voice};

    ModuleCallbackReleaseProbe probe;
    probe.state = &state;
    probe.memory = &memory;
    probe.system = system;
    probe.rack = source_rack;
    probe.description = source_description;
    probe.buffer = source_buffer;
    probe.old_instance = state.rack_instance_id(source_buffer.data.address());
    source_rack->modules[0] = std::make_unique<ReleaseRackDuringModuleProcess>(probe);
    KernelState kernel;
    system->voice_scheduler.update(kernel, memory, 42);

    EXPECT_EQ(probe.action, ngs::RackReleaseAction::ReleaseModuleCallback);
    EXPECT_TRUE(probe.released);
    EXPECT_TRUE(probe.reinitialized);
    const auto new_instance = state.rack_instance_id(source_buffer.data.address());
    ASSERT_TRUE(new_instance.has_value());
    EXPECT_NE(new_instance, probe.old_instance);
    ASSERT_EQ(system->voice_scheduler.queue.size(), 1u);
    EXPECT_EQ(system->voice_scheduler.queue[0], master_voice);
    EXPECT_EQ(master_voice->frame_count, 0u);
    ASSERT_EQ(master_voice->patches[0].size(), 1u);
    EXPECT_EQ(master_voice->patches[0][0].get(memory)->dest.address(), source_voice_address);
}

TEST_F(NgsQueueRestore, ReleasingRackLeavesOtherQueuedRackAndWaitTokenRejectsAddressReuse) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    auto *system = Ptr<ngs::System>(0x2000).get(memory);
    const Address master_definition_address = state.definitions.address()
        + static_cast<uint32_t>(ngs::BussType::BUSS_MASTER) * sizeof(ngs::VoiceDefinition);
    *Ptr<ngs::VoiceDefinition>(master_definition_address).get(memory) = {ngs::BussType::BUSS_MASTER, 1};

    SceNgsRackDescription source_description{state.definitions, 1, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo source_buffer{Ptr<void>(0x4000), 0x5000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &source_buffer, &source_description));
    auto *source_rack = source_buffer.data.cast<ngs::Rack>().get(memory);
    ngs::Voice *source_voice = source_rack->voices[0].get(memory);

    SceNgsRackDescription master_description{Ptr<ngs::VoiceDefinition>(master_definition_address), 1, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo master_buffer{Ptr<void>(0xA000), 0x5000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &master_buffer, &master_description));
    auto *master_rack = master_buffer.data.cast<ngs::Rack>().get(memory);
    ngs::Voice *master_voice = master_rack->voices[0].get(memory);
    system->voice_scheduler.queue = {source_voice, master_voice};

    const auto old_instance = state.rack_instance_id(source_buffer.data.address());
    ASSERT_TRUE(old_instance.has_value());
    {
        std::lock_guard<std::recursive_mutex> lock(system->voice_scheduler.mutex);
        ngs::release_rack(state, memory, system, source_rack);
    }
    ASSERT_EQ(system->voice_scheduler.queue.size(), 1u);
    EXPECT_EQ(system->voice_scheduler.queue[0], master_voice);
    EXPECT_FALSE(state.rack_instance_id(source_buffer.data.address()).has_value());

    ASSERT_TRUE(ngs::init_rack(state, memory, system, &source_buffer, &source_description));
    const auto new_instance = state.rack_instance_id(source_buffer.data.address());
    ASSERT_TRUE(new_instance.has_value());
    EXPECT_NE(new_instance, old_instance);
    EXPECT_EQ(system->voice_scheduler.queue.size(), 1u);
    EXPECT_EQ(system->voice_scheduler.queue[0], master_voice);
}

TEST_F(NgsQueueRestore, InvalidRackVoicePointerRejectsCaptureWithoutDereference) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    auto *system = Ptr<ngs::System>(0x2000).get(memory);
    SceNgsRackDescription description{state.definitions, 2, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo buffer{Ptr<void>(0x4000), 0x10000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &buffer, &description));
    auto *rack = buffer.data.cast<ngs::Rack>().get(memory);
    const Ptr<ngs::Voice> valid_voice = rack->voices[0];
    rack->voices[0] = Ptr<ngs::Voice>(0x80000000);

    Ptr<ngs::VoiceDefinition> definitions(0x1234);
    std::vector<ngs::SystemInitInfo> systems(1);
    systems[0].address = 0x5678;
    std::vector<ngs::RackInitInfo> racks(1);
    racks[0].address = 0x9ABC;
    std::string error;
    EXPECT_FALSE(ngs::capture_state(state, memory, definitions, systems, racks, &error));
    EXPECT_NE(error.find("invalid voice pointer"), std::string::npos);
    EXPECT_EQ(definitions.address(), 0x1234u);
    ASSERT_EQ(systems.size(), 1u);
    EXPECT_EQ(systems[0].address, 0x5678u);
    ASSERT_EQ(racks.size(), 1u);
    EXPECT_EQ(racks[0].address, 0x9ABCu);

    // Keep fixture teardown on the valid live rack so it can free the system.
    rack->voices[0] = valid_voice;
}

TEST_F(NgsQueueRestore, CorruptRackVectorControlBlockRejectsCaptureBeforeFollowingBackingPointer) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    auto *system = Ptr<ngs::System>(0x2000).get(memory);
    SceNgsRackDescription description{state.definitions, 2, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo buffer{Ptr<void>(0x4000), 0x10000};
    ASSERT_TRUE(ngs::init_rack(state, memory, system, &buffer, &description));
    auto *rack = buffer.data.cast<ngs::Rack>().get(memory);

    std::array<std::byte, sizeof(rack->voices)> original{};
    std::memcpy(original.data(), &rack->voices, sizeof(rack->voices));
    const uintptr_t invalid_begin = 1;
    std::memcpy(&rack->voices, &invalid_begin, sizeof(invalid_begin));

    Ptr<ngs::VoiceDefinition> definitions(0x1234);
    std::vector<ngs::SystemInitInfo> systems;
    std::vector<ngs::RackInitInfo> racks;
    std::string error;
    const bool captured = ngs::capture_state(state, memory, definitions, systems, racks, &error);
    std::memcpy(&rack->voices, original.data(), sizeof(rack->voices));

    EXPECT_FALSE(captured);
    EXPECT_NE(error.find("corrupted rack voices vector"), std::string::npos);
    EXPECT_EQ(definitions.address(), 0x1234u);
    EXPECT_TRUE(systems.empty());
    EXPECT_TRUE(racks.empty());
}

TEST_F(NgsQueueRestore, RackInitRejectsPartialCrossSystemOverlapButAllowsTouchingBuffers) {
    SceNgsSystemInitParams parameters{2, 4, 64, 48000, 0};
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x2000), 0x1000));
    ASSERT_TRUE(ngs::init_system(state, memory, &parameters, Ptr<void>(0x3000), 0x1000));
    auto *first_system = Ptr<ngs::System>(0x2000).get(memory);
    auto *second_system = Ptr<ngs::System>(0x3000).get(memory);
    SceNgsRackDescription description{state.definitions, 1, 2, 1, 1, Ptr<void>()};
    SceNgsBufferInfo first_buffer{Ptr<void>(0x4000), 0x5000}; // [0x4000, 0x9000)
    ASSERT_TRUE(ngs::init_rack(state, memory, first_system, &first_buffer, &description));
    auto *first_rack = first_buffer.data.cast<ngs::Rack>().get(memory);
    const auto *first_voice_storage = first_rack->voices.data();

    SceNgsBufferInfo partial_overlap{Ptr<void>(0x8800), 0x1000}; // [0x8800, 0x9800)
    bool overlap_rejected = false;
    EXPECT_FALSE(ngs::init_rack(state, memory, second_system, &partial_overlap, &description, &overlap_rejected));
    EXPECT_TRUE(overlap_rejected);
    ASSERT_EQ(state.rack_infos.size(), 1u);
    ASSERT_EQ(state.rack_vector_integrity.size(), 1u);
    EXPECT_EQ(first_rack->voices.data(), first_voice_storage);
    EXPECT_TRUE(state.overlapping_rack_buffer(partial_overlap.data.address(), partial_overlap.size).has_value());

    SceNgsBufferInfo touching{Ptr<void>(0x9000), 0x1000}; // starts exactly at the first range's end
    overlap_rejected = false;
    EXPECT_TRUE(ngs::init_rack(state, memory, second_system, &touching, &description, &overlap_rejected));
    EXPECT_FALSE(overlap_rejected);
    EXPECT_EQ(state.rack_infos.size(), 2u);
}

TEST(NgsRackRanges, DetectsOverlapWithoutOverflowingAddressArithmetic) {
    EXPECT_TRUE(ngs::rack_buffer_ranges_overlap(0x1000, 0x1000, 0x1800, 0x1000));
    EXPECT_FALSE(ngs::rack_buffer_ranges_overlap(0x1000, 0x800, 0x1800, 0x1000));
    EXPECT_FALSE(ngs::rack_buffer_ranges_overlap(0x1000, 0x1000, 0x3000, 0x1000));
    EXPECT_TRUE(ngs::rack_buffer_ranges_overlap(0xFFFFFF00u, 0x200, 0xFFFFFFFFu, 1));
}
} // namespace
