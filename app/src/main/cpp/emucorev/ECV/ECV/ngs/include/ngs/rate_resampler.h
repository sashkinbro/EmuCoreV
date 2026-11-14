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

#include <ngs/system.h>
#include <numeric>

struct SwrContext;

namespace ngs {
// Bound reconstruction work/memory for untrusted archive rates and positions.
inline constexpr uint64_t max_rate_replay_prefix_frames = 1 << 20;
inline constexpr uint64_t max_rate_replay_output_frames = 8 << 20;

struct StereoRateResamplerLogicalState {
    PCMFrameQueue input_history;
    bool needs_reset = false;
    // Absolute input position is needed to reconstruct the rational sampling
    // phase after old FIR history has been discarded.
    uint64_t total_input_frames = 0;
    int32_t source_rate = 0;
    int32_t dest_rate = 0;

    bool is_replayable() const {
        const uint64_t history = input_history.available_frames();
        if (source_rate == 0 && dest_rate == 0)
            return total_input_frames == 0 && history == 0;
        if (source_rate <= 0 || dest_rate <= 0 || total_input_frames < history || (total_input_frames && !history))
            return false;
        const uint64_t period = source_rate / std::gcd(source_rate, dest_rate);
        const uint64_t prefix = (total_input_frames - history) % period;
        return prefix <= max_rate_replay_prefix_frames
            && (history + std::min<uint64_t>(prefix, 1024)) * static_cast<uint64_t>(dest_rate) / source_rate <= max_rate_replay_output_frames;
    }

    void clear() {
        input_history.clear();
        needs_reset = false;
        total_input_frames = 0;
        source_rate = 0;
        dest_rate = 0;
    }

    void reset() {
        clear();
        needs_reset = true;
    }
};

struct StereoRateResamplerRuntimeState {
    SwrContext *context = nullptr;
    int source_rate = 0;
    int dest_rate = 0;
    std::vector<uint8_t> scratch_buffer;
};

void destroy_stereo_rate_resampler(StereoRateResamplerRuntimeState &runtime);
bool ensure_stereo_rate_resampler(StereoRateResamplerRuntimeState &runtime, StereoRateResamplerLogicalState &logical,
    int source_rate, int dest_rate);
uint32_t process_stereo_rate_resampler(StereoRateResamplerRuntimeState &runtime, StereoRateResamplerLogicalState &logical,
    const uint8_t *input, uint32_t input_frames, PCMFrameQueue &output);

} // namespace ngs
