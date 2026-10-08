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

#include "audio/impl/cubeb_audio.h"
#include "util/log.h"

#include <cstring>

namespace {

constexpr uint32_t kCubebSnapshotVersion = 1;
constexpr size_t kMaxCubebSnapshotBytes = 16u * 1024u * 1024u;

void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<uint8_t>(value >> shift));
}

bool read_u32(const std::vector<uint8_t> &bytes, size_t &offset, uint32_t &value) {
    if (offset > bytes.size() || bytes.size() - offset < 4)
        return false;
    value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8)
        value |= static_cast<uint32_t>(bytes[offset++]) << shift;
    return true;
}

} // namespace

bool validate_cubeb_audio_snapshot(int len_bytes, const std::vector<uint8_t> &state) {
    if (len_bytes <= 0 || state.size() > kMaxCubebSnapshotBytes)
        return false;
    size_t offset = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    if (!read_u32(state, offset, version) || version != kCubebSnapshotVersion || !read_u32(state, offset, count) ||
        count > 4096 || count > (state.size() - offset) / 4)
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t position = 0;
        if (!read_u32(state, offset, position) || position >= static_cast<uint32_t>(len_bytes) ||
            static_cast<size_t>(len_bytes) > state.size() - offset)
            return false;
        offset += static_cast<size_t>(len_bytes);
    }
    return offset == state.size();
}

static long impl_cubeb_audio_callback(cubeb_stream *stream, void *user_data, const void *input, void *output, long nframes) {
    assert(user_data != nullptr);
    assert(stream != nullptr);
    CubebAudioOutPort *port = static_cast<CubebAudioOutPort *>(user_data);
    uint8_t *output_buffer = static_cast<uint8_t *>(output);

    std::unique_lock producer_lock(port->mutex);
    int bytes_given = 0;
    const int bytes_to_give = nframes * port->spec.channels * sizeof(uint16_t);
    while (bytes_given < bytes_to_give) {
        if (port->nb_buffers_ready == 0) {
            // no data available, should we wait for it or return nothing?
            // return nothing for now
            break;
        }

        AudioBuffer &audio_buffer = port->audio_buffers[port->next_audio_buffer];
        // compute the number of bytes we can copy from this buffer to the output
        const int bytes_to_copy = std::min(bytes_to_give - bytes_given, port->len_bytes - audio_buffer.buffer_position);
        memcpy(&output_buffer[bytes_given], &audio_buffer.buffer[audio_buffer.buffer_position], bytes_to_copy);
        audio_buffer.buffer_position += bytes_to_copy;

        if (audio_buffer.buffer_position == port->len_bytes) {
            // if we are done with this buffer, tell it
            port->next_audio_buffer = (port->next_audio_buffer + 1) % port->audio_buffers.size();
            port->nb_buffers_ready--;

        }

        bytes_given += bytes_to_copy;
    }

    producer_lock.unlock();
    port->cond_var.notify_all();
    port->notify_output_ready();
    return nframes;
}

static void impl_cubeb_state_callback(cubeb_stream *stm, void *user, cubeb_state state) {
    // we must give this function as a parameter to cubeb, but we don't care about it
}

CubebAudioOutPort::~CubebAudioOutPort() {
    if (out_stream) {
        cubeb_stream_stop(out_stream);
        cubeb_stream_destroy(out_stream);
    }
}

CubebAudioAdapter::CubebAudioAdapter(AudioState &audio_state)
    : AudioAdapter(audio_state) {}

CubebAudioAdapter::~CubebAudioAdapter() {
    if (cubeb_ctx)
        cubeb_destroy(cubeb_ctx);
}

bool CubebAudioAdapter::init() {
    if (cubeb_init(&cubeb_ctx, "Vita3K audio", nullptr) != CUBEB_OK) {
        LOG_ERROR("Could not initialize cubeb context");
        return false;
    }

    return true;
}

