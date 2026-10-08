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

#include <util/types.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#define SCE_AUDIO_OUT_MAX_VOL 32768 //!< Maximum output port volume
#define SCE_AUDIO_VOLUME_0DB SCE_AUDIO_OUT_MAX_VOL //!< Maximum output port volume

enum class AudioSubmitResult { submitted, would_block, stopped, error };

struct AudioOutPort {
    virtual ~AudioOutPort() = default;

    // Producer state and PCM submission commit share this lock.
    std::mutex mutex;
    // SDL invokes its callback with the stream lock held: wake subscribers must
    // not take the producer mutex (producer calls SDL while holding it).
    std::mutex output_wakers_mutex;
    std::map<int, std::function<void()>> output_wakers;
    void notify_output_ready() {
        const std::lock_guard lock(output_wakers_mutex);
        for (auto &[_, wake] : output_wakers) wake();
    }

    // shutdown flag
    std::atomic<bool> stopping{ false };
    // Channel range from 0 - 32768
    int left_channel_volume = SCE_AUDIO_VOLUME_0DB;
    int right_channel_volume = SCE_AUDIO_VOLUME_0DB;
    // Volume range from 0 to 1
    float volume = 1.0f;
    // length of the buffer for each call
    int len_bytes = 0;
    // number of microseconds a buffer lasts for
    uint64_t len_microseconds = 0;
    // last time sceAudioOutOutput was called with this port (timestamp in microseconds)
    uint64_t last_output = 0;

    // current config
    int type = 0;
    int len = 0;
    int freq = 0;
    int mode = 0;
};

typedef std::shared_ptr<AudioOutPort> AudioOutPortPtr;
typedef std::map<int, AudioOutPortPtr> AudioOutPortPtrs;

struct AudioInPort {
    void *id = nullptr;
    bool running = false;
    int len_bytes = 0;
};

struct ThreadState;
struct AudioState;

// abstract class that need to be overloaded with an audio implementation
class AudioAdapter {
public:
    AudioState &state;

    AudioAdapter(AudioState &audio_state)
        : state(audio_state) {}
    virtual ~AudioAdapter() = default;

    virtual bool init() = 0;
    virtual AudioOutPortPtr open_port(int nb_channels, int freq, int nb_sample) { return nullptr; }
    virtual AudioOutPortPtr open_port_for_restore(int nb_channels, int freq, int nb_sample) {
        return open_port(nb_channels, freq, nb_sample);
    }
    // Called with out_port.mutex held; must never block for capacity.
    virtual AudioSubmitResult try_audio_output(AudioOutPort &out_port, const void *buffer, bool allow_overflow) { return AudioSubmitResult::error; }
    virtual uint64_t output_wait_timeout_us(const AudioOutPort &out_port) const { return 0; }
    virtual void set_volume(AudioOutPort &out_port, float volume) {}
    virtual void switch_state(const bool pause) {}
    virtual int get_rest_sample(AudioOutPort &out_port) { return 0; };
    virtual void wake_all_ports() {}
    virtual uint32_t state_codec() const { return 0; }
    // Opaque, bounded adapter-owned playback queue state. The default adapter
    // supports only empty queues; concrete adapters override both operations.
    virtual bool save_port_state(AudioOutPort &out_port, std::vector<uint8_t> &state) {
        state.clear();
        return get_rest_sample(out_port) == 0;
    }
    virtual bool restore_port_state(AudioOutPort &out_port, const std::vector<uint8_t> &state) {
        return state.empty();
    }
    friend struct AudioState;
};

struct AudioState {
    //  the adapter must be before out_ports for the destructors to work correctly
    std::unique_ptr<AudioAdapter> adapter;
    std::mutex mutex;
    int next_port_id = 1;
    AudioOutPortPtrs out_ports;
    AudioInPort in_port;
    std::string audio_backend;
    float global_volume = 1;

    bool init(const std::string &adapter_name);
    void deinit();
    void stop_all_ports();
    void set_backend(const std::string &adapter_name);
    AudioOutPortPtr open_port(int nb_channels, int freq, int nb_sample);
    AudioOutPortPtr open_port_for_restore(int nb_channels, int freq, int nb_sample);
    void set_volume(AudioOutPort &out_port, float volume);
    void set_global_volume(float volume);
    void switch_state(const bool pause);
    int get_rest_sample(AudioOutPort &out_port);
    void wake_all_ports();
};

bool validate_audio_port_snapshot(uint32_t codec, int len_bytes, const std::vector<uint8_t> &snapshot);
