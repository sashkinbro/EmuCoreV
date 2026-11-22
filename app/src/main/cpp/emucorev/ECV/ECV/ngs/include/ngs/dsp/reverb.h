// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

// Host-independent I3DL2-style reverb kernel for the NGS reverb module.
//
// PROVISIONAL: the original reverb is not recovered. SceNgsReverbParams follows the
// I3DL2 / EAX parameter set, so the fields are mapped with documented formulas to a
// generic early-reflection + Freeverb-style late network. Assumed units (not proven):
// room, roomHF, roomLF, reflections, reverb, dryMB in millibels (gain = 10^(mB/2000)),
// decayTime / reflectionsDelay / reverbDelay in seconds, diffusion / density in percent,
// HFReference / LFReference in Hz.
//
// Signal flow, per sample (mono sum m = (L + R) / 2):
//   w  = roomGain * ( lowGain * LP(m, LFRef) + mid + hfGain * (m - LP(m, HFRef)) )
//        with mid = m - LP(m, LFRef) - (m - LP(m, HFRef)); lowGain = 10^(roomLF/2000),
//        hfGain = 10^(roomHF/2000)                      (two-band room filter)
//   early_c = reflGain * sum_k g_k * w[n - (reflectionsDelay + t_k * earlyScalar) * fs]
//        (6 taps from the table selected by eEarlyReflectionPattern[c]; t_k, g_k PROVISIONAL)
//   late_c  = AP4_c( sum_i COMB_i,c( w[n - (reflectionsDelay + reverbDelay) * fs] ) / nComb )
//        8 Freeverb comb lengths scaled to the sample rate, stereo spread 23 samples on the
//        right channel, nComb = 4 + round(4 * density / 100) active combs (PROVISIONAL),
//        allpass feedback 0.7 * diffusion / 100.
//        Each comb has loop gain g = 10^(-3 L / (fs * decayTime)) at DC (so the reverb tail
//        decays 60 dB in decayTime) and a first-order damping filter with gain
//        gHF = 10^(-3 L / (fs * decayTime * decayHFRatio)) at Nyquist (PROVISIONAL: the
//        HF decay is referenced to Nyquist, not to HFReference).
//   out_c = clamp( dryGain * in_c + early_c + reverbGain * late_c, -1, 1 )
// The state is kept per voice across buffers and cleared on key-on.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ngs::dsp {

inline constexpr uint32_t reverb_max_combs = 8;
inline constexpr uint32_t reverb_allpasses = 4;
inline constexpr uint32_t reverb_early_taps = 6;
inline constexpr uint32_t reverb_patterns = 6;

struct ReverbParams {
    float room_mb = 0.0f;
    float room_hf_mb = 0.0f;
    float decay_time = 1.49f;
    float decay_hf_ratio = 0.83f;
    float reflections_mb = -2602.0f;
    float reflections_delay = 0.007f;
    float reverb_mb = 200.0f;
    float reverb_delay = 0.011f;
    float diffusion = 100.0f;
    float density = 100.0f;
    float hf_reference = 5000.0f;
    uint32_t pattern[2] = { 0, 1 };
    float early_scalar = 1.0f;
    float lf_reference = 250.0f;
    float room_lf_mb = 0.0f;
    float dry_mb = 0.0f;
};

