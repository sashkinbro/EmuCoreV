// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

// The on-disk representation contains scalar values, never C++ object layouts,
// runtime pointers, padding, or vtables. Readers build a temporary state and only
// publish it after the entire versioned payload has passed validation.
namespace ngs::logical_state_io {
class Writer {
public:
    std::vector<uint8_t> bytes;
    template <typename T> void scalar(T value) {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);
        const auto representation = std::bit_cast<std::array<uint8_t, sizeof(T)>>(value);
        if constexpr (std::endian::native == std::endian::little)
            bytes.insert(bytes.end(), representation.begin(), representation.end());
        else
            bytes.insert(bytes.end(), representation.rbegin(), representation.rend());
    }
    void boolean(bool value) { scalar<uint8_t>(value ? 1 : 0); }
    template <typename T> void vector(const std::vector<T> &values) {
        if (values.size() > std::numeric_limits<uint32_t>::max())
            throw std::length_error("NGS logical state vector exceeds archive capacity");
        scalar(static_cast<uint32_t>(values.size()));
        for (T value : values)
            scalar(value);
    }
    template <typename Queue> void queue(const Queue &value) {
        scalar(value.read_offset_frames);
        vector(value.samples);
    }
    template <typename Rate> void rate(const Rate &value) {
        queue(value.input_history);
        boolean(value.needs_reset);
        scalar(value.total_input_frames);
        scalar(value.source_rate);
        scalar(value.dest_rate);
    }
};

class Reader {
    const std::vector<uint8_t> &bytes;
    size_t offset = 0;
public:
    explicit Reader(const std::vector<uint8_t> &bytes) : bytes(bytes) {}
    template <typename T> bool scalar(T &value) {
        static_assert(std::is_arithmetic_v<T> && !std::is_same_v<T, bool>);
        if (sizeof(T) > bytes.size() - offset)
            return false;
        std::array<uint8_t, sizeof(T)> representation{};
        for (size_t i = 0; i < sizeof(T); ++i)
            representation[std::endian::native == std::endian::little ? i : sizeof(T) - 1 - i] = bytes[offset++];
        value = std::bit_cast<T>(representation);
        return true;
    }
    bool boolean(bool &value) {
        uint8_t encoded;
        if (!scalar(encoded) || encoded > 1)
            return false;
        value = encoded != 0;
        return true;
    }
    template <typename T> bool vector(std::vector<T> &values) {
        uint32_t count;
        if (!scalar(count) || count > (bytes.size() - offset) / sizeof(T))
            return false;
        values.resize(count);
        for (T &value : values)
            if (!scalar(value))
                return false;
        return true;
    }
    template <typename Queue> bool queue(Queue &value) {
        return scalar(value.read_offset_frames) && vector(value.samples)
            && value.samples.size() % 2 == 0 && value.read_offset_frames <= value.samples.size() / 2;
    }
    template <typename Rate> bool rate(Rate &value) {
        if (!queue(value.input_history) || !boolean(value.needs_reset) || !scalar(value.total_input_frames)
            || !scalar(value.source_rate) || !scalar(value.dest_rate))
            return false;
        return value.is_replayable();
    }
    bool finished() const { return offset == bytes.size(); }
};

template <typename State, typename Fields>
void capture(const State *state, uint32_t module, std::vector<uint8_t> &out, Fields fields) {
    Writer writer;
    writer.scalar(module);
    writer.scalar<uint32_t>(1); // logical schema version, independent of the outer archive
    writer.boolean(state != nullptr);
    if (state)
        fields(writer, *state);
    out = std::move(writer.bytes);
}

template <typename State, typename Fields>
bool restore(const std::vector<uint8_t> &in, uint32_t module, std::unique_ptr<State> &out, Fields fields) {
    Reader reader(in);
    uint32_t saved_module, version;
    bool present;
    if (!reader.scalar(saved_module) || saved_module != module || !reader.scalar(version) || version != 1 || !reader.boolean(present))
        return false;
    auto candidate = present ? std::make_unique<State>() : nullptr;
    if ((present && !fields(reader, *candidate)) || !reader.finished())
        return false;
    out = std::move(candidate);
    return true;
}

template <typename State> void capture_player(const State *state, std::vector<uint8_t> &out) {
    capture(state, 0x5CE6, out, [](Writer &w, const State &s) {
        w.queue(s.decoded_pcm);
        w.rate(s.rate_resampler);
        w.vector(s.adpcm_buffer);
        w.scalar(s.current_loop_count);
        for (const auto &h : s.adpcm_history) {
            w.scalar(h.hist1); w.scalar(h.hist2); w.scalar(h.hist3); w.scalar(h.hist4);
        }
        w.boolean(s.requested_initial_buffer);
        w.scalar(s.starved_ticks);
    });
}
template <typename State> bool restore_player(const std::vector<uint8_t> &in, std::unique_ptr<State> &out) {
    return restore(in, 0x5CE6, out, [](Reader &r, State &s) {
        if (!r.queue(s.decoded_pcm) || !r.rate(s.rate_resampler)
            || !r.vector(s.adpcm_buffer) || !r.scalar(s.current_loop_count))
            return false;
        for (auto &h : s.adpcm_history)
            if (!r.scalar(h.hist1) || !r.scalar(h.hist2) || !r.scalar(h.hist3) || !r.scalar(h.hist4))
                return false;
        return r.boolean(s.requested_initial_buffer) && r.scalar(s.starved_ticks);
    });
}

