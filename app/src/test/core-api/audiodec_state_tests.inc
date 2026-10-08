#include <modules/SceAudiodec/SceAudiodecUser.h>
#include <modules/SceAudiodec/state_snapshot.h>

#include <bit>

namespace {
struct AudiodecFixture {
    EmuEnvState env;
    SceAudiodecInfo info{};
    SceAudiodecCtrl ctrl{};

    AudiodecFixture() {
        env.mem.memory = Memory(new uint8_t[0x20000]{}, [](uint8_t *p) { delete[] p; });
        env.kernel.set_next_uid(0x4100);
        audiodec::initialize_state(env);
        info.size = sizeof(info);
        info.at9.config_data = 0xE03F74FE;
        std::memcpy(env.mem.memory.get() + 0x1000, &info, sizeof(info));
        ctrl.size = sizeof(ctrl);
        ctrl.info = Ptr<SceAudiodecInfo>(0x1000);
    }

    bool create_at9() {
        SceAudiodecInitParam init{};
        init.at9.size = sizeof(init.at9);
        init.at9.totalCh = 2;
        if (export_sceAudiodecInitLibrary(env, 0, "test", SCE_AUDIODEC_TYPE_AT9, &init) != 0)
            return false;
        return export_sceAudiodecCreateDecoder(env, 0, "test", &ctrl, SCE_AUDIODEC_TYPE_AT9) == 0;
    }

    bool create_aac() {
        SceAudiodecInitParam init{};
        init.aac.size = sizeof(init.aac);
        init.aac.totalStreams = 1;
        if (export_sceAudiodecInitLibrary(env, 0, "test", SCE_AUDIODEC_TYPE_AAC, &init) != 0)
            return false;
        info = {};
        info.size = sizeof(info);
        info.aac.channels = 2;
        info.aac.sample_rate = 44100;
        std::memcpy(env.mem.memory.get() + 0x1000, &info, sizeof(info));
        return export_sceAudiodecCreateDecoder(env, 0, "test", &ctrl, SCE_AUDIODEC_TYPE_AAC) == 0;
    }
};
} // namespace

TEST(AudiodecSavestate, RestoresActiveAt9HandleAndRoundTripsTypedState) {
    AudiodecFixture fixture;
    ASSERT_TRUE(fixture.create_at9());
    const SceUID saved_handle = fixture.ctrl.handle;

    std::vector<uint8_t> before;
    std::string error;
    ASSERT_TRUE(audiodec::capture_state(fixture.env, before, error)) << error;
    ASSERT_GT(before.size(), 17493u + 4u);
    const uint64_t sample_bits = std::bit_cast<uint64_t>(0.125);
    for (size_t byte = 0; byte < sizeof(sample_bits); ++byte)
        before[45 + byte] = static_cast<uint8_t>(sample_bits >> (byte * 8));
    const int32_t scale_factor = 17;
    for (size_t byte = 0; byte < sizeof(scale_factor); ++byte)
        before[16429 + 3 * sizeof(scale_factor) + byte] = static_cast<uint8_t>(uint32_t(scale_factor) >> (byte * 8));
    audiodec::clear_state(fixture.env);
    ASSERT_TRUE(audiodec::restore_state(fixture.env, before, error)) << error;

    EXPECT_EQ(fixture.ctrl.handle, saved_handle);
    std::vector<uint8_t> after;
    ASSERT_TRUE(audiodec::capture_state(fixture.env, after, error)) << error;
    EXPECT_EQ(after, before);
    EXPECT_EQ(export_sceAudiodecClearContext(fixture.env, 0, "test", &fixture.ctrl), 0);
}

TEST(AudiodecSavestate, MissingHandleReturnsGuestErrorInsteadOfDereferencingNull) {
    AudiodecFixture fixture;
    fixture.ctrl.handle = 0x7777;
    fixture.ctrl.es_data = Ptr<uint8_t>(0x2000);
    fixture.ctrl.pcm_data = Ptr<uint8_t>(0x3000);
    const int invalid_handle = static_cast<int>(0x807F0009u);

    EXPECT_EQ(export_sceAudiodecDecode(fixture.env, 0, "test", &fixture.ctrl), invalid_handle);
    EXPECT_EQ(export_sceAudiodecPartlyDecode(fixture.env, 0, "test", &fixture.ctrl, 0, 16), invalid_handle);
}