// PROVISIONAL early reflection taps: {time in ms, gain}. Left/right tables of three rooms.
inline constexpr float reverb_early_table[reverb_patterns][reverb_early_taps][2] = {
    { { 5.0f, 0.70f }, { 11.0f, 0.55f }, { 17.0f, 0.45f }, { 23.0f, 0.36f }, { 31.0f, 0.28f }, { 41.0f, 0.20f } },
    { { 7.0f, 0.68f }, { 13.0f, 0.52f }, { 19.0f, 0.43f }, { 29.0f, 0.34f }, { 37.0f, 0.26f }, { 43.0f, 0.19f } },
    { { 9.0f, 0.65f }, { 19.0f, 0.50f }, { 29.0f, 0.40f }, { 41.0f, 0.32f }, { 53.0f, 0.24f }, { 67.0f, 0.17f } },
    { { 11.0f, 0.63f }, { 23.0f, 0.48f }, { 31.0f, 0.38f }, { 47.0f, 0.30f }, { 59.0f, 0.22f }, { 71.0f, 0.16f } },
    { { 13.0f, 0.60f }, { 27.0f, 0.46f }, { 43.0f, 0.37f }, { 59.0f, 0.29f }, { 73.0f, 0.21f }, { 89.0f, 0.15f } },
    { { 17.0f, 0.58f }, { 31.0f, 0.44f }, { 47.0f, 0.35f }, { 61.0f, 0.27f }, { 79.0f, 0.20f }, { 97.0f, 0.14f } },
};

// Freeverb tunings at 44100 Hz.
inline constexpr uint32_t reverb_comb_base[reverb_max_combs] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
inline constexpr uint32_t reverb_allpass_base[reverb_allpasses] = { 556, 441, 341, 225 };
inline constexpr uint32_t reverb_stereo_spread = 23;
inline constexpr double reverb_max_pre_seconds = 0.6; // reflectionsDelay (<= 0.3) + early tap (<= 0.16) / + reverbDelay (<= 0.1)

struct ReverbComb {
    std::vector<double> buf;
    uint32_t idx = 0;
    double damp = 0.0;
};

struct ReverbAllpass {
    std::vector<double> buf;
    uint32_t idx = 0;
};

struct ReverbState {
    double rate = 0.0;
    std::vector<double> pre; // power of two, holds the room-filtered mono input
    uint64_t pre_write = 0;
    double lf_lp = 0.0;
    double hf_lp = 0.0;
    ReverbComb comb[2][reverb_max_combs];
    ReverbAllpass allpass[2][reverb_allpasses];

    static uint32_t scaled(const uint32_t base, const double rate) {
        return std::max<uint32_t>(1, static_cast<uint32_t>(std::lround(static_cast<double>(base) * rate / 44100.0)));
    }
    uint32_t comb_length(const uint32_t channel, const uint32_t i) const {
        return scaled(reverb_comb_base[i] + (channel ? reverb_stereo_spread : 0), rate);
    }

    // (Re)allocates for `new_rate` and clears everything.
    void init(const double new_rate) {
        rate = new_rate;
        uint64_t cap = 16;
        const uint64_t need = static_cast<uint64_t>(std::ceil(reverb_max_pre_seconds * rate)) + 2;
        while (cap < need)
            cap <<= 1;
        pre.assign(cap, 0.0);
        for (uint32_t c = 0; c < 2; c++) {
            for (uint32_t i = 0; i < reverb_max_combs; i++)
                comb[c][i].buf.assign(comb_length(c, i), 0.0);
            for (uint32_t i = 0; i < reverb_allpasses; i++)
                allpass[c][i].buf.assign(scaled(reverb_allpass_base[i] + (c ? reverb_stereo_spread : 0), rate), 0.0);
        }
        reset();
    }

    void reset() {
        std::fill(pre.begin(), pre.end(), 0.0);
        pre_write = 0;
        lf_lp = hf_lp = 0.0;
        for (auto &ch : comb)
            for (auto &c : ch) {
                std::fill(c.buf.begin(), c.buf.end(), 0.0);
                c.idx = 0;
                c.damp = 0.0;
            }
        for (auto &ch : allpass)
            for (auto &a : ch) {
                std::fill(a.buf.begin(), a.buf.end(), 0.0);
                a.idx = 0;
            }
    }
};

inline float reverb_finite_or(const float v, const float fallback, const float lo, const float hi) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

