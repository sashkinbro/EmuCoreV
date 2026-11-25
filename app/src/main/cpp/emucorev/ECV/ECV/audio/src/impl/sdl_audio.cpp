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

#include "audio/impl/sdl_audio.h"
#include "SDL_audio_state.h"
#include "util/log.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#ifdef ECV_ORACLE
#include <ecv_lab.h> // oracle: lab audio metrics (debug builds only)
#endif

#define SDL_CHECK_EXT(condition, ret)                         \
    do {                                                      \
        if (!(condition)) {                                   \
            LOG_ERROR("SDL audio error: {}", SDL_GetError()); \
            return ret;                                       \
        }                                                     \
    } while (0)

#define SDL_CHECK(f_call) SDL_CHECK_EXT(f_call, {})
#define SDL_CHECK_VOID(f_call) SDL_CHECK_EXT(f_call, )
#define SDL_CHECK_NEG(f_call) SDL_CHECK_EXT((f_call) >= 0, {})

static int get_threshold_samples(const int device_buffer_samples) {
    return 4 * device_buffer_samples;
}

void SDLCALL SDLAudioAdapter::thread_wakeup_callback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
    assert(userdata != nullptr);
    assert(stream != nullptr);
    SDLAudioOutPort *port = static_cast<SDLAudioOutPort *>(userdata);
    const int samples_available = port->adapter.get_rest_sample(*port);
#ifdef ECV_ORACLE
    ecv::oracle::lab::audio_pull(port, samples_available, port->adapter.device_buffer_samples, port->adapter.dst_spec.freq);
#endif
    if (samples_available < get_threshold_samples(port->adapter.device_buffer_samples) || additional_amount > 0) {
        port->cond_var.notify_one();
        // This runs on the real-time device thread with the stream lock held: never wait for a guest thread's state
        // lock here (a missed wake is retried by the next device pull).
        bool nonblocking = true;
#ifdef ECV_ORACLE
        nonblocking = !ecv::oracle::lab::audio_ab_off(1); // A/B switch: audio-blocking-wake
#endif
        port->notify_output_ready(nonblocking);
    }
}

SDLAudioAdapter::SDLAudioAdapter(AudioState &audio_state)
    : AudioAdapter(audio_state) {}

SDLAudioAdapter::~SDLAudioAdapter() {
    if (device_id > 0) {
        SDL_CloseAudioDevice(device_id);
    }
}

bool SDLAudioAdapter::init() {
    // SDL3 default is 1024 sample frames for 48kHz audio, which is higher than cubeb.
    // Request smaller device buffer for lower latency callbacks.
    // 512 sample frames = 2048 bytes for stereo 16-bit, matching cubeb's callback size.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
    device_id = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    SDL_CHECK_EXT(device_id > 0, false);
    return true;
}

void SDLAudioAdapter::switch_state(const bool pause) {
    if (pause)
        SDL_CHECK_VOID(SDL_PauseAudioDevice(device_id));
    else
        SDL_CHECK_VOID(SDL_ResumeAudioDevice(device_id));
}

AudioOutPortPtr SDLAudioAdapter::open_port(int nb_channels, int freq, int nb_sample) {
    return open_port_internal(nb_channels, freq, nb_sample, true);
}

AudioOutPortPtr SDLAudioAdapter::open_port_for_restore(int nb_channels, int freq, int nb_sample) {
    return open_port_internal(nb_channels, freq, nb_sample, false);
}

AudioOutPortPtr SDLAudioAdapter::open_port_internal(int nb_channels, int freq, int nb_sample, bool resume_device) {
    SDL_AudioSpec src_spec = {
        .format = SDL_AUDIO_S16LE,
        .channels = nb_channels,
        .freq = freq
    };
    SDL_CHECK(SDL_GetAudioDeviceFormat(device_id, &dst_spec, &device_buffer_samples));
    const AudioStreamPtr stream(SDL_CreateAudioStream(&src_spec, &dst_spec), SDL_DestroyAudioStream);
    SDL_CHECK(stream);
    SDL_CHECK(SDL_BindAudioStream(device_id, stream.get()));
    auto port = std::make_shared<SDLAudioOutPort>(stream, *this);
    SDL_CHECK(SDL_SetAudioStreamGetCallback(stream.get(), SDLAudioAdapter::thread_wakeup_callback, port.get()));
    port->channels = nb_channels;
    port->len_microseconds = (nb_sample * 1'000'000ULL) / freq;
    port->len_bytes = nb_sample * nb_channels * sizeof(int16_t);
    if (resume_device)
        switch_state(false);
    return port;
}

