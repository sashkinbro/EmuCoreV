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

#include <ngs/dsp/mix.h>
#include <ngs/modules/generator.h>
#include <ngs/modules/mixer.h>
#include <util/log.h>

namespace ngs {

uint32_t InputMixerModule::input_index_of(const ModuleData &data) {
    uint32_t input_index = 0;
    const auto &modules = data.parent->rack->modules;
    for (uint32_t i = 0; i < data.index && i < modules.size(); i++) {
        if (modules[i] && modules[i]->module_id() == input_mixer_module_id)
            input_index++;
    }
    return input_index;
}

bool InputMixerModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    const uint32_t input_index = input_index_of(data);
    if (input_index >= data.parent->inputs.inputs.size())
        return false;

    if (input_index == 0)
        data.parent->products[0].data = data.parent->inputs.inputs[0].data();

    return false;
}

// The mixer module of the player / ATRAC9 voices has two input ports: port 0 is
// the sample player output (products[0]) and port 1 the generator module of the
// same voice; SceNgsMixerParams::fGainIn[port] scales each before they are summed.
// (Patches into a buss voice are already summed, with their own 2x2 volume
// matrices, by VoiceInputManager::receive.) PROVISIONAL: the port assignment and
// the final clamp to [-1, 1] follow the pipeline float conventions, they are
// not recovered from firmware. uPauseLeadIn / uPauseLeadOut are not used.
bool MixerModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    if (data.is_bypassed)
        return false;

    const SceNgsMixerParams *params = data.get_parameters<SceNgsMixerParams>(mem);
    // Without a valid parameter block the voice stays a plain pass-through.
    if (!params || params->desc.id != SCE_NGS_MIXER_PARAMS_STRUCT_ID)
        return false;

    Voice *voice = data.parent;
    const float gain_player = dsp::sanitise_gain(params->fGainIn[0]);
    const float gain_generator = dsp::sanitise_gain(params->fGainIn[1]);

    const float *player = reinterpret_cast<const float *>(voice->products[0].data);

    const float *generator = nullptr;
    for (size_t i = 0; i < voice->rack->modules.size() && i < voice->datas.size(); i++) {
        if (voice->rack->modules[i] && voice->rack->modules[i]->module_id() == 0x5CE8) {
            const auto *gen_state = static_cast<const GeneratorLogicalState *>(voice->datas[i].logical_state.get());
            if (gen_state && gen_state->produced)
                generator = reinterpret_cast<const float *>(voice->datas[i].scratch_data.data());
            break;
        }
    }

    if (!player && !generator)
        return false;
    // nothing to add and unity gain: leave the player buffer untouched
    if (!generator && gain_player == 1.0f)
        return false;

    const uint32_t frames = static_cast<uint32_t>(voice->rack->system->granularity);
    data.ensure_scratch_size(static_cast<size_t>(frames) * 2 * sizeof(float));
    float *out = reinterpret_cast<float *>(data.scratch_data.data());
    dsp::mix_two_ports(player, gain_player, generator, gain_generator, out, frames);
    voice->products[0].data = reinterpret_cast<uint8_t *>(out);

    return false;
}
} // namespace ngs
