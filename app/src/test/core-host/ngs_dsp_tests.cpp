// Host tests for the NGS DSP kernels (ngs/dsp/*.h) and the sceNgsSystemLock protocol
// (ngs/param_lock.h). Every kernel is compared with an independent double precision
// reference written from the textbook formulas; neither header depends on the emulator.
#include <ngs/dsp/biquad.h>
#include <ngs/dsp/generator.h>
#include <ngs/dsp/mix.h>
#include <ngs/param_lock.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;
constexpr double kTol = 1e-5;

::testing::AssertionResult close(double got, double want, double tol = kTol) {
    const double err = std::fabs(got - want);
    if (err <= tol * std::max(1.0, std::fabs(want)))
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure() << "got " << got << " want " << want << " err " << err;
}

// ---------------------------------------------------------------- biquad reference
struct RefCoeffs {
    double b0, b1, b2, a1, a2;
};

// RBJ Audio EQ Cookbook, written out independently of the kernel under test.
RefCoeffs ref_biquad(int mode, double f0, double Q, double dBgain, double fs) {
    const double w0 = 2.0 * kPi * f0 / fs;
    const double c = std::cos(w0), s = std::sin(w0);
    const double alpha = s / (2.0 * Q);
    const double A = std::pow(10.0, dBgain / 40.0);
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (mode) {
    case 1: // LPF
    case 12:
        b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        if (mode == 12) {
            const double k = Q > 1.0 ? 1.0 / Q : 1.0;
            b0 *= k; b1 *= k; b2 *= k;
        }
        break;
    case 2: // HPF
        b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        break;
    case 3: // BPF constant skirt
        b0 = s / 2; b1 = 0; b2 = -s / 2;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        break;
    case 4: // BPF 0 dB peak
        b0 = alpha; b1 = 0; b2 = -alpha;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        break;
    case 5: // notch
        b0 = 1; b1 = -2 * c; b2 = 1;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        break;
    case 6: // peaking
        b0 = 1 + alpha * A; b1 = -2 * c; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * c; a2 = 1 - alpha / A;
        break;
    case 7: { // high shelf
        const double t = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * c + t);
        b1 = -2 * A * ((A - 1) + (A + 1) * c);
        b2 = A * ((A + 1) + (A - 1) * c - t);
        a0 = (A + 1) - (A - 1) * c + t;
        a1 = 2 * ((A - 1) - (A + 1) * c);
        a2 = (A + 1) - (A - 1) * c - t;
        break;
    }
    case 8: { // low shelf
        const double t = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * c + t);
        b1 = 2 * A * ((A - 1) - (A + 1) * c);
        b2 = A * ((A + 1) - (A - 1) * c - t);
        a0 = (A + 1) + (A - 1) * c + t;
        a1 = -2 * ((A - 1) + (A + 1) * c);
        a2 = (A + 1) + (A - 1) * c - t;
        break;
    }
    case 9: { // one pole LP, bilinear
        const double K = std::tan(w0 / 2);
        b0 = K; b1 = K; a0 = 1 + K; a1 = K - 1;
        break;
    }
    case 10: { // one pole HP
        const double K = std::tan(w0 / 2);
        b0 = 1; b1 = -1; a0 = 1 + K; a1 = K - 1;
        break;
    }
    case 11: // all pass
        b0 = 1 - alpha; b1 = -2 * c; b2 = 1 + alpha;
        a0 = 1 + alpha; a1 = -2 * c; a2 = 1 - alpha;
        break;
    }
    return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
}

struct RefFilter {
    RefCoeffs c;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double run(double x) {
        const double y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

std::vector<float> make_input(int kind, uint32_t frames) {
    std::vector<float> in(frames * 2, 0.0f);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-0.9f, 0.9f);
    for (uint32_t i = 0; i < frames; i++) {
        float l = 0, r = 0;
        if (kind == 0) { // impulse (R delayed by 3 frames)
            l = i == 0 ? 1.0f : 0.0f;
            r = i == 3 ? -0.5f : 0.0f;
        } else if (kind == 1) { // sines
            l = static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * i / kRate));
            r = static_cast<float>(0.4 * std::sin(2 * kPi * 3100.0 * i / kRate + 0.3));
        } else { // noise
            l = dist(rng);
            r = dist(rng);
        }
        in[i * 2] = l;
        in[i * 2 + 1] = r;
    }
    return in;
}

