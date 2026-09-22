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

#include <ngs/modules/atrac9.h>
#include <util/log.h>

extern "C" {
#include <libswresample/swresample.h>
}

#include <cassert>
#include <cmath>
#include <cstring>

namespace ngs {

SwrContext *Atrac9Module::swr_mono_to_stereo = nullptr;
SwrContext *Atrac9Module::swr_stereo = nullptr;

std::unique_ptr<ModuleLogicalState> Atrac9Module::create_logical_state() const {
    return std::make_unique<Atrac9LogicalState>();
}

std::unique_ptr<ModuleRuntimeState> Atrac9Module::create_runtime_state() const {
    return std::make_unique<Atrac9RuntimeState>();
}

void Atrac9Module::on_state_change(const MemState &mem, ModuleData &data, const VoiceState previous) {
    SceNgsAT9States *state = data.get_state<SceNgsAT9States>();
    Atrac9LogicalState *logical = data.get_logical_state<Atrac9LogicalState>();

    if (data.parent->state == VOICE_STATE_ACTIVE && previous == VOICE_STATE_AVAILABLE) {
        state->samples_generated_since_key_on = 0;
        state->bytes_consumed_since_key_on = 0;
        state->current_byte_position_in_buffer = 0;
        logical->current_loop_count = 0;
        state->current_buffer = 0;
        logical->decoded_pcm.clear();
        logical->rate_resampler.reset();
        logical->superframe_staging.clear();
        logical->starved_ticks = 0;
        logical->in_underrun_wait = false;
        {
            static std::atomic<uint64_t> keyons{ 0 };
            const uint64_t n = keyons.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((n % 128) == 0)
                LOG_WARN("[NGSLIFE] AT9 key-ons so far: {}", n);
        }
        std::memset(&logical->saved_state, 0, sizeof(logical->saved_state));
    } else if (data.parent->is_keyed_off) {
        state->current_byte_position_in_buffer = 0;
        logical->current_loop_count = 0;
        state->current_buffer = 0;
        logical->rate_resampler.reset();
    }
}

void Atrac9Module::on_param_change(const MemState &mem, ModuleData &data) {
    Atrac9LogicalState *logical = data.get_logical_state<Atrac9LogicalState>();
    const SceNgsAT9Params *old_params = reinterpret_cast<SceNgsAT9Params *>(data.last_info.data());
    const SceNgsAT9Params *new_params = static_cast<SceNgsAT9Params *>(data.info.data.get(mem));

    if (old_params->playback_frequency != new_params->playback_frequency || old_params->playback_scalar != new_params->playback_scalar) {
        logical->rate_resampler.reset();
    }
}

bool Atrac9Module::decode_more_data(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, const SceNgsAT9Params *params, SceNgsAT9States *state, Atrac9LogicalState *logical, Atrac9RuntimeState *runtime, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    const SceNgsAT9BufferParams &bufparam = params->buffer_params[state->current_buffer];

    const bool config_changed = (params->config_data != logical->decoder_config);

    // re-create the decoder if necessary
    if (!runtime->decoder || config_changed) {
        runtime->decoder = std::make_unique<Atrac9DecoderState>(params->config_data);
        if (config_changed) {
            logical->decoder_config = params->config_data;
            std::memset(&logical->saved_state, 0, sizeof(logical->saved_state));
            logical->superframe_staging.clear();
        }
    }

    // re-apply the logical decoder state before we resume decoding
    runtime->decoder->load_state(&logical->saved_state);

    if (state->current_byte_position_in_buffer >= bufparam.bytes_count) {
        const int32_t prev_index = state->current_buffer;
        {
            static std::atomic<uint64_t> diag_wraps{ 0 };
            const uint64_t n = diag_wraps.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 8 || (n % 256) == 0)
                LOG_CRITICAL("[savestate-ngs] AT9 buffer wrap voice={} buf={} loop_count={} loop_now={} next={} bytes={} total_wraps={}",
                    fmt::ptr(data.parent), state->current_buffer, bufparam.loop_count, logical->current_loop_count,
                    bufparam.next_buffer_index, bufparam.bytes_count, n);
        }

        voice_lock.unlock();
        scheduler_lock.unlock();

        logical->current_loop_count++;
        state->current_byte_position_in_buffer = 0;

        if ((bufparam.loop_count != -1) && (logical->current_loop_count > bufparam.loop_count)) {
            state->current_buffer = bufparam.next_buffer_index;
            logical->current_loop_count = 0;

            if (state->current_buffer == -1) {
                data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_END_OF_DATA, 0, 0);

                // we are done
                scheduler_lock.lock();
                voice_lock.lock();
                return false;
            } else if (!params->buffer_params[state->current_buffer].buffer
                || (params->buffer_params[state->current_buffer].bytes_count == 0)) {
                // Chain continues but the next buffer is empty: prod the streamer and wait.
                data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_SWAPPED_BUFFER, prev_index,
                    params->buffer_params[state->current_buffer].buffer.address());
                scheduler_lock.lock();
                voice_lock.lock();
                logical->in_underrun_wait = true;
                return false;
            } else {
                data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_SWAPPED_BUFFER, prev_index,
                    params->buffer_params[state->current_buffer].buffer.address());
            }
        } else {
            if (!logical->superframe_staging.empty()) {
                LOG_ERROR("[AT9DIAG] voice={} loop wrap with {} staged bytes - dropping the partial superframe and resetting the decoder", fmt::ptr(data.parent), logical->superframe_staging.size());
                logical->superframe_staging.clear();
                std::memset(&logical->saved_state, 0, sizeof(logical->saved_state));
                runtime->decoder = std::make_unique<Atrac9DecoderState>(params->config_data);
            }
            data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_LOOPED_BUFFER, logical->current_loop_count,
                params->buffer_params[state->current_buffer].buffer.address());
        }

        scheduler_lock.lock();
        voice_lock.lock();

        // re-call this function
        return true;
    }

    // now we are sure we have a buffer with some data in it
    uint8_t *input = bufparam.buffer.cast<uint8_t>().get(mem) + state->current_byte_position_in_buffer;

    const uint32_t superframe_size = runtime->decoder->get(DecoderQuery::AT9_SUPERFRAME_SIZE);
    uint32_t frame_bytes_gotten = bufparam.bytes_count - state->current_byte_position_in_buffer;
    const bool diag_staged = frame_bytes_gotten < superframe_size || !logical->superframe_staging.empty();
    uint64_t diag_in_head = 0;
    for (uint32_t k = 0; k < 8 && k < frame_bytes_gotten; k++)
        diag_in_head = (diag_in_head << 8) | input[k];
    if (frame_bytes_gotten < superframe_size || !logical->superframe_staging.empty()) {
        // the superframe overlaps two buffers...
        uint32_t bytes_transferred = std::min<uint32_t>(frame_bytes_gotten, superframe_size - static_cast<uint32_t>(logical->superframe_staging.size()));
        uint32_t old_size = static_cast<uint32_t>(logical->superframe_staging.size());
        logical->superframe_staging.resize(old_size + bytes_transferred);
        std::memcpy(logical->superframe_staging.data() + old_size, input, bytes_transferred);

        if (logical->superframe_staging.size() < superframe_size) {
            // continue getting data
            state->current_byte_position_in_buffer = bufparam.bytes_count;
            return true;
        }

        // make the byte position negative, will be positive at the end
        state->current_byte_position_in_buffer = -(int32_t)old_size;
        input = logical->superframe_staging.data();
    }

    const uint32_t samples_per_frame = runtime->decoder->get(DecoderQuery::AT9_SAMPLE_PER_FRAME);
    const uint32_t samples_per_superframe = runtime->decoder->get(DecoderQuery::AT9_SAMPLE_PER_SUPERFRAME);
    // we need to account for sampled skipped at the beginning or the end of the buffer
    uint32_t decoded_size = samples_per_superframe;
    uint32_t decoded_start_offset = 0;

    // some games (like Muramasa) send the atrac9 files with the header, we need to skip it
    if (std::memcmp(input, "RIFF", 4) == 0
        && std::memcmp(input + 8, "WAVE", 4) == 0) {
        // file header is 12 bytes long
        input += 3 * sizeof(uint32_t);
        state->current_byte_position_in_buffer += 3 * sizeof(uint32_t);

        while (std::memcmp(input, "data", 4) != 0) {
            // each chunk has a 4-byte identifier followed by its size (minus 8) in an int32
            const int32_t header_data = 2 * sizeof(uint32_t) + *reinterpret_cast<int32_t *>(input + 4);
            state->current_byte_position_in_buffer += header_data;
            input += header_data;
        }

        state->current_byte_position_in_buffer += 2 * sizeof(uint32_t);
        return true;
    }

    // if the superframe is across two buffers, I don't know how to interpret the skipped samples (which are in the middle of the frame)...
    if (logical->superframe_staging.empty()) {
        // remove skipped samples at the beginning and the end of the buffer
        // in case you have more than a superframe of samples skipped (I don't know if this can happen)
        const uint32_t sample_index = (state->current_byte_position_in_buffer / superframe_size) * samples_per_superframe;
        if (bufparam.samples_discard_start_off > sample_index) {
            // first chunk
            const uint32_t skipped_samples = std::min(samples_per_superframe, static_cast<uint32_t>(bufparam.samples_discard_start_off - sample_index));
            decoded_start_offset += skipped_samples;
            decoded_size -= skipped_samples;
        }

        const uint32_t samples_left_after = (frame_bytes_gotten / superframe_size - 1) * samples_per_superframe;
        if (bufparam.samples_discard_end_off > samples_left_after) {
            // last chunk
            decoded_size -= std::min(decoded_size, static_cast<uint32_t>(bufparam.samples_discard_end_off - samples_left_after));
        }
    }

    runtime->decoded_superframe_samples.resize(static_cast<size_t>(runtime->decoder->get(DecoderQuery::AT9_SAMPLE_PER_SUPERFRAME)) * sizeof(float) * 2);
    uint32_t decoded_superframe_pos = 0;
    bool got_decode_error = false;
    const int32_t pos_after_this_superframe = state->current_byte_position_in_buffer + static_cast<int32_t>(superframe_size);
    // decode a whole superframe at a time
    for (uint32_t frame = 0; frame < runtime->decoder->get(DecoderQuery::AT9_FRAMES_IN_SUPERFRAME); frame++) {
        if (!runtime->decoder->send(input, 0)) {
            got_decode_error = true;
            break;
        }

        // convert from int16 to float
        const uint32_t channel_count = runtime->decoder->get(DecoderQuery::CHANNELS);
        runtime->temporary_bytes.resize(static_cast<size_t>(samples_per_frame) * sizeof(int16_t) * channel_count);
        DecoderSize decoder_size;
        runtime->decoder->receive(runtime->temporary_bytes.data(), &decoder_size);

        SwrContext *swr;
        if (channel_count == 1) {
            if (!swr_mono_to_stereo) {
                AVChannelLayout layout_mono = AV_CHANNEL_LAYOUT_MONO;
                AVChannelLayout layout_stereo = AV_CHANNEL_LAYOUT_STEREO;

                int ret = swr_alloc_set_opts2(&swr_mono_to_stereo,
                    &layout_stereo, AV_SAMPLE_FMT_FLT, 480000,
                    &layout_mono, AV_SAMPLE_FMT_S16, 480000,
                    0, nullptr);
                assert(ret == 0);
                ret = swr_init(swr_mono_to_stereo);
                assert(ret == 0);
            }

            swr = swr_mono_to_stereo;
        } else {
            if (!swr_stereo) {
                AVChannelLayout layout_stereo = AV_CHANNEL_LAYOUT_STEREO;
                int ret = swr_alloc_set_opts2(&swr_stereo,
                    &layout_stereo, AV_SAMPLE_FMT_FLT, 480000,
                    &layout_stereo, AV_SAMPLE_FMT_S16, 480000,
                    0, nullptr);
                assert(ret == 0);
                ret = swr_init(swr_stereo);
                assert(ret == 0);
            }

            swr = swr_stereo;
        }

        const uint8_t *swr_data_in = runtime->temporary_bytes.data();
        uint8_t *swr_data_out = runtime->decoded_superframe_samples.data() + decoded_superframe_pos;
        swr_convert(swr, &swr_data_out, decoder_size.samples, &swr_data_in, decoder_size.samples);

        decoded_superframe_pos += decoder_size.samples * sizeof(float) * 2;
        input += runtime->decoder->get_es_size();
        state->current_byte_position_in_buffer += runtime->decoder->get_es_size();
    }

    {
        float sf_peak = 0.0f;
        const float *sf = reinterpret_cast<const float *>(runtime->decoded_superframe_samples.data());
        for (uint32_t k = 0; k < decoded_superframe_pos / sizeof(float); k++)
            sf_peak = std::max(sf_peak, std::abs(sf[k]));
        logical->diag_superframes++;
        if (sf_peak <= 0.0001f)
            logical->diag_silent_streak++;
        else
            logical->diag_silent_streak = 0;
        {
            static std::atomic<uint64_t> diag_sf_total{ 0 };
            const uint64_t sf_total = diag_sf_total.fetch_add(1, std::memory_order_relaxed) + 1;
            if ((sf_total % 8192) == 0)
                LOG_CRITICAL("[savestate-ngs] AT9 superframes total={} voice_sf={} peak={:.4f} silent_streak={} staged={} err={}",
                    sf_total, logical->diag_superframes, sf_peak, logical->diag_silent_streak, diag_staged ? 1 : 0, got_decode_error ? 1 : 0);
        }
        {
            auto &r = logical->diag_ring[logical->diag_ring_next++ % 16];
            r = { logical->diag_superframes, state->current_byte_position_in_buffer, state->current_buffer,
                static_cast<uint8_t>(diag_staged ? 1 : 0), diag_in_head, sf_peak };
        }
        constexpr bool AT9_DIAG_VERBOSE = false;
        if (AT9_DIAG_VERBOSE && logical->diag_superframes <= 3)
            LOG_ERROR("[AT9DIAG] voice={} superframe #{} peak={:.4f} staged={} err={} in_head=0x{:016X} cfg=0x{:08X} ch={} buf={} pos={} discard_s={} discard_e={}",
                fmt::ptr(data.parent), logical->diag_superframes, sf_peak, diag_staged ? 1 : 0, got_decode_error ? 1 : 0,
                diag_in_head, static_cast<uint32_t>(params->config_data), static_cast<int>(params->channels),
                state->current_buffer, state->current_byte_position_in_buffer,
                bufparam.samples_discard_start_off, bufparam.samples_discard_end_off);
        if (logical->diag_silent_streak == 47 && !logical->diag_reported_silent) {
            logical->diag_reported_silent = true;
            LOG_ERROR("[AT9DIAG] voice={} WENT SILENT: 47 consecutive silent superframes at #{} staged={} err={} in_head=0x{:016X} cfg=0x{:08X} ch={} buf={} pos={} discard_s={} discard_e={}",
                fmt::ptr(data.parent), logical->diag_superframes, diag_staged ? 1 : 0, got_decode_error ? 1 : 0,
                diag_in_head, static_cast<uint32_t>(params->config_data), static_cast<int>(params->channels),
                state->current_buffer, state->current_byte_position_in_buffer,
                bufparam.samples_discard_start_off, bufparam.samples_discard_end_off);
        }
        if (logical->diag_silent_streak == 0) {
            if (logical->heal_done_this_episode)
                LOG_ERROR("[AT9DIAG] voice={} REVIVED: nonzero output after a recreate (peak={:.4f})", fmt::ptr(data.parent), sf_peak);
            logical->heal_done_this_episode = false;
        }
        if (logical->diag_silent_streak >= 12 && (logical->diag_silent_streak % 96) == 12) {
            logical->heal_done_this_episode = true;
            LOG_ERROR("[AT9DIAG] voice={} DECODER RECREATE at silent streak {} (in_head=0x{:016X})", fmt::ptr(data.parent), logical->diag_silent_streak, diag_in_head);
            std::memset(&logical->saved_state, 0, sizeof(logical->saved_state));
            logical->superframe_staging.clear();
            runtime->decoder = std::make_unique<Atrac9DecoderState>(params->config_data);
        }
        if (logical->diag_silent_streak == 8 && !logical->diag_reported_onset) {
            logical->diag_reported_onset = true;
            std::string ring;
            for (uint32_t k = 0; k < 16; k++) {
                const auto &r = logical->diag_ring[(logical->diag_ring_next + k) % 16];
                ring += fmt::format("\n  sf#{} buf={} pos={} staged={} head=0x{:016X} peak={:.4f}", r.index, r.buf, r.pos, r.staged, r.head, r.peak);
            }
            LOG_ERROR("[AT9DIAG] voice={} SILENCE ONSET - last 16 superframes (oldest first, onset is where peak dies):{}", fmt::ptr(data.parent), ring);
        }
    }

    const int32_t sample_rate = data.parent->rack->system->sample_rate;
    if (params->playback_scalar != 1 || static_cast<int>(std::round(params->playback_frequency)) != sample_rate) {
        LOG_INFO_ONCE("The currently running game requests playback rate scaling when decoding audio. Audio might crackle.");
        double src_sample_rate = params->playback_frequency;
        if (params->playback_scalar != 1.0f) {
            src_sample_rate *= params->playback_scalar;
        }

        ensure_stereo_rate_resampler(runtime->rate_resampler, logical->rate_resampler,
            static_cast<int>(src_sample_rate), sample_rate);
        // sssume skipped samples happen before playback-rate scaling
        decoded_size = process_stereo_rate_resampler(runtime->rate_resampler, logical->rate_resampler,
            runtime->decoded_superframe_samples.data() + decoded_start_offset * sizeof(float) * 2, decoded_size,
            logical->decoded_pcm);

    } else {
        logical->decoded_pcm.append_bytes(runtime->decoded_superframe_samples.data() + decoded_start_offset * sizeof(float) * 2, decoded_size);
    }

    if (got_decode_error) {
        {
            static std::atomic<uint64_t> resyncs{ 0 };
            const uint64_t n = resyncs.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n <= 8 || (n % 256) == 0)
                LOG_ERROR("[AT9DIAG] voice={} decode error - resyncing position to next superframe boundary ({} resyncs so far)", fmt::ptr(data.parent), n);
        }
        state->current_byte_position_in_buffer = pos_after_this_superframe;
        voice_lock.unlock();
        scheduler_lock.unlock();

        data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_DECODE_ERROR, state->current_byte_position_in_buffer,
            params->buffer_params[state->current_buffer].buffer.address());

        scheduler_lock.lock();
        voice_lock.lock();

        // flush or we'll get an error next time we want to decode
        runtime->decoder->flush();
        std::memset(&logical->saved_state, 0, sizeof(logical->saved_state));
    }

    logical->superframe_staging.clear();

    state->samples_generated_since_key_on += decoded_size * params->channels;
    state->samples_generated_total += decoded_size * params->channels;
    state->bytes_consumed_since_key_on += superframe_size;
    state->total_bytes_consumed += superframe_size;
    runtime->decoder->export_state(&logical->saved_state);

    return true;
}

