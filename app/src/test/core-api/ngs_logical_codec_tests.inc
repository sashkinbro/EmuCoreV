// Shared by the host serializer target and the actual emulator core API target.
#include <ngs/modules/atrac9.h>
#include <ngs/modules/compressor.h>
#include <ngs/modules/envelope.h>
#include <ngs/modules/logical_state_io.h>
#include <ngs/modules/player.h>

TEST(NgsLogicalCodec, PlayerRoundtripIsExactAndCaptureDoesNotConsumePcm) {
    ngs::PlayerLogicalState state;
    state.decoded_pcm.samples = { 0.25f, -0.5f, 0.75f, -1.0f };
    state.decoded_pcm.read_offset_frames = 1;
    state.rate_resampler.input_history.samples = { -0.0f, 0.125f, 0.875f, -0.25f };
    state.rate_resampler.input_history.read_offset_frames = 1;
    state.rate_resampler.needs_reset = true;
    state.rate_resampler.source_rate = 44100;
    state.rate_resampler.dest_rate = 48000;
    state.rate_resampler.total_input_frames = 161;
    state.adpcm_buffer = { 0x21, 0x43, 0x65 };
    state.adpcm_history[0] = { 1, -2, 3, -4 };
    state.adpcm_history[1] = { -5, 6, -7, 8 };
    state.current_loop_count = -1;
    state.requested_initial_buffer = true;
    state.starved_ticks = 7;
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_player(&state, blob);
    std::unique_ptr<ngs::PlayerLogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_player(blob, restored));
    ASSERT_NE(restored, nullptr);
    EXPECT_EQ(state.decoded_pcm.read_offset_frames, 1u);
    EXPECT_EQ(restored->decoded_pcm.samples, state.decoded_pcm.samples);
    EXPECT_EQ(restored->decoded_pcm.read_offset_frames, 1u);
    EXPECT_EQ(restored->rate_resampler.input_history.samples, state.rate_resampler.input_history.samples);
    EXPECT_EQ(restored->rate_resampler.input_history.read_offset_frames, 1u);
    EXPECT_TRUE(restored->rate_resampler.needs_reset);
    EXPECT_EQ(restored->adpcm_buffer, state.adpcm_buffer);
    EXPECT_EQ(restored->adpcm_history[0].hist4, -4);
    EXPECT_EQ(restored->adpcm_history[1].hist1, -5);
    EXPECT_EQ(restored->current_loop_count, -1);
    EXPECT_TRUE(restored->requested_initial_buffer);
    EXPECT_EQ(restored->starved_ticks, 7);
    std::vector<uint8_t> recaptured;
    ngs::logical_state_io::capture_player(restored.get(), recaptured);
    EXPECT_EQ(recaptured, blob); // Every encoded field, including all predictors and float bits.
}

TEST(NgsLogicalCodec, EnvelopeRoundtripPreservesReleaseAndSegmentPhase) {
    ngs::EnvelopeLogicalState state;
    state.position_in_segment_ms = 3.25;
    state.total_position_ms = 123.5;
    state.current_point = 2;
    state.releasing = true;
    state.release_start_height = 0.75f;
    state.release_height = 0.375f;
    state.release_position_ms = 8.125;
    state.completed_at_zero_ms = 0.5;
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_envelope(&state, blob);
    std::unique_ptr<ngs::EnvelopeLogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_envelope(blob, restored));
    EXPECT_DOUBLE_EQ(restored->position_in_segment_ms, 3.25);
    EXPECT_DOUBLE_EQ(restored->total_position_ms, 123.5);
    EXPECT_DOUBLE_EQ(restored->release_position_ms, 8.125);
    EXPECT_EQ(restored->current_point, 2);
    EXPECT_TRUE(restored->releasing);
    std::vector<uint8_t> recaptured;
    ngs::logical_state_io::capture_envelope(restored.get(), recaptured);
    EXPECT_EQ(recaptured, blob);
}

TEST(NgsLogicalCodec, Atrac9RoundtripIncludesMdctHistoryAndHealingPhase) {
    ngs::Atrac9LogicalState state;
    state.decoder_config = 0xE03F74FE;
    state.saved_state.prev_values[0][0] = -0.25;
    state.saved_state.prev_values[1][255] = 0.875;
    state.decoded_pcm.samples = { 0.125f, -0.875f };
    state.rate_resampler.input_history.samples = { 0.25f, 0.75f };
    state.rate_resampler.needs_reset = true;
    state.rate_resampler.source_rate = 44100;
    state.rate_resampler.dest_rate = 48000;
    state.rate_resampler.total_input_frames = 161;
    state.superframe_staging = { 1, 2, 3 };
    state.current_loop_count = -1;
    state.starved_ticks = 3;
    state.in_underrun_wait = true;
    state.diag_superframes = 37;
    state.diag_silent_streak = 31;
    state.diag_reported_silent = true;
    state.diag_reported_onset = true;
    state.heal_done_this_episode = true;
    state.diag_ring[15] = { 36, -1, 2, 1, 0x123456789ABCDEF0ULL, 0.25f };
    state.diag_ring_next = 37;
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_atrac9(&state, blob);
    std::unique_ptr<ngs::Atrac9LogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_atrac9(blob, restored));
    EXPECT_DOUBLE_EQ(restored->saved_state.prev_values[1][255], 0.875);
    EXPECT_EQ(restored->diag_silent_streak, 31u);
    EXPECT_TRUE(restored->heal_done_this_episode);
    EXPECT_EQ(restored->diag_ring[15].head, 0x123456789ABCDEF0ULL);
    std::vector<uint8_t> recaptured;
    ngs::logical_state_io::capture_atrac9(restored.get(), recaptured);
    EXPECT_EQ(recaptured, blob);
}