struct ModeCase {
    uint32_t mode;
    float f, q, g;
};
const ModeCase kModes[] = {
    { 1, 1000.0f, 0.707f, 0.0f }, { 1, 200.0f, 8.0f, 0.0f }, { 2, 2500.0f, 1.2f, 0.0f }, { 3, 800.0f, 4.0f, 0.0f },
    { 4, 800.0f, 4.0f, 0.0f }, { 5, 1500.0f, 2.0f, 0.0f }, { 6, 1200.0f, 1.5f, 9.0f }, { 6, 1200.0f, 1.5f, -12.0f },
    { 7, 6000.0f, 0.707f, 6.0f }, { 8, 300.0f, 0.707f, -6.0f }, { 9, 700.0f, 1.0f, 0.0f }, { 10, 700.0f, 1.0f, 0.0f },
    { 11, 2000.0f, 0.9f, 0.0f }, { 12, 500.0f, 5.0f, 0.0f }, { 1, 30.0f, 0.707f, 0.0f }, { 1, 23900.0f, 2.0f, 0.0f },
};
} // namespace

TEST(NgsBiquad, MatchesDoubleReferenceForEveryModeAndInput) {
    for (const ModeCase &m : kModes) {
        for (int kind = 0; kind < 3; kind++) {
            const uint32_t frames = 4096;
            const auto in = make_input(kind, frames);
            std::vector<float> out(in.size());
            ngs::dsp::BiquadState st[2];
            const auto c = ngs::dsp::make_biquad(m.mode, m.f, m.q, m.g, kRate);
            ASSERT_TRUE(c.active);
            ngs::dsp::biquad_process(c, st, 2, in.data(), out.data(), frames);

            RefFilter l{ ref_biquad(static_cast<int>(m.mode), m.f, m.q, m.g, kRate) };
            RefFilter r = l;
            for (uint32_t i = 0; i < frames; i++) {
                ASSERT_TRUE(close(out[i * 2], l.run(in[i * 2]))) << "mode " << m.mode << " kind " << kind << " L frame " << i;
                ASSERT_TRUE(close(out[i * 2 + 1], r.run(in[i * 2 + 1]))) << "mode " << m.mode << " kind " << kind << " R frame " << i;
            }
        }
    }
}

TEST(NgsBiquad, StateContinuesAcrossBufferBoundaries) {
    const uint32_t frames = 1000;
    const auto in = make_input(2, frames);
    for (const ModeCase &m : kModes) {
        const auto c = ngs::dsp::make_biquad(m.mode, m.f, m.q, m.g, kRate);
        std::vector<float> whole(in.size()), chunked(in.size());
        ngs::dsp::BiquadState a[2], b[2];
        ngs::dsp::biquad_process(c, a, 2, in.data(), whole.data(), frames);
        const uint32_t sizes[] = { 1, 63, 64, 7, 256, 5, 604 }; // sums to 1000
        uint32_t pos = 0;
        for (uint32_t n : sizes) {
            ngs::dsp::biquad_process(c, b, 2, in.data() + pos * 2, chunked.data() + pos * 2, n);
            pos += n;
        }
        ASSERT_EQ(pos, frames);
        EXPECT_EQ(whole, chunked) << "mode " << m.mode; // identical arithmetic: bit exact
    }
}

TEST(NgsBiquad, CoefficientChangeMidStreamKeepsHistory) {
    const uint32_t frames = 2048, split = 777;
    const auto in = make_input(1, frames);
    const auto c1 = ngs::dsp::make_biquad(1, 900.0f, 2.0f, 0.0f, kRate);
    const auto c2 = ngs::dsp::make_biquad(2, 3000.0f, 0.8f, 0.0f, kRate);
    std::vector<float> out(in.size());
    ngs::dsp::BiquadState st[2];
    ngs::dsp::biquad_process(c1, st, 2, in.data(), out.data(), split);
    ngs::dsp::biquad_process(c2, st, 2, in.data() + split * 2, out.data() + split * 2, frames - split);

    RefFilter l{ ref_biquad(1, 900.0, 2.0, 0.0, kRate) };
    RefFilter r = l;
    for (uint32_t i = 0; i < frames; i++) {
        if (i == split) {
            l.c = ref_biquad(2, 3000.0, 0.8, 0.0, kRate);
            r.c = l.c;
        }
        ASSERT_TRUE(close(out[i * 2], l.run(in[i * 2]))) << i;
        ASSERT_TRUE(close(out[i * 2 + 1], r.run(in[i * 2 + 1]))) << i;
    }
}

