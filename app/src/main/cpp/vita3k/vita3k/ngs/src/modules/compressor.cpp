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

#include <ngs/modules/compressor.h>
#include <util/log.h>

#include <algorithm>
#include <cmath>

namespace ngs {

namespace {

constexpr float min_level = 1.0e-7f;

float linear_to_db(const float linear) {
    return 20.0f * std::log10(std::max(linear, min_level));
}

float db_to_linear(const float db) {
    return std::pow(10.0f, db * 0.05f);
}

float time_to_coefficient(const float seconds, const int32_t sample_rate) {
    if (seconds <= 0.0f || sample_rate <= 0)
        return 0.0f;
    return std::exp(-1.0f / (seconds * static_cast<float>(sample_rate)));
}

float sanitise_time(const float value, const float lo, const float hi) {
    float seconds = std::isfinite(value) ? std::fabs(value) : lo;
    if (seconds > 10.0f)
        seconds *= 0.001f;
    return std::clamp(seconds, lo, hi);
}

} // namespace

bool CompressorModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    if (data.is_bypassed)
        return false;

    const SceNgsCompressorParams *params = data.get_parameters<SceNgsCompressorParams>(mem);
    if (!params)
        return false;

    const bool params_are_compressor = params->desc.id == SCE_NGS_COMPRESSOR_PARAMS_STRUCT_ID
        || params->desc.id == SCE_NGS_COMPRESSOR_PARAMS_STRUCT_ID_V2;
    if (!params_are_compressor)
        return false;

    if (!implement_compressor) {
        LOG_WARN_ONCE("Game is using unimplemented compressor audio module");
        return false;
    }

    Voice *voice = data.parent;
    const int32_t granularity = voice->rack->system->granularity;
    const int32_t sample_rate = voice->rack->system->sample_rate;
    if (granularity <= 0 || voice->inputs.inputs.empty())
        return false;

    float *signal = reinterpret_cast<float *>(voice->inputs.inputs[0].data());
    if (!signal)
        return false;

    const bool has_side_chain = voice->inputs.inputs.size() > 1;
    const float *key = has_side_chain
        ? reinterpret_cast<const float *>(voice->inputs.inputs[1].data())
        : signal;

    float raw_ratio = std::isfinite(params->fRatio) ? params->fRatio : 1.0f;
    if (interpret_ratio_below_one_as_reciprocal && raw_ratio > 0.0f && raw_ratio < 1.0f)
        raw_ratio = 1.0f / raw_ratio;
    const float ratio = std::clamp(raw_ratio, 1.0f, 100.0f);
    const float threshold_db = std::clamp(std::isfinite(params->fThreshold) ? params->fThreshold : 0.0f, -96.0f, 24.0f);
    const float makeup_db = std::clamp(std::isfinite(params->fMakeupGain) ? params->fMakeupGain : 0.0f, -24.0f, 24.0f);
    const float knee_db = std::clamp(std::isfinite(params->fSoftKnee) ? std::fabs(params->fSoftKnee) : 0.0f, 0.0f, 24.0f);
    const float attack_coef = time_to_coefficient(sanitise_time(params->fAttack, 0.0001f, 1.0f), sample_rate);
    const float release_coef = time_to_coefficient(sanitise_time(params->fRelease, 0.001f, 5.0f), sample_rate);
    const bool rms_mode = params->nPeakMode == SCE_NGS_COMPRESSOR_RMS_MODE;
    const bool stereo_link = params->nStereoLink == SCE_NGS_COMPRESSOR_STEREO_LINK_ON;
    const float makeup_linear = db_to_linear(makeup_db);
    const float slope = 1.0f / ratio - 1.0f;

    CompressorLogicalState *logical = data.get_logical_state<CompressorLogicalState>();
    SceNgsCompressorStates *state = data.get_state<SceNgsCompressorStates>();

    const auto reduction_db_for = [&](const float detector_linear) {
        const float over = linear_to_db(detector_linear) - threshold_db;
        if (knee_db > 0.0f && 2.0f * over > -knee_db && 2.0f * over < knee_db) {
            // soft knee: a smooth quadratic transition centred on the threshold
            const float t = over + knee_db * 0.5f;
            return slope * (t * t) / (2.0f * knee_db);
        }
        return (over > 0.0f) ? slope * over : 0.0f;
    };

    float peak_in[2] = { 0.0f, 0.0f };
    float peak_out[2] = { 0.0f, 0.0f };

    for (int32_t frame = 0; frame < granularity; frame++) {
        float *sample = &signal[frame * 2];
        const float *key_sample = &key[frame * 2];

        peak_in[0] = std::max(peak_in[0], std::fabs(sample[0]));
        peak_in[1] = std::max(peak_in[1], std::fabs(sample[1]));

        for (int channel = 0; channel < 2; channel++) {
            float detector_input;
            if (stereo_link)
                detector_input = std::max(std::fabs(key_sample[0]), std::fabs(key_sample[1]));
            else
                detector_input = std::fabs(key_sample[channel]);

            const int env_index = stereo_link ? 0 : channel;
            float &envelope = logical->envelope[env_index];

            float detector_level;
            if (rms_mode) {
                const float squared = detector_input * detector_input;
                const float coef = (squared > envelope) ? attack_coef : release_coef;
                envelope = coef * envelope + (1.0f - coef) * squared;
                detector_level = std::sqrt(std::max(envelope, 0.0f));
            } else {
                const float coef = (detector_input > envelope) ? attack_coef : release_coef;
                envelope = coef * envelope + (1.0f - coef) * detector_input;
                detector_level = envelope;
            }

            float target_gain = db_to_linear(reduction_db_for(detector_level)) * makeup_linear;

            if (compressor_never_amplifies)
                target_gain = std::min(target_gain, 1.0f);

            float &applied = logical->applied_gain[channel];
            applied = target_gain;

            sample[channel] = std::clamp(sample[channel] * applied, -1.0f, 1.0f);
            peak_out[channel] = std::max(peak_out[channel], std::fabs(sample[channel]));

            if (stereo_link) {
                // one detector drives both channels; do not run it twice per frame
                if (channel == 0) {
                    float &applied_right = logical->applied_gain[1];
                    applied_right = applied;
                    sample[1] = std::clamp(sample[1] * applied_right, -1.0f, 1.0f);
                    peak_out[1] = std::max(peak_out[1], std::fabs(sample[1]));
                }
                break;
            }
        }
    }

    for (int channel = 0; channel < SCE_NGS_MAX_SYSTEM_CHANNELS && channel < 2; channel++) {
        state->fInputLevel[channel] = peak_in[channel];
        state->fOutputLevel[channel] = peak_out[channel];
    }

    return false;
}
} // namespace ngs
