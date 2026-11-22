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

// Host-independent biquad kernels for the NGS filter / equalizer modules.
// No dependency on EmuEnvState, guest memory or the NGS system types, so the
// exact same code is unit-tested on the host (app/src/test/core-host).
//
// PROVISIONAL: the SDK only names the filter modes and gives the units of
// SceNgsParamFilter (frequency, resonance, gain). The transfer functions below
// are the standard RBJ "Audio EQ Cookbook" forms; they have not been verified
// against firmware output. Interpretation used:
//   fFrequency  - Hz
//   fResonance  - Q
//   fGain       - dB (only used by PEAK / HIGHSHELF / LOWSHELF)

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ngs::dsp {

// Numeric values mirror SceNgsParamFilterMode (static_assert'ed in filter.cpp).
enum FilterModeId : uint32_t {
    FILTER_OFF = 0,
    FILTER_LOWPASS_RESONANT = 1,
    FILTER_HIGHPASS_RESONANT = 2,
    FILTER_BANDPASS_PEAK = 3,
    FILTER_BANDPASS_ZERO = 4,
    FILTER_NOTCH = 5,
    FILTER_PEAK = 6,
    FILTER_HIGHSHELF = 7,
    FILTER_LOWSHELF = 8,
    FILTER_LOWPASS_ONEPOLE = 9,
    FILTER_HIGHPASS_ONEPOLE = 10,
    FILTER_ALLPASS = 11,
    FILTER_LOWPASS_RESONANT_NORMALIZED = 12,
};

// Transfer function (a0 normalised to 1):
//   y[n] = b0 x[n] + b1 x[n-1] + b2 x[n-2] - a1 y[n-1] - a2 y[n-2]
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    bool active = false; // false -> pass-through (mode OFF or invalid parameters)
};

// Per-channel direct form I history. Doubles keep long resonant tails exact
// enough for the 1e-5 host reference tolerance and make coefficient changes
// mid-stream well defined (the history is signal, not coefficient dependent).
struct BiquadState {
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    void reset() { *this = BiquadState(); }
};

constexpr double biquad_default_q = 0.70710678118654752440; // PROVISIONAL default when Q is not finite