TEST(NgsBiquad, FrequencyResponseProperties) {
    // Steady state gain of a sine at f measured from the kernel, checked against the
    // known cookbook properties instead of re-deriving the coefficients.
    const auto gain_at = [](const ngs::dsp::BiquadCoeffs &c, double f) {
        const uint32_t frames = 48000;
        std::vector<float> in(frames * 2), out(frames * 2);
        for (uint32_t i = 0; i < frames; i++)
            in[i * 2] = in[i * 2 + 1] = static_cast<float>(std::sin(2 * kPi * f * i / kRate));
        ngs::dsp::BiquadState st[2];
        ngs::dsp::biquad_process(c, st, 2, in.data(), out.data(), frames);
        double peak = 0;
        for (uint32_t i = frames - 4800; i < frames; i++)
            peak = std::max(peak, static_cast<double>(std::fabs(out[i * 2])));
        return peak;
    };
    const auto mk = [](uint32_t mode, float f, float q, float g) { return ngs::dsp::make_biquad(mode, f, q, g, kRate); };

    EXPECT_NEAR(gain_at(mk(1, 1000, 0.7071f, 0), 20.0), 1.0, 1e-3); // LPF: unity at DC side
    EXPECT_NEAR(gain_at(mk(1, 1000, 0.7071f, 0), 1000.0), 0.7071, 5e-3); // -3 dB at f0 for Q=1/sqrt2
    EXPECT_LT(gain_at(mk(1, 1000, 0.7071f, 0), 10000.0), 0.02);
    EXPECT_NEAR(gain_at(mk(2, 1000, 0.7071f, 0), 10000.0), 1.0, 1e-2);
    EXPECT_LT(gain_at(mk(2, 1000, 0.7071f, 0), 30.0), 0.01);
    EXPECT_NEAR(gain_at(mk(4, 1000, 3.0f, 0), 1000.0), 1.0, 5e-3); // 0 dB peak band pass
    EXPECT_NEAR(gain_at(mk(3, 1000, 3.0f, 0), 1000.0), 3.0, 2e-2); // peak gain = Q
    EXPECT_LT(gain_at(mk(5, 1000, 2.0f, 0), 1000.0), 5e-3); // notch
    EXPECT_NEAR(gain_at(mk(6, 1000, 1.0f, 12.0f), 1000.0), std::pow(10.0, 12.0 / 20), 2e-2); // peak: full gain at f0
    EXPECT_NEAR(gain_at(mk(6, 1000, 1.0f, 12.0f), 40.0), 1.0, 2e-2);
    EXPECT_NEAR(gain_at(mk(8, 500, 0.7071f, 6.0f), 30.0), std::pow(10.0, 6.0 / 20), 3e-2); // low shelf boosts lows
    EXPECT_NEAR(gain_at(mk(8, 500, 0.7071f, 6.0f), 15000.0), 1.0, 3e-2);
    EXPECT_NEAR(gain_at(mk(7, 5000, 0.7071f, -6.0f), 20000.0), std::pow(10.0, -6.0 / 20), 3e-2); // high shelf cut
    EXPECT_NEAR(gain_at(mk(11, 2000, 1.0f, 0), 777.0), 1.0, 1e-3); // allpass magnitude
    EXPECT_NEAR(gain_at(mk(9, 700, 1.0f, 0), 20.0), 1.0, 5e-3);
    EXPECT_NEAR(gain_at(mk(9, 700, 1.0f, 0), 700.0), 0.7071, 1e-2);
    EXPECT_NEAR(gain_at(mk(10, 700, 1.0f, 0), 700.0), 0.7071, 1e-2);
    EXPECT_LE(gain_at(mk(12, 500, 8.0f, 0), 500.0), 1.05); // normalised resonance does not boost
}