inline ReverbParams reverb_sanitise(const ReverbParams &p) {
    const ReverbParams d;
    ReverbParams r;
    r.room_mb = reverb_finite_or(p.room_mb, d.room_mb, -10000.0f, 2000.0f);
    r.room_hf_mb = reverb_finite_or(p.room_hf_mb, d.room_hf_mb, -10000.0f, 2000.0f);
    r.decay_time = reverb_finite_or(p.decay_time, d.decay_time, 0.1f, 20.0f);
    r.decay_hf_ratio = reverb_finite_or(p.decay_hf_ratio, d.decay_hf_ratio, 0.1f, 2.0f);
    r.reflections_mb = reverb_finite_or(p.reflections_mb, d.reflections_mb, -10000.0f, 2000.0f);
    r.reflections_delay = reverb_finite_or(p.reflections_delay, d.reflections_delay, 0.0f, 0.3f);
    r.reverb_mb = reverb_finite_or(p.reverb_mb, d.reverb_mb, -10000.0f, 2000.0f);
    r.reverb_delay = reverb_finite_or(p.reverb_delay, d.reverb_delay, 0.0f, 0.1f);
    r.diffusion = reverb_finite_or(p.diffusion, d.diffusion, 0.0f, 100.0f);
    r.density = reverb_finite_or(p.density, d.density, 0.0f, 100.0f);
    r.hf_reference = reverb_finite_or(p.hf_reference, d.hf_reference, 20.0f, 20000.0f);
    r.lf_reference = reverb_finite_or(p.lf_reference, d.lf_reference, 20.0f, 20000.0f);
    r.room_lf_mb = reverb_finite_or(p.room_lf_mb, d.room_lf_mb, -10000.0f, 2000.0f);
    r.dry_mb = reverb_finite_or(p.dry_mb, d.dry_mb, -10000.0f, 2000.0f);
    r.early_scalar = reverb_finite_or(p.early_scalar, d.early_scalar, 0.0f, 2.0f);
    for (uint32_t c = 0; c < 2; c++)
        r.pattern[c] = p.pattern[c] < reverb_patterns ? p.pattern[c] : c;
    return r;
}

inline double reverb_mb_to_gain(const double mb) {
    return std::pow(10.0, mb / 2000.0);
}

inline uint32_t reverb_active_combs(const double density) {
    return 4 + static_cast<uint32_t>(std::lround(4.0 * density / 100.0));
}

