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

// Host-independent mixing kernels. All buffers are interleaved stereo float
// (L,R per frame), the internal NGS format. Every accumulation is hard-clamped
// to [-1, 1], which is what the pipeline already does when a patch delivers
// into a voice input (VoiceInputManager::receive).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ngs::dsp {

// dest += src routed through a 2x2 volume matrix:
//   dest.L += src.L * m[0][0] + src.R * m[1][0]
//   dest.R += src.L * m[0][1] + src.R * m[1][1]
// (matrix[source channel][destination channel], as in SceNgsVolumeMatrix.)
// Operation order is kept identical to the historical inline loop in
// VoiceInputManager::receive so results stay bit-exact.
inline void accumulate_matrix(float *dest, const float *src, const float (&m)[2][2], const uint32_t frames) {
    for (uint32_t k = 0; k < frames; k++) {
        dest[k * 2] = std::clamp(dest[k * 2] + src[k * 2] * m[0][0] + src[k * 2 + 1] * m[1][0], -1.0f, 1.0f);
        dest[k * 2 + 1] = std::clamp(dest[k * 2 + 1] + src[k * 2] * m[0][1] + src[k * 2 + 1] * m[1][1], -1.0f, 1.0f);
    }
}

// Guards the mixer against garbage gains read from guest memory.
inline float sanitise_gain(const float gain) {
    return std::isfinite(gain) ? gain : 0.0f;
}

// out = clamp(a * gain_a + b * gain_b). Either input may be null (treated as
// silence). `out` may alias `a` or `b`.
inline void mix_two_ports(const float *a, const float gain_a, const float *b, const float gain_b, float *out, const uint32_t frames) {
    const uint32_t samples = frames * 2;
    for (uint32_t i = 0; i < samples; i++) {
        const float va = a ? a[i] * gain_a : 0.0f;
        const float vb = b ? b[i] * gain_b : 0.0f;
        out[i] = std::clamp(va + vb, -1.0f, 1.0f);
    }
}

} // namespace ngs::dsp
