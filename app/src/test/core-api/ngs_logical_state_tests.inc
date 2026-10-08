// Exercise the production module serializers, not copies of their encoding.
#include <mem/state.h>
#include <ngs/state.h>
#include <ngs/modules/envelope.h>
#include <ngs/modules/player.h>
#include <ngs/rate_resampler.h>
#include <ngs/modules/logical_state_io.h>

TEST(NgsLogicalState, PlayerPreservesQueuedPcmPartialAdpcmAndResamplerHistory) {
    ngs::PlayerModule module;
    ngs::ModuleData original;
    module.initialize_voice_data(original);
    auto *before = original.get_logical_state<ngs::PlayerLogicalState>();
    before->decoded_pcm.samples = { 0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f };
    before->decoded_pcm.read_offset_frames = 1;
    before->rate_resampler.input_history.samples = { 0.7f, -0.8f, 0.9f, -1.0f };
    before->rate_resampler.input_history.read_offset_frames = 1;
    before->rate_resampler.needs_reset = true;
    before->rate_resampler.source_rate = 44100;
    before->rate_resampler.dest_rate = 48000;
    before->rate_resampler.total_input_frames = 161;
    before->adpcm_buffer = { 0x12, 0x34, 0x56 };
    before->current_loop_count = -1;
    before->adpcm_history[0] = { 100, -200, 300, -400 };
    before->adpcm_history[1] = { -500, 600, -700, 800 };
    before->requested_initial_buffer = true;
    before->starved_ticks = 7;
    std::vector<uint8_t> blob;
    module.capture_logical_state(original, blob);
    ASSERT_FALSE(blob.empty());
    EXPECT_EQ(before->decoded_pcm.read_offset_frames, 1u); // Capture is nonmutating.

    ngs::ModuleData restored;
    module.initialize_voice_data(restored);
    ASSERT_TRUE(module.restore_logical_state(restored, blob));
    const auto *after = restored.get_logical_state<ngs::PlayerLogicalState>();
    EXPECT_EQ(after->decoded_pcm.samples, before->decoded_pcm.samples);
    EXPECT_EQ(after->decoded_pcm.read_offset_frames, 1u);
    EXPECT_EQ(after->rate_resampler.input_history.samples, before->rate_resampler.input_history.samples);
    EXPECT_EQ(after->rate_resampler.input_history.read_offset_frames, 1u);
    EXPECT_TRUE(after->rate_resampler.needs_reset);
    EXPECT_EQ(after->adpcm_buffer, before->adpcm_buffer);
    EXPECT_EQ(after->current_loop_count, -1);
    EXPECT_EQ(after->adpcm_history[0].hist4, -400);
    EXPECT_EQ(after->adpcm_history[1].hist1, -500);
    EXPECT_TRUE(after->requested_initial_buffer);
    EXPECT_EQ(after->starved_ticks, 7);
}

TEST(NgsLogicalState, EnvelopeResumesItsReleaseAndSegmentPhase) {
    ngs::EnvelopeModule module;
    ngs::ModuleData original;
    module.initialize_voice_data(original);
    auto *before = original.get_logical_state<ngs::EnvelopeLogicalState>();
    before->position_in_segment_ms = 3.25;
    before->total_position_ms = 123.5;
    before->current_point = 2;
    before->releasing = true;
    before->release_start_height = 0.75f;
    before->release_height = 0.375f;
    before->release_position_ms = 8.125;
    before->completed_at_zero_ms = 0.5;
    std::vector<uint8_t> blob;
    module.capture_logical_state(original, blob);
    ASSERT_FALSE(blob.empty());
    ngs::ModuleData restored;
    module.initialize_voice_data(restored);
    ASSERT_TRUE(module.restore_logical_state(restored, blob));
    const auto *after = restored.get_logical_state<ngs::EnvelopeLogicalState>();
    EXPECT_DOUBLE_EQ(after->position_in_segment_ms, 3.25);
    EXPECT_DOUBLE_EQ(after->total_position_ms, 123.5);
    EXPECT_EQ(after->current_point, 2);
    EXPECT_TRUE(after->releasing);
    EXPECT_FLOAT_EQ(after->release_start_height, 0.75f);
    EXPECT_FLOAT_EQ(after->release_height, 0.375f);
    EXPECT_DOUBLE_EQ(after->release_position_ms, 8.125);
    EXPECT_DOUBLE_EQ(after->completed_at_zero_ms, 0.5);
}