// Interleaved stereo frames; `in` may alias `out`. channels == 1 runs the left channel
// only and duplicates it.
inline void reverb_process(ReverbState &state, const ReverbParams &raw, const double rate, const uint32_t channels, const float *in, float *out, const uint32_t frames) {
    if (state.rate != rate || state.pre.empty())
        state.init(rate);
    const ReverbParams p = reverb_sanitise(raw);
    const uint32_t lanes = channels == 1 ? 1 : 2;
    constexpr double two_pi = 6.283185307179586476925286766559;

    const double room_gain = reverb_mb_to_gain(p.room_mb);
    const double low_gain = reverb_mb_to_gain(p.room_lf_mb);
    const double hf_gain = reverb_mb_to_gain(p.room_hf_mb);
    const double refl_gain = reverb_mb_to_gain(p.reflections_mb);
    const double rev_gain = reverb_mb_to_gain(p.reverb_mb);
    const double dry_gain = reverb_mb_to_gain(p.dry_mb);
    const double lf_a = std::exp(-two_pi * std::min<double>(p.lf_reference, 0.45 * rate) / rate);
    const double hf_a = std::exp(-two_pi * std::min<double>(p.hf_reference, 0.45 * rate) / rate);
    const double ap_g = 0.7 * static_cast<double>(p.diffusion) / 100.0;
    const uint32_t n_comb = reverb_active_combs(p.density);

    double comb_b[2][reverb_max_combs], comb_p[2][reverb_max_combs];
    for (uint32_t c = 0; c < lanes; c++) {
        for (uint32_t i = 0; i < n_comb; i++) {
            const double len = static_cast<double>(state.comb[c][i].buf.size());
            const double g = std::pow(10.0, -3.0 * len / (rate * p.decay_time));
            const double g_hf = std::pow(10.0, -3.0 * len / (rate * p.decay_time * p.decay_hf_ratio));
            const double pole = (g - g_hf) / (g + g_hf);
            comb_p[c][i] = pole;
            comb_b[c][i] = g * (1.0 - pole);
        }
    }

    const uint64_t pre_mask = state.pre.size() - 1;
    const auto delay_samples = [&](const double seconds) {
        return static_cast<uint64_t>(std::min<double>(std::lround(seconds * rate), static_cast<double>(state.pre.size() - 2)));
    };
    const uint64_t late_delay = delay_samples(static_cast<double>(p.reflections_delay) + static_cast<double>(p.reverb_delay));
    uint64_t early_delay[2][reverb_early_taps];
    double early_gain[2][reverb_early_taps];
    for (uint32_t c = 0; c < lanes; c++) {
        for (uint32_t k = 0; k < reverb_early_taps; k++) {
            const double t = static_cast<double>(p.reflections_delay)
                + static_cast<double>(reverb_early_table[p.pattern[c]][k][0]) * 0.001 * static_cast<double>(p.early_scalar);
            early_delay[c][k] = delay_samples(t);
            early_gain[c][k] = static_cast<double>(reverb_early_table[p.pattern[c]][k][1]) * refl_gain;
        }
    }

    for (uint32_t i = 0; i < frames; i++) {
        double x[2] = { 0.0, 0.0 };
        for (uint32_t c = 0; c < lanes; c++)
            x[c] = std::isfinite(in[i * 2 + c]) ? static_cast<double>(in[i * 2 + c]) : 0.0;
        const double mono = lanes == 1 ? x[0] : 0.5 * (x[0] + x[1]);

        state.lf_lp = (1.0 - lf_a) * mono + lf_a * state.lf_lp;
        state.hf_lp = (1.0 - hf_a) * mono + hf_a * state.hf_lp;
        const double low = state.lf_lp;
        const double high = mono - state.hf_lp;
        const double mid = mono - low - high;
        const double w = room_gain * (low_gain * low + mid + hf_gain * high);

        state.pre[state.pre_write & pre_mask] = w;
        state.pre_write++;
        const auto tap = [&](const uint64_t back) {
            return back < state.pre_write ? state.pre[(state.pre_write - 1 - back) & pre_mask] : 0.0;
        };
        const double late_in = tap(late_delay);

        double res[2] = { 0.0, 0.0 };
        for (uint32_t c = 0; c < lanes; c++) {
            double early = 0.0;
            for (uint32_t k = 0; k < reverb_early_taps; k++)
                early += early_gain[c][k] * tap(early_delay[c][k]);

            double sum = 0.0;
            for (uint32_t k = 0; k < n_comb; k++) {
                ReverbComb &cb = state.comb[c][k];
                const double y = cb.buf[cb.idx];
                cb.damp = comb_b[c][k] * y + comb_p[c][k] * cb.damp;
                cb.buf[cb.idx] = late_in + cb.damp;
                cb.idx = cb.idx + 1 == cb.buf.size() ? 0 : cb.idx + 1;
                sum += y;
            }
            double late = sum / static_cast<double>(n_comb);
            for (uint32_t k = 0; k < reverb_allpasses; k++) {
                ReverbAllpass &ap = state.allpass[c][k];
                const double buffered = ap.buf[ap.idx];
                const double input = late;
                late = -input + buffered;
                ap.buf[ap.idx] = input + buffered * ap_g;
                ap.idx = ap.idx + 1 == ap.buf.size() ? 0 : ap.idx + 1;
            }
            res[c] = std::clamp(dry_gain * x[c] + early + rev_gain * late, -1.0, 1.0);
        }
        out[i * 2] = static_cast<float>(res[0]);
        out[i * 2 + 1] = static_cast<float>(lanes == 1 ? res[0] : res[1]);
    }
}

} // namespace ngs::dsp