TEST(NgsBiquad, InvalidParametersAreTransparent) {
    const auto in = make_input(2, 256);
    const ngs::dsp::BiquadCoeffs bad[] = {
        ngs::dsp::make_biquad(0, 1000, 1, 0, kRate), // OFF
        ngs::dsp::make_biquad(99, 1000, 1, 0, kRate),
        ngs::dsp::make_biquad(1, std::nanf(""), 1, 0, kRate),
        ngs::dsp::make_biquad(1, -5.0f, 1, 0, kRate),
        ngs::dsp::make_biquad(1, 1000, 1, 0, 0.0),
        ngs::dsp::make_biquad_from_coeffs(1, 0, 0, std::nanf(""), 0),
    };
    for (const auto &c : bad) {
        EXPECT_FALSE(c.active);
        std::vector<float> out(in.size(), 5.0f);
        ngs::dsp::BiquadState st[2];
        ngs::dsp::biquad_process(c, st, 2, in.data(), out.data(), 256);
        EXPECT_EQ(in, out);
    }
    // frequency above Nyquist and silly Q are clamped, not rejected, and stay finite
    const auto c = ngs::dsp::make_biquad(1, 1.0e9f, 1.0e9f, 1.0e9f, kRate);
    EXPECT_TRUE(c.active);
    EXPECT_TRUE(std::isfinite(c.b0) && std::isfinite(c.a1) && std::isfinite(c.a2));
}

TEST(NgsBiquad, DirectCoefficientsAndMonoAndInPlace) {
    const uint32_t frames = 512;
    const auto in = make_input(2, frames);
    const auto c = ngs::dsp::make_biquad_from_coeffs(0.2f, 0.3f, 0.1f, -0.4f, 0.2f);
    std::vector<float> out(in.size());
    ngs::dsp::BiquadState st[2];
    ngs::dsp::biquad_process(c, st, 2, in.data(), out.data(), frames);
    RefFilter ref{ { 0.2f, 0.3f, 0.1f, -0.4f, 0.2f } };
    for (uint32_t i = 0; i < frames; i++)
        ASSERT_TRUE(close(out[i * 2], ref.run(in[i * 2]))) << i;

    // in place gives the same result as out of place
    std::vector<float> inplace = in;
    ngs::dsp::BiquadState st2[2];
    ngs::dsp::biquad_process(c, st2, 2, inplace.data(), inplace.data(), frames);
    EXPECT_EQ(out, inplace);

    // mono: only the left channel is filtered and copied to the right
    std::vector<float> mono(in.size());
    ngs::dsp::BiquadState st3[2];
    ngs::dsp::biquad_process(c, st3, 1, in.data(), mono.data(), frames);
    for (uint32_t i = 0; i < frames; i++) {
        ASSERT_EQ(mono[i * 2], out[i * 2]);
        ASSERT_EQ(mono[i * 2 + 1], mono[i * 2]);
    }
}

// ---------------------------------------------------------------- mixer
TEST(NgsMix, MatrixAccumulateMatchesDoubleReferenceAndLegacyLoop) {
    const uint32_t frames = 512;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.8f, 0.8f);
    std::vector<float> src(frames * 2), dst(frames * 2);
    for (auto &v : src) v = dist(rng);
    for (auto &v : dst) v = dist(rng);
    const float m[2][2] = { { 0.9f, 0.1f }, { -0.3f, 0.7f } };

    std::vector<float> legacy = dst, got = dst;
    for (uint32_t k = 0; k < frames; k++) { // the loop VoiceInputManager::receive used to inline
        legacy[k * 2] = std::clamp(legacy[k * 2] + src[k * 2] * m[0][0] + src[k * 2 + 1] * m[1][0], -1.0f, 1.0f);
        legacy[k * 2 + 1] = std::clamp(legacy[k * 2 + 1] + src[k * 2] * m[0][1] + src[k * 2 + 1] * m[1][1], -1.0f, 1.0f);
    }
    ngs::dsp::accumulate_matrix(got.data(), src.data(), m, frames);
    EXPECT_EQ(legacy, got);

    for (uint32_t k = 0; k < frames; k++) {
        const double l = std::clamp(static_cast<double>(dst[k * 2]) + static_cast<double>(src[k * 2]) * m[0][0] + static_cast<double>(src[k * 2 + 1]) * m[1][0], -1.0, 1.0);
        const double r = std::clamp(static_cast<double>(dst[k * 2 + 1]) + static_cast<double>(src[k * 2]) * m[0][1] + static_cast<double>(src[k * 2 + 1]) * m[1][1], -1.0, 1.0);
        ASSERT_TRUE(close(got[k * 2], l));
        ASSERT_TRUE(close(got[k * 2 + 1], r));
    }
}

