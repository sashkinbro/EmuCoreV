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

#include <ngs/modules/reverb.h>
#include <util/log.h>

namespace ngs {

std::unique_ptr<ModuleLogicalState> ReverbModule::create_logical_state() const {
    return std::make_unique<ReverbLogicalState>();
}

void ReverbModule::on_key_on(const MemState &mem, ModuleData &data) {
    if (auto *state = static_cast<ReverbLogicalState *>(data.logical_state.get()))
        state->dsp.reset();
}

bool ReverbModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    Voice *voice = data.parent;
    if (!data.parent->inputs.inputs.empty()) {
        voice->products[0].data = voice->inputs.inputs[0].data();
    }

    ReverbLogicalState *state = data.get_logical_state<ReverbLogicalState>();
    const uint32_t frames = static_cast<uint32_t>(voice->rack->system->granularity);
    const float *main_signal = reinterpret_cast<const float *>(voice->products[0].data);

    const SceNgsReverbParams *params = nullptr;
    if (!data.is_bypassed) {
        const SceNgsParamsDescriptor *desc = data.get_parameters<SceNgsParamsDescriptor>(mem);
        if (desc && desc->id == SCE_NGS_REVERB_PARAMS_STRUCT_ID)
            params = data.get_parameters<SceNgsReverbParams>(mem);
    }
    if (!params || !main_signal || frames == 0 || voice->rack->system->sample_rate <= 0) {
        // not processing: do not replay a stale tail when the effect is enabled again
        state->dsp.reset();
        return false;
    }

    dsp::ReverbParams p;
    p.room_mb = params->fRoom;
    p.room_hf_mb = params->fRoomHF;
    p.decay_time = params->fDecayTime;
    p.decay_hf_ratio = params->fDecayHFRatio;
    p.reflections_mb = params->fReflections;
    p.reflections_delay = params->fReflectionsDelay;
    p.reverb_mb = params->fReverb;
    p.reverb_delay = params->fReverbDelay;
    p.diffusion = params->fDiffusion;
    p.density = params->fDensity;
    p.hf_reference = params->fHFReference;
    p.pattern[0] = static_cast<uint32_t>(params->eEarlyReflectionPattern[0]);
    p.pattern[1] = static_cast<uint32_t>(params->eEarlyReflectionPattern[1]);
    p.early_scalar = params->fEarlyReflectionScalar;
    p.lf_reference = params->fLFReference;
    p.room_lf_mb = params->fRoomLF;
    p.dry_mb = params->fDryMB;

    data.ensure_scratch_size(static_cast<size_t>(frames) * 2 * sizeof(float));
    float *out = reinterpret_cast<float *>(data.scratch_data.data());
    const uint32_t channels = voice->rack->channels_per_voice == 1 ? 1 : 2;
    dsp::reverb_process(state->dsp, p, static_cast<double>(voice->rack->system->sample_rate), channels, main_signal, out, frames);
    voice->products[0].data = reinterpret_cast<uint8_t *>(out);

    return false;
}
} // namespace ngs
