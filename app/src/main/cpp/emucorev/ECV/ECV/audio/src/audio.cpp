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

#include <audio/state.h>

#include <audio/impl/cubeb_audio.h>
#include <audio/impl/sdl_audio.h>
#include "SDL_audio_state.h"

#include <util/log.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

bool validate_audio_port_snapshot(uint32_t codec, int len_bytes, const std::vector<uint8_t> &snapshot) {
    if (snapshot.empty())
        return false;
    if (codec == 1)
        return SDL_ValidateAudioStreamState(snapshot.data(), snapshot.size());
    if (codec == 2)
        return validate_cubeb_audio_snapshot(len_bytes, snapshot);
    return false;
}

bool AudioState::init(const std::string &adapter_name) {
    set_backend(adapter_name);
    if (!adapter)
        return false;

    return true;
}

void AudioState::stop_all_ports() {
    {
        const std::lock_guard<std::mutex> lock(mutex);
        for (auto &[_, port] : out_ports) {
            port->stopping = true;
            port->notify_output_ready();
        }
    }
    if (adapter)
        adapter->wake_all_ports();
}

void AudioState::deinit() {
    stop_all_ports();

    const std::lock_guard<std::mutex> lock(mutex);

    out_ports.clear();

    audio_in_close_all(in_port);

    next_port_id = 1;
}

bool audio_in_has_open_ports(AudioInPort &in) {
    const std::lock_guard<std::mutex> lock(in.capture_mutex);
    return in.ports.any();
}

void audio_in_close_all(AudioInPort &in) {
    void *feeds[ecv_audio_in::kPortSlots] = {};
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        for (int i = 0; i < ecv_audio_in::kPortSlots; ++i) {
            feeds[i] = in.slot[i].feed;
            in.slot[i] = AudioInSlot{};
            in.ports.open[i] = false;
        }
    }
    for (void *feed : feeds)
        if (feed)
            SDL_DestroyAudioStream(static_cast<SDL_AudioStream *>(feed));
    audio_in_release_capture(in);
}

void audio_in_release_capture(AudioInPort &in) {
    void *stream = nullptr;
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        stream = in.id;
        in.id = nullptr;
    }
    if (stream) {
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream *>(stream));
        LOG_INFO("AudioIn: capture released");
    }
}

void audio_in_set_suspended(AudioInPort &in, bool suspended) {
    if (ecv_audio_in::settings().suspended.exchange(suspended) == suspended)
        return;
    if (suspended) {
        audio_in_release_capture(in);
        // The open port restarts capture on the guest thread (sceAudioInInput) after the resume.
        if (audio_in_has_open_ports(in))
            in.reopen_requested = true;
        LOG_INFO("AudioIn paused");
    } else {
        LOG_INFO("AudioIn resumed");
    }
}

void audio_in_settings_changed(AudioInPort &in) {
    // Disabled: stay silent. Enabled again or another input device: restart capture. Either way the
    // stream is released here and (re)opened by the guest thread in sceAudioInInput when wanted.
    audio_in_release_capture(in);
    if (audio_in_has_open_ports(in))
        in.reopen_requested = true;
}

void AudioState::set_backend(const std::string &adapter_name) {
    if (adapter_name == this->audio_backend)
        return;

    // first delete all ports then delete the backend
    out_ports.clear();
    adapter.reset();
    if (adapter_name == "SDL") {
        adapter = std::make_unique<SDLAudioAdapter>(*this);
    } else if (adapter_name == "Cubeb") {
        adapter = std::make_unique<CubebAudioAdapter>(*this);
    } else {
        LOG_ERROR("Unknown audio adapter {}", adapter_name);
        return;
    }
    this->audio_backend = adapter_name;

    // lock the mutex to make sure nothing happens until the initialisation is done
    const std::lock_guard<std::mutex> lock(mutex);
    if (!adapter->init()) {
        adapter.reset();
    }
}

AudioOutPortPtr AudioState::open_port(int nb_channels, int freq, int nb_sample) {
    // Guard against a dead backend or a failed port open instead of crashing.
    if (!adapter)
        return nullptr;

    AudioOutPortPtr port = adapter->open_port(nb_channels, freq, nb_sample);
    if (!port)
        return nullptr;

    set_volume(*port, port->volume);
    return port;
}

AudioOutPortPtr AudioState::open_port_for_restore(int nb_channels, int freq, int nb_sample) {
    if (!adapter)
        return nullptr;
    AudioOutPortPtr port = adapter->open_port_for_restore(nb_channels, freq, nb_sample);
    if (!port)
        return nullptr;
    set_volume(*port, port->volume);
    return port;
}

void AudioState::set_volume(AudioOutPort &out_port, float volume) {
    out_port.volume = volume;
    adapter->set_volume(out_port, volume * global_volume);
}

void AudioState::set_global_volume(float volume) {
    // Out-of-range or NaN input (corrupt config) must never reach the backend: clamp to [0, 1].
    volume = std::isnan(volume) ? 1.0f : std::clamp(volume, 0.0f, 1.0f);
    //  Update adapter volume for each port.
    const std::lock_guard lock(mutex);
    global_volume = volume;
    if (!adapter)
        return;
    for (const auto &[_, port] : out_ports) {
        adapter->set_volume(*port, port->volume * volume);
    }
}

void AudioState::switch_state(const bool pause) {
    adapter->switch_state(pause);
}

int AudioState::get_rest_sample(AudioOutPort &out_port) {
    return adapter->get_rest_sample(out_port);
}

void AudioState::wake_all_ports() {
    const std::lock_guard<std::mutex> lock(mutex);
    for (auto &[_, port] : out_ports) {
        port->stopping = true;
        port->notify_output_ready();
    }
    if (adapter)
        adapter->wake_all_ports();
}
