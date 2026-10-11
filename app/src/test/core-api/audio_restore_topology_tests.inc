// Compile the production parser/restore body into this standalone test TU,
// as the archive probes do. This exercises private read_audio without a
// test-only application API or a second implementation of restoration.
#include "../../main/cpp/emucorev/src/savestate/savestate.cpp"
#include <audio/impl/sdl_audio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

namespace {
class AudioRestoreTopology : public testing::Test {
protected:
    std::unique_ptr<EmuEnvState> env;

    void SetUp() override {
        // The isolated native test process has no running emulator session.
        SDL_SetMainReady();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_FORMAT, "S16LE"));
        ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_FREQUENCY, "44100"));
        ASSERT_TRUE(SDL_InitSubSystem(SDL_INIT_AUDIO)) << SDL_GetError();
        env = std::make_unique<EmuEnvState>();
        ASSERT_TRUE(env->audio.init("SDL"));
        env->audio.switch_state(true);
    }

    void TearDown() override {
        env.reset();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        SDL_ResetHint(SDL_HINT_AUDIO_DRIVER);
        SDL_ResetHint(SDL_HINT_AUDIO_FORMAT);
        SDL_ResetHint(SDL_HINT_AUDIO_FREQUENCY);
    }

    void add_port(int id) {
        auto port = env->audio.open_port_for_restore(2, 48000, 1024);
        ASSERT_TRUE(port) << SDL_GetError();
        port->type = 0;
        port->len = 1024;
        port->freq = 48000;
        port->mode = 1;
        port->left_channel_volume = SCE_AUDIO_OUT_MAX_VOL;
        port->right_channel_volume = SCE_AUDIO_OUT_MAX_VOL;
        env->audio.out_ports.emplace(id, std::move(port));
        env->audio.next_port_id = id + 1;
    }

    SDL_AudioStream *stream(int id) {
        return static_cast<SDLAudioOutPort &>(*env->audio.out_ports.at(id)).stream.get();
    }

    void enqueue_and_prime(int id) {
        std::array<int16_t, 2048> pcm;
        for (size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = static_cast<int16_t>((i * 193 + id * 997) % 24000 - 12000);
        for (int i = 0; i < 4; ++i)
            ASSERT_TRUE(SDL_PutAudioStreamData(stream(id), pcm.data(), sizeof(pcm)));
        // 48k -> 44.1k leaves a nonzero resampler phase, not just queued bytes.
        std::array<int16_t, 80> prefix; // Whole frames for both S16 and F32 stereo.
        ASSERT_EQ(SDL_GetAudioStreamData(stream(id), prefix.data(), sizeof(prefix)), sizeof(prefix));
    }

    std::vector<uint8_t> capture_section(std::map<int, std::vector<uint8_t>> &queues) {
        emucorev::savestate::BufferWriter bytes;
        bytes.u32(env->audio.next_port_id);
        bytes.u32(static_cast<uint32_t>(env->audio.out_ports.size()));
        for (const auto &[id, port] : env->audio.out_ports) {
            auto &queue = queues[id];
            if (!env->audio.adapter->save_port_state(*port, queue)) {
                ADD_FAILURE() << "cannot capture real SDL queue: " << SDL_GetError();
                return {};
            }
            bytes.i32(id);
            bytes.i32(port->left_channel_volume);
            bytes.i32(port->right_channel_volume);
            bytes.value(port->volume);
            bytes.i32(port->type);
            bytes.i32(port->len);
            bytes.i32(port->freq);
            bytes.i32(port->mode);
            bytes.u64(port->len_microseconds);
            bytes.u64(0);
            bytes.u32(env->audio.adapter->state_codec());
            bytes.u32(static_cast<uint32_t>(queue.size()));
            bytes.append(queue.data(), queue.size());
        }
        return bytes.data();
    }

    void expect_queue_and_pause(int id, const std::vector<uint8_t> &saved) {
        std::vector<uint8_t> restored;
        ASSERT_TRUE(env->audio.adapter->save_port_state(*env->audio.out_ports.at(id), restored));
        EXPECT_EQ(restored, saved); // Queue bytes, history, fractional phase, maps and gain.
        EXPECT_TRUE(SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(stream(id))));
        EXPECT_FALSE(env->audio.out_ports.at(id)->stopping);
    }
};