TEST(NgsLogicalState, InvalidPlayerPayloadLeavesLogicalAndRuntimeObjectsUntouched) {
    ngs::PlayerModule module;
    ngs::ModuleData data;
    module.initialize_voice_data(data);
    auto *logical = data.get_logical_state<ngs::PlayerLogicalState>();
    auto *runtime = data.get_runtime_state<ngs::PlayerRuntimeState>();
    logical->adpcm_buffer = { 0x12, 0x34 };
    std::vector<uint8_t> blob;
    module.capture_logical_state(data, blob);
    blob.pop_back();
    EXPECT_FALSE(module.restore_logical_state(data, blob));
    EXPECT_EQ(data.logical_state.get(), logical);
    EXPECT_EQ(data.runtime_state.get(), runtime);
    EXPECT_EQ(logical->adpcm_buffer, (std::vector<uint8_t>{ 0x12, 0x34 }));
    module.capture_logical_state(data, blob);
    ASSERT_TRUE(module.restore_logical_state(data, blob));
    EXPECT_EQ(data.runtime_state, nullptr); // Recreated lazily from restored predictors/history.
    EXPECT_EQ(data.get_logical_state<ngs::PlayerLogicalState>()->adpcm_buffer, (std::vector<uint8_t>{ 0x12, 0x34 }));
}

#include "ngs_decoder_tests.inc"

TEST(NgsLogicalState, RestoredHistoryRebuildsIntegerRateResamplerPcm) {
    ngs::PlayerLogicalState state;
    ngs::StereoRateResamplerRuntimeState original;
    ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(original, state.rate_resampler, 24000, 48000));
    std::vector<float> input(2048);
    for (size_t i = 0; i < input.size(); ++i)
        input[i] = static_cast<float>(static_cast<int>(i % 31) - 15) / 32.0f;
    ngs::PCMFrameQueue initial;
    ASSERT_GT(ngs::process_stereo_rate_resampler(original, state.rate_resampler,
                  reinterpret_cast<const uint8_t *>(input.data()), input.size() / 2, initial), 0u);
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_player(&state, blob);
    std::unique_ptr<ngs::PlayerLogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_player(blob, restored));
    ngs::StereoRateResamplerRuntimeState rebuilt;
    ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(rebuilt, restored->rate_resampler, 24000, 48000));
    ngs::PCMFrameQueue expected, actual;
    ngs::process_stereo_rate_resampler(original, state.rate_resampler,
        reinterpret_cast<const uint8_t *>(input.data()), input.size() / 2, expected);
    ngs::process_stereo_rate_resampler(rebuilt, restored->rate_resampler,
        reinterpret_cast<const uint8_t *>(input.data()), input.size() / 2, actual);
    EXPECT_EQ(actual.samples, expected.samples);
    ngs::destroy_stereo_rate_resampler(original);
    ngs::destroy_stereo_rate_resampler(rebuilt);
}

TEST(NgsLogicalState, CaptureRejectsCallbackBoundaryAndPendingOperationWithoutChangingOutputs) {
    MemState mem;
    ngs::State state;
    ngs::System system(Ptr<void>(0), 0);
    state.systems.push_back(&system);
    Ptr<ngs::VoiceDefinition> definitions(0x1234);
    std::vector<ngs::SystemInitInfo> systems(1);
    systems[0].address = 0x5678;
    std::vector<ngs::RackInitInfo> racks(1);
    racks[0].address = 0x9ABC;
    system.voice_scheduler.is_updating = true;
    EXPECT_FALSE(ngs::capture_state(state, mem, definitions, systems, racks));
    EXPECT_EQ(definitions.address(), 0x1234u);
    ASSERT_EQ(systems.size(), 1u);
    EXPECT_EQ(systems[0].address, 0x5678u);
    ASSERT_EQ(racks.size(), 1u);
    EXPECT_EQ(racks[0].address, 0x9ABCu);
    system.voice_scheduler.is_updating = false;
    system.voice_scheduler.operations_pending.push({});
    EXPECT_FALSE(ngs::capture_state(state, mem, definitions, systems, racks));
    EXPECT_EQ(definitions.address(), 0x1234u);
    system.voice_scheduler.operations_pending.pop();
    EXPECT_TRUE(ngs::capture_state(state, mem, definitions, systems, racks));
    EXPECT_EQ(definitions.address(), 0u);
    EXPECT_TRUE(systems.empty());
    EXPECT_TRUE(racks.empty());
}
