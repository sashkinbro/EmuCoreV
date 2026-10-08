#include <ngs/modules/atrac9.h>
#include <ngs/modules/logical_state_io.h>
namespace at9_test {
struct Block;
struct Frame;
extern "C" {
#include <structures.h>
#include <band_extension.h>
#include <imdct.h>
}
}

namespace {
void prepare_at9_noise_frame(at9_test::Frame &frame) {
    auto &block = frame.Blocks[0];
    block.BandExtensionEnabled = 1;
    block.HasExtensionData = 1;
    // The bitstream decoder only admits mode 1 when BexGroupInfo.BandCount > 2.
    // Unit 16 gives band count 3 and six noise units (22 - 16); unit 13 would
    // force mode 4 and overrun the mode-1 helper's six-entry scale workspace.
    block.QuantizationUnitCount = 16;
    for (int channel = 0; channel < block.ChannelCount; ++channel) {
        auto &c = block.Channels[channel];
        c.BexMode = 1;
        std::fill_n(c.ScaleFactors, 31, 1);
        std::fill_n(c.Spectra, MAX_FRAME_SAMPLES, 0.001);
    }
}
void synthesize_at9_noise_frame(at9_test::Frame &frame) {
    at9_test::ApplyBandExtension(&frame.Blocks[0]);
    for (int channel = 0; channel < frame.Blocks[0].ChannelCount; ++channel) {
        auto &c = frame.Blocks[0].Channels[channel];
        at9_test::RunImdct(&c.Mdct, c.Spectra, c.Pcm);
    }
}
} // namespace

TEST(NgsAtrac9Decoder, RestoredBandExtensionNoiseAndMdctProduceIdenticalNextPcm) {
    // Valid stereo/48kHz config; production band extension and IMDCT provide an
    // encoded-fixture-free test of persistent decoder history between frames.
    const uint32_t config = std::bit_cast<uint32_t>(std::array<uint8_t, 4>{ 0xFE, 0x74, 0x3F, 0xE0 });
    Atrac9DecoderState original(config), rebuilt(config);
    ASSERT_TRUE(original.valid);
    ASSERT_TRUE(rebuilt.valid);
    auto &before = static_cast<at9_test::Atrac9Handle *>(original.decoder_handle)->Frame;
    auto &after = static_cast<at9_test::Atrac9Handle *>(rebuilt.decoder_handle)->Frame;
    ASSERT_EQ(before.Config->ChannelCount, 2);
    ASSERT_EQ(before.Config->FrameSamples, MAX_FRAME_SAMPLES);
    ASSERT_EQ(before.Blocks[0].ChannelCount, 2);
    ASSERT_EQ(before.Channels[0]->Mdct.Bits, 8);
    ASSERT_EQ(before.Channels[1]->Mdct.Bits, 8);
    prepare_at9_noise_frame(before);
    ASSERT_GT(at9_test::BexGroupInfo[before.Blocks[0].QuantizationUnitCount - 13].BandCount, 2);
    synthesize_at9_noise_frame(before); // Advance both the RNG and overlap history.
    ASSERT_TRUE(before.Channels[0]->Rng.Initialized);
    ASSERT_TRUE(std::any_of(std::begin(before.Channels[0]->Mdct.ImdctPrevious),
        std::end(before.Channels[0]->Mdct.ImdctPrevious), [](double value) { return value != 0.0; }));
    ngs::Atrac9LogicalState state;
    state.decoder_config = config;
    original.export_state(&state.saved_state);
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_atrac9(&state, blob);
    std::unique_ptr<ngs::Atrac9LogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_atrac9(blob, restored));
    rebuilt.load_state(&restored->saved_state);
    for (int frame = 0; frame < 3; ++frame) {
        prepare_at9_noise_frame(before);
        prepare_at9_noise_frame(after);
        synthesize_at9_noise_frame(before);
        synthesize_at9_noise_frame(after);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 256; ++sample)
                ASSERT_EQ(before.Channels[channel]->Pcm[sample], after.Channels[channel]->Pcm[sample])
                    << "frame=" << frame << " channel=" << channel << " sample=" << sample;
    }
}

