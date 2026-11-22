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

// Host-independent multi-tap delay kernel for the NGS delay module.
//
// PROVISIONAL: the original algorithm is not recovered; this is the textbook feedback
// delay per tap, mapped onto the SceNgsDelayParams fields:
//   wet_t[n]  = F_t( line_t[n - D_t(n)] )              (linear interpolation, D_t >= 1 sample)
//   line_t[n] = x[n] + feedback_t * wet_t[n]
//   out[n]    = clamp(dry * x[n] + sum_t volume_t * wet_t[n], -1, 1)
//   D_t(n)    = delayMs * fs / 1000 + modWidthMs * fs / 1000 * sin(2 pi modRate n / fs + phaseDeg)
// F_t is the tap filter (off, one-pole lowpass, one-pole highpass, first-order allpass)
// at `cutoff` Hz. A tap with volume == 0 is inactive and keeps no state.
// Every channel has its own line; the LFO is shared by all taps. Delays are capped at
// delay_max_ms (PROVISIONAL) and the line grows on demand to fit the requested delay.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ngs::dsp {

inline constexpr uint32_t delay_max_taps = 4;
inline constexpr float delay_max_ms = 2000.0f; // PROVISIONAL cap

struct DelayTapParams {
    float delay_ms = 0.0f;
    float volume = 0.0f;
    float feedback = 0.0f;
    uint32_t filter_mode = 0; // 0 off, 1 lowpass one-pole, 2 highpass one-pole, 3 allpass
    float cutoff = 1000.0f;
    float phase_deg = 0.0f;
    float mod_width_ms = 0.0f;
};

struct DelayParams {
    float dry = 1.0f;
    float mod_rate = 0.0f;
    DelayTapParams taps[delay_max_taps];
};

struct DelayLane {
    std::vector<float> line; // power of two capacity
    uint64_t write = 0; // number of samples written so far
    double lp = 0.0; // one-pole lowpass / highpass state
    double ap_x1 = 0.0, ap_y1 = 0.0; // allpass state

    void reset() {
        if (write != 0)
            std::fill(line.begin(), line.end(), 0.0f);
        write = 0;
        lp = ap_x1 = ap_y1 = 0.0;
    }
    uint64_t mask() const { return line.size() - 1; }

    // Makes the line hold at least `needed` samples, keeping its history.
    void ensure(const uint64_t needed) {
        if (line.size() >= needed && !line.empty())
            return;
        uint64_t cap = 16;
        while (cap < needed)
            cap <<= 1;
        std::vector<float> grown(cap, 0.0f);
        const uint64_t old_cap = line.size();
        for (uint64_t k = 0; k < old_cap && k < write; k++) {
            const uint64_t t = write - 1 - k;
            grown[t & (cap - 1)] = line[t & (old_cap - 1)];
        }
        line.swap(grown);
    }
    // sample written `back` samples ago (back == 1 is the previous sample)
    double read(const uint64_t back) const {
        if (back > write || back > line.size())
            return 0.0;
        return line[(write - back) & mask()];
    }
};

struct DelayState {
    DelayLane lane[delay_max_taps][2];
    uint32_t last_mode[delay_max_taps] = {};
    uint64_t sample_counter = 0; // LFO time, continuous across buffers

    void reset() {
        for (auto &taps : lane)
            for (auto &l : taps)
                l.reset();
        sample_counter = 0;
    }
};

