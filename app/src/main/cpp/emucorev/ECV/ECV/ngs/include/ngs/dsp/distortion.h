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

// Host-independent distortion kernel for the NGS distortion module.
//
// PROVISIONAL: the original transfer function is not recovered. The parameter names
// (A, B, clip, gate, wet gain, dry gain) are mapped to a generic gated hard clipper:
//   g(x) = 0 if |x| <= gate else x                  (noise gate, applied to the wet path only)
//   u    = A * g(x) + B                             (pre-gain A, static bias B)
//   wet  = clamp(u, -|clip|, +|clip|) - clamp(B, -|clip|, +|clip|)   (hard clip, bias response removed)
//   out  = clamp(wet * wetGain + x * dryGain, -1, 1)
// The module is stateless.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ngs::dsp {

struct DistortionParams {
    float a = 1.0f;
    float b = 0.0f;
    float clip = 1.0f;
    float gate = 0.0f;
    float wet_gain = 1.0f;
    float dry_gain = 0.0f;
};

inline float distortion_finite_or(const float v, const float fallback, const float limit) {
    return std::isfinite(v) ? std::clamp(v, -limit, limit) : fallback;
}

// Replaces non-finite values by neutral ones and bounds the rest.
inline DistortionParams distortion_sanitise(const DistortionParams &p) {
    DistortionParams r;
    r.a = distortion_finite_or(p.a, 1.0f, 1.0e4f);
    r.b = distortion_finite_or(p.b, 0.0f, 1.0e4f);
    r.clip = std::fabs(distortion_finite_or(p.clip, 1.0f, 1.0e4f));
    r.gate = std::fabs(distortion_finite_or(p.gate, 0.0f, 1.0e4f));
    r.wet_gain = distortion_finite_or(p.wet_gain, 0.0f, 1.0e4f);
    r.dry_gain = distortion_finite_or(p.dry_gain, 0.0f, 1.0e4f);
    return r;
}

// Wet-path transfer curve (before the wet/dry gains). `p` must be sanitised.
inline double distortion_shape(const double x, const DistortionParams &p) {
    const double clip = p.clip;
    const double g = std::fabs(x) <= static_cast<double>(p.gate) ? 0.0 : x;
    const double u = static_cast<double>(p.a) * g + static_cast<double>(p.b);
    return std::clamp(u, -clip, clip) - std::clamp(static_cast<double>(p.b), -clip, clip);
}

// Interleaved stereo frames; `in` may alias `out`. channels == 1 processes only the
// left channel and duplicates it (mono rack voices carry duplicated data).
inline void distortion_process(const DistortionParams &raw, const uint32_t channels, const float *in, float *out, const uint32_t frames) {
    const DistortionParams p = distortion_sanitise(raw);
    const uint32_t lanes = channels == 1 ? 1 : 2;
    for (uint32_t i = 0; i < frames; i++) {
        float res[2] = { 0.0f, 0.0f };
        for (uint32_t c = 0; c < lanes; c++) {
            const double x = std::isfinite(in[i * 2 + c]) ? static_cast<double>(in[i * 2 + c]) : 0.0;
            const double y = distortion_shape(x, p) * p.wet_gain + x * p.dry_gain;
            res[c] = static_cast<float>(std::clamp(y, -1.0, 1.0));
        }
        out[i * 2] = res[0];
        out[i * 2 + 1] = lanes == 1 ? res[0] : res[1];
    }
}

} // namespace ngs::dsp