TEST(NgsMix, SumOfSeveralPatchesClampsAfterEveryAddition) {
    const uint32_t frames = 4;
    const float unity[2][2] = { { 1, 0 }, { 0, 1 } };
    std::vector<float> dst(frames * 2, 0.0f);
    const std::vector<float> a(frames * 2, 0.7f), b(frames * 2, 0.6f), c(frames * 2, -0.5f);
    ngs::dsp::accumulate_matrix(dst.data(), a.data(), unity, frames);
    ngs::dsp::accumulate_matrix(dst.data(), b.data(), unity, frames); // 1.3 -> clamped to 1.0
    ngs::dsp::accumulate_matrix(dst.data(), c.data(), unity, frames); // 0.5, not 0.8
    for (float v : dst)
        EXPECT_FLOAT_EQ(v, 0.5f);
}

TEST(NgsMix, TwoPortMixMatchesReference) {
    const uint32_t frames = 640;
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(-0.9f, 0.9f);
    std::vector<float> a(frames * 2), b(frames * 2), out(frames * 2);
    for (auto &v : a) v = dist(rng);
    for (auto &v : b) v = dist(rng);
    const float gains[][2] = { { 1.0f, 0.0f }, { 0.5f, 0.25f }, { 1.0f, 1.0f }, { 2.0f, -1.5f }, { 0.0f, 0.0f } };
    for (const auto &g : gains) {
        ngs::dsp::mix_two_ports(a.data(), g[0], b.data(), g[1], out.data(), frames);
        for (uint32_t i = 0; i < frames * 2; i++)
            ASSERT_TRUE(close(out[i], std::clamp(static_cast<double>(a[i]) * g[0] + static_cast<double>(b[i]) * g[1], -1.0, 1.0)));
    }
    // null ports are silence
    ngs::dsp::mix_two_ports(nullptr, 1.0f, b.data(), 0.5f, out.data(), frames);
    for (uint32_t i = 0; i < frames * 2; i++)
        ASSERT_TRUE(close(out[i], b[i] * 0.5));
    ngs::dsp::mix_two_ports(a.data(), 1.0f, nullptr, 1.0f, out.data(), frames);
    EXPECT_EQ(out, a);
    // output may alias the first input
    std::vector<float> alias = a;
    ngs::dsp::mix_two_ports(alias.data(), 0.5f, b.data(), 0.5f, alias.data(), frames);
    for (uint32_t i = 0; i < frames * 2; i++)
        ASSERT_TRUE(close(alias[i], 0.5 * a[i] + 0.5 * b[i]));
    EXPECT_EQ(ngs::dsp::sanitise_gain(std::nanf("")), 0.0f);
    EXPECT_EQ(ngs::dsp::sanitise_gain(0.25f), 0.25f);
}

// ---------------------------------------------------------------- generator
namespace {
// Independent reference of the documented waveforms (see ngs/dsp/generator.h).
struct RefGen {
    double phase = 0;
    uint32_t rng = 0x1badf00du;
    double hold = 0, held = 0;
    double noise() {
        uint32_t x = rng;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        rng = x;
        return x / 2147483648.0 - 1.0;
    }
    void start(double angle_deg, double offset, double inc) {
        phase = angle_deg / 360.0 + offset * inc;
        phase -= std::floor(phase);
        held = noise(); // first draw seeds the held value
    }
    double next(uint32_t mode, double inc, double width) {
        double v = 0;
        switch (mode) {
        case 0: v = std::sin(2 * kPi * phase); break;
        case 1: v = phase < 0.25 ? 4 * phase : phase < 0.75 ? 2 - 4 * phase : 4 * phase - 4; break;
        case 2: v = phase < 0.5 ? 2 * phase : 2 * phase - 2; break;
        case 3: v = noise(); break;
        case 4:
            hold += inc;
            while (hold >= 1.0) { hold -= 1.0; held = noise(); }
            v = held;
            break;
        case 5: v = phase < width ? 1.0 : -1.0; break;
        }
        phase += inc;
        phase -= std::floor(phase);
        return v;
    }
};
} // namespace

