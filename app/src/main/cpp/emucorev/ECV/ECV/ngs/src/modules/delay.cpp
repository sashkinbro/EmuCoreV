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

#include <ngs/modules/delay.h>
#include <util/log.h>

namespace ngs {

std::unique_ptr<ModuleLogicalState> DelayModule::create_logical_state() const {
    return std::make_unique<DelayLogicalState>();
}

void DelayModule::on_key_on(const MemState &mem, ModuleData &data) {
    if (auto *state = static_cast<DelayLogicalState *>(data.logical_state.get()))
        state->dsp.reset();
}

bool DelayModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    Voice *voice = data.parent;
    if (!voice->inputs.inputs.empty()) {
        voice->products[0].data = voice->inputs.inputs[0].data();
    }

    DelayLogicalState *state = data.get_logical_state<DelayLogicalState>();
    const uint32_t frames = static_cast<uint32_t>(voice->rack->system->granularity);
    const float *main_signal = reinterpret_cast<const float *>(voice->products[0].data);

    const SceNgsDelayParams *params = nullptr;
    if (!data.is_bypassed) {
        const SceNgsParamsDescriptor *desc = data.get_parameters<SceNgsParamsDescriptor>(mem);
        if (desc && desc->id == SCE_NGS_DELAY_PARAMS_STRUCT_ID)
            params = data.get_parameters<SceNgsDelayParams>(mem);
    }
    if (!params || !main_signal || frames == 0 || voice->rack->system->sample_rate <= 0) {
        // not processing: do not replay stale delay lines when the effect is enabled again
        state->dsp.reset();
        return false;
    }

    dsp::DelayParams p;
    p.dry = params->fDryVol;
    p.mod_rate = params->fModRate;
    for (uint32_t t = 0; t < dsp::delay_max_taps; t++) {
        const SceNgsDelayTap &src = params->taps[t];
        dsp::DelayTapParams &dst = p.taps[t];
        dst.delay_ms = src.fDelayMillisecs;
        dst.volume = src.fVolume;
        dst.feedback = src.fFeedback;
        dst.filter_mode = static_cast<uint32_t>(src.eFilterMode);
        dst.cutoff = src.fCutoff;
        dst.phase_deg = src.fPhaseOffsetDeg;
        dst.mod_width_ms = src.fModWidthMillisecs;
    }

    data.ensure_scratch_size(static_cast<size_t>(frames) * 2 * sizeof(float));
    float *out = reinterpret_cast<float *>(data.scratch_data.data());
    const uint32_t channels = voice->rack->channels_per_voice == 1 ? 1 : 2;
    dsp::delay_process(state->dsp, p, static_cast<double>(voice->rack->system->sample_rate), channels, main_signal, out, frames);
    voice->products[0].data = reinterpret_cast<uint8_t *>(out);

    return false;
}
} // namespace ngs
