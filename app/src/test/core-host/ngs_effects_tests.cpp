// Host tests for the NGS effect kernels (ngs/dsp/{distortion,delay,reverb}.h). Every kernel is
// compared with an independent double precision reference that keeps the full signal history in
// plain vectors (no ring buffers) and is written from the formulas documented in the headers.
// The headers do not depend on the emulator.
#include <ngs/dsp/delay.h>
#include <ngs/dsp/distortion.h>
#include <ngs/dsp/reverb.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace {
using namespace ngs::dsp;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRate = 48000.0;

std::vector<float> random_stereo(const uint32_t frames, const uint32_t seed, const float amp = 0.5f) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amp, amp);
    std::vector<float> v(static_cast<size_t>(frames) * 2);
    for (auto &s : v)
        s = dist(rng);
    return v;
}

bool all_bounded(const std::vector<float> &v) {
    for (const float s : v)
        if (!std::isfinite(s) || std::fabs(s) > 1.0f)
            return false;
    return true;
}

// ================================================================= distortion
double ref_distortion(const double x, const double a, const double b, const double clip, const double gate, const double wet, const double dry) {
    const double g = std::fabs(x) <= gate ? 0.0 : x;
    const double u = a * g + b;
    const double c = std::fabs(clip);
    const double shaped = std::min(std::max(u, -c), c) - std::min(std::max(b, -c), c);
    return std::min(1.0, std::max(-1.0, shaped * wet + x * dry));
}

TEST(NgsDistortion, TransferCurveMatchesReference) {
    DistortionParams p;
    p.a = 4.0f;
    p.b = 0.25f;
    p.clip = 0.6f;
    p.gate = 0.05f;
    p.wet_gain = 0.75f;
    p.dry_gain = 0.2f;
    constexpr uint32_t n = 2001;
    std::vector<float> in(n * 2), out(n * 2);
    for (uint32_t i = 0; i < n; i++) {
        in[i * 2] = -1.0f + 2.0f * static_cast<float>(i) / (n - 1);
        in[i * 2 + 1] = -in[i * 2];
    }
    distortion_process(p, 2, in.data(), out.data(), n);
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t c = 0; c < 2; c++) {
            const double want = ref_distortion(in[i * 2 + c], 4.0, 0.25, 0.6, 0.05, 0.75, 0.2);
            ASSERT_NEAR(out[i * 2 + c], want, 1e-6) << "i=" << i << " c=" << c;
        }
    }
}

TEST(NgsDistortion, HardClipPlateauAndGate) {
    DistortionParams p;
    p.a = 10.0f;
    p.clip = 0.5f;
    p.gate = 0.1f;
    p.wet_gain = 1.0f;
    p.dry_gain = 0.0f;
    const float in[8] = { 0.05f, -0.05f, 0.2f, -0.2f, 0.9f, -0.9f, 0.1f, 0.1f };
    float out[8];
    distortion_process(p, 2, in, out, 4);
    EXPECT_FLOAT_EQ(out[0], 0.0f); // gated
    EXPECT_FLOAT_EQ(out[1], 0.0f);
    EXPECT_FLOAT_EQ(out[2], 0.5f); // clipped 2.0 -> 0.5
    EXPECT_FLOAT_EQ(out[3], -0.5f);
    EXPECT_FLOAT_EQ(out[4], 0.5f);
    EXPECT_FLOAT_EQ(out[5], -0.5f);
    EXPECT_FLOAT_EQ(out[6], 0.0f); // |x| == gate is gated
}

TEST(NgsDistortion, InPlaceAndMono) {
    DistortionParams p;
    p.a = 3.0f;
    p.clip = 0.4f;
    p.dry_gain = 0.0f;
    auto buf = random_stereo(256, 5);
    auto copy = buf;
    std::vector<float> ref(buf.size());
    distortion_process(p, 1, copy.data(), ref.data(), 256);
    distortion_process(p, 1, buf.data(), buf.data(), 256);
    EXPECT_EQ(buf, ref);
    for (uint32_t i = 0; i < 256; i++)
        EXPECT_EQ(buf[i * 2], buf[i * 2 + 1]);
}

