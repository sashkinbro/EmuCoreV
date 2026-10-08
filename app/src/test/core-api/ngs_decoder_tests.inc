#include <ngs/modules/player.h>
#include <ngs/modules/logical_state_io.h>

TEST(NgsLogicalState, RestoredAdpcmPredictorsRebuildIdenticalNextPcm) {
    PCMDecoderState original(48000.0f);
    original.he_adpcm = true;
    std::array<uint8_t, 32> first{};
    first.fill(0x22);
    first[0] = first[16] = 0x1C; // predictor 1, low amplitude
    first[1] = first[17] = 0;
    ASSERT_TRUE(original.send(first.data(), first.size()));
    ngs::PlayerLogicalState state;
    std::copy_n(original.adpcm_history, 2, state.adpcm_history);
    std::vector<uint8_t> blob;
    ngs::logical_state_io::capture_player(&state, blob);
    std::unique_ptr<ngs::PlayerLogicalState> restored;
    ASSERT_TRUE(ngs::logical_state_io::restore_player(blob, restored));
    PCMDecoderState rebuilt(48000.0f);
    rebuilt.he_adpcm = true;
    std::copy_n(restored->adpcm_history, 2, rebuilt.adpcm_history);
    auto next = first;
    next[2] = 0x43;
    ASSERT_TRUE(original.send(next.data(), next.size()));
    ASSERT_TRUE(rebuilt.send(next.data(), next.size()));
    DecoderSize expected_size{}, actual_size{};
    original.receive(nullptr, &expected_size);
    rebuilt.receive(nullptr, &actual_size);
    ASSERT_EQ(actual_size.samples, expected_size.samples);
    std::vector<float> expected(expected_size.samples * 2), actual(actual_size.samples * 2);
    original.receive(reinterpret_cast<uint8_t *>(expected.data()), nullptr);
    rebuilt.receive(reinterpret_cast<uint8_t *>(actual.data()), nullptr);
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(std::any_of(actual.begin(), actual.end(), [](float sample) { return sample != 0.0f; }));
}
