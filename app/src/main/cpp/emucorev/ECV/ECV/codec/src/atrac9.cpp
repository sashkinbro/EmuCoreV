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

#include <codec/state.h>

extern "C" {
#include <libatrac9.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <structures.h>
}

#include <error_codes.h>

#include <util/log.h>

#include <algorithm>

struct FFMPEGAtrac9Info {
    uint32_t version;
    uint32_t config_data;
    uint32_t padding;
};

uint32_t Atrac9DecoderState::get(DecoderQuery query) {
    Atrac9CodecInfo *info = static_cast<Atrac9CodecInfo *>(atrac9_info);
    if (!valid)
        return 0;

    switch (query) {
    case DecoderQuery::CHANNELS: return info->channels;
    // The bit rate is the size of a superframe times the number of superframes per second (times 8)
    case DecoderQuery::BIT_RATE: return static_cast<uint32_t>((info->superframeSize * 8ULL * info->samplingRate) / (info->frameSamples * info->framesInSuperframe));
    case DecoderQuery::SAMPLE_RATE: return info->samplingRate;
    case DecoderQuery::AT9_SAMPLE_PER_FRAME: return info->frameSamples;
    case DecoderQuery::AT9_SAMPLE_PER_SUPERFRAME: return info->frameSamples * info->framesInSuperframe;
    case DecoderQuery::AT9_FRAMES_IN_SUPERFRAME: return info->framesInSuperframe;
    case DecoderQuery::AT9_SUPERFRAME_SIZE: return info->superframeSize;
    default: return 0;
    }
}

uint32_t Atrac9DecoderState::get_es_size() {
    return es_size_used;
}

void Atrac9DecoderState::flush() {
    if (!valid)
        return;
    Atrac9CodecInfo *info = static_cast<Atrac9CodecInfo *>(atrac9_info);
    superframe_frame_idx = 0;
    superframe_data_left = info->superframeSize;

    Frame &frame = static_cast<Atrac9Handle *>(decoder_handle)->Frame;
    frame.IndexInSuperframe = 0;
    if (frame.Channels[0])
        std::fill_n(frame.Channels[0]->Mdct.ImdctPrevious, 256, 0.0);
    if (frame.Channels[1])
        std::fill_n(frame.Channels[1]->Mdct.ImdctPrevious, 256, 0.0);
}

void Atrac9DecoderState::export_state(Atrac9DecoderSavedState *dest) const {
    *dest = {};
    if (!valid)
        return;
    dest->has_history = true;
    const Frame &frame = static_cast<Atrac9Handle *>(decoder_handle)->Frame;
    for (int i = 0; i < frame.Config->ChannelCount; ++i) {
        const Channel &channel = *frame.Channels[i];
        std::copy_n(channel.Mdct.ImdctPrevious, 256, dest->prev_values[i]);
        auto &history = dest->channels[i];
        std::copy_n(channel.ScaleFactorsPrev, 31, history.scale_factors_prev);
        history.rng_initialized = channel.Rng.Initialized != 0;
        history.rng_state[0] = channel.Rng.StateA;
        history.rng_state[1] = channel.Rng.StateB;
        history.rng_state[2] = channel.Rng.StateC;
        history.rng_state[3] = channel.Rng.StateD;
    }
    for (int i = 0; i < frame.Config->ChannelConfig.BlockCount; ++i) {
        const Block &block = frame.Blocks[i];
        dest->blocks[i] = { block.BandCount, block.StereoBand, block.ExtensionBand,
            block.QuantizationUnitCount, block.StereoQuantizationUnit, block.ExtensionUnit,
            block.QuantizationUnitsPrev, block.BandExtensionEnabled != 0 };
    }
    dest->frame_index = frame.IndexInSuperframe;
    dest->superframe_frame_index = superframe_frame_idx;
    dest->superframe_data_left = superframe_data_left;
}