static AudioOutPortPtr create_cubeb_audio_port(cubeb *context, int nb_channels, int freq, int nb_sample, bool start_stream) {
    std::shared_ptr<CubebAudioOutPort> port = std::make_shared<CubebAudioOutPort>();
    port->spec = {
        // all the ps vita samples are signed 16 bits low edian
        .format = CUBEB_SAMPLE_S16LE,
        .rate = static_cast<uint32_t>(freq),
        .channels = static_cast<uint32_t>(nb_channels),
        .layout = CUBEB_LAYOUT_UNDEFINED,
        // we could use the params of sceAudioOutOpenPort to select some prefs, although I don't think it will change anything
        .prefs = CUBEB_STREAM_PREF_NONE
    };

    uint32_t latency;
    cubeb_get_min_latency(context, &port->spec, &latency);

    if (cubeb_stream_init(context, &port->out_stream, "Vita3K audio out", nullptr, nullptr, nullptr,
            &port->spec, latency, impl_cubeb_audio_callback, impl_cubeb_state_callback, port.get())
        != CUBEB_OK) {
        LOG_ERROR("Could not initialize cubeb stream");
        return nullptr;
    }

    port->len_bytes = nb_sample * nb_channels * sizeof(uint16_t);

    // allocate enough buffers to be able to satisfy a callback (+1 to make sure one buffer can be ready)
    const int nb_buffers = (latency + nb_sample - 1) / nb_sample + 1;
    port->audio_buffers.resize(nb_buffers);
    for (AudioBuffer &audio_buffer : port->audio_buffers) {
        // initialize all the buffers
        audio_buffer.buffer.resize(port->len_bytes);
        audio_buffer.buffer_position = 0;
    }

    if (start_stream && cubeb_stream_start(port->out_stream) != CUBEB_OK) {
        LOG_ERROR("Could not start cubeb stream");
        return nullptr;
    }
    port->stream_started = start_stream;
    return port;
}

AudioOutPortPtr CubebAudioAdapter::open_port(int nb_channels, int freq, int nb_sample) {
    return create_cubeb_audio_port(cubeb_ctx, nb_channels, freq, nb_sample, true);
}

AudioOutPortPtr CubebAudioAdapter::open_port_for_restore(int nb_channels, int freq, int nb_sample) {
    return create_cubeb_audio_port(cubeb_ctx, nb_channels, freq, nb_sample, false);
}

AudioSubmitResult CubebAudioAdapter::try_audio_output(AudioOutPort &out_port, const void *buffer, bool allow_overflow) {
    auto &port = static_cast<CubebAudioOutPort &>(out_port);
    if (port.stopping)
        return AudioSubmitResult::stopped;
    if (port.audio_buffers.empty())
        return AudioSubmitResult::error;
    if (port.nb_buffers_ready == port.audio_buffers.size())
        return AudioSubmitResult::would_block;
    const size_t index = (port.next_audio_buffer + port.nb_buffers_ready) % port.audio_buffers.size();
    memcpy(port.audio_buffers[index].buffer.data(), buffer, port.len_bytes);
    port.audio_buffers[index].buffer_position = 0;
    ++port.nb_buffers_ready;
    return AudioSubmitResult::submitted;
}

void CubebAudioAdapter::set_volume(AudioOutPort &out_port, float volume) {
    CubebAudioOutPort &port = static_cast<CubebAudioOutPort &>(out_port);
    cubeb_stream_set_volume(port.out_stream, volume);
}

void CubebAudioAdapter::switch_state(const bool pause) {
    for (auto &[_, out_port] : state.out_ports) {
        CubebAudioOutPort &port = static_cast<CubebAudioOutPort &>(*out_port);
        if (pause && port.stream_started) {
            if (cubeb_stream_stop(port.out_stream) == CUBEB_OK)
                port.stream_started = false;
        } else if (!pause && !port.stream_started) {
            if (cubeb_stream_start(port.out_stream) == CUBEB_OK)
                port.stream_started = true;
        }
    }
}