TEST(NgsDistortion, GarbageParamsAndInputAreBounded) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float vals[] = { nan, inf, -inf, 1e30f, -1e30f, 0.0f, 1.0f };
    auto in = random_stereo(64, 9);
    in[3] = nan;
    in[10] = inf;
    in[11] = -inf;
    std::vector<float> out(in.size());
    for (const float a : vals)
        for (const float b : vals)
            for (const float c : vals) {
                DistortionParams p;
                p.a = a;
                p.b = b;
                p.clip = c;
                p.gate = a;
                p.wet_gain = b;
                p.dry_gain = c;
                distortion_process(p, 2, in.data(), out.data(), 64);
                ASSERT_TRUE(all_bounded(out));
            }
}

// ================================================================= delay
struct RefDelayTap {
    double delay_ms, volume, feedback;
    int mode;
    double cutoff, phase_deg, mod_ms;
};

struct RefDelay {
    double rate;
    std::vector<double> hist[4][2]; // line[n] for every n written so far
    double lp[4][2] = {}, ax1[4][2] = {}, ay1[4][2] = {};
    int last_mode[4] = {};
    uint64_t n = 0;

    explicit RefDelay(const double r)
        : rate(r) {}

    double line_at(const int t, const int c, const double pos) const {
        // linear interpolation of line[pos], zero before the start
        const auto get = [&](const int64_t i) { return (i < 0 || i >= static_cast<int64_t>(hist[t][c].size())) ? 0.0 : hist[t][c][static_cast<size_t>(i)]; };
        const int64_t i0 = static_cast<int64_t>(std::floor(pos));
        const double f = pos - static_cast<double>(i0);
        return f == 0.0 ? get(i0) : get(i0) * (1.0 - f) + get(i0 + 1) * f;
    }

    void process(const double dry, const double mod_rate, const RefDelayTap (&taps)[4], const float *in, float *out, const uint32_t frames) {
        bool active[4];
        double lpa[4], apc[4];
        for (int t = 0; t < 4; t++) {
            active[t] = taps[t].volume != 0.0;
            if (!active[t]) {
                for (int c = 0; c < 2; c++) {
                    hist[t][c].clear();
                    lp[t][c] = ax1[t][c] = ay1[t][c] = 0.0;
                }
                continue;
            }
            if (last_mode[t] != taps[t].mode) {
                for (int c = 0; c < 2; c++)
                    lp[t][c] = ax1[t][c] = ay1[t][c] = 0.0;
                last_mode[t] = taps[t].mode;
            }
            const double fc = std::min(std::max(taps[t].cutoff, 1.0), 0.45 * rate);
            lpa[t] = std::exp(-2.0 * kPi * fc / rate);
            const double tn = std::tan(kPi * fc / rate);
            apc[t] = (tn - 1.0) / (tn + 1.0);
        }
        for (uint32_t i = 0; i < frames; i++, n++) {
            for (int c = 0; c < 2; c++) {
                const double x = in[i * 2 + c];
                double res = dry * x;
                for (int t = 0; t < 4; t++) {
                    if (!active[t])
                        continue;
                    // the line at time n has not been written yet: sample index n corresponds to hist.size()
                    double d = taps[t].delay_ms * rate / 1000.0;
                    if (taps[t].mod_ms != 0.0)
                        d += taps[t].mod_ms * rate / 1000.0 * std::sin(2.0 * kPi * mod_rate * static_cast<double>(n) / rate + taps[t].phase_deg * kPi / 180.0);
                    d = std::max(1.0, d);
                    const double pos = static_cast<double>(hist[t][c].size()) - d;
                    const double v = line_at(t, c, pos);
                    double w = v;
                    switch (taps[t].mode) {
                    case 1:
                        lp[t][c] = (1.0 - lpa[t]) * v + lpa[t] * lp[t][c];
                        w = lp[t][c];
                        break;
                    case 2:
                        lp[t][c] = (1.0 - lpa[t]) * v + lpa[t] * lp[t][c];
                        w = v - lp[t][c];
                        break;
                    case 3:
                        w = apc[t] * v + ax1[t][c] - apc[t] * ay1[t][c];
                        ax1[t][c] = v;
                        ay1[t][c] = w;
                        break;
                    default:
                        break;
                    }
                    res += taps[t].volume * w;
                    // the kernel stores the line in float
                    hist[t][c].push_back(static_cast<double>(static_cast<float>(x + taps[t].feedback * w)));
                }
                out[i * 2 + c] = static_cast<float>(std::min(1.0, std::max(-1.0, res)));
            }
        }
    }
};

