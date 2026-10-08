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

#pragma once

#include "../state.h"

#include <condition_variable>

#include <cubeb/cubeb.h>

struct AudioBuffer {
    std::vector<uint8_t> buffer;
    int buffer_position;
};

struct CubebAudioOutPort : AudioOutPort {
    cubeb_stream *out_stream = nullptr;
    cubeb_stream_params spec;
    // sync variables used to wait for the buffer to be ready
    std::condition_variable cond_var;
    // buffer filled with audio data to pass to cubeb
    std::vector<AudioBuffer> audio_buffers;
    // position of the next audio buffer to put audio
    int next_audio_buffer = 0;
    int nb_buffers_ready = 0;
    bool stream_started = false;

    // use the destructor to destroy the cubeb stream
    ~CubebAudioOutPort();
};

class CubebAudioAdapter : public AudioAdapter {
    cubeb *cubeb_ctx = nullptr;

public:
    CubebAudioAdapter(AudioState &audio_state);
    ~CubebAudioAdapter() override;

    bool init() override;
    AudioOutPortPtr open_port(int nb_channels, int freq, int nb_sample) override;
    AudioOutPortPtr open_port_for_restore(int nb_channels, int freq, int nb_sample) override;
    AudioSubmitResult try_audio_output(AudioOutPort &out_port, const void *buffer, bool allow_overflow) override;
    void set_volume(AudioOutPort &out_port, float volume) override;
    void switch_state(const bool pause) override;
    void wake_all_ports() override;
    bool save_port_state(AudioOutPort &out_port, std::vector<uint8_t> &state) override;
    bool restore_port_state(AudioOutPort &out_port, const std::vector<uint8_t> &state) override;
    uint32_t state_codec() const override { return 2; }
};

bool validate_cubeb_audio_snapshot(int len_bytes, const std::vector<uint8_t> &state);