bool Atrac9Module::process(KernelState &kern, const MemState &mem, const SceUID thread_id, ModuleData &data, std::unique_lock<std::recursive_mutex> &scheduler_lock, std::unique_lock<std::mutex> &voice_lock) {
    {
        static std::atomic<uint64_t> diag_process_calls{ 0 };
        const uint64_t n = diag_process_calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if ((n % 4096) == 0)
            LOG_CRITICAL("[savestate-ngs] AT9 process calls={} thread={}", n, thread_id);
    }
    // Restored voices are not decoded until the game re-arms them.
    if (data.needs_reinit)
        return true;

    const SceNgsAT9Params *params = data.get_parameters<SceNgsAT9Params>(mem);
    SceNgsAT9States *state = data.get_state<SceNgsAT9States>();
    Atrac9LogicalState *logical = data.get_logical_state<Atrac9LogicalState>();
    Atrac9RuntimeState *runtime = data.get_runtime_state<Atrac9RuntimeState>();
    assert(state);

    if (state->current_buffer == -1) {
        return true;
    }
    if (!params->buffer_params[state->current_buffer].buffer
        || params->buffer_params[state->current_buffer].bytes_count == 0) {
        // Keyed on before the first buffer, or a mid-stream underrun: stay alive, do not finish.
        constexpr int8_t max_starved_ticks = 16; // ~170ms at the default granularity
        if (logical->starved_ticks < max_starved_ticks) {
            logical->starved_ticks++;
            LOG_WARN_ONCE("NGS AT9: waiting for the game to publish buffer data (was previously an instant voice kill)");
            return false;
        }
        // never recovered: deliver the end the stream never reached, then finish
        voice_lock.unlock();
        scheduler_lock.unlock();
        data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_END_OF_DATA, 0, 0);
        scheduler_lock.lock();
        voice_lock.lock();
        return true;
    }
    logical->starved_ticks = 0;

    logical->decoded_pcm.compact();

    bool is_finished = false;
    // call decode more data until we either have an error or reached end of data
    while (static_cast<int32_t>(logical->decoded_pcm.available_frames()) < data.parent->rack->system->granularity) {
        if (!decode_more_data(kern, mem, thread_id, data, params, state, logical, runtime, scheduler_lock, voice_lock)) {
            is_finished = !logical->in_underrun_wait;
            logical->in_underrun_wait = false;
            break;
        }
    }

    const uint32_t granularity = static_cast<uint32_t>(data.parent->rack->system->granularity);
    const uint32_t available_samples = logical->decoded_pcm.available_frames();
    const uint32_t samples_to_be_passed = std::min<uint32_t>(available_samples, granularity);

    if (available_samples >= granularity) {
        data.parent->products[0].data = logical->decoded_pcm.read_bytes();
    } else {
        data.ensure_scratch_size(static_cast<size_t>(granularity) * sizeof(float) * 2);
        std::fill(data.scratch_data.begin(), data.scratch_data.end(), 0);
        if (available_samples > 0) {
            std::memcpy(data.scratch_data.data(), logical->decoded_pcm.read_bytes(), static_cast<size_t>(available_samples) * sizeof(float) * 2);
        }
        data.parent->products[0].data = data.scratch_data.data();
    }

    logical->decoded_pcm.consume_frames(samples_to_be_passed);

    // Assume a live unpaused stream decoding just silence for ~15s is an abandoned stream
    constexpr bool NGS_FINISH_DEAD_STREAMS = true;
    constexpr uint32_t DEAD_STREAM_SILENT_SUPERFRAMES = 700;
    if (NGS_FINISH_DEAD_STREAMS && !is_finished && logical->diag_silent_streak >= DEAD_STREAM_SILENT_SUPERFRAMES) {
        LOG_ERROR("[NGSLIFE] DEAD-STREAM FINISH voice={} after {} silent superframes (~{}s) - delivering END_OF_DATA so the game reclaims its stream entry",
            fmt::ptr(data.parent), logical->diag_silent_streak, logical->diag_silent_streak * 21 / 1000);
        voice_lock.unlock();
        scheduler_lock.unlock();
        data.invoke_callback(kern, mem, thread_id, SCE_NGS_AT9_END_OF_DATA, 0, 0);
        scheduler_lock.lock();
        voice_lock.lock();
        return true;
    }

    return is_finished;
}