TEST(NgsLogicalCodec, MalformedPlayerNeverPublishesPartialState) {
    ngs::PlayerLogicalState state;
    state.decoded_pcm.samples = { 0.25f, 0.5f };
    state.adpcm_buffer = { 1, 2, 3 };
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_player(&state, blob);
    auto restored = std::make_unique<ngs::PlayerLogicalState>();
    restored->starved_ticks = 42;
    const auto *sentinel = restored.get();
    // Every truncation, including one after all large vectors were allocated.
    for (size_t length = 0; length < blob.size(); ++length) {
        std::vector<uint8_t> truncated(blob.begin(), blob.begin() + length);
        EXPECT_FALSE(ngs::logical_state_io::restore_player(truncated, restored)) << length;
        EXPECT_EQ(restored.get(), sentinel);
        EXPECT_EQ(restored->starved_ticks, 42);
    }
    for (size_t position : { size_t(0), size_t(4), size_t(8) }) {
        auto invalid = blob;
        invalid[position] = 0xFF; // wrong module, unknown version, invalid presence bool
        EXPECT_FALSE(ngs::logical_state_io::restore_player(invalid, restored));
        EXPECT_EQ(restored.get(), sentinel);
    }
    auto trailing = blob;
    trailing.push_back(0);
    EXPECT_FALSE(ngs::logical_state_io::restore_player(trailing, restored));
    EXPECT_EQ(restored.get(), sentinel);
    // PCM read cursor beyond the number of saved stereo frames.
    auto bad_cursor = blob;
    bad_cursor[9] = 2;
    EXPECT_FALSE(ngs::logical_state_io::restore_player(bad_cursor, restored));
    // Implausible vector length must be checked against remaining bytes before allocation.
    auto bad_size = blob;
    for (size_t i = 13; i < 17; ++i) bad_size[i] = 0xFF;
    EXPECT_FALSE(ngs::logical_state_io::restore_player(bad_size, restored));
    EXPECT_EQ(restored.get(), sentinel);
}

TEST(NgsLogicalCodec, OddStereoQueueAndNonBooleanStateAreRejected) {
    ngs::PlayerLogicalState state;
    state.decoded_pcm.samples = { 0.25f };
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_player(&state, blob);
    std::unique_ptr<ngs::PlayerLogicalState> restored;
    EXPECT_FALSE(ngs::logical_state_io::restore_player(blob, restored));
    state.decoded_pcm.samples.clear();
    ngs::logical_state_io::capture_player(&state, blob);
    blob[25] = 2; // needs_reset after two empty queue records
    EXPECT_FALSE(ngs::logical_state_io::restore_player(blob, restored));
    EXPECT_EQ(restored, nullptr);
}

TEST(NgsLogicalCodec, NeverInitializedEnvelopeIsExplicitAndClearsPreviousState) {
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_envelope<ngs::EnvelopeLogicalState>(nullptr, blob);
    auto restored = std::make_unique<ngs::EnvelopeLogicalState>();
    restored->releasing = true;
    EXPECT_TRUE(ngs::logical_state_io::restore_envelope(blob, restored));
    EXPECT_EQ(restored, nullptr);
    EXPECT_FALSE(ngs::logical_state_io::restore_envelope({}, restored));
}

TEST(NgsLogicalCodec, Atrac9AndEnvelopeRejectMalformedTailAtomically) {
    ngs::Atrac9LogicalState at9;
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_atrac9(&at9, blob);
    auto restored_at9 = std::make_unique<ngs::Atrac9LogicalState>();
    restored_at9->diag_silent_streak = 42;
    blob.pop_back();
    EXPECT_FALSE(ngs::logical_state_io::restore_atrac9(blob, restored_at9));
    EXPECT_EQ(restored_at9->diag_silent_streak, 42u);
    ngs::EnvelopeLogicalState envelope;
    ngs::logical_state_io::capture_envelope(&envelope, blob);
    auto restored_envelope = std::make_unique<ngs::EnvelopeLogicalState>();
    restored_envelope->current_point = 3;
    blob.pop_back();
    EXPECT_FALSE(ngs::logical_state_io::restore_envelope(blob, restored_envelope));
    EXPECT_EQ(restored_envelope->current_point, 3);
}