void CubebAudioAdapter::wake_all_ports() {
    for (auto &[_, port_ptr] : state.out_ports) {
        auto &port = static_cast<CubebAudioOutPort &>(*port_ptr);
        {
            std::lock_guard<std::mutex> lock(port.mutex);
        }
        port.cond_var.notify_all();
        port.notify_output_ready();
    }
}

bool CubebAudioAdapter::save_port_state(AudioOutPort &out_port, std::vector<uint8_t> &state) {
    auto &port = static_cast<CubebAudioOutPort &>(out_port);
    const std::lock_guard<std::mutex> lock(port.mutex);
    if (port.len_bytes <= 0 || port.next_audio_buffer < 0 || port.nb_buffers_ready < 0 ||
        static_cast<size_t>(port.next_audio_buffer) >= port.audio_buffers.size() ||
        static_cast<size_t>(port.nb_buffers_ready) > port.audio_buffers.size())
        return false;

    state.clear();
    append_u32(state, kCubebSnapshotVersion);
    append_u32(state, static_cast<uint32_t>(port.nb_buffers_ready));
    for (int i = 0; i < port.nb_buffers_ready; ++i) {
        const size_t index = (static_cast<size_t>(port.next_audio_buffer) + static_cast<size_t>(i)) % port.audio_buffers.size();
        const AudioBuffer &buffer = port.audio_buffers[index];
        if (buffer.buffer.size() != static_cast<size_t>(port.len_bytes) || buffer.buffer_position < 0 ||
            buffer.buffer_position >= port.len_bytes)
            return false;
        const size_t length = static_cast<size_t>(port.len_bytes);
        if (length > kMaxCubebSnapshotBytes - 4 || state.size() > kMaxCubebSnapshotBytes - 4 - length)
            return false;
        append_u32(state, static_cast<uint32_t>(buffer.buffer_position));
        state.insert(state.end(), buffer.buffer.begin(), buffer.buffer.end());
    }
    return state.size() <= kMaxCubebSnapshotBytes;
}

bool CubebAudioAdapter::restore_port_state(AudioOutPort &out_port, const std::vector<uint8_t> &state) {
    auto &port = static_cast<CubebAudioOutPort &>(out_port);
    if (state.size() > kMaxCubebSnapshotBytes)
        return false;

    size_t offset = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    if (!read_u32(state, offset, version) || version != kCubebSnapshotVersion || !read_u32(state, offset, count) || count > 4096 || count > (state.size() - offset) / 4)
        return false;

    struct PendingBuffer {
        uint32_t position = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<PendingBuffer> pending;
    pending.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t position = 0;
        if (!read_u32(state, offset, position) || port.len_bytes <= 0 || static_cast<size_t>(port.len_bytes) > state.size() - offset || position >= static_cast<uint32_t>(port.len_bytes))
            return false;
        PendingBuffer buffer;
        buffer.position = position;
        buffer.bytes.assign(state.begin() + offset, state.begin() + offset + port.len_bytes);
        pending.emplace_back(std::move(buffer));
        offset += static_cast<size_t>(port.len_bytes);
    }
    if (offset != state.size())
        return false;

    const std::lock_guard<std::mutex> lock(port.mutex);
    if (port.len_bytes <= 0 || pending.size() > port.audio_buffers.size())
        return false;
    for (const AudioBuffer &buffer : port.audio_buffers) {
        if (buffer.buffer.size() != static_cast<size_t>(port.len_bytes))
            return false;
    }

    for (size_t i = 0; i < port.audio_buffers.size(); ++i) {
        AudioBuffer &buffer = port.audio_buffers[i];
        if (i < pending.size()) {
            std::memcpy(buffer.buffer.data(), pending[i].bytes.data(), pending[i].bytes.size());
            buffer.buffer_position = static_cast<int>(pending[i].position);
        } else {
            buffer.buffer_position = 0;
        }
    }
    port.next_audio_buffer = 0;
    port.nb_buffers_ready = static_cast<int>(pending.size());
    port.cond_var.notify_one();
    return true;
}