TEST(NgsAtrac9Decoder, RestoresPriorScaleFactorsAndBandParametersForAcceptedReuse) {
    const uint32_t config = std::bit_cast<uint32_t>(std::array<uint8_t, 4>{ 0xFE, 0x74, 0x3F, 0xE0 });
    Atrac9DecoderState original(config), rebuilt(config);
    ASSERT_TRUE(original.valid);
    ASSERT_TRUE(rebuilt.valid);
    auto &before = static_cast<at9_test::Atrac9Handle *>(original.decoder_handle)->Frame;
    auto &after = static_cast<at9_test::Atrac9Handle *>(rebuilt.decoder_handle)->Frame;
    before.Channels[0]->ScaleFactorsPrev[7] = 13;
    before.Channels[1]->ScaleFactorsPrev[30] = 17;
    before.Blocks[0].BandCount = 5;
    before.Blocks[0].StereoBand = 4;
    before.Blocks[0].ExtensionBand = 6;
    before.Blocks[0].QuantizationUnitCount = 13;
    before.Blocks[0].StereoQuantizationUnit = 12;
    before.Blocks[0].ExtensionUnit = 14;
    before.Blocks[0].QuantizationUnitsPrev = 14;
    before.Blocks[0].BandExtensionEnabled = 1;
    Atrac9DecoderSavedState saved;
    original.export_state(&saved);
    rebuilt.load_state(&saved);
    EXPECT_EQ(after.Channels[0]->ScaleFactorsPrev[7], 13);
    EXPECT_EQ(after.Channels[1]->ScaleFactorsPrev[30], 17);
    EXPECT_EQ(after.Blocks[0].BandCount, 5);
    EXPECT_EQ(after.Blocks[0].StereoBand, 4);
    EXPECT_EQ(after.Blocks[0].ExtensionBand, 6);
    EXPECT_EQ(after.Blocks[0].QuantizationUnitCount, 13);
    EXPECT_EQ(after.Blocks[0].StereoQuantizationUnit, 12);
    EXPECT_EQ(after.Blocks[0].ExtensionUnit, 14);
    EXPECT_EQ(after.Blocks[0].QuantizationUnitsPrev, 14);
    EXPECT_TRUE(after.Blocks[0].BandExtensionEnabled);
}

TEST(NgsAtrac9Decoder, EmptyHistoryKeepsFreshDecoderSuperframeAccounting) {
    const uint32_t config = std::bit_cast<uint32_t>(std::array<uint8_t, 4>{ 0xFE, 0x74, 0x3F, 0xE0 });
    Atrac9DecoderState decoder(config);
    ASSERT_TRUE(decoder.valid);
    const int initial_remaining = decoder.superframe_data_left;
    ASSERT_GT(initial_remaining, 0);
    auto &frame = static_cast<at9_test::Atrac9Handle *>(decoder.decoder_handle)->Frame;
    frame.Channels[0]->Mdct.ImdctPrevious[0] = 0.75;
    frame.Channels[0]->Rng.Initialized = 1;
    frame.Channels[0]->Rng.StateA = 123;
    const Atrac9DecoderSavedState empty{};
    decoder.load_state(&empty);
    EXPECT_EQ(decoder.superframe_data_left, initial_remaining);
    EXPECT_EQ(decoder.superframe_frame_idx, 0);
    EXPECT_DOUBLE_EQ(frame.Channels[0]->Mdct.ImdctPrevious[0], 0.0);
    EXPECT_EQ(frame.Channels[0]->Rng.StateA, 123);
}

TEST(NgsAtrac9Decoder, CapturePendingKeyOnResetDoesNotMutateLiveAudioAndPreservesNextPcm) {
    const uint32_t config = std::bit_cast<uint32_t>(std::array<uint8_t, 4>{ 0xFE, 0x74, 0x3F, 0xE0 });
    Atrac9DecoderState original(config), rebuilt(config);
    ASSERT_TRUE(original.valid);
    ASSERT_TRUE(rebuilt.valid);
    auto &before = static_cast<at9_test::Atrac9Handle *>(original.decoder_handle)->Frame;
    auto &after = static_cast<at9_test::Atrac9Handle *>(rebuilt.decoder_handle)->Frame;
    prepare_at9_noise_frame(before);
    synthesize_at9_noise_frame(before);
    ngs::Atrac9LogicalState state;
    state.decoder_config = config;
    // A new key-on clears logical overlap, retaining the runtime decoder.
    const double overlap = before.Channels[0]->Mdct.ImdctPrevious[0];
    const auto rng = before.Channels[0]->Rng.StateA;
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_atrac9_snapshot(&state, &original, blob);
    EXPECT_DOUBLE_EQ(before.Channels[0]->Mdct.ImdctPrevious[0], overlap);
    EXPECT_EQ(before.Channels[0]->Rng.StateA, rng);
    EXPECT_FALSE(state.saved_state.has_history);
    std::unique_ptr<ngs::Atrac9LogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_atrac9(blob, restored));
    original.load_state(&state.saved_state);
    rebuilt.load_state(&restored->saved_state);
    prepare_at9_noise_frame(before);
    prepare_at9_noise_frame(after);
    synthesize_at9_noise_frame(before);
    synthesize_at9_noise_frame(after);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 256; ++sample)
            ASSERT_EQ(before.Channels[channel]->Pcm[sample], after.Channels[channel]->Pcm[sample]);
}