template <typename State> void capture_envelope(const State *state, std::vector<uint8_t> &out) {
    capture(state, 0x5CE3, out, [](Writer &w, const State &s) {
        w.scalar(s.position_in_segment_ms); w.scalar(s.total_position_ms); w.scalar(s.current_point);
        w.boolean(s.releasing); w.scalar(s.release_start_height); w.scalar(s.release_height);
        w.scalar(s.release_position_ms); w.scalar(s.completed_at_zero_ms);
    });
}
template <typename State> bool restore_envelope(const std::vector<uint8_t> &in, std::unique_ptr<State> &out) {
    return restore(in, 0x5CE3, out, [](Reader &r, State &s) {
        return r.scalar(s.position_in_segment_ms) && r.scalar(s.total_position_ms) && r.scalar(s.current_point)
            && r.boolean(s.releasing) && r.scalar(s.release_start_height) && r.scalar(s.release_height)
            && r.scalar(s.release_position_ms) && r.scalar(s.completed_at_zero_ms)
            // current_point indexes the fixed four-point guest parameter array.
            && s.current_point >= 0 && s.current_point < 4
            && std::isfinite(s.position_in_segment_ms) && s.position_in_segment_ms >= 0
            && std::isfinite(s.total_position_ms) && s.total_position_ms >= 0
            && std::isfinite(s.release_position_ms) && s.release_position_ms >= 0
            && std::isfinite(s.completed_at_zero_ms) && s.completed_at_zero_ms >= 0
            && std::isfinite(s.release_start_height) && std::isfinite(s.release_height);
    });
}

template <typename State> void capture_compressor(const State *state, std::vector<uint8_t> &out) {
    capture(state, 0x5CE1, out, [](Writer &w, const State &s) {
        for (float value : s.envelope) w.scalar(value);
        for (float value : s.applied_gain) w.scalar(value);
        for (float value : s.reduction_db) w.scalar(value);
    });
}
template <typename State> bool restore_compressor(const std::vector<uint8_t> &in, std::unique_ptr<State> &out) {
    return restore(in, 0x5CE1, out, [](Reader &r, State &s) {
        for (float &value : s.envelope)
            if (!r.scalar(value) || !std::isfinite(value) || value < 0) return false;
        for (float &value : s.applied_gain)
            if (!r.scalar(value) || !std::isfinite(value) || value < 0) return false;
        for (float &value : s.reduction_db)
            if (!r.scalar(value) || !std::isfinite(value)) return false;
        return true;
    });
}