inline float delay_finite_or(const float v, const float fallback, const float lo, const float hi) {
    return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

inline DelayParams delay_sanitise(const DelayParams &p) {
    DelayParams r;
    r.dry = delay_finite_or(p.dry, 1.0f, -16.0f, 16.0f);
    r.mod_rate = delay_finite_or(p.mod_rate, 0.0f, 0.0f, 100.0f);
    for (uint32_t t = 0; t < delay_max_taps; t++) {
        const DelayTapParams &s = p.taps[t];
        DelayTapParams &d = r.taps[t];
        d.delay_ms = delay_finite_or(s.delay_ms, 0.0f, 0.0f, delay_max_ms);
        d.volume = delay_finite_or(s.volume, 0.0f, -16.0f, 16.0f);
        d.feedback = delay_finite_or(s.feedback, 0.0f, -0.999f, 0.999f);
        d.filter_mode = s.filter_mode <= 3 ? s.filter_mode : 0;
        d.cutoff = delay_finite_or(s.cutoff, 1000.0f, 0.0f, 1.0e6f);
        d.phase_deg = delay_finite_or(s.phase_deg, 0.0f, -1.0e6f, 1.0e6f);
        d.mod_width_ms = delay_finite_or(s.mod_width_ms, 0.0f, 0.0f, delay_max_ms);
    }
    return r;
}

// Delay of one tap in samples at LFO time `n`.
inline double delay_tap_samples(const DelayTapParams &t, const float mod_rate, const double rate, const uint64_t n) {
    constexpr double two_pi = 6.283185307179586476925286766559;
    constexpr double deg2rad = 0.017453292519943295769236907684886;
    double d = static_cast<double>(t.delay_ms) * rate / 1000.0;
    if (t.mod_width_ms != 0.0f) {
        const double phase = two_pi * static_cast<double>(mod_rate) * static_cast<double>(n) / rate
            + static_cast<double>(t.phase_deg) * deg2rad;
        d += static_cast<double>(t.mod_width_ms) * rate / 1000.0 * std::sin(phase);
    }
    const double cap = static_cast<double>(delay_max_ms) * rate / 1000.0;
    return std::clamp(d, 1.0, std::max(1.0, cap));
}

// Interleaved stereo frames; `in` may alias `out`. channels == 1 runs only the left lane
// and duplicates it.
inline void delay_process(DelayState &state, const DelayParams &raw, const double rate, const uint32_t channels, const float *in, float *out, const uint32_t frames) {
    const DelayParams p = delay_sanitise(raw);
    const uint32_t lanes = channels == 1 ? 1 : 2;
    constexpr double two_pi = 6.283185307179586476925286766559;
    constexpr double pi = 3.14159265358979323846264338327950288;

    double lp_a[delay_max_taps] = {}, ap_c[delay_max_taps] = {};
    bool active[delay_max_taps];
    for (uint32_t t = 0; t < delay_max_taps; t++) {
        const DelayTapParams &tp = p.taps[t];
        active[t] = tp.volume != 0.0f;
        if (!active[t]) {
            for (auto &l : state.lane[t])
                l.reset();
            continue;
        }
        if (state.last_mode[t] != tp.filter_mode) {
            for (auto &l : state.lane[t])
                l.lp = l.ap_x1 = l.ap_y1 = 0.0;
            state.last_mode[t] = tp.filter_mode;
        }
        const double fc = std::clamp(static_cast<double>(tp.cutoff), 1.0, 0.45 * rate);
        lp_a[t] = std::exp(-two_pi * fc / rate);
        const double tn = std::tan(pi * fc / rate);
        ap_c[t] = (tn - 1.0) / (tn + 1.0);
        const double max_samples = (static_cast<double>(tp.delay_ms) + static_cast<double>(tp.mod_width_ms)) * rate / 1000.0;
        const uint64_t need = static_cast<uint64_t>(std::ceil(std::min(max_samples, static_cast<double>(delay_max_ms) * rate / 1000.0))) + 4;
        for (uint32_t c = 0; c < lanes; c++)
            state.lane[t][c].ensure(need);
    }

    for (uint32_t i = 0; i < frames; i++) {
        const uint64_t n = state.sample_counter + i;
        double res[2] = { 0.0, 0.0 };
        double x[2] = { 0.0, 0.0 };
        for (uint32_t c = 0; c < lanes; c++) {
            x[c] = std::isfinite(in[i * 2 + c]) ? static_cast<double>(in[i * 2 + c]) : 0.0;
            res[c] = static_cast<double>(p.dry) * x[c];
        }

        for (uint32_t t = 0; t < delay_max_taps; t++) {
            if (!active[t])
                continue;
            const DelayTapParams &tp = p.taps[t];
            const double d = delay_tap_samples(tp, p.mod_rate, rate, n);
            const uint64_t d_int = static_cast<uint64_t>(d);
            const double frac = d - static_cast<double>(d_int);
            for (uint32_t c = 0; c < lanes; c++) {
                DelayLane &l = state.lane[t][c];
                double v = l.read(d_int);
                if (frac != 0.0)
                    v = v * (1.0 - frac) + l.read(d_int + 1) * frac;
                double w = v;
                switch (tp.filter_mode) {
                case 1:
                    l.lp = (1.0 - lp_a[t]) * v + lp_a[t] * l.lp;
                    w = l.lp;
                    break;
                case 2:
                    l.lp = (1.0 - lp_a[t]) * v + lp_a[t] * l.lp;
                    w = v - l.lp;
                    break;
                case 3:
                    w = ap_c[t] * v + l.ap_x1 - ap_c[t] * l.ap_y1;
                    l.ap_x1 = v;
                    l.ap_y1 = w;
                    break;
                default:
                    break;
                }
                res[c] += static_cast<double>(tp.volume) * w;
                l.line[l.write & l.mask()] = static_cast<float>(x[c] + static_cast<double>(tp.feedback) * w);
                l.write++;
            }
        }
        for (uint32_t c = 0; c < lanes; c++)
            res[c] = std::clamp(res[c], -1.0, 1.0);
        out[i * 2] = static_cast<float>(res[0]);
        out[i * 2 + 1] = static_cast<float>(lanes == 1 ? res[0] : res[1]);
    }
    state.sample_counter += frames;
}

} // namespace ngs::dsp
