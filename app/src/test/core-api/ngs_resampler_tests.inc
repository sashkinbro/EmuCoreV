#include <ngs/modules/player.h>
#include <ngs/modules/logical_state_io.h>
#include <ngs/rate_resampler.h>
#include <cmath>

namespace {
struct NgsResamplerFixture {
    ngs::PlayerLogicalState original;
    std::unique_ptr<ngs::PlayerLogicalState> restored;
    ngs::StereoRateResamplerRuntimeState runtime, rebuilt;
    uint64_t signal_position = 0;
    ~NgsResamplerFixture() {
        ngs::destroy_stereo_rate_resampler(runtime);
        ngs::destroy_stereo_rate_resampler(rebuilt);
    }
    std::vector<float> input(uint32_t frames) {
        std::vector<float> samples(frames * 2);
        for (uint32_t i = 0; i < frames; ++i, ++signal_position) {
            samples[2 * i] = static_cast<float>(std::sin(signal_position * 0.03125));
            samples[2 * i + 1] = static_cast<float>(std::cos(signal_position * 0.019));
        }
        return samples;
    }
    void snapshot(int source, int dest) {
        std::vector<uint8_t> blob;
        ngs::logical_state_io::capture_player(&original, blob);
        ASSERT_TRUE(ngs::logical_state_io::restore_player(blob, restored));
        ngs::destroy_stereo_rate_resampler(rebuilt);
        ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(rebuilt, restored->rate_resampler, source, dest));
    }
    void compare_next(uint32_t frames) {
        auto samples = input(frames);
        ngs::PCMFrameQueue expected, actual;
        const auto *bytes = reinterpret_cast<const uint8_t *>(samples.data());
        const auto expected_frames = ngs::process_stereo_rate_resampler(runtime, original.rate_resampler, bytes, frames, expected);
        const auto actual_frames = ngs::process_stereo_rate_resampler(rebuilt, restored->rate_resampler, bytes, frames, actual);
        ASSERT_EQ(actual_frames, expected_frames);
        ASSERT_EQ(actual.samples.size(), expected.samples.size());
        for (size_t i = 0; i < expected.samples.size(); ++i)
            ASSERT_EQ(actual.samples[i], expected.samples[i]) << "PCM mismatch at sample " << i;
    }
};
} // namespace

TEST(NgsResampler, NonintegerRatesMatchUninterruptedPcmAfterCompactionAndRepeatedRestores) {
    for (const auto &[source, dest] : { std::pair{ 44100, 48000 }, { 48000, 44100 }, { 32000, 48000 }, { 47999, 48000 }, { 192000, 8000 }, { 96000, 8000 }, { 48000, 4000 } }) {
        SCOPED_TRACE(std::to_string(source) + "->" + std::to_string(dest));
        NgsResamplerFixture f;
        ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(f.runtime, f.original.rate_resampler, source, dest));
        for (uint32_t frames : { 17u, 513u, 1001u, 31u, 4096u }) {
            auto input = f.input(frames);
            ngs::PCMFrameQueue discarded;
            ngs::process_stereo_rate_resampler(f.runtime, f.original.rate_resampler,
                reinterpret_cast<const uint8_t *>(input.data()), frames, discarded);
        }
        f.snapshot(source, dest);
        for (uint32_t frames : { 1u, 17u, 1003u, 4096u, 23u })
            f.compare_next(frames);
        // Capture the restored chain, while the reference context remains uninterrupted.
        std::vector<uint8_t> blob;
        ngs::logical_state_io::capture_player(f.restored.get(), blob);
        ASSERT_TRUE(ngs::logical_state_io::restore_player(blob, f.restored));
        ngs::destroy_stereo_rate_resampler(f.rebuilt);
        ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(f.rebuilt, f.restored->rate_resampler, source, dest));
        f.compare_next(1777);
        f.compare_next(50003);
        f.snapshot(source, dest);
        f.compare_next(1031);
    }
}

TEST(NgsResampler, RateChangeThenRestoreMatchesItsUninterruptedContext) {
    NgsResamplerFixture f;
    ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(f.runtime, f.original.rate_resampler, 44100, 48000));
    auto initial = f.input(1027);
    ngs::PCMFrameQueue discarded;
    ngs::process_stereo_rate_resampler(f.runtime, f.original.rate_resampler,
        reinterpret_cast<const uint8_t *>(initial.data()), initial.size() / 2, discarded);
    ASSERT_TRUE(ngs::ensure_stereo_rate_resampler(f.runtime, f.original.rate_resampler, 47999, 48000));
    auto changed = f.input(777);
    ngs::process_stereo_rate_resampler(f.runtime, f.original.rate_resampler,
        reinterpret_cast<const uint8_t *>(changed.data()), changed.size() / 2, discarded);
    f.snapshot(47999, 48000);
    f.compare_next(1001);
}
