#include <codec/state.h>
#include <gtest/gtest.h>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <vector>

namespace {
void set_decoded_frame(AacDecoderState &decoder, int channels, int sample_rate, int samples) {
    av_frame_unref(decoder.frame);
    decoder.frame->format = AV_SAMPLE_FMT_FLTP;
    decoder.frame->sample_rate = sample_rate;
    decoder.frame->nb_samples = samples;
    av_channel_layout_default(&decoder.frame->ch_layout, channels);
    ASSERT_EQ(0, av_frame_get_buffer(decoder.frame, 0));
    for (int channel = 0; channel < channels; ++channel) {
        auto *plane = reinterpret_cast<float *>(decoder.frame->extended_data[channel]);
        std::fill_n(plane, samples, channel == 0 ? 0.5f : -0.5f);
    }
}
} // namespace

TEST(AacConversionTest, DownmixesDecodedStereoToRequestedMono) {
    AacDecoderState decoder(44100, 1, false);
    set_decoded_frame(decoder, 2, 44100, 1024);
    std::array<int16_t, 1024> pcm{};
    DecoderSize size{};

    ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(pcm.data()), &size));
    ASSERT_EQ(1024u, size.samples);
    // Equal opposite channels cancel when downmixed. Reading just the left
    // channel instead produces a loud constant signal.
    EXPECT_TRUE(std::all_of(pcm.begin(), pcm.end(), [](int16_t sample) { return sample == 0; }));
}

TEST(AacConversionTest, BoundsUnexpectedSbrFrameToGuestPcmCapacity) {
    AacDecoderState decoder(44100, 1, false);
    set_decoded_frame(decoder, 1, 44100, 2048);
    // Keep enough backing storage to observe the old overrun without invoking
    // undefined behavior; only the first 1024 samples belong to the guest.
    std::vector<int16_t> pcm(2048, 0x1234);
    DecoderSize size{};

    ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(pcm.data()), &size));
    EXPECT_LE(size.samples, 1024u);
    EXPECT_TRUE(std::all_of(pcm.begin() + 1024, pcm.end(), [](int16_t sample) { return sample == 0x1234; }));
}

TEST(AacConversionTest, SbrUsesDoubledSampleRateAndLargerGuestCapacity) {
    AacDecoderState decoder(22050, 1, true);
    set_decoded_frame(decoder, 1, 44100, 2048);
    std::array<int16_t, 2064> pcm{};
    std::fill(pcm.begin() + 2048, pcm.end(), 0x1234);
    DecoderSize size{};

    ASSERT_EQ(44100u, decoder.get(DecoderQuery::SAMPLE_RATE));
    ASSERT_EQ(1u, decoder.get(DecoderQuery::CHANNELS));
    ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(pcm.data()), &size));
    EXPECT_EQ(2048u, size.samples);
    EXPECT_TRUE(std::all_of(pcm.begin(), pcm.begin() + 2048, [](int16_t sample) { return sample > 0; }));
    EXPECT_TRUE(std::all_of(pcm.begin() + 2048, pcm.end(), [](int16_t sample) { return sample == 0x1234; }));
}

TEST(AacConversionTest, ReconfiguresConverterWhenFrameChannelsChange) {
    AacDecoderState decoder(44100, 1, false);
    std::array<int16_t, 1024> pcm{};
    DecoderSize size{};
    set_decoded_frame(decoder, 1, 44100, 1024);
    ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(pcm.data()), &size));
    ASSERT_TRUE(std::all_of(pcm.begin(), pcm.end(), [](int16_t sample) { return sample > 0; }));

    set_decoded_frame(decoder, 2, 44100, 1024);
    ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(pcm.data()), &size));
    EXPECT_EQ(1024u, size.samples);
    EXPECT_TRUE(std::all_of(pcm.begin(), pcm.end(), [](int16_t sample) { return sample == 0; }));
}

TEST(AacConversionTest, OversizedFramesDoNotAccumulateOldAudio) {
    AacDecoderState decoder(44100, 1, false);
    std::array<int16_t, 1024> pcm{};
    DecoderSize size{};
    for (int index = 0; index < 4; ++index) {
        const float signal = index % 2 == 0 ? 0.5f : -0.5f;
        set_decoded_frame(decoder, 1, 44100, 2048);
        std::fill_n(reinterpret_cast<float *>(decoder.frame->extended_data[0]), 2048, signal);

        ASSERT_TRUE(decoder.receive(reinterpret_cast<uint8_t *>(pcm.data()), &size));
        ASSERT_EQ(1024u, size.samples);
        EXPECT_EQ(0, swr_get_delay(decoder.swr, 44100));
        EXPECT_TRUE(std::all_of(pcm.begin(), pcm.end(), [signal](int16_t sample) {
            return signal > 0 ? sample > 0 : sample < 0;
        })) << "Guest received audio buffered from an older frame at index " << index;
    }
}
