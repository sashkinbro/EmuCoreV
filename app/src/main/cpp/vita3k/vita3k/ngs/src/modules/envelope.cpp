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

#include <ngs/modules/envelope.h>
#include <util/log.h>

#include <algorithm>
#include <cmath>

namespace ngs {

// Multi-point amplitude automation: games drive music crossfades and dialogue ducking through it.

static float point_amplitude(const SceNgsEnvelopeParams *params, const int32_t index) {
    const int32_t count = static_cast<int32_t>(std::min<uint32_t>(params->uNumPoints, SCE_NGS_ENVELOPE_MAX_POINTS));
    if (count == 0)
        return 1.0f;
    const int32_t clamped = std::clamp(index, 0, count - 1);
    const float amp = params->envelopePoints[clamped].fAmplitude;
    return std::isfinite(amp) ? std::clamp(amp, 0.0f, 16.0f) : 1.0f;
}

void EnvelopeModule::on_param_change(const MemState &mem, ModuleData &data) {
    // a rewritten point set (re)starts the envelope - this is how fades are triggered
    EnvelopeLogicalState *logical = data.get_logical_state<EnvelopeLogicalState>();
    logical->position_in_segment_ms = 0.0;
    logical->total_position_ms = 0.0;
    logical->current_point = 0;
    logical->releasing = false;
    logical->completed_at_zero_ms = 0.0;
}

void EnvelopeModule::on_key_on(const MemState &mem, ModuleData &data) {
    // KZ re-keys pooled gun voices via VoicePlay with NO param rewrite: hardware restarts the envelope
    // on key-on, ours resumed at completed-zero and every later shot was multiplied to silence.
    on_param_change(mem, data);
}

bool EnvelopeModule::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    if (data.is_bypassed)
        return false;

    const SceNgsEnvelopeParams *params = data.get_parameters<SceNgsEnvelopeParams>(mem);
    if (!params)
        return false;

    if (params->desc.id != SCE_NGS_ENVELOPE_PARAMS_STRUCT_ID) {
        if (params->desc.id != 0)
            LOG_WARN_ONCE("[NGSDIAG] envelope module driven with unrecognised params struct id {:#010x} (passthrough)", params->desc.id);
        return false;
    }

    const int32_t point_count = static_cast<int32_t>(std::min<uint32_t>(params->uNumPoints, SCE_NGS_ENVELOPE_MAX_POINTS));
    if (point_count == 0)
        return false;

    Voice *voice = data.parent;
    const int32_t granularity = voice->rack->system->granularity;
    const int32_t sample_rate = voice->rack->system->sample_rate;
    if (granularity <= 0 || sample_rate <= 0)
        return false;

    // the buffer this voice is carrying: a source rack's product, or a bus rack's patched input
    float *signal = nullptr;
    if (voice->products[0].data)
        signal = reinterpret_cast<float *>(voice->products[0].data);
    else if (!voice->inputs.inputs.empty())
        signal = reinterpret_cast<float *>(voice->inputs.inputs[0].data());

    EnvelopeLogicalState *logical = data.get_logical_state<EnvelopeLogicalState>();
    SceNgsEnvelopeStates *state = data.get_state<SceNgsEnvelopeStates>();

    const double tick_ms = static_cast<double>(granularity) * 1000.0 / static_cast<double>(sample_rate);

    const auto height_at_current = [&]() -> float {
        if (logical->current_point >= point_count - 1)
            return point_amplitude(params, point_count - 1);
        const float from = point_amplitude(params, logical->current_point);
        const float to = point_amplitude(params, logical->current_point + 1);
        const double seg_ms = std::max<double>(params->envelopePoints[logical->current_point].uMsecsToNextPoint, 0.0);
        if (seg_ms <= 0.0)
            return to;
        double t = std::clamp(logical->position_in_segment_ms / seg_ms, 0.0, 1.0);
        if (params->envelopePoints[logical->current_point].eCurveType == SCE_NGS_ENVELOPE_CURVED)
            t = t * t * (3.0 - 2.0 * t); // smoothstep for the curved segments
        return static_cast<float>(from + (to - from) * t);
    };

    const bool keyed_off = voice->is_keyed_off || voice->state == VOICE_STATE_FINALIZING;
    const float start_gain = logical->releasing || keyed_off
        ? logical->release_height
        : height_at_current();

    // advance one tick
    if (!logical->releasing && keyed_off) {
        logical->releasing = true;
        logical->release_height = start_gain;
        logical->release_start_height = start_gain;
        logical->release_position_ms = 0.0;
    }