TEST_F(AudioRestoreTopology, RunningSinglePortLoadRestoresS16QueueWithoutOldMixingStream) {
    add_port(1);
    ASSERT_EQ(env->audio.out_ports.size(), 1u);
    enqueue_and_prime(1);
    SDL_AudioSpec src{}, dst{};
    ASSERT_TRUE(SDL_GetAudioStreamFormat(stream(1), &src, &dst));
    ASSERT_EQ(dst.format, SDL_AUDIO_S16LE);
    std::weak_ptr<AudioOutPort> old = env->audio.out_ports.at(1);
    std::map<int, std::vector<uint8_t>> queues;
    const auto section = capture_section(queues);
    std::string error;
    ASSERT_TRUE(emucorev::savestate::read_audio(*env, section, error)) << error;
    EXPECT_TRUE(old.expired());
    ASSERT_EQ(env->audio.out_ports.size(), 1u);
    EXPECT_EQ(env->audio.next_port_id, 2);
    expect_queue_and_pause(1, queues.at(1));
}

TEST_F(AudioRestoreTopology, ColdTwoPortLoadBindsCompleteFloatMixingTopologyBeforeQueues) {
    add_port(1);
    add_port(2);
    ASSERT_EQ(env->audio.out_ports.size(), 2u);
    for (int id : {1, 2}) {
        enqueue_and_prime(id);
        SDL_AudioSpec src{}, dst{};
        ASSERT_TRUE(SDL_GetAudioStreamFormat(stream(id), &src, &dst));
        ASSERT_EQ(dst.format, SDL_AUDIO_F32);
    }
    std::map<int, std::vector<uint8_t>> queues;
    const auto section = capture_section(queues);
    env->audio.out_ports.clear(); // Startup load has no previous guest ports.
    std::string error;
    ASSERT_TRUE(emucorev::savestate::read_audio(*env, section, error)) << error;
    ASSERT_EQ(env->audio.out_ports.size(), 2u);
    EXPECT_EQ(env->audio.next_port_id, 3);
    for (int id : {1, 2})
        expect_queue_and_pause(id, queues.at(id));
}

TEST_F(AudioRestoreTopology, RestoredDownsamplerCanBeSavedAgainWithMoreThanOneFrameOfPhase) {
    add_port(1);
    enqueue_and_prime(1); // 40 output frames at 48k -> 44.1k.
    std::map<int, std::vector<uint8_t>> queues;
    const auto section = capture_section(queues);
    std::string error;
    ASSERT_TRUE(emucorev::savestate::read_audio(*env, section, error)) << error;

    // Six more output frames leave a legitimate phase of 1.068 source
    // frames. SDL carries a whole frame across downsampling calls.
    std::array<int16_t, 12> continued;
    ASSERT_EQ(SDL_GetAudioStreamData(stream(1), continued.data(), sizeof(continued)), sizeof(continued));
    std::vector<uint8_t> after;
    ASSERT_TRUE(env->audio.adapter->save_port_state(*env->audio.out_ports.at(1), after));
    ASSERT_GT(after.size(), 68u);
    // Default channel maps have three zero counts at offsets 40,44,48.
    uint32_t maps[3]; std::memcpy(maps, after.data() + 40, sizeof(maps));
    ASSERT_EQ(maps[0] | maps[1] | maps[2], 0u);
    int64_t phase; std::memcpy(&phase, after.data() + 60, sizeof(phase));
    ASSERT_GE(phase, INT64_C(0x100000000));
    EXPECT_TRUE(validate_audio_port_snapshot(1, 4096, after));

    SDL_AudioSpec src{}, dst{};
    ASSERT_TRUE(SDL_GetAudioStreamFormat(stream(1), &src, &dst));
    const int64_t step = (((int64_t(src.freq) << 32) - 1) / dst.freq) + 1;
    ASSERT_LT(phase, step);
    auto malformed = after;
    std::memcpy(malformed.data() + 60, &step, sizeof(step));
    EXPECT_FALSE(validate_audio_port_snapshot(1, 4096, malformed));
    const int64_t negative_overflow = -INT64_C(0x100000000) - 1;
    std::memcpy(malformed.data() + 60, &negative_overflow, sizeof(negative_overflow));
    EXPECT_FALSE(validate_audio_port_snapshot(1, 4096, malformed));
    // Reject a finite ratio whose scaled input frequency cannot be cast to int.
    const int64_t zero_phase = 0;
    const float invalid_ratio = 100000.0f;
    std::memcpy(malformed.data() + 60, &zero_phase, sizeof(zero_phase));
    std::memcpy(malformed.data() + 52, &invalid_ratio, sizeof(invalid_ratio));
    EXPECT_FALSE(validate_audio_port_snapshot(1, 4096, malformed));

    std::map<int, std::vector<uint8_t>> resaved;
    const auto new_section = capture_section(resaved);
    ASSERT_TRUE(emucorev::savestate::preflight_audio(new_section, error)) << error;
    std::array<int16_t, 256> expected, actual;
    ASSERT_EQ(SDL_GetAudioStreamData(stream(1), expected.data(), sizeof(expected)), sizeof(expected));
    ASSERT_TRUE(emucorev::savestate::read_audio(*env, new_section, error)) << error;
    ASSERT_EQ(SDL_GetAudioStreamData(stream(1), actual.data(), sizeof(actual)), sizeof(actual));
    EXPECT_EQ(actual, expected); // Preserve phase/history, not merely allow Save.
}

