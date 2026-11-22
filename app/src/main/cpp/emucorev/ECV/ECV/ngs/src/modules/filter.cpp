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

#include <ngs/modules/filter.h>
#include <ngs/modules/send_stage.h>
#include <util/log.h>

namespace ngs {

static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_LOWPASS_RESONANT) == static_cast<uint32_t>(dsp::FILTER_LOWPASS_RESONANT));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_HIGHPASS_RESONANT) == static_cast<uint32_t>(dsp::FILTER_HIGHPASS_RESONANT));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_BANDPASS_PEAK) == static_cast<uint32_t>(dsp::FILTER_BANDPASS_PEAK));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_BANDPASS_ZERO) == static_cast<uint32_t>(dsp::FILTER_BANDPASS_ZERO));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_NOTCH) == static_cast<uint32_t>(dsp::FILTER_NOTCH));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_PEAK) == static_cast<uint32_t>(dsp::FILTER_PEAK));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_HIGHSHELF) == static_cast<uint32_t>(dsp::FILTER_HIGHSHELF));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_LOWSHELF) == static_cast<uint32_t>(dsp::FILTER_LOWSHELF));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_LOWPASS_ONEPOLE) == static_cast<uint32_t>(dsp::FILTER_LOWPASS_ONEPOLE));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_HIGHPASS_ONEPOLE) == static_cast<uint32_t>(dsp::FILTER_HIGHPASS_ONEPOLE));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_ALLPASS) == static_cast<uint32_t>(dsp::FILTER_ALLPASS));
static_assert(static_cast<uint32_t>(SCE_NGS_FILTER_LOWPASS_RESONANT_NORMALIZED) == static_cast<uint32_t>(dsp::FILTER_LOWPASS_RESONANT_NORMALIZED));

std::unique_ptr<ModuleLogicalState> FilterModule::create_logical_state() const {
    return std::make_unique<BiquadLogicalState>();
}

void FilterModule::on_key_on(const MemState &mem, ModuleData &data) {
    if (auto *state = static_cast<BiquadLogicalState *>(data.logical_state.get()))
        state->reset_stage(0);
}

bool FilterModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    Voice *voice = data.parent;

    // Definitions with filters have 2 outputs
    voice->products[1] = voice->products[0];

    BiquadLogicalState *state = data.get_logical_state<BiquadLogicalState>();

    dsp::BiquadCoeffs coeffs;
    if (!data.is_bypassed) {
        const SceNgsParamsDescriptor *desc = data.get_parameters<SceNgsParamsDescriptor>(mem);
        if (desc && desc->id == SCE_NGS_FILTER_PARAMS_STRUCT_ID) {
            const SceNgsFilterParams *params = data.get_parameters<SceNgsFilterParams>(mem);
            coeffs = dsp::make_biquad(params->params.eFilterMode, params->params.fFrequency, params->params.fResonance,
                params->params.fGain, voice->rack->system->sample_rate);
        } else if (desc && desc->id == SCE_NGS_FILTER_PARAMS_COEFF_STRUCT_ID) {
            const SceNgsFilterParamsCoEff *params = data.get_parameters<SceNgsFilterParamsCoEff>(mem);
            coeffs = dsp::make_biquad_from_coeffs(params->params.fB0, params->params.fB1, params->params.fB2,
                params->params.fA1, params->params.fA2);
        }
    }
    // a stage that is switched off must not replay stale history when it is enabled again
    if (!coeffs.active)
        state->reset_stage(0);

    // PROVISIONAL topology: see run_send_stage. Each filter module is one send output.
    const ModuleOrdinal ordinal = module_ordinal(data, module_id());
    const uint32_t channels = voice->rack->channels_per_voice == 1 ? 1 : 2;
    run_send_stage(data, module_id(), 0, ordinal.index, ordinal.count, [&](const float *in, float *out, const uint32_t frames) {
        dsp::biquad_process(coeffs, state->stage[0], channels, in, out, frames);
    });

    return false;
}
} // namespace ngs