void Atrac9Module::capture_logical_state(ModuleData &data, std::vector<uint8_t> &out) const {
    Atrac9LogicalState *logical = static_cast<Atrac9LogicalState *>(data.logical_state.get());
    if (!logical) {
        out.clear();
        return;
    }

    std::vector<uint8_t> buffer;
    auto append = [&buffer](const void *src, size_t size) {
        const size_t old = buffer.size();
        buffer.resize(old + size);
        std::memcpy(buffer.data() + old, src, size);
    };
    auto append_u32 = [&append](uint32_t value) { append(&value, sizeof(value)); };
    auto append_u8 = [&append](uint8_t value) { append(&value, sizeof(value)); };
    auto append_blob = [&append, &append_u32](const void *src, size_t size) {
        append_u32(static_cast<uint32_t>(size));
        if (size)
            append(src, size);
    };
    auto append_pcm_queue = [&append_u32, &append_blob](const PCMFrameQueue &queue) {
        append_u32(queue.read_offset_frames);
        append_blob(queue.samples.data(), queue.samples.size() * sizeof(float));
    };

    append_u32(logical->decoder_config);
    append(&logical->saved_state, sizeof(logical->saved_state));
    append_u8(static_cast<uint8_t>(logical->current_loop_count));
    append_u8(static_cast<uint8_t>(logical->starved_ticks));
    append_u8(logical->in_underrun_wait ? 1 : 0);
    append_blob(logical->superframe_staging.data(), logical->superframe_staging.size());
    append_pcm_queue(logical->decoded_pcm);
    append_pcm_queue(logical->rate_resampler.input_history);
    append_u8(logical->rate_resampler.needs_reset ? 1 : 0);

    out = std::move(buffer);
}