DelayParams to_kernel(const double dry, const double mod_rate, const RefDelayTap (&taps)[4]) {
    DelayParams p;
    p.dry = static_cast<float>(dry);
    p.mod_rate = static_cast<float>(mod_rate);
    for (int t = 0; t < 4; t++) {
        p.taps[t].delay_ms = static_cast<float>(taps[t].delay_ms);
        p.taps[t].volume = static_cast<float>(taps[t].volume);
        p.taps[t].feedback = static_cast<float>(taps[t].feedback);
        p.taps[t].filter_mode = static_cast<uint32_t>(taps[t].mode);
        p.taps[t].cutoff = static_cast<float>(taps[t].cutoff);
        p.taps[t].phase_deg = static_cast<float>(taps[t].phase_deg);
        p.taps[t].mod_width_ms = static_cast<float>(taps[t].mod_ms);
    }
    return p;
}

TEST(NgsDelay, ImpulseExactOffsetsAndFeedbackDecay) {
    RefDelayTap taps[4] = {};
    taps[0] = { 10.0, 0.5, 0.5, 0, 1000.0, 0.0, 0.0 }; // 480 samples at 48 kHz
    const DelayParams p = to_kernel(1.0, 0.0, taps);
    constexpr uint32_t frames = 3000;
    std::vector<float> in(frames * 2, 0.0f), out(frames * 2);
    in[0] = 1.0f;
    in[1] = -0.5f;
    DelayState st;
    delay_process(st, p, kRate, 2, in.data(), out.data(), frames);
    for (uint32_t i = 0; i < frames; i++) {
        double wantL = 0.0, wantR = 0.0;
        if (i == 0) {
            wantL = 1.0;
            wantR = -0.5;
        } else if (i % 480 == 0) {
            // k-th echo: volume * feedback^(k-1)
            const double g = 0.5 * std::pow(0.5, i / 480 - 1);
            wantL = g * 1.0;
            wantR = g * -0.5;
        }
        ASSERT_NEAR(out[i * 2], wantL, 1e-7) << "i=" << i;
        ASSERT_NEAR(out[i * 2 + 1], wantR, 1e-7) << "i=" << i;
    }
}

TEST(NgsDelay, BlockSplitIsBitIdenticalAndMatchesReference) {
    RefDelayTap taps[4] = {
        { 3.0, 0.5, 0.4, 0, 1000.0, 0.0, 0.0 },
        { 7.3, -0.3, 0.3, 1, 2000.0, 0.0, 0.0 },
        { 11.0, 0.4, 0.2, 2, 800.0, 90.0, 0.5 },
        { 19.7, 0.25, 0.5, 3, 5000.0, 45.0, 0.8 },
    };
    const DelayParams p = to_kernel(0.8, 3.5, taps);
    constexpr uint32_t frames = 6000;
    const auto in = random_stereo(frames, 21);
    std::vector<float> whole(in.size()), split(in.size()), ref(in.size());
    DelayState a, b;
    delay_process(a, p, kRate, 2, in.data(), whole.data(), frames);
    const uint32_t sizes[] = { 1, 7, 256, 100, 999, 17 };
    uint32_t pos = 0, k = 0;
    while (pos < frames) {
        const uint32_t len = std::min(sizes[k++ % 6], frames - pos);
        delay_process(b, p, kRate, 2, in.data() + pos * 2, split.data() + pos * 2, len);
        pos += len;
    }
    EXPECT_EQ(whole, split);

    RefDelay r(kRate);
    r.process(0.8, 3.5, taps, in.data(), ref.data(), frames);
    for (size_t i = 0; i < ref.size(); i++)
        ASSERT_NEAR(whole[i], ref[i], 1e-4) << "i=" << i;
}

