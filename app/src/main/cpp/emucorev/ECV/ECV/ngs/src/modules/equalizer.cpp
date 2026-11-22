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

#include <ngs/modules/equalizer.h>
#include <ngs/modules/send_stage.h>
#include <util/log.h>

namespace ngs {

static_assert(SCE_NGS_MAX_EQ_FILTERS == max_biquad_stages);

std::unique_ptr<ModuleLogicalState> EqualizerModule::create_logical_state() const {
    return std::make_unique<BiquadLogicalState>();
}

void EqualizerModule::on_key_on(const MemState &mem, ModuleData &data) {
    if (auto *state = static_cast<BiquadLogicalState *>(data.logical_state.get())) {
        for (uint32_t i = 0; i < max_biquad_stages; i++)
            state->reset_stage(i);
    }
}

bool EqualizerModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    Voice *voice = data.parent;
    BiquadLogicalState *state = data.get_logical_state<BiquadLogicalState>();

    dsp::BiquadCoeffs coeffs[max_biquad_stages];
    if (!data.is_bypassed) {
        const SceNgsParamsDescriptor *desc = data.get_parameters<SceNgsParamsDescriptor>(mem);
        if (desc && desc->id == SCE_NGS_PARAM_EQ_STRUCT_ID) {
            const SceNgsParamEqParams *params = data.get_parameters<SceNgsParamEqParams>(mem);
            for (uint32_t i = 0; i < max_biquad_stages; i++) {
                const SceNgsParamFilter &f = params->filter[i];
                coeffs[i] = dsp::make_biquad(f.eFilterMode, f.fFrequency, f.fResonance, f.fGain, voice->rack->system->sample_rate);
            }
        } else if (desc && desc->id == SCE_NGS_PARAM_EQ_COEFF_STRUCT_ID) {
            const SceNgsParamEqParamsCoEff *params = data.get_parameters<SceNgsParamEqParamsCoEff>(mem);
            for (uint32_t i = 0; i < max_biquad_stages; i++) {
                const SceNgsParamCoEff &c = params->filterCoEff[i];
                coeffs[i] = dsp::make_biquad_from_coeffs(c.fB0, c.fB1, c.fB2, c.fA1, c.fA2);
            }
        }
    }
    for (uint32_t i = 0; i < max_biquad_stages; i++) {
        if (!coeffs[i].active)
            state->reset_stage(i);
    }

    const uint32_t channels = voice->rack->channels_per_voice == 1 ? 1 : 2;
    const ModuleOrdinal ordinal = module_ordinal(data, module_id());

    // PROVISIONAL topology: with several equalizers in a voice the first one is an
    // insert on the main signal and the following ones are one send output each
    // (normal-player / ATRAC9 voices: 1 insert + 4 sends). A lone equalizer is an insert.
    if (ordinal.count >= 2 && ordinal.index >= 1) {
        run_send_stage(data, module_id(), 1, ordinal.index - 1, ordinal.count - 1, [&](const float *in, float *out, const uint32_t frames) {
            dsp::biquad_process(coeffs[0], state->stage[0], channels, in, out, frames);
            for (uint32_t i = 1; i < max_biquad_stages; i++)
                dsp::biquad_process(coeffs[i], state->stage[i], channels, out, out, frames);
        });
        return false;
    }

    float *signal = reinterpret_cast<float *>(voice->products[0].data);
    if (signal) {
        const uint32_t frames = static_cast<uint32_t>(voice->rack->system->granularity);
        for (uint32_t i = 0; i < max_biquad_stages; i++)
            dsp::biquad_process(coeffs[i], state->stage[i], channels, signal, signal, frames);
    }

    // Definitions with equalizers can have up to 4 outputs; the send stages replace these
    voice->products[1] = voice->products[0];
    voice->products[2] = voice->products[0];
    voice->products[3] = voice->products[0];

    return false;
}
} // namespace ngs