TEST(NgsGenerator, WaveformsMatchReference) {
    for (uint32_t mode = 0; mode <= 5; mode++) {
        ngs::dsp::GeneratorParams p;
        p.mode = mode;
        p.frequency_hz = 997;
        p.amplitude = 0.8f;
        p.pulse_width = 0.3f;
        p.sample_offset = 11;
        p.phase_angle_deg = 90;
        ngs::dsp::GeneratorState st;
        std::vector<float> out(2 * 4800);
        ngs::dsp::generator_render(st, p, kRate, out.data(), 4800);

        RefGen ref;
        const double inc = 997.0 / kRate;
        ref.start(90, 11, inc);
        for (uint32_t i = 0; i < 4800; i++) {
            const double want = ref.next(mode, inc, 0.3) * static_cast<double>(0.8f);
            ASSERT_TRUE(close(out[i * 2], want)) << "mode " << mode << " frame " << i;
            ASSERT_EQ(out[i * 2], out[i * 2 + 1]);
        }
    }
}

TEST(NgsGenerator, SineHasRequestedFrequencyAndAmplitude) {
    ngs::dsp::GeneratorParams p;
    p.mode = ngs::dsp::GENERATOR_SINE;
    p.frequency_hz = 480; // exactly 100 frames per period at 48 kHz
    p.amplitude = 0.5f;
    ngs::dsp::GeneratorState st;
    std::vector<float> out(2 * 1000);
    ngs::dsp::generator_render(st, p, kRate, out.data(), 1000);
    EXPECT_NEAR(out[0], 0.0f, 1e-7);
    EXPECT_NEAR(out[2 * 25], 0.5f, 1e-5); // quarter period
    EXPECT_NEAR(out[2 * 100], 0.0f, 1e-5); // full period
    float peak = 0;
    for (uint32_t i = 0; i < 1000; i++)
        peak = std::max(peak, std::fabs(out[i * 2]));
    EXPECT_NEAR(peak, 0.5f, 1e-5);
}

TEST(NgsGenerator, ContinuousAcrossBuffersAndParameterChanges) {
    ngs::dsp::GeneratorParams a, b;
    a.mode = ngs::dsp::GENERATOR_SAW;
    a.frequency_hz = 1234;
    a.amplitude = 0.6f;
    b = a;
    b.mode = ngs::dsp::GENERATOR_TRIANGLE;
    b.frequency_hz = 2222;
    b.amplitude = 0.3f;

    ngs::dsp::GeneratorState st;
    std::vector<float> out(2 * 1500);
    ngs::dsp::generator_render(st, a, kRate, out.data(), 100);
    ngs::dsp::generator_render(st, a, kRate, out.data() + 200, 300);
    ngs::dsp::generator_render(st, b, kRate, out.data() + 800, 500); // mid-stream change keeps the phase
    ngs::dsp::generator_render(st, b, kRate, out.data() + 1800, 600);

    RefGen ref;
    ref.start(0, 0, 1234.0 / kRate);
    for (uint32_t i = 0; i < 1500; i++) {
        const bool second = i >= 400;
        const double inc = (second ? 2222.0 : 1234.0) / kRate;
        const double amp = second ? static_cast<double>(0.3f) : static_cast<double>(0.6f);
        ASSERT_TRUE(close(out[i * 2], ref.next(second ? 1 : 2, inc, 0.5) * amp)) << i;
    }
}