// Sanitises the guest supplied values (guest memory may hold anything).
inline BiquadCoeffs make_biquad(const uint32_t mode, const float frequency_hz, const float q_in, const float gain_db_in, const double sample_rate) {
    BiquadCoeffs c;
    if (mode == FILTER_OFF || mode > FILTER_LOWPASS_RESONANT_NORMALIZED || !(sample_rate > 0.0))
        return c;
    if (!std::isfinite(frequency_hz) || frequency_hz <= 0.0f)
        return c;

    const double nyquist = sample_rate * 0.5;
    const double freq = std::clamp(static_cast<double>(frequency_hz), 1.0, nyquist * 0.999);
    const double q = std::isfinite(q_in) ? std::clamp(static_cast<double>(q_in), 0.05, 100.0) : biquad_default_q;
    const double gain_db = std::isfinite(gain_db_in) ? std::clamp(static_cast<double>(gain_db_in), -60.0, 60.0) : 0.0;

    const double pi = 3.14159265358979323846;
    const double w0 = 2.0 * pi * freq / sample_rate;
    const double cs = std::cos(w0);
    const double sn = std::sin(w0);
    const double alpha = sn / (2.0 * q);
    const double A = std::pow(10.0, gain_db / 40.0);

    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (mode) {
    case FILTER_LOWPASS_RESONANT:
    case FILTER_LOWPASS_RESONANT_NORMALIZED:
        b0 = (1.0 - cs) / 2.0;
        b1 = 1.0 - cs;
        b2 = (1.0 - cs) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha;
        if (mode == FILTER_LOWPASS_RESONANT_NORMALIZED) {
            // PROVISIONAL: "normalized" taken as peak-normalised, i.e. the resonant
            // peak (gain Q at w0) is scaled back to at most unity.
            const double norm = 1.0 / std::max(1.0, q);
            b0 *= norm;
            b1 *= norm;
            b2 *= norm;
        }
        break;
    case FILTER_HIGHPASS_RESONANT:
        b0 = (1.0 + cs) / 2.0;
        b1 = -(1.0 + cs);
        b2 = (1.0 + cs) / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha;
        break;
    case FILTER_BANDPASS_PEAK: // constant skirt gain, peak gain = Q
        b0 = sn / 2.0;
        b1 = 0.0;
        b2 = -sn / 2.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha;
        break;
    case FILTER_BANDPASS_ZERO: // constant 0 dB peak gain
        b0 = alpha;
        b1 = 0.0;
        b2 = -alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha;
        break;
    case FILTER_NOTCH:
        b0 = 1.0;
        b1 = -2.0 * cs;
        b2 = 1.0;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha;
        break;
    case FILTER_PEAK:
        b0 = 1.0 + alpha * A;
        b1 = -2.0 * cs;
        b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha / A;
        break;
    case FILTER_LOWSHELF: {
        // PROVISIONAL: shelf alpha uses sin(w0)/(2Q) (cookbook Q form), not the slope form.
        const double sa = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cs + sa);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cs);
        b2 = A * ((A + 1.0) - (A - 1.0) * cs - sa);
        a0 = (A + 1.0) + (A - 1.0) * cs + sa;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs);
        a2 = (A + 1.0) + (A - 1.0) * cs - sa;
        break;
    }
    case FILTER_HIGHSHELF: {
        const double sa = 2.0 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cs + sa);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cs);
        b2 = A * ((A + 1.0) + (A - 1.0) * cs - sa);
        a0 = (A + 1.0) - (A - 1.0) * cs + sa;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs);
        a2 = (A + 1.0) - (A - 1.0) * cs - sa;
        break;
    }
    case FILTER_ALLPASS:
        b0 = 1.0 - alpha;
        b1 = -2.0 * cs;
        b2 = 1.0 + alpha;
        a0 = 1.0 + alpha;
        a1 = -2.0 * cs;
        a2 = 1.0 - alpha;
        break;
    case FILTER_LOWPASS_ONEPOLE:
    case FILTER_HIGHPASS_ONEPOLE: {
        // PROVISIONAL: first order sections through the bilinear transform, Q/gain ignored.
        const double K = std::tan(w0 / 2.0);
        a0 = 1.0 + K;
        a1 = K - 1.0;
        a2 = 0.0;
        if (mode == FILTER_LOWPASS_ONEPOLE) {
            b0 = K;
            b1 = K;
        } else {
            b0 = 1.0;
            b1 = -1.0;
        }
        b2 = 0.0;
        break;
    }
    default:
        return c;
    }

    c.b0 = b0 / a0;
    c.b1 = b1 / a0;
    c.b2 = b2 / a0;
    c.a1 = a1 / a0;
    c.a2 = a2 / a0;
    c.active = true;
    return c;
}

// PROVISIONAL: SceNgsParamCoEff is applied as b0,b1,b2 / 1,a1,a2 with the
// denominator sign convention of make_biquad above.
inline BiquadCoeffs make_biquad_from_coeffs(const float b0, const float b1, const float b2, const float a1, const float a2) {
    BiquadCoeffs c;
    if (!std::isfinite(b0) || !std::isfinite(b1) || !std::isfinite(b2) || !std::isfinite(a1) || !std::isfinite(a2))
        return c;
    c.b0 = b0;
    c.b1 = b1;
    c.b2 = b2;
    c.a1 = a1;
    c.a2 = a2;
    c.active = true;
    return c;
}

inline double biquad_tick(const BiquadCoeffs &c, BiquadState &s, const double x) {
    const double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    return y;
}

// Filters interleaved stereo frames (L,R). `in` may alias `out`.
// channels == 1: only the left channel is filtered through states[0] and copied
// to the right channel (mono rack voices carry duplicated data in the stereo buffer).
inline void biquad_process(const BiquadCoeffs &c, BiquadState *states, const uint32_t channels, const float *in, float *out, const uint32_t frames) {
    if (!c.active) {
        if (in != out) {
            for (uint32_t i = 0; i < frames * 2; i++)
                out[i] = in[i];
        }
        return;
    }
    if (channels == 1) {
        for (uint32_t i = 0; i < frames; i++) {
            const float y = static_cast<float>(biquad_tick(c, states[0], in[i * 2]));
            out[i * 2] = y;
            out[i * 2 + 1] = y;
        }
        return;
    }
    for (uint32_t i = 0; i < frames; i++) {
        const float l = static_cast<float>(biquad_tick(c, states[0], in[i * 2]));
        const float r = static_cast<float>(biquad_tick(c, states[1], in[i * 2 + 1]));
        out[i * 2] = l;
        out[i * 2 + 1] = r;
    }
}

} // namespace ngs::dsp