template <typename State> void capture_atrac9(const State *state, std::vector<uint8_t> &out) {
    capture(state, 0x5CAA, out, [](Writer &w, const State &s) {
        w.scalar(s.decoder_config);
        w.boolean(s.saved_state.has_history);
        for (const auto &channel : s.saved_state.prev_values)
            for (double value : channel) w.scalar(value);
        for (const auto &channel : s.saved_state.channels) {
            for (int32_t value : channel.scale_factors_prev) w.scalar(value);
            w.boolean(channel.rng_initialized);
            for (uint16_t value : channel.rng_state) w.scalar(value);
        }
        for (const auto &block : s.saved_state.blocks) {
            w.scalar(block.band_count); w.scalar(block.stereo_band); w.scalar(block.extension_band);
            w.scalar(block.quantization_unit_count); w.scalar(block.stereo_quantization_unit);
            w.scalar(block.extension_unit); w.scalar(block.quantization_units_prev); w.boolean(block.band_extension_enabled);
        }
        w.scalar(s.saved_state.frame_index); w.scalar(s.saved_state.superframe_frame_index); w.scalar(s.saved_state.superframe_data_left);
        w.scalar(s.current_loop_count); w.scalar(s.starved_ticks); w.boolean(s.in_underrun_wait);
        w.vector(s.superframe_staging); w.queue(s.decoded_pcm); w.rate(s.rate_resampler);
        // These counters also control healing and silent-stream completion.
        w.scalar(s.diag_superframes); w.scalar(s.diag_silent_streak);
        w.boolean(s.diag_reported_silent); w.boolean(s.diag_reported_onset); w.boolean(s.heal_done_this_episode);
        for (const auto &record : s.diag_ring) {
            w.scalar(record.index); w.scalar(record.pos); w.scalar(record.buf);
            w.scalar(record.staged); w.scalar(record.head); w.scalar(record.peak);
        }
        w.scalar(s.diag_ring_next);
    });
}
template <typename State> bool restore_atrac9(const std::vector<uint8_t> &in, std::unique_ptr<State> &out) {
    return restore(in, 0x5CAA, out, [](Reader &r, State &s) {
        if (!r.scalar(s.decoder_config) || !r.boolean(s.saved_state.has_history)) return false;
        // LibAtrac9's config parser indexes a six-entry channel table directly.
        // Reject malformed configs before lazy decoder construction can see them.
        if ((s.decoder_config != 0 || s.saved_state.has_history)
            && ((s.decoder_config & 0xFF) != 0xFE || ((s.decoder_config >> 9) & 7) > 5 || (s.decoder_config & 0x100))) return false;
        for (auto &channel : s.saved_state.prev_values)
            for (double &value : channel) if (!r.scalar(value) || !std::isfinite(value)) return false;
        for (auto &channel : s.saved_state.channels) {
            for (int32_t &value : channel.scale_factors_prev)
                if (!r.scalar(value) || value < 0 || value > 31) return false;
            if (!r.boolean(channel.rng_initialized)) return false;
            for (uint16_t &value : channel.rng_state) if (!r.scalar(value)) return false;
        }
        for (auto &block : s.saved_state.blocks) {
            if (!r.scalar(block.band_count) || !r.scalar(block.stereo_band) || !r.scalar(block.extension_band)
                || !r.scalar(block.quantization_unit_count) || !r.scalar(block.stereo_quantization_unit)
                || !r.scalar(block.extension_unit) || !r.scalar(block.quantization_units_prev) || !r.boolean(block.band_extension_enabled)) return false;
            if (block.band_count < 0 || block.band_count > 18 || block.stereo_band < 0 || block.stereo_band > 18
                || block.extension_band < 0 || block.extension_band > 18 || block.quantization_unit_count < 0 || block.quantization_unit_count > 30
                || block.stereo_quantization_unit < 0 || block.stereo_quantization_unit > 30 || block.extension_unit < 0 || block.extension_unit > 30
                || block.quantization_units_prev < 0 || block.quantization_units_prev > 30) return false;
        }
        if (!r.scalar(s.saved_state.frame_index) || !r.scalar(s.saved_state.superframe_frame_index) || !r.scalar(s.saved_state.superframe_data_left)
            || s.saved_state.frame_index < 0 || s.saved_state.frame_index > 7
            || s.saved_state.superframe_frame_index < 0 || s.saved_state.superframe_frame_index > 7
            || s.saved_state.superframe_data_left < 0 || s.saved_state.superframe_data_left > 16384
            || (s.saved_state.has_history && s.saved_state.superframe_data_left == 0)) return false;
        if (s.saved_state.has_history) {
            const int frames_per_superframe = 1 << ((s.decoder_config >> 27) & 3);
            const int frame_bytes = (((s.decoder_config >> 16) & 0xFF) << 3 | ((s.decoder_config >> 29) & 7)) + 1;
            if (s.saved_state.frame_index >= frames_per_superframe || s.saved_state.superframe_frame_index >= frames_per_superframe
                || s.saved_state.superframe_data_left > frame_bytes * frames_per_superframe) return false;
        }
        if (!r.scalar(s.current_loop_count) || !r.scalar(s.starved_ticks) || !r.boolean(s.in_underrun_wait)
            || !r.vector(s.superframe_staging) || !r.queue(s.decoded_pcm) || !r.rate(s.rate_resampler)
            || !r.scalar(s.diag_superframes) || !r.scalar(s.diag_silent_streak)
            || !r.boolean(s.diag_reported_silent) || !r.boolean(s.diag_reported_onset) || !r.boolean(s.heal_done_this_episode)) return false;
        for (auto &record : s.diag_ring)
            if (!r.scalar(record.index) || !r.scalar(record.pos) || !r.scalar(record.buf)
                || !r.scalar(record.staged) || !r.scalar(record.head) || !r.scalar(record.peak)) return false;
        return r.scalar(s.diag_ring_next);
    });
}

template <typename State, typename Decoder>
void capture_atrac9_snapshot(const State *logical, const Decoder *decoder, std::vector<uint8_t> &out) {
    if (logical && decoder && !logical->saved_state.has_history) {
        // Key-on requests zero overlap while reusing runtime RNG/band history.
        // Describe the next decode without applying its reset to the live game.
        State snapshot = *logical;
        decoder->export_state(&snapshot.saved_state);
        for (auto &channel : snapshot.saved_state.prev_values)
            for (double &sample : channel) sample = 0.0;
        capture_atrac9(&snapshot, out);
        return;
    }
    capture_atrac9(logical, out);
}
} // namespace ngs::logical_state_io