void Atrac9Module::restore_logical_state(ModuleData &data, const std::vector<uint8_t> &in) const {
    Atrac9LogicalState *logical = static_cast<Atrac9LogicalState *>(data.logical_state.get());
    if (!logical || in.empty())
        return;

    size_t offset = 0;
    auto read_bytes = [&in, &offset](void *dst, size_t size) {
        if (offset + size > in.size())
            return false;
        std::memcpy(dst, in.data() + offset, size);
        offset += size;
        return true;
    };
    auto read_u32 = [&read_bytes](uint32_t &value) { return read_bytes(&value, sizeof(value)); };
    auto read_u8 = [&read_bytes](uint8_t &value) { return read_bytes(&value, sizeof(value)); };
    auto read_blob = [&in, &offset](std::vector<uint8_t> &dst, size_t element_size) {
        uint32_t size = 0;
        if (offset + sizeof(size) > in.size())
            return false;
        std::memcpy(&size, in.data() + offset, sizeof(size));
        offset += sizeof(size);
        if (offset + size > in.size() || (element_size && (size % element_size) != 0))
            return false;
        dst.resize(size);
        if (size)
            std::memcpy(dst.data(), in.data() + offset, size);
        offset += size;
        return true;
    };
    auto read_pcm_queue = [&read_u32, &read_blob](PCMFrameQueue &queue) {
        std::vector<uint8_t> samples;
        if (!read_u32(queue.read_offset_frames) || !read_blob(samples, sizeof(float)))
            return false;
        queue.samples.resize(samples.size() / sizeof(float));
        if (!samples.empty())
            std::memcpy(queue.samples.data(), samples.data(), samples.size());
        return true;
    };

    uint32_t decoder_config = 0;
    uint8_t loop_count = 0;
    uint8_t starved_ticks = 0;
    uint8_t in_underrun = 0;
    uint8_t needs_reset = 0;
    if (!read_u32(decoder_config) ||
        !read_bytes(&logical->saved_state, sizeof(logical->saved_state)) ||
        !read_u8(loop_count) ||
        !read_u8(starved_ticks) ||
        !read_u8(in_underrun) ||
        !read_blob(logical->superframe_staging, 1) ||
        !read_pcm_queue(logical->decoded_pcm) ||
        !read_pcm_queue(logical->rate_resampler.input_history) ||
        !read_u8(needs_reset)) {
        return;
    }

    logical->decoder_config = decoder_config;
    logical->current_loop_count = static_cast<int8_t>(loop_count);
    logical->starved_ticks = static_cast<int8_t>(starved_ticks);
    logical->in_underrun_wait = in_underrun != 0;
    logical->rate_resampler.needs_reset = needs_reset != 0;
    // The runtime decoder is rebuilt on demand from decoder_config + saved_state.
    if (auto *runtime = static_cast<Atrac9RuntimeState *>(data.runtime_state.get())) {
        runtime->decoder.reset();
    }
}

void Atrac9Module::free_swr_contexts() {
    if (swr_mono_to_stereo) {
        swr_free(&swr_mono_to_stereo);
        swr_mono_to_stereo = nullptr;
    }
    if (swr_stereo) {
        swr_free(&swr_stereo);
        swr_stereo = nullptr;
    }
}

void Atrac9Module::cleanup_voice_state(ModuleData &data) {
    if (auto *runtime = static_cast<Atrac9RuntimeState *>(data.runtime_state.get())) {
        destroy_stereo_rate_resampler(runtime->rate_resampler);
    }
    data.runtime_state.reset();
}

} // namespace ngs