AudioSubmitResult SDLAudioAdapter::try_audio_output(AudioOutPort &out_port, const void *buffer, bool allow_overflow) {
    if (out_port.stopping)
        return AudioSubmitResult::stopped;
    auto &port = static_cast<SDLAudioOutPort &>(out_port);
    const int samples_available = get_rest_sample(port);
    if (samples_available < 0)
        return AudioSubmitResult::error;
    const int threshold = get_threshold_samples(device_buffer_samples);
    if (!allow_overflow && samples_available > threshold)
        return AudioSubmitResult::would_block;
    // An overflow submit (the guest's wait expired, e.g. after the device stalled or the app was in
    // the background) adds data on top of a full queue. Without a bound that surplus is never
    // drained and the sound lags further behind the picture every time it happens. Drop the stale
    // backlog instead: one short skip, then audio is in sync again.
    if (samples_available > 4 * threshold) {
        LOG_WARN_ONCE("Audio output backlog of {} samples (limit {}): dropping it to keep audio in sync", samples_available, 4 * threshold);
#ifdef ECV_ORACLE
        ecv::oracle::lab::audio_dropped(samples_available);
#endif
        SDL_ClearAudioStream(port.stream.get());
    }
    const bool put = SDL_PutAudioStreamData(port.stream.get(), buffer, port.len_bytes);
#ifdef ECV_ORACLE
    if (put && ecv::oracle::lab::audio_armed())
        ecv::oracle::lab::audio_submitted(&port, port.len_microseconds, get_rest_sample(port), dst_spec.freq);
#endif
    return put ? AudioSubmitResult::submitted : AudioSubmitResult::error;
}

void SDLAudioAdapter::set_volume(AudioOutPort &out_port, float volume) {
    SDL_CHECK_VOID(SDL_SetAudioStreamGain(static_cast<SDLAudioOutPort &>(out_port).stream.get(), volume));
}

int SDLAudioAdapter::get_rest_sample(AudioOutPort &out_port) {
    auto &port = static_cast<SDLAudioOutPort &>(out_port);
    const int bytes_available = SDL_GetAudioStreamAvailable(port.stream.get());
    SDL_CHECK_NEG(bytes_available);
    // we have the number of bytes left, we can convert it back to the number of samples left
    return bytes_available / SDL_AUDIO_FRAMESIZE(dst_spec);
}

void SDLAudioAdapter::wake_all_ports() {
    for (auto &[_, port_ptr] : state.out_ports) {
        auto &port = static_cast<SDLAudioOutPort &>(*port_ptr);
        {
            std::lock_guard<std::mutex> lock(port.mutex);
        }
        port.cond_var.notify_all();
        port.notify_output_ready();
    }
}

bool SDLAudioAdapter::save_port_state(AudioOutPort &out_port, std::vector<uint8_t> &state) {
    auto &port = static_cast<SDLAudioOutPort &>(out_port);
    auto *stream = port.stream.get();
    if (!SDL_LockAudioStream(stream))
        return false;
    struct UnlockStream {
        SDL_AudioStream *stream;
        ~UnlockStream() { SDL_UnlockAudioStream(stream); }
    } unlock{stream};
    // Keep the size query and serialization on the same playback state. SDL's
    // stream lock is recursive, so the snapshot APIs can take it internally.
    const size_t size = SDL_GetAudioStreamStateSize(stream);
    if (size == 0 || size > 16u * 1024u * 1024u + 256u)
        return false;
    state.resize(size);
    if (!SDL_SaveAudioStreamState(stream, state.data(), state.size())) {
        state.clear();
        return false;
    }
    return true;
}

bool SDLAudioAdapter::restore_port_state(AudioOutPort &out_port, const std::vector<uint8_t> &state) {
    auto &port = static_cast<SDLAudioOutPort &>(out_port);
    return !state.empty() && SDL_LoadAudioStreamState(port.stream.get(), state.data(), state.size());
}
