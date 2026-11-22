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

// Host-independent waveform generator kernel for the NGS generator module.
//
// PROVISIONAL: the SDK names the modes and the SceNgsGeneratorParams fields
// but not the exact waveforms. Choices made here, none verified on firmware:
//   - phase phi in [0,1), advanced by nFrequency / sample_rate per frame and
//     kept across buffers and across parameter changes (phase continuous)
//   - SINE      sin(2 pi phi)
//   - TRIANGLE  starts at 0 rising: 4phi, 2-4phi, 4phi-4 over the three segments
//   - SAW       starts at 0 rising to +1 at phi = 0.5, wraps to -1: 2phi, 2phi-2
//   - PULSE     +1 while phi < fPulseWidth (fraction of the period), else -1
//   - NOISE     white noise from a xorshift32 generator, uniform in [-1, 1)
//   - NOISE_PSP sample-and-hold noise, a new random value every sample_rate/frequency frames
//   - fAmplitude is a linear gain; both stereo channels carry the same signal
//   - uPhaseAngle is the start phase in degrees, uSampleOffset advances the
//     start phase by that many frames; both are applied once per key-on

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ngs::dsp {

// Numeric values mirror SceNgsGeneratorMode (static_assert'ed in generator.cpp).
enum GeneratorModeId : uint32_t {
    GENERATOR_SINE = 0,
    GENERATOR_TRIANGLE = 1,
    GENERATOR_SAW = 2,
    GENERATOR_NOISE = 3,
    GENERATOR_NOISE_PSP = 4,
    GENERATOR_PULSE = 5,
};

struct GeneratorParams {
    uint32_t mode = GENERATOR_SINE;
    int32_t frequency_hz = 0;
    float amplitude = 0.0f;
    float pulse_width = 0.5f;
    uint32_t sample_offset = 0;
    uint32_t phase_angle_deg = 0;
};

struct GeneratorState {
    double phase = 0.0;
    uint32_t rng = 0x1badf00du;
    double hold_counter = 0.0;
    double held_value = 0.0;
    bool started = false;
};

inline double generator_next_noise(GeneratorState &s) {
    uint32_t x = s.rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s.rng = x;
    return static_cast<double>(x) / 2147483648.0 - 1.0;
}

inline double generator_phase_increment(const GeneratorParams &p, const double sample_rate) {
    if (!(sample_rate > 0.0))
        return 0.0;
    const double freq = std::clamp(static_cast<double>(p.frequency_hz), 0.0, sample_rate * 0.5);
    return freq / sample_rate;
}

// Applies uPhaseAngle / uSampleOffset and reseeds the noise. Called on the
// first rendered buffer after a key-on.
inline void generator_start(GeneratorState &s, const GeneratorParams &p, const double sample_rate) {
    s = GeneratorState();
    const double inc = generator_phase_increment(p, sample_rate);
    double phase = static_cast<double>(p.phase_angle_deg) / 360.0 + static_cast<double>(p.sample_offset) * inc;
    phase -= std::floor(phase);
    s.phase = phase;
    s.held_value = generator_next_noise(s);
    s.hold_counter = 0.0;
    s.started = true;
}

inline double generator_sample(GeneratorState &s, const GeneratorParams &p, const double inc) {
    const double phi = s.phase;
    double v = 0.0;
    switch (p.mode) {
    case GENERATOR_SINE:
        v = std::sin(2.0 * 3.14159265358979323846 * phi);
        break;
    case GENERATOR_TRIANGLE:
        v = phi < 0.25 ? 4.0 * phi : (phi < 0.75 ? 2.0 - 4.0 * phi : 4.0 * phi - 4.0);
        break;
    case GENERATOR_SAW:
        v = phi < 0.5 ? 2.0 * phi : 2.0 * phi - 2.0;
        break;
    case GENERATOR_PULSE: {
        const double width = std::isfinite(p.pulse_width) ? std::clamp(static_cast<double>(p.pulse_width), 0.0, 1.0) : 0.5;
        v = phi < width ? 1.0 : -1.0;
        break;
    }
    case GENERATOR_NOISE:
        v = generator_next_noise(s);
        break;
    case GENERATOR_NOISE_PSP:
        s.hold_counter += inc;
        while (s.hold_counter >= 1.0) {
            s.hold_counter -= 1.0;
            s.held_value = generator_next_noise(s);
        }
        v = s.held_value;
        break;
    default:
        break;
    }
    s.phase += inc;
    s.phase -= std::floor(s.phase);
    return v;
}

// Renders `frames` interleaved stereo frames. The caller supplies the
// parameters of this buffer (they may differ from the previous call).
inline void generator_render(GeneratorState &s, const GeneratorParams &p, const double sample_rate, float *out, const uint32_t frames) {
    if (!s.started)
        generator_start(s, p, sample_rate);
    const double inc = generator_phase_increment(p, sample_rate);
    const double amp = std::isfinite(p.amplitude) ? static_cast<double>(p.amplitude) : 0.0;
    for (uint32_t i = 0; i < frames; i++) {
        const float v = static_cast<float>(generator_sample(s, p, inc) * amp);
        out[i * 2] = v;
        out[i * 2 + 1] = v;
    }
}

} // namespace ngs::dsp