TEST(NgsGenerator, KeyOnRestartsFromConfiguredPhase) {
    ngs::dsp::GeneratorParams p;
    p.mode = ngs::dsp::GENERATOR_SAW;
    p.frequency_hz = 1000;
    p.amplitude = 1.0f;
    ngs::dsp::GeneratorState st;
    std::vector<float> first(2 * 100), again(2 * 100);
    ngs::dsp::generator_render(st, p, kRate, first.data(), 100);
    st.started = false; // what GeneratorModule::on_key_on does
    ngs::dsp::generator_render(st, p, kRate, again.data(), 100);
    EXPECT_EQ(first, again);
}

TEST(NgsGenerator, NoiseIsBoundedAndSpread) {
    ngs::dsp::GeneratorParams p;
    p.mode = ngs::dsp::GENERATOR_NOISE;
    p.frequency_hz = 0;
    p.amplitude = 0.5f;
    ngs::dsp::GeneratorState st;
    std::vector<float> out(2 * 20000);
    ngs::dsp::generator_render(st, p, kRate, out.data(), 20000);
    double sum = 0, sq = 0;
    for (uint32_t i = 0; i < 20000; i++) {
        ASSERT_LE(std::fabs(out[i * 2]), 0.5f);
        sum += out[i * 2];
        sq += static_cast<double>(out[i * 2]) * out[i * 2];
    }
    EXPECT_NEAR(sum / 20000, 0.0, 0.01);
    EXPECT_NEAR(std::sqrt(sq / 20000), 0.5 / std::sqrt(3.0), 0.01); // uniform: rms = A/sqrt(3)
}

TEST(NgsGenerator, GarbageParametersStaySafe) {
    ngs::dsp::GeneratorParams p;
    p.mode = ngs::dsp::GENERATOR_SINE;
    p.frequency_hz = -5;
    p.amplitude = std::nanf("");
    ngs::dsp::GeneratorState st;
    std::vector<float> out(2 * 64, 1.0f);
    ngs::dsp::generator_render(st, p, kRate, out.data(), 64);
    for (float v : out)
        EXPECT_EQ(v, 0.0f);
    p.amplitude = 1.0f;
    p.frequency_hz = 2000000000; // clamped to Nyquist
    ngs::dsp::generator_render(st, p, kRate, out.data(), 64);
    for (float v : out)
        EXPECT_TRUE(std::isfinite(v));
}

// ---------------------------------------------------------------- sceNgsSystemLock protocol
TEST(NgsParamLock, RecursiveAndReleasableFromAnyThread) {
    std::mutex m;
    std::condition_variable cv;
    ngs::ParamLockState st;
    std::unique_lock lk(m);
    const auto never = [] { return false; };
    const auto nop = [] {};
    ngs::param_lock_acquire(st, lk, cv, 7, never, nop);
    ngs::param_lock_acquire(st, lk, cv, 7, never, nop); // same thread re-enters
    EXPECT_EQ(st.depth, 2u);
    EXPECT_TRUE(st.blocks(8));
    EXPECT_FALSE(st.blocks(7)); // the locker's own update is never blocked
    EXPECT_TRUE(ngs::param_lock_release(st, cv, 7));
    EXPECT_TRUE(st.blocks(8));
    EXPECT_TRUE(ngs::param_lock_release(st, cv, 8)); // another thread may unlock (DOA5 does)
    EXPECT_FALSE(st.blocks(8));
    EXPECT_FALSE(ngs::param_lock_release(st, cv, 7)); // unbalanced unlock is reported
}

TEST(NgsParamLock, CrossThreadUnlockLetsTheUpdateRun) {
    // DOA5 Plus regression: game thread locks, a worker unlocks, the audio thread's update
    // must then proceed instead of waiting forever.
    std::mutex m;
    std::condition_variable cv;
    ngs::ParamLockState st;
    std::unique_lock lk(m);
    ngs::param_lock_acquire(st, lk, cv, 8, [] { return false; }, [] {});
    EXPECT_TRUE(ngs::param_lock_release(st, cv, 385));
    bool waited = false;
    ngs::param_lock_wait_for_update(st, lk, cv, 334, [&] { waited = true; });
    EXPECT_FALSE(waited);
}