TEST(AudiodecSavestate, InvalidSnapshotDoesNotReplaceExistingDecoderMap) {
    AudiodecFixture fixture;
    ASSERT_TRUE(fixture.create_at9());
    std::vector<uint8_t> snapshot;
    std::string error;
    ASSERT_TRUE(audiodec::capture_state(fixture.env, snapshot, error)) << error;
    ASSERT_FALSE(snapshot.empty());
    snapshot.pop_back();

    EXPECT_FALSE(audiodec::validate_state(snapshot, error));
    EXPECT_FALSE(audiodec::restore_state(fixture.env, snapshot, error));
    EXPECT_EQ(export_sceAudiodecClearContext(fixture.env, 0, "test", &fixture.ctrl), 0);
}

TEST(AudiodecSavestate, InvalidReplayPacketFailsPreflightAndPreservesLiveDecoder) {
    AudiodecFixture fixture;
    ASSERT_TRUE(fixture.create_aac());
    std::vector<uint8_t> snapshot;
    std::string error;
    ASSERT_TRUE(audiodec::capture_state(fixture.env, snapshot, error)) << error;
    ASSERT_EQ(snapshot.size(), 48u);

    // One structurally valid Send event claims success, but an invalid AAC
    // packet cannot be replayed. Validation must catch this before load teardown.
    auto append_u32 = [&snapshot](uint32_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8)
            snapshot.push_back(static_cast<uint8_t>(value >> shift));
    };
    snapshot[44] = 1;
    snapshot[45] = snapshot[46] = snapshot[47] = 0;
    snapshot.insert(snapshot.end(), { 1, 0, 1, 0 }); // Send, non-null, expected success, reserved.
    append_u32(0); // es_size_used
    append_u32(0); // samples
    append_u32(1); // one byte of invalid AAC input
    snapshot.push_back(0xFF);

    EXPECT_FALSE(audiodec::validate_state(snapshot, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(audiodec::restore_state(fixture.env, snapshot, error));
    EXPECT_EQ(export_sceAudiodecClearContext(fixture.env, 0, "test", &fixture.ctrl), 0);
}

TEST(AudiodecSavestate, ReplaysWarmAacDecoderAndContinuesWithIdenticalNextPcm) {
    AudiodecFixture fixture;
    ASSERT_TRUE(fixture.create_aac());
    fixture.ctrl.es_data = Ptr<uint8_t>(0x2000);
    fixture.ctrl.pcm_data = Ptr<uint8_t>(0x4000);
    // A raw stereo AAC silence frame, with zero padding in the guest arena.
    const std::array<uint8_t, 6> packet{0x21, 0x10, 0x04, 0x60, 0x8c, 0x1c};
    fixture.ctrl.es_size_max = packet.size();
    std::memcpy(fixture.env.mem.memory.get() + 0x2000, packet.data(), packet.size());
    for (int frame = 0; frame < 8; ++frame)
        ASSERT_EQ(export_sceAudiodecDecode(fixture.env, 0, "test", &fixture.ctrl), 0);

    std::vector<uint8_t> snapshot;
    std::string error;
    ASSERT_TRUE(audiodec::capture_state(fixture.env, snapshot, error)) << error;
    const auto next_uid = fixture.env.kernel.get_next_uid();
    std::shared_ptr<audiodec::PreparedState> prepared;
    ASSERT_TRUE(audiodec::prepare_state(snapshot, prepared, error)) << error;
    EXPECT_EQ(audiodec::decoder_handles(*prepared), std::vector<int32_t>{fixture.ctrl.handle});
    // Preparation must not allocate a guest UID or alter the active decoder.
    EXPECT_EQ(fixture.env.kernel.get_next_uid(), next_uid + 1);
    ASSERT_EQ(export_sceAudiodecDecode(fixture.env, 0, "test", &fixture.ctrl), 0);
    ASSERT_EQ(fixture.ctrl.es_size_used, 6u);
    ASSERT_EQ(fixture.ctrl.pcm_size_given, 4096u);
    const std::vector<uint8_t> expected(fixture.env.mem.memory.get() + 0x4000,
        fixture.env.mem.memory.get() + 0x4000 + fixture.ctrl.pcm_size_given);
    audiodec::clear_state(fixture.env);
    audiodec::apply_state(fixture.env, std::move(prepared));
    std::memset(fixture.env.mem.memory.get() + 0x4000, 0x7f, expected.size());
    ASSERT_EQ(export_sceAudiodecDecode(fixture.env, 0, "test", &fixture.ctrl), 0);
    EXPECT_EQ(fixture.ctrl.es_size_used, 6u);
    EXPECT_EQ(fixture.ctrl.pcm_size_given, expected.size());
    EXPECT_EQ(std::vector<uint8_t>(fixture.env.mem.memory.get() + 0x4000,
        fixture.env.mem.memory.get() + 0x4000 + expected.size()), expected);
}