TEST(NgsDelay, ParamChangeMidStreamMatchesReference) {
    RefDelayTap t1[4] = { { 5.0, 0.6, 0.5, 0, 1000.0, 0.0, 0.0 }, { 9.0, 0.3, 0.2, 1, 1500.0, 0.0, 0.0 } };
    // longer delay (line must grow and keep history), a tap switched off and one switched on
    RefDelayTap t2[4] = { { 20.0, 0.6, 0.5, 0, 1000.0, 0.0, 0.0 }, {}, { 2.0, 0.5, 0.0, 2, 3000.0, 0.0, 0.0 } };
    constexpr uint32_t frames = 4800;
    const auto in = random_stereo(frames, 33);
    std::vector<float> got(in.size()), ref(in.size());
    DelayState st;
    RefDelay r(kRate);
    uint32_t pos = 0;
    int phase = 0;
    while (pos < frames) {
        const uint32_t len = std::min<uint32_t>(480, frames - pos);
        const auto &taps = (phase++ % 2 == 0) ? t1 : t2;
        delay_process(st, to_kernel(0.5, 0.0, taps), kRate, 2, in.data() + pos * 2, got.data() + pos * 2, len);
        r.process(0.5, 0.0, taps, in.data() + pos * 2, ref.data() + pos * 2, len);
        pos += len;
    }
    for (size_t i = 0; i < ref.size(); i++)
        ASSERT_NEAR(got[i], ref[i], 1e-4) << "i=" << i;
}

TEST(NgsDelay, KeyOnResetClearsHistory) {
    RefDelayTap taps[4] = { { 5.0, 1.0, 0.8, 0, 1000.0, 0.0, 0.0 } };
    const DelayParams p = to_kernel(1.0, 0.0, taps);
    const auto in = random_stereo(2000, 4);
    std::vector<float> o1(in.size()), o2(in.size());
    DelayState st;
    delay_process(st, p, kRate, 2, in.data(), o1.data(), 2000);
    st.reset();
    delay_process(st, p, kRate, 2, in.data(), o2.data(), 2000);
    EXPECT_EQ(o1, o2);
}

TEST(NgsDelay, MonoVoiceDuplicatesLeft) {
    RefDelayTap taps[4] = { { 1.0, 0.7, 0.3, 0, 1000.0, 0.0, 0.0 } };
    const auto in = random_stereo(500, 6);
    std::vector<float> mono(in.size()), stereo(in.size());
    DelayState a, b;
    delay_process(a, to_kernel(1.0, 0.0, taps), kRate, 1, in.data(), mono.data(), 500);
    delay_process(b, to_kernel(1.0, 0.0, taps), kRate, 2, in.data(), stereo.data(), 500);
    for (uint32_t i = 0; i < 500; i++) {
        EXPECT_EQ(mono[i * 2], stereo[i * 2]);
        EXPECT_EQ(mono[i * 2 + 1], mono[i * 2]);
    }
}

TEST(NgsDelay, GarbageParamsAreSafe) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float vals[] = { nan, inf, -inf, 1e30f, -1e30f, -5.0f, 0.0f, 1.0f, 3000.0f };
    auto in = random_stereo(512, 12);
    in[5] = nan;
    in[9] = inf;
    std::vector<float> out(in.size());
    DelayState st;
    for (const float a : vals)
        for (const float b : vals) {
            DelayParams p;
            p.dry = a;
            p.mod_rate = b;
            for (auto &t : p.taps) {
                t.delay_ms = a;
                t.volume = 1.0f;
                t.feedback = b;
                t.filter_mode = 77;
                t.cutoff = a;
                t.phase_deg = b;
                t.mod_width_ms = a;
            }
            p.taps[1].filter_mode = 3;
            p.taps[2].filter_mode = 1;
            p.taps[3].filter_mode = 2;
            for (int rep = 0; rep < 3; rep++) {
                delay_process(st, p, kRate, 2, in.data(), out.data(), 512);
                ASSERT_TRUE(all_bounded(out));
            }
        }
}