TEST(NgsParamLock, ParameterWritesAreNeverObservedTornByUpdates) {
    // Model of the real roles: `update` stands for VoiceScheduler::update (waits at its entry,
    // marks is_updating, reads the two halves of a parameter block), the writer thread stands
    // for a guest thread that rewrites both halves between sceNgsSystemLock/Unlock.
    std::mutex m;
    std::condition_variable cv;
    ngs::ParamLockState lock_state;
    bool is_updating = false;
    int32_t update_thread = 0;
    std::atomic<int> half_a{ 0 }, half_b{ 0 };
    std::atomic<bool> stop{ false };
    std::atomic<int> torn{ 0 }, updates{ 0 };

    std::thread updater([&] {
        const int32_t tid = 1;
        while (!stop) {
            std::unique_lock lk(m);
            ngs::param_lock_wait_for_update(lock_state, lk, cv, tid, [] {});
            is_updating = true;
            update_thread = tid;
            lk.unlock();
            const int a = half_a.load();
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            const int b = half_b.load();
            if (a != b)
                torn++;
            updates++;
            lk.lock();
            is_updating = false;
            update_thread = 0;
            cv.notify_all();
        }
    });

    std::thread writer([&] {
        const int32_t tid = 2;
        for (int i = 1; i <= 40; i++) {
            std::unique_lock lk(m);
            ngs::param_lock_acquire(lock_state, lk, cv, tid, [&] { return is_updating && update_thread != tid; }, [] {});
            lk.unlock();
            // a locked update can not be running now: writes may take as long as they like
            half_a = i;
            std::this_thread::sleep_for(std::chrono::microseconds(80));
            half_b = i;
            lk.lock();
            ngs::param_lock_release(lock_state, cv, tid);
        }
        stop = true;
        std::lock_guard g(m);
        cv.notify_all();
    });

    writer.join();
    updater.join();
    EXPECT_EQ(torn.load(), 0);
    EXPECT_GT(updates.load(), 0);
}

TEST(NgsParamLock, WithoutTheLockUpdatesDoObserveTornWrites) {
    // Control experiment: the same writer without the lock is observed half done, which shows the
    // test above is able to detect a missing exclusion.
    std::atomic<int> half_a{ 0 }, half_b{ 0 };
    std::atomic<bool> stop{ false };
    int torn = 0;
    std::thread reader([&] {
        while (!stop) {
            const int a = half_a.load();
            std::this_thread::sleep_for(std::chrono::microseconds(10));
            if (a != half_b.load())
                torn++;
        }
    });
    for (int i = 1; i <= 40; i++) {
        half_a = i;
        std::this_thread::sleep_for(std::chrono::microseconds(100));
        half_b = i;
    }
    stop = true;
    reader.join();
    EXPECT_GT(torn, 0);
}

TEST(NgsParamLock, LockWaitsForUpdateInFlight) {
    std::mutex m;
    std::condition_variable cv;
    ngs::ParamLockState st;
    bool is_updating = true;
    std::atomic<bool> acquired{ false };
    std::thread t([&] {
        std::unique_lock lk(m);
        ngs::param_lock_acquire(st, lk, cv, 5, [&] { return is_updating; }, [] {});
        acquired = true;
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(acquired.load());
    {
        std::lock_guard g(m);
        is_updating = false;
    }
    cv.notify_all();
    t.join();
    EXPECT_TRUE(acquired.load());
    EXPECT_EQ(st.owner, 5);
}

TEST(NgsParamLock, RegistryReturnsStablePerSystemState) {
    int a = 0, b = 0;
    ngs::ParamLockState &sa = ngs::system_param_lock(&a);
    ngs::ParamLockState &sb = ngs::system_param_lock(&b);
    EXPECT_NE(&sa, &sb);
    sa.owner = 3;
    sa.depth = 1;
    for (int i = 0; i < 200; i++) // force rehashes
        ngs::system_param_lock(reinterpret_cast<const void *>(static_cast<uintptr_t>(0x1000 + i * 16)));
    EXPECT_EQ(&ngs::system_param_lock(&a), &sa);
    EXPECT_EQ(sa.depth, 1u);
    ngs::system_param_lock_erase(&a);
    EXPECT_EQ(ngs::system_param_lock(&a).depth, 0u);
}