TEST(NgsLogicalCodec, UnsafeEnvelopePointAndNonFinitePhaseAreRejected) {
    ngs::EnvelopeLogicalState state;
    std::vector<uint8_t> blob;
    std::unique_ptr<ngs::EnvelopeLogicalState> restored;
    for (int point : { -1, 4, INT32_MAX }) {
        state.current_point = point;
        ngs::logical_state_io::capture_envelope(&state, blob);
        EXPECT_FALSE(ngs::logical_state_io::restore_envelope(blob, restored));
    }
    state.current_point = 0;
    state.release_position_ms = std::numeric_limits<double>::infinity();
    ngs::logical_state_io::capture_envelope(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_envelope(blob, restored));
    EXPECT_EQ(restored, nullptr);
}

TEST(NgsLogicalCodec, CompressorPreservesAudibleGainSmoothingAndRejectsMalformedState) {
    ngs::CompressorLogicalState state;
    state.envelope[0] = 0.125f;
    state.envelope[1] = 0.75f;
    state.applied_gain[0] = 0.25f;
    state.applied_gain[1] = 0.5f;
    state.reduction_db[0] = -12.5f;
    state.reduction_db[1] = -6.25f;
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_compressor(&state, blob);
    std::unique_ptr<ngs::CompressorLogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_compressor(blob, restored));
    EXPECT_FLOAT_EQ(restored->envelope[1], 0.75f);
    EXPECT_FLOAT_EQ(restored->applied_gain[0], 0.25f);
    EXPECT_FLOAT_EQ(restored->reduction_db[1], -6.25f);
    std::vector<uint8_t> recaptured;
    ngs::logical_state_io::capture_compressor(restored.get(), recaptured);
    EXPECT_EQ(recaptured, blob);
    const auto *sentinel = restored.get();
    for (size_t length = 0; length < blob.size(); ++length) {
        EXPECT_FALSE(ngs::logical_state_io::restore_compressor(std::vector<uint8_t>(blob.begin(), blob.begin() + length), restored));
        EXPECT_EQ(restored.get(), sentinel);
        EXPECT_FLOAT_EQ(restored->applied_gain[0], 0.25f);
    }
    state.envelope[0] = std::numeric_limits<float>::quiet_NaN();
    ngs::logical_state_io::capture_compressor(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_compressor(blob, restored));
    EXPECT_EQ(restored.get(), sentinel);
}

TEST(NgsLogicalCodec, InvalidAtrac9ConfigAndPredictorIndicesAreRejectedBeforeConstruction) {
    ngs::Atrac9LogicalState state;
    state.decoder_config = 0xE03F7EFE; // channel config 7 indexes beyond LibAtrac9's six-entry table
    std::vector<uint8_t> blob;
    std::unique_ptr<ngs::Atrac9LogicalState> restored;
    ngs::logical_state_io::capture_atrac9(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_atrac9(blob, restored));
    state.decoder_config = 0xE03F74FE;
    state.saved_state.channels[0].scale_factors_prev[7] = 32;
    ngs::logical_state_io::capture_atrac9(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_atrac9(blob, restored));
    state.saved_state.channels[0].scale_factors_prev[7] = 0;
    state.saved_state.blocks[0].extension_unit = 31;
    ngs::logical_state_io::capture_atrac9(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_atrac9(blob, restored));
    EXPECT_EQ(restored, nullptr);
}

TEST(NgsLogicalCodec, ResamplerHistoryCannotExceedItsAbsoluteInputPosition) {
    ngs::PlayerLogicalState state;
    state.rate_resampler.input_history.samples = { 0.25f, 0.5f };
    state.rate_resampler.source_rate = 44100;
    state.rate_resampler.dest_rate = 48000;
    // A history frame with zero absolute input position cannot be replayed correctly.
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_player(&state, blob);
    std::unique_ptr<ngs::PlayerLogicalState> restored;
    EXPECT_FALSE(ngs::logical_state_io::restore_player(blob, restored));
    EXPECT_EQ(restored, nullptr);
    state.rate_resampler.source_rate = 2147483647;
    state.rate_resampler.dest_rate = 2147483646;
    state.rate_resampler.total_input_frames = 1000000000;
    ngs::logical_state_io::capture_player(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_player(blob, restored)); // bounded prefix replay work
    state.rate_resampler.source_rate = 1;
    state.rate_resampler.dest_rate = 2147483647;
    state.rate_resampler.total_input_frames = 1;
    ngs::logical_state_io::capture_player(&state, blob);
    EXPECT_FALSE(ngs::logical_state_io::restore_player(blob, restored)); // bounded scratch allocation
}
