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

#define DEBUG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>

#include <libavcodec/codec_internal.h>
}

#include <util/log.h>

#include <algorithm>
#include <cstring>
#include <vector>

AacDecoderState::AacDecoderState(uint32_t sample_rate, uint32_t channels, bool sbr) {
    // SBR doubles the core rate the guest gives
    output_sample_rate = sbr ? sample_rate * 2 : sample_rate;
    output_channels = channels;
    is_sbr = sbr;

    codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    assert(codec);

    context = avcodec_alloc_context3(codec);
    assert(context);

    frame = av_frame_alloc();

    context->codec_type = AVMEDIA_TYPE_AUDIO;
    av_channel_layout_default(&context->ch_layout, channels);
    context->sample_rate = sample_rate;

    int err = avcodec_open2(context, codec, nullptr);
    assert(err == 0);
}

AacDecoderState::~AacDecoderState() {
    av_frame_free(&frame);
    swr_free(&swr);
}

uint32_t AacDecoderState::get(DecoderQuery query) {
    switch (query) {
    case DecoderQuery::CHANNELS: return output_channels;
    case DecoderQuery::BIT_RATE: return context->bit_rate;
    case DecoderQuery::SAMPLE_RATE: return output_sample_rate;
    default:
        return 0;
    }
}

bool AacDecoderState::send(const uint8_t *data, uint32_t size) {
    AVPacket *packet = av_packet_alloc();
    packet->data = const_cast<uint8_t *>(data);
    packet->size = size;

    av_frame_unref(frame);

    const FFCodec *ff_codec = ffcodec(codec);
    int got_frame;
    int len = ff_codec->cb.decode(context, frame, &got_frame, packet);
    assert(got_frame);

    av_packet_free(&packet);
    if (len < 0) {
        LOG_WARN("Error sending Aac packet: {}.", codec_error_name(len));
        return false;
    }

    es_size_used = static_cast<uint32_t>(len);

    return true;
}

bool AacDecoderState::receive(uint8_t *data, DecoderSize *size) {
    if (frame->nb_samples <= 0) {
        if (size)
            size->samples = 0;
        return true;
    }

    // FFmpeg decodes implicit SBR in mono as stereo the guest gets the layout it asked for
    const int input_sample_rate = frame->sample_rate > 0 ? frame->sample_rate : context->sample_rate;
    const int input_channels = frame->ch_layout.nb_channels > 0 ? frame->ch_layout.nb_channels : context->ch_layout.nb_channels;
    if (input_sample_rate <= 0 || input_channels <= 0) {
        LOG_WARN("AAC frame has an invalid format: {} Hz, {} channels.", input_sample_rate, input_channels);
        return false;
    }

    if (!swr || configured_input_sample_rate != input_sample_rate || configured_input_channels != input_channels
        || configured_input_format != frame->format) {
        AVChannelLayout input_layout{};
        if (frame->ch_layout.nb_channels > 0)
            av_channel_layout_copy(&input_layout, &frame->ch_layout);
        else
            av_channel_layout_default(&input_layout, input_channels);
        AVChannelLayout output_layout{};
        av_channel_layout_default(&output_layout, static_cast<int>(output_channels));

        SwrContext *new_swr = nullptr;
        const int ret = swr_alloc_set_opts2(&new_swr,
            &output_layout, AV_SAMPLE_FMT_S16, static_cast<int>(output_sample_rate),
            &input_layout, static_cast<AVSampleFormat>(frame->format), input_sample_rate,
            0, nullptr);
        av_channel_layout_uninit(&output_layout);
        av_channel_layout_uninit(&input_layout);
        if (ret < 0 || !new_swr || swr_init(new_swr) < 0) {
            swr_free(&new_swr);
            LOG_WARN("Failed to configure the AAC converter: {} Hz {} ch -> {} Hz {} ch.", input_sample_rate, input_channels, output_sample_rate, output_channels);
            return false;
        }

        swr_free(&swr);
        swr = new_swr;
        configured_input_sample_rate = input_sample_rate;
        configured_input_channels = input_channels;
        configured_input_format = frame->format;
    }

    const int max_samples = is_sbr ? 2048 : 1024;
    const int capacity = swr_get_out_samples(swr, frame->nb_samples);
    if (capacity < 0) {
        LOG_WARN("AAC output size query failed: {}.", codec_error_name(capacity));
        return false;
    }
    int converted_samples;
    if (data) {
        // Consume the complete frame even if it produces more PCM than the guest
        // requested. Limiting swr_convert's capacity instead would queue the
        // surplus indefinitely and feed old audio to subsequent guest frames.
        std::vector<uint8_t> converted_pcm;
        uint8_t *output = data;
        if (capacity > max_samples) {
            converted_pcm.resize(size_t(capacity) * output_channels * sizeof(int16_t));
            output = converted_pcm.data();
        }
        converted_samples = swr_convert(swr, &output, capacity, const_cast<const uint8_t **>(frame->extended_data), frame->nb_samples);
        if (converted_samples < 0) {
            LOG_WARN("AAC conversion failed: {}.", codec_error_name(converted_samples));
            return false;
        }
        converted_samples = std::min(converted_samples, max_samples);
        if (!converted_pcm.empty())
            std::memcpy(data, converted_pcm.data(), size_t(converted_samples) * output_channels * sizeof(int16_t));
    } else {
        converted_samples = std::min(capacity, max_samples);
    }

    if (size) {
        size->samples = static_cast<uint32_t>(converted_samples);
    }

    return true;
}

uint32_t AacDecoderState::get_es_size() {
    return es_size_used;
}
