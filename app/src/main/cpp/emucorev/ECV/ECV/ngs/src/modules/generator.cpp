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

#include <ngs/modules/generator.h>
#include <util/log.h>

#include <cmath>

namespace ngs {

static_assert(static_cast<uint32_t>(SCE_NGS_GENERATOR_SINE) == static_cast<uint32_t>(dsp::GENERATOR_SINE));
static_assert(static_cast<uint32_t>(SCE_NGS_GENERATOR_TRIANGLE) == static_cast<uint32_t>(dsp::GENERATOR_TRIANGLE));
static_assert(static_cast<uint32_t>(SCE_NGS_GENERATOR_SAW) == static_cast<uint32_t>(dsp::GENERATOR_SAW));
static_assert(static_cast<uint32_t>(SCE_NGS_GENERATOR_NOISE) == static_cast<uint32_t>(dsp::GENERATOR_NOISE));
static_assert(static_cast<uint32_t>(SCE_NGS_GENERATOR_NOISE_PSP) == static_cast<uint32_t>(dsp::GENERATOR_NOISE_PSP));
static_assert(static_cast<uint32_t>(SCE_NGS_GENERATOR_PULSE) == static_cast<uint32_t>(dsp::GENERATOR_PULSE));

std::unique_ptr<ModuleLogicalState> GeneratorModule::create_logical_state() const {
    return std::make_unique<GeneratorLogicalState>();
}

void GeneratorModule::on_key_on(const MemState &mem, ModuleData &data) {
    if (auto *state = static_cast<GeneratorLogicalState *>(data.logical_state.get()))
        state->generator.started = false;
}

// Renders into this module's scratch buffer; the mixer module of the same voice
// mixes it with the player output.
bool GeneratorModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    GeneratorLogicalState *state = data.get_logical_state<GeneratorLogicalState>();
    state->produced = false;

    if (data.is_bypassed)
        return false;

    const SceNgsParamsDescriptor *desc = data.get_parameters<SceNgsParamsDescriptor>(mem);
    if (!desc || desc->id != SCE_NGS_GENERATOR_PARAMS_STRUCT_ID)
        return false;

    const SceNgsGeneratorParams *params = data.get_parameters<SceNgsGeneratorParams>(mem);
    dsp::GeneratorParams p;
    p.mode = params->eGeneratorMode;
    p.frequency_hz = params->nFrequency;
    p.amplitude = params->fAmplitude;
    p.pulse_width = params->fPulseWidth;
    p.sample_offset = params->uSampleOffset;
    p.phase_angle_deg = params->uPhaseAngle;
    if (p.mode > dsp::GENERATOR_PULSE || !std::isfinite(p.amplitude) || p.amplitude == 0.0f)
        return false;

    const uint32_t frames = static_cast<uint32_t>(data.parent->rack->system->granularity);
    if (frames == 0)
        return false;
    data.ensure_scratch_size(static_cast<size_t>(frames) * 2 * sizeof(float));
    dsp::generator_render(state->generator, p, data.parent->rack->system->sample_rate,
        reinterpret_cast<float *>(data.scratch_data.data()), frames);
    state->produced = true;

    return false;
}
} // namespace ngs
