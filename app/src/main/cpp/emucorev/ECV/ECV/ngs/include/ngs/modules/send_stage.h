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

#include <ngs/dsp/biquad.h>
#include <ngs/system.h>

#include <cstdint>
#include <cstring>

namespace ngs {

// Biquad history shared by the filter module (stage 0) and the equalizer module (4 bands).
// Not part of save states: a restored voice starts with a clean history.
inline constexpr uint32_t max_biquad_stages = 4;

struct BiquadLogicalState : public ModuleLogicalState {
    dsp::BiquadState stage[max_biquad_stages][2];
    // voice->frame_count of the update that last published this send output
    uint32_t published_frame = 0;
    bool published = false;

    void reset_stage(const uint32_t index) {
        stage[index][0].reset();
        stage[index][1].reset();
    }
};

struct ModuleOrdinal {
    uint32_t index = 0; ///< position among the voice's modules with the same id
    uint32_t count = 0; ///< number of modules with that id
};

inline ModuleOrdinal module_ordinal(const ModuleData &data, const uint32_t module_id) {
    ModuleOrdinal result;
    const auto &modules = data.parent->rack->modules;
    for (uint32_t i = 0; i < modules.size(); i++) {
        if (modules[i] && modules[i]->module_id() == module_id) {
            if (i < data.index)
                result.index++;
            result.count++;
        }
    }
    return result;
}

inline ModuleData *module_data_at_ordinal(Voice *voice, const uint32_t module_id, const uint32_t ordinal) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < voice->rack->modules.size() && i < voice->datas.size(); i++) {
        if (voice->rack->modules[i] && voice->rack->modules[i]->module_id() == module_id) {
            if (seen == ordinal)
                return &voice->datas[i];
            seen++;
        }
    }
    return nullptr;
}

// PROVISIONAL output topology (not recovered from firmware): the trailing modules
// of a voice that exist once per output (the FILTER modules of the simple/scream
// voices, the last four EQUALIZER modules of the normal-player/ATRAC9 voices)
// each derive output `send_index` from the main signal (products[0]) as it is at
// that point of the chain. Send 0 replaces the main signal only after every other
// send has read it, so it is published into the main buffer by the last send.
// `process(in, out, frames)` fills `out` from `in` (stereo float frames).
template <class ProcessFn>
void run_send_stage(ModuleData &data, const uint32_t module_id, const uint32_t first_send_ordinal, const uint32_t send_index, const uint32_t send_count, ProcessFn &&process) {
    Voice *voice = data.parent;
    const uint32_t frames = static_cast<uint32_t>(voice->rack->system->granularity);
    float *main_signal = reinterpret_cast<float *>(voice->products[0].data);
    if (!main_signal || frames == 0 || send_count == 0)
        return;

    const size_t bytes = static_cast<size_t>(frames) * 2 * sizeof(float);
    data.ensure_scratch_size(bytes);
    float *out = reinterpret_cast<float *>(data.scratch_data.data());
    process(static_cast<const float *>(main_signal), out, frames);

    BiquadLogicalState *state = static_cast<BiquadLogicalState *>(data.logical_state.get());
    if (state) {
        state->published = true;
        state->published_frame = voice->frame_count;
    }

    const bool is_last = send_index + 1 == send_count;
    if (send_index != 0 && send_index < MAX_VOICE_OUTPUT && send_index < voice->rack->vdef->output_count)
        voice->products[send_index].data = reinterpret_cast<uint8_t *>(out);

    if (!is_last)
        return;

    if (send_count == 1) {
        voice->products[0].data = reinterpret_cast<uint8_t *>(out);
        return;
    }

    ModuleData *first = module_data_at_ordinal(voice, module_id, first_send_ordinal);
    const BiquadLogicalState *first_state = first ? static_cast<const BiquadLogicalState *>(first->logical_state.get()) : nullptr;
    if (first && first_state && first_state->published && first_state->published_frame == voice->frame_count
        && first->scratch_data.size() >= bytes) {
        std::memcpy(main_signal, first->scratch_data.data(), bytes);
    }
}

} // namespace ngs