    float end_gain;
    if (logical->releasing) {
        logical->release_position_ms += tick_ms;
        const double rel_ms = std::max<double>(params->uReleaseMsecs, 0.0);
        const double t = rel_ms <= 0.0 ? 1.0 : std::clamp(logical->release_position_ms / rel_ms, 0.0, 1.0);
        end_gain = static_cast<float>(logical->release_start_height * (1.0 - t));
        logical->release_height = end_gain;
    } else {
        logical->position_in_segment_ms += tick_ms;
        logical->total_position_ms += tick_ms;
        // consume completed segments (0-length segments advance instantly)
        while (logical->current_point < point_count - 1) {
            const double seg_ms = std::max<double>(params->envelopePoints[logical->current_point].uMsecsToNextPoint, 0.0);
            if (logical->position_in_segment_ms < seg_ms)
                break;
            logical->position_in_segment_ms -= seg_ms;
            logical->current_point++;
            // loop while held: after passing nLoopEnd, wrap back to uLoopStart
            const int32_t loop_end = params->nLoopEnd;
            const int32_t loop_start = static_cast<int32_t>(params->uLoopStart);
            if (loop_end >= 0 && loop_end < point_count && loop_start <= loop_end
                && logical->current_point > loop_end) {
                logical->current_point = loop_start;
            }
        }
        end_gain = height_at_current();
    }

    // guest-visible states (games poll these to sequence fades)
    if (state) {
        state->fCurrentHeight = end_gain;
        state->fPosition = static_cast<float>(logical->releasing ? logical->release_position_ms : logical->total_position_ms);
        state->fReleaseScale = logical->releasing ? logical->release_start_height : 1.0f;
        state->nCurrentPoint = logical->current_point;
        state->nReleasing = logical->releasing ? 1 : 0;
    }

    if (signal) {
        // per-sample ramp across the tick to avoid zipper noise
        const float step = (end_gain - start_gain) / static_cast<float>(granularity);
        float gain = start_gain;
        for (int32_t frame = 0; frame < granularity; frame++) {
            signal[frame * 2 + 0] *= gain;
            signal[frame * 2 + 1] *= gain;
            gain += step;
        }
    }

    // Envelope-complete finish: on hardware a fade that ends at zero ENDS THE VOICE (finish callback ->
    // the game reclaims it; AC3L clears its stream-table entry in that callback). We only applied the
    // gain, so faded-out voices lived forever multiplying by zero - the entry leak behind the missing
    // dialogue. Guards: only past the LAST point, final amplitude zero, and an earlier point was audible
    // (a real fade-down) - pre-muted constant-zero envelopes, fade-ins and looping envelopes never match.
    constexpr bool NGS_ENVELOPE_COMPLETE_FINISHES = true;
    if (NGS_ENVELOPE_COMPLETE_FINISHES) {
        if (logical->releasing) {
            const double rel_ms = std::max<double>(params->uReleaseMsecs, 0.0);
            if (logical->release_position_ms >= rel_ms) {
                LOG_WARN("[NGSLIFE] ENVELOPE-RELEASE FINISH voice={} (release {}ms complete)", fmt::ptr(voice), rel_ms);
                return true;
            }
        } else if (logical->current_point >= point_count - 1 && end_gain <= 0.0001f
            && point_amplitude(params, point_count - 1) <= 0.0001f) {
            bool had_audible_point = false;
            for (int32_t i = 0; i < point_count - 1; i++)
                had_audible_point |= point_amplitude(params, i) > 0.0001f;
            const bool loop_active = params->nLoopEnd >= 0 && params->nLoopEnd < point_count;
            if (had_audible_point && !loop_active) {
                // Grace period before finishing: KZ re-triggers gun envelopes (param rewrite resets this
                // accumulator) every ~150ms, so an instantly-finishing envelope killed active weapon voices.
                // Only a fade left completed-at-zero and un-retriggered this long is truly abandoned (AC3L
                // stop-fades sit here forever; their stream-table entries clear well inside the reload window).
                constexpr double ENVELOPE_COMPLETE_FINISH_GRACE_MS = 5000.0;
                logical->completed_at_zero_ms += tick_ms;
                if (logical->completed_at_zero_ms >= ENVELOPE_COMPLETE_FINISH_GRACE_MS) {
                    LOG_ERROR("[NGSLIFE] ENVELOPE-COMPLETE FINISH voice={} ({} points, final=0, at zero {:.0f}ms) - abandoned fade, finishing voice",
                        fmt::ptr(voice), point_count, logical->completed_at_zero_ms);
                    return true;
                }
            }
            if (had_audible_point && loop_active)
                LOG_WARN_ONCE("[NGSLIFE] envelope at zero after audible fade but LOOPING (loop {}..{}) - finish suppressed, voice sustained",
                    static_cast<int32_t>(params->uLoopStart), params->nLoopEnd);
        }
    }

    return false;
}
} // namespace ngs