TEST(NgsDelay, LongDelayCappedAndLineSized) {
    RefDelayTap taps[4] = { { 1.0e9, 1.0, 0.5, 0, 1000.0, 0.0, 0.0 } };
    DelayState st;
    std::vector<float> in(512 * 2, 0.25f), out(in.size());
    delay_process(st, to_kernel(1.0, 0.0, taps), kRate, 2, in.data(), out.data(), 512);
    EXPECT_LE(st.lane[0][0].line.size(), 262144u); // 2 s at 48 kHz rounded up to a power of two
    EXPECT_TRUE(all_bounded(out));
}

// ================================================================= reverb
struct RefReverb {
    double rate;
    ReverbParams p;
    std::vector<double> w_hist; // room filtered mono input
    std::vector<double> comb_u[2][8]; // comb line contents u[n]
    double comb_damp[2][8] = {};
    std::vector<double> ap_s[2][4];
    double lf = 0.0, hf = 0.0;

    explicit RefReverb(const double r)
        : rate(r) {}

    static double mb(const double v) { return std::pow(10.0, v / 2000.0); }
    static double clampd(const double v, const double lo, const double hi) { return std::min(hi, std::max(lo, v)); }
    uint32_t sc(const uint32_t base) const { return std::max<uint32_t>(1, static_cast<uint32_t>(std::llround(base * rate / 44100.0))); }