TEST_F(AudioRestoreTopology, InvalidQueueIsRejectedBeforeChangingExistingPorts) {
    add_port(1);
    ASSERT_EQ(env->audio.out_ports.size(), 1u);
    enqueue_and_prime(1);
    std::map<int, std::vector<uint8_t>> queues;
    auto section = capture_section(queues);
    ASSERT_GT(section.size(), 64u);
    // Audio table header 8 + fixed port header 56: poison SDL snapshot version.
    section[64] = 0xFF;
    const auto old = env->audio.out_ports.at(1);
    std::string error;
    EXPECT_FALSE(emucorev::savestate::read_audio(*env, section, error));
    EXPECT_EQ(env->audio.out_ports.at(1), old);
    EXPECT_EQ(env->audio.next_port_id, 2);
    expect_queue_and_pause(1, queues.at(1));
}

TEST_F(AudioRestoreTopology, RuntimeFormatFailureReleasesObsoleteAndReplacementStreams) {
    add_port(1);
    ASSERT_EQ(env->audio.out_ports.size(), 1u);
    enqueue_and_prime(1);
    std::weak_ptr<AudioOutPort> old = env->audio.out_ports.at(1);
    std::map<int, std::vector<uint8_t>> queues;
    auto section = capture_section(queues);
    ASSERT_GT(section.size(), 84u);
    // A valid saved output format can still disagree with this host device.
    // This exercises failure AFTER pure validation and port reconstruction.
    const uint32_t different_output_format = SDL_AUDIO_S32LE;
    std::memcpy(section.data() + 64 + 16, &different_output_format, sizeof(different_output_format));
    std::string error;
    ASSERT_TRUE(emucorev::savestate::preflight_audio(section, error)) << error;
    EXPECT_FALSE(emucorev::savestate::read_audio(*env, section, error));
    EXPECT_EQ(error, "failed to restore audio playback queue");
    EXPECT_TRUE(old.expired());
    EXPECT_TRUE(env->audio.out_ports.empty());
    // No replacement binding survived failure: a new single port stays S16.
    const auto probe = env->audio.open_port_for_restore(2, 48000, 1024);
    ASSERT_TRUE(probe);
    const auto probe_stream = static_cast<SDLAudioOutPort &>(*probe).stream.get();
    SDL_AudioSpec src{}, dst{};
    ASSERT_TRUE(SDL_GetAudioStreamFormat(probe_stream, &src, &dst));
    EXPECT_EQ(dst.format, SDL_AUDIO_S16LE);
    EXPECT_TRUE(SDL_AudioDevicePaused(SDL_GetAudioStreamDevice(probe_stream)));
}
} // namespace
