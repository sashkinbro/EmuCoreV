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

#include <ngs/modules/distortion.h>
#include <util/log.h>

namespace ngs {

bool DistortionModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    if (data.is_bypassed)
        return false;

    Voice *voice = data.parent;
    const SceNgsParamsDescriptor *desc = data.get_parameters<SceNgsParamsDescriptor>(mem);
    if (!desc || desc->id != SCE_NGS_DISTORTION_PARAMS_STRUCT_ID)
        return false;
    const SceNgsDistortionParams *params = data.get_parameters<SceNgsDistortionParams>(mem);

    const uint32_t frames = static_cast<uint32_t>(voice->rack->system->granularity);
    const float *main_signal = reinterpret_cast<const float *>(voice->products[0].data);
    if (!main_signal || frames == 0)
        return false;

    dsp::DistortionParams p;
    p.a = params->fA;
    p.b = params->fB;
    p.clip = params->fClip;
    p.gate = params->fGate;
    p.wet_gain = params->fWetGain;
    p.dry_gain = params->fDryGain;

    data.ensure_scratch_size(static_cast<size_t>(frames) * 2 * sizeof(float));
    float *out = reinterpret_cast<float *>(data.scratch_data.data());
    const uint32_t channels = voice->rack->channels_per_voice == 1 ? 1 : 2;
    dsp::distortion_process(p, channels, main_signal, out, frames);
    voice->products[0].data = reinterpret_cast<uint8_t *>(out);

    return false;
}
} // namespace ngs