    void process(const ReverbParams &raw, const float *in, float *out, const uint32_t frames) {
        p = raw; // callers pass in-range values
        static const uint32_t comb_base[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const uint32_t ap_base[4] = { 556, 441, 341, 225 };
        const uint32_t n_comb = 4 + static_cast<uint32_t>(std::llround(4.0 * p.density / 100.0));
        const double ap_g = 0.7 * p.diffusion / 100.0;
        const double lfa = std::exp(-2.0 * kPi * std::min<double>(p.lf_reference, 0.45 * rate) / rate);
        const double hfa = std::exp(-2.0 * kPi * std::min<double>(p.hf_reference, 0.45 * rate) / rate);
        const auto delay_of = [&](const double sec) { return static_cast<int64_t>(std::llround(sec * rate)); };
        for (uint32_t i = 0; i < frames; i++) {
            const double mono = 0.5 * (static_cast<double>(in[i * 2]) + in[i * 2 + 1]);
            lf = (1.0 - lfa) * mono + lfa * lf;
            hf = (1.0 - hfa) * mono + hfa * hf;
            const double low = lf, high = mono - hf, mid = mono - low - high;
            w_hist.push_back(mb(p.room_mb) * (mb(p.room_lf_mb) * low + mid + mb(p.room_hf_mb) * high));
            const int64_t now = static_cast<int64_t>(w_hist.size()) - 1;
            const auto w_at = [&](const int64_t back) { return now - back >= 0 ? w_hist[static_cast<size_t>(now - back)] : 0.0; };
            const double late_in = w_at(delay_of(static_cast<double>(p.reflections_delay) + p.reverb_delay));
            for (int c = 0; c < 2; c++) {
                double early = 0.0;
                for (int k = 0; k < 6; k++) {
                    const double t = p.reflections_delay + reverb_early_table[p.pattern[c]][k][0] * 0.001 * p.early_scalar;
                    early += reverb_early_table[p.pattern[c]][k][1] * mb(p.reflections_mb) * w_at(delay_of(t));
                }
                double sum = 0.0;
                for (uint32_t k = 0; k < n_comb; k++) {
                    const uint32_t len = sc(comb_base[k] + (c ? 23 : 0));
                    auto &u = comb_u[c][k];
                    const double y = u.size() >= len ? u[u.size() - len] : 0.0; // u[n - len]
                    const double g = std::pow(10.0, -3.0 * len / (rate * p.decay_time));
                    const double ghf = std::pow(10.0, -3.0 * len / (rate * p.decay_time * p.decay_hf_ratio));
                    const double pole = (g - ghf) / (g + ghf);
                    comb_damp[c][k] = g * (1.0 - pole) * y + pole * comb_damp[c][k];
                    u.push_back(late_in + comb_damp[c][k]);
                    sum += y;
                }
                double late = sum / n_comb;
                for (int k = 0; k < 4; k++) {
                    const uint32_t len = sc(ap_base[k] + (c ? 23 : 0));
                    auto &s = ap_s[c][k];
                    const double buffered = s.size() >= len ? s[s.size() - len] : 0.0;
                    const double input = late;
                    late = -input + buffered;
                    s.push_back(input + buffered * ap_g);
                }
                const double y = mb(p.dry_mb) * in[i * 2 + c] + early + mb(p.reverb_mb) * late;
                out[i * 2 + c] = static_cast<float>(clampd(y, -1.0, 1.0));
            }
        }
    }
};

ReverbParams default_reverb() {
    ReverbParams p;
    p.room_mb = -300.0f;
    p.room_hf_mb = -500.0f;
    p.decay_time = 1.2f;
    p.decay_hf_ratio = 0.7f;
    p.reflections_mb = -700.0f;
    p.reflections_delay = 0.012f;
    p.reverb_mb = -200.0f;
    p.reverb_delay = 0.02f;
    p.diffusion = 80.0f;
    p.density = 60.0f;
    p.hf_reference = 4000.0f;
    p.pattern[0] = 2;
    p.pattern[1] = 5;
    p.early_scalar = 0.8f;
    p.lf_reference = 300.0f;
    p.room_lf_mb = -200.0f;
    p.dry_mb = -600.0f;
    return p;
}

TEST(NgsReverb, MatchesIndependentReferenceBothRates) {
    for (const double rate : { 48000.0, 44100.0 }) {
        const ReverbParams p = default_reverb();
        const uint32_t frames = 12000;
        auto in = random_stereo(frames, 77, 0.3f);
        std::vector<float> got(in.size()), ref(in.size());
        ReverbState st;
        reverb_process(st, p, rate, 2, in.data(), got.data(), frames);
        RefReverb r(rate);
        r.process(p, in.data(), ref.data(), frames);
        for (size_t i = 0; i < ref.size(); i++)
            ASSERT_NEAR(got[i], ref[i], 2e-6) << "rate=" << rate << " i=" << i;
    }
}

TEST(NgsReverb, BufferBoundariesAreInvisible) {
    const ReverbParams p = default_reverb();
    const uint32_t frames = 9000;
    const auto in = random_stereo(frames, 8, 0.3f);
    std::vector<float> whole(in.size()), split(in.size());
    ReverbState a, b;
    reverb_process(a, p, kRate, 2, in.data(), whole.data(), frames);
    uint32_t pos = 0, len = 1;
    while (pos < frames) {
        const uint32_t n = std::min(len, frames - pos);
        reverb_process(b, p, kRate, 2, in.data() + pos * 2, split.data() + pos * 2, n);
        pos += n;
        len = len * 3 + 1;
        if (len > 700)
            len = 1;
    }
    EXPECT_EQ(whole, split);
}

TEST(NgsReverb, ParamChangeMidStreamMatchesReference) {
    ReverbParams p1 = default_reverb();
    ReverbParams p2 = p1;
    p2.decay_time = 3.0f;
    p2.decay_hf_ratio = 1.5f;
    p2.reverb_mb = 0.0f;
    p2.density = 20.0f;
    p2.diffusion = 30.0f;
    p2.room_hf_mb = -2000.0f;
    p2.dry_mb = 0.0f;
    constexpr uint32_t frames = 8000;
    const auto in = random_stereo(frames, 99, 0.3f);
    std::vector<float> got(in.size()), ref(in.size());
    ReverbState st;
    RefReverb r(kRate);
    for (uint32_t pos = 0, k = 0; pos < frames; pos += 1000, k++) {
        const ReverbParams &p = (k % 2) ? p2 : p1;
        reverb_process(st, p, kRate, 2, in.data() + pos * 2, got.data() + pos * 2, 1000);
        r.process(p, in.data() + pos * 2, ref.data() + pos * 2, 1000);
    }
    for (size_t i = 0; i < ref.size(); i++)
        ASSERT_NEAR(got[i], ref[i], 2e-6) << "i=" << i;
}

TEST(NgsReverb, EarlyReflectionsLandOnTheTablePositions) {
    ReverbParams p;
    p.room_mb = 0.0f;
    p.room_hf_mb = 0.0f;
    p.room_lf_mb = 0.0f;
    p.reflections_mb = 0.0f;
    p.reflections_delay = 0.010f;
    p.early_scalar = 1.0f;
    p.pattern[0] = 0;
    p.pattern[1] = 3;
    p.reverb_mb = -10000.0f; // late tail ~1e-5
    p.reverb_delay = 0.1f;
    p.dry_mb = -10000.0f;
    constexpr uint32_t frames = 6000;
    std::vector<float> in(frames * 2, 0.0f), out(frames * 2);
    in[0] = 1.0f;
    in[1] = 1.0f; // mono sum = 1
    ReverbState st;
    reverb_process(st, p, kRate, 2, in.data(), out.data(), frames);
    for (int c = 0; c < 2; c++) {
        for (uint32_t k = 0; k < reverb_early_taps; k++) {
            const double t = 0.010 + reverb_early_table[p.pattern[c]][k][0] * 0.001;
            const size_t n = static_cast<size_t>(std::llround(t * kRate));
            EXPECT_NEAR(out[n * 2 + c], reverb_early_table[p.pattern[c]][k][1], 1e-4) << "c=" << c << " k=" << k;
        }
    }
    // no energy between the first tap and the dry sample position other than the table taps
    EXPECT_NEAR(out[100 * 2], 0.0, 1e-4);
}

double measure_t60(const std::vector<double> &h, const double rate) {
    // Schroeder backward integration; T60 extrapolated from the -10 dB .. -30 dB slope
    std::vector<double> edc(h.size());
    double acc = 0.0;
    for (size_t i = h.size(); i-- > 0;) {
        acc += h[i] * h[i];
        edc[i] = acc;
    }
    const double total = edc[0];
    double t10 = -1.0, t30 = -1.0;
    for (size_t i = 0; i < edc.size(); i++) {
        const double db = 10.0 * std::log10(edc[i] / total);
        if (t10 < 0.0 && db <= -10.0)
            t10 = static_cast<double>(i) / rate;
        if (t30 < 0.0 && db <= -30.0) {
            t30 = static_cast<double>(i) / rate;
            break;
        }
    }
    return 3.0 * (t30 - t10);
}

std::vector<double> late_impulse_response(const ReverbParams &p, const double rate, const double seconds, const uint32_t channel = 0) {
    const uint32_t frames = static_cast<uint32_t>(seconds * rate);
    std::vector<float> in(frames * 2, 0.0f), out(frames * 2);
    in[0] = 1.0f;
    in[1] = 1.0f;
    ReverbState st;
    reverb_process(st, p, rate, 2, in.data(), out.data(), frames);
    std::vector<double> h(frames);
    for (uint32_t i = 0; i < frames; i++)
        h[i] = out[i * 2 + channel];
    return h;
}

ReverbParams tail_only(const float decay) {
    ReverbParams p;
    p.room_mb = 0.0f;
    p.room_hf_mb = 0.0f;
    p.room_lf_mb = 0.0f;
    p.decay_time = decay;
    p.decay_hf_ratio = 1.0f;
    p.reflections_mb = -10000.0f;
    p.reflections_delay = 0.0f;
    p.reverb_mb = 0.0f;
    p.reverb_delay = 0.0f;
    p.diffusion = 50.0f;
    p.density = 100.0f;
    p.dry_mb = -10000.0f;
    return p;
}

TEST(NgsReverb, DecayTimeParameterSetsTheTailLength) {
    for (const float decay : { 0.5f, 1.0f, 2.0f, 4.0f }) {
        for (const uint32_t ch : { 0u, 1u }) {
            const auto h = late_impulse_response(tail_only(decay), kRate, decay * 3.0 + 1.0, ch);
            const double t60 = measure_t60(h, kRate);
            EXPECT_NEAR(t60, decay, 0.2 * decay) << "decay=" << decay << " ch=" << ch;
        }
    }
}

TEST(NgsReverb, HFRatioShortensTheHighBand) {
    ReverbParams flat = tail_only(2.0f);
    ReverbParams damped = flat;
    damped.decay_hf_ratio = 0.2f;
    const auto a = late_impulse_response(flat, kRate, 3.0);
    const auto b = late_impulse_response(damped, kRate, 3.0);
    double ea = 0.0, eb = 0.0;
    for (size_t i = static_cast<size_t>(1.0 * kRate); i < a.size(); i++) {
        ea += a[i] * a[i];
        eb += b[i] * b[i];
    }
    EXPECT_LT(eb, ea);
    // the damped tail has less high-frequency content: compare first-difference energy ratio
    double da = 0.0, db = 0.0;
    for (size_t i = static_cast<size_t>(1.0 * kRate); i + 1 < a.size(); i++) {
        da += (a[i + 1] - a[i]) * (a[i + 1] - a[i]);
        db += (b[i + 1] - b[i]) * (b[i + 1] - b[i]);
    }
    EXPECT_LT(db / eb, da / ea);
}

TEST(NgsReverb, GainParametersScaleTheirPath) {
    ReverbParams loud = tail_only(1.0f);
    ReverbParams quiet = loud;
    quiet.reverb_mb = -2000.0f; // -20 dB
    const auto a = late_impulse_response(loud, kRate, 1.0);
    const auto b = late_impulse_response(quiet, kRate, 1.0);
    for (size_t i = 0; i < a.size(); i += 97)
        EXPECT_NEAR(b[i], a[i] * 0.1, 1e-5);
}

TEST(NgsReverb, KeyOnResetAndBypassedStateIsClean) {
    const ReverbParams p = default_reverb();
    const auto in = random_stereo(5000, 3, 0.3f);
    std::vector<float> o1(in.size()), o2(in.size());
    ReverbState st;
    reverb_process(st, p, kRate, 2, in.data(), o1.data(), 5000);
    st.reset();
    reverb_process(st, p, kRate, 2, in.data(), o2.data(), 5000);
    EXPECT_EQ(o1, o2);
}

TEST(NgsReverb, SilenceDecaysAndStaysBounded) {
    ReverbParams p = default_reverb();
    p.decay_time = 20.0f;
    p.reverb_mb = 2000.0f;
    p.reflections_mb = 2000.0f;
    p.room_mb = 2000.0f;
    const auto loud = random_stereo(48000, 1, 1.0f);
    std::vector<float> out(loud.size());
    ReverbState st;
    reverb_process(st, p, kRate, 2, loud.data(), out.data(), 48000);
    EXPECT_TRUE(all_bounded(out));
    std::vector<float> zero(loud.size(), 0.0f);
    for (int i = 0; i < 20; i++) {
        reverb_process(st, p, kRate, 2, zero.data(), out.data(), 48000);
        ASSERT_TRUE(all_bounded(out));
    }
}

TEST(NgsReverb, MonoVoiceDuplicatesLeft) {
    const ReverbParams p = default_reverb();
    const auto in = random_stereo(3000, 14, 0.3f);
    std::vector<float> out(in.size());
    ReverbState st;
    reverb_process(st, p, kRate, 1, in.data(), out.data(), 3000);
    for (uint32_t i = 0; i < 3000; i++)
        EXPECT_EQ(out[i * 2], out[i * 2 + 1]);
}

TEST(NgsReverb, GarbageParamsAreSafe) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float vals[] = { nan, inf, -inf, 1e30f, -1e30f, -5.0f, 0.0f, 1.0f, 30000.0f };
    auto in = random_stereo(600, 13, 0.8f);
    in[7] = nan;
    in[20] = -inf;
    std::vector<float> out(in.size());
    ReverbState st;
    for (const float a : vals)
        for (const float b : vals) {
            ReverbParams p;
            p.room_mb = a;
            p.room_hf_mb = b;
            p.decay_time = a;
            p.decay_hf_ratio = b;
            p.reflections_mb = a;
            p.reflections_delay = b;
            p.reverb_mb = b;
            p.reverb_delay = a;
            p.diffusion = a;
            p.density = b;
            p.hf_reference = a;
            p.lf_reference = b;
            p.room_lf_mb = a;
            p.dry_mb = b;
            p.early_scalar = a;
            p.pattern[0] = 99;
            p.pattern[1] = 0xFFFFFFFFu;
            for (int rep = 0; rep < 2; rep++) {
                reverb_process(st, p, kRate, 2, in.data(), out.data(), 600);
                ASSERT_TRUE(all_bounded(out));
            }
        }
}

} // namespace