void Atrac9DecoderState::load_state(const Atrac9DecoderSavedState *src) {
    if (!valid)
        return;
    Frame &frame = static_cast<Atrac9Handle *>(decoder_handle)->Frame;
    if (!src->has_history) {
        // Key-on/reset uses a zero logical overlap history even when the runtime
        // decoder is reused. Leave its initialized superframe byte accounting
        // and the other existing decoder history intact, as before.
        for (int i = 0; i < frame.Config->ChannelCount; ++i)
            std::fill_n(frame.Channels[i]->Mdct.ImdctPrevious, 256, 0.0);
        return;
    }
    for (int i = 0; i < frame.Config->ChannelCount; ++i) {
        Channel &channel = *frame.Channels[i];
        std::copy_n(src->prev_values[i], 256, channel.Mdct.ImdctPrevious);
        const auto &history = src->channels[i];
        std::copy_n(history.scale_factors_prev, 31, channel.ScaleFactorsPrev);
        channel.Rng.Initialized = history.rng_initialized;
        channel.Rng.StateA = history.rng_state[0];
        channel.Rng.StateB = history.rng_state[1];
        channel.Rng.StateC = history.rng_state[2];
        channel.Rng.StateD = history.rng_state[3];
    }
    for (int i = 0; i < frame.Config->ChannelConfig.BlockCount; ++i) {
        Block &block = frame.Blocks[i];
        const auto &history = src->blocks[i];
        block.BandCount = history.band_count;
        block.StereoBand = history.stereo_band;
        block.ExtensionBand = history.extension_band;
        block.QuantizationUnitCount = history.quantization_unit_count;
        block.StereoQuantizationUnit = history.stereo_quantization_unit;
        block.ExtensionUnit = history.extension_unit;
        block.QuantizationUnitsPrev = history.quantization_units_prev;
        block.BandExtensionEnabled = history.band_extension_enabled;
    }
    frame.IndexInSuperframe = src->frame_index;
    superframe_frame_idx = src->superframe_frame_index;
    superframe_data_left = src->superframe_data_left;
}

bool Atrac9DecoderState::send(const uint8_t *data, uint32_t size) {
    if (!valid)
        return false;
    Atrac9CodecInfo *info = static_cast<Atrac9CodecInfo *>(atrac9_info);

    int decode_used = 0;

    const int res = Atrac9Decode(decoder_handle, data, reinterpret_cast<short *>(result.data()), &decode_used);
    if (res != At9Status::ERR_SUCCESS) {
        static uint64_t failure_count = 0;
        ++failure_count;
        if (failure_count <= 4 || (failure_count & 0x3FF) == 0) {
            uint8_t head[8] = {};
            if (data)
                memcpy(head, data, std::min<uint32_t>(size, 8));
            LOG_ERROR("Decode failure with code {} (failure #{}, size {}, superframe frame {}/{} data_left {}, config {:#010x}, head {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x} {:02x})",
                log_hex(res), failure_count, size, superframe_frame_idx, info->framesInSuperframe, superframe_data_left, config_data,
                head[0], head[1], head[2], head[3], head[4], head[5], head[6], head[7]);
        } else {
            LOG_ERROR("Decode failure with code {}", log_hex(res));
        }
        return false;
    }

    es_size_used = static_cast<uint32_t>(decode_used);
    superframe_data_left -= decode_used;
    superframe_frame_idx++;
    if (superframe_frame_idx == info->framesInSuperframe) {
        // add the padding between two superframes as size used if there is
        es_size_used += superframe_data_left;
        superframe_frame_idx = 0;
        superframe_data_left = info->superframeSize;
    }

    return true;
}

bool Atrac9DecoderState::receive(uint8_t *data, DecoderSize *size) {
    if (!valid)
        return false;
    Atrac9CodecInfo *info = static_cast<Atrac9CodecInfo *>(atrac9_info);

    if (data) {
        memcpy(data, result.data(), info->frameSamples * info->channels * sizeof(uint16_t));
    }

    if (size) {
        size->samples = static_cast<uint32_t>(info->frameSamples);
    }

    return true;
}

Atrac9DecoderState::Atrac9DecoderState(uint32_t config_data)
    : config_data(config_data) {
    decoder_handle = Atrac9GetHandle();
    const int err = Atrac9InitDecoder(decoder_handle, reinterpret_cast<uint8_t *>(&config_data));
    valid = (err == At9Status::ERR_SUCCESS);

    if (!valid) {
        LOG_ERROR("Error initializing decoder. Error code: {}", log_hex(err));
    }

    Atrac9CodecInfo *info = new Atrac9CodecInfo;
    atrac9_info = info;
    Atrac9GetCodecInfo(decoder_handle, info);

    result.resize(info->frameSamples * info->channels * sizeof(uint16_t));

    superframe_frame_idx = 0;
    superframe_data_left = info->superframeSize;
}

Atrac9DecoderState::~Atrac9DecoderState() {
    Atrac9ReleaseHandle(decoder_handle);
    delete static_cast<Atrac9CodecInfo *>(atrac9_info);
    context = nullptr;
}
