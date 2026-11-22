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

#include <codec/avc_picture.h>
#include <codec/state.h>

#include <util/log.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cassert>

void copy_yuv_data_from_frame(AVFrame *frame, uint8_t *dest, const uint32_t width, const uint32_t height, bool is_p3) {
    // the buffer can be larger than the picture (titles pass their maximum size until a picture reports its own)
    const uint32_t copy_width = std::min(width, static_cast<uint32_t>(frame->width));
    const uint32_t copy_height = std::min(height, static_cast<uint32_t>(frame->height));

    for (uint32_t i = 0; i < copy_height; i++)
        memcpy(dest + i * width, &frame->data[0][frame->linesize[0] * i], copy_width);

    uint8_t *const chroma = dest + width * height;
    if (is_p3) {
        const size_t uv_pitch = width / 2;
        uint8_t *const v_plane = chroma + uv_pitch * (height / 2);
        for (uint32_t i = 0; i < copy_height / 2; i++) {
            memcpy(chroma + i * uv_pitch, &frame->data[1][frame->linesize[1] * i], copy_width / 2);
            memcpy(v_plane + i * uv_pitch, &frame->data[2][frame->linesize[2] * i], copy_width / 2);
        }
    } else {
        // p2 format, U and V are interleaved
        const size_t uv_pitch = width;
        for (uint32_t i = 0; i < copy_height / 2; i++) {
            const uint8_t *src_u = &frame->data[1][frame->linesize[1] * i];
            const uint8_t *src_v = &frame->data[2][frame->linesize[2] * i];
            uint8_t *row = chroma + i * uv_pitch;
            for (uint32_t j = 0; j < copy_width / 2; j++) {
                row[0] = src_u[j];
                row[1] = src_v[j];
                row += 2;
            }
        }
    }
}

namespace {
// 4:2:0 view of a decoded frame. Frames in another chroma format (the hardware decoder never
// produces them, FFmpeg may) are first converted with swscale.
struct Planes420 {
    avc_picture::Yuv420Planes planes;
    std::vector<uint8_t> storage;
};

bool get_planes_420(const AVFrame *frame, Planes420 &out) {
    if (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P) {
        out.planes.y = frame->data[0];
        out.planes.y_pitch = static_cast<size_t>(frame->linesize[0]);
        out.planes.u = frame->data[1];
        out.planes.v = frame->data[2];
        out.planes.chroma_pitch = static_cast<size_t>(frame->linesize[1]);
        out.planes.chroma_step = 1;
        return true;
    }
    const int w = frame->width, h = frame->height;
    if (w <= 0 || h <= 0 || frame->format < 0)
        return false;
    const size_t cw = (w + 1) / 2, ch = (h + 1) / 2;
    out.storage.resize(static_cast<size_t>(w) * h + 2 * cw * ch);
    uint8_t *dst[4] = { out.storage.data(), out.storage.data() + static_cast<size_t>(w) * h, out.storage.data() + static_cast<size_t>(w) * h + cw * ch, nullptr };
    int dst_stride[4] = { w, static_cast<int>(cw), static_cast<int>(cw), 0 };
    SwsContext *sws = sws_getContext(w, h, static_cast<AVPixelFormat>(frame->format), w, h, AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws) {
        LOG_ERROR("Cannot convert decoded video frame format {} to YUV420P.", frame->format);
        return false;
    }
    sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dst_stride);
    sws_freeContext(sws);
    out.planes = { dst[0], static_cast<size_t>(w), dst[1], dst[2], cw, 1 };
    return true;
}

// Copies the 4:2:0 planes into a guest picture buffer laid out for the given pitch.
void copy_planes_to_picture(const avc_picture::Yuv420Planes &src, uint8_t *dest, uint32_t type, uint32_t pitch, uint32_t src_w, uint32_t src_h, uint32_t dst_w, uint32_t dst_h) {
    avc_picture::YuvLayout layout;
    if (!avc_picture::yuv420_layout(type, pitch, dst_h, layout))
        return;
    const auto extent = avc_picture::copy_extent(src_w, src_h, dst_w, dst_h);
    for (uint32_t i = 0; i < extent.height; i++)
        memcpy(dest + static_cast<size_t>(i) * layout.luma_pitch, src.y + src.y_pitch * i, extent.width);

    uint8_t *const chroma = dest + layout.chroma_offset;
    if (type == avc_picture::kPixelYuv420Raster) {
        uint8_t *const v_plane = dest + layout.v_offset;
        for (uint32_t i = 0; i < extent.height / 2; i++) {
            memcpy(chroma + static_cast<size_t>(i) * layout.chroma_pitch, src.u + src.chroma_pitch * i, extent.width / 2);
            memcpy(v_plane + static_cast<size_t>(i) * layout.chroma_pitch, src.v + src.chroma_pitch * i, extent.width / 2);
        }
    } else {
        for (uint32_t i = 0; i < extent.height / 2; i++) {
            const uint8_t *src_u = src.u + src.chroma_pitch * i;
            const uint8_t *src_v = src.v + src.chroma_pitch * i;
            uint8_t *row = chroma + static_cast<size_t>(i) * layout.chroma_pitch;
            for (uint32_t j = 0; j < extent.width / 2; j++) {
                row[0] = src_u[j];
                row[1] = src_v[j];
                row += 2;
            }
        }
    }
}
} // namespace

uint32_t H264DecoderState::buffer_size(DecoderSize size) {
    return size.width * size.height * 3 / 2;
}

uint32_t H264DecoderState::get(DecoderQuery query) {
    switch (query) {
    case DecoderQuery::WIDTH: return context->width;
    case DecoderQuery::HEIGHT: return context->height;
    default: return 0;
    }
}

bool H264DecoderState::send(const uint8_t *data, uint32_t size) {
    std::lock_guard<std::mutex> lock(codec_mutex);

    int error = 0;

    std::vector<uint8_t> au_frame(size + AV_INPUT_BUFFER_PADDING_SIZE);
    memcpy(au_frame.data(), data, size);

    AVPacket *packet = av_packet_alloc();
    if (!packet) {
        LOG_ERROR("Error allocating H264 packet.");
        return false;
    }
    error = av_parser_parse2(
        parser, // AVCodecParserContext *s,
        context, // AVCodecContext *avctx,
        &packet->data, // uint8_t **poutbuf,
        &packet->size, // int *poutbuf_size,
        au_frame.data(), // const uint8_t *buf,
        size, // int buf_size,
        pts == ~0ull ? AV_NOPTS_VALUE : pts, // int64_t pts,
        dts == ~0ull ? AV_NOPTS_VALUE : dts, // int64_t dts,
        0 // int64_t pos
    );
    if (error < 0) {
        LOG_WARN("Error parsing H264 packet: {}.", codec_error_name(error));
        av_packet_free(&packet);
        return false;
    }

    packet->pts = parser->pts;
    packet->dts = parser->dts;

    error = avcodec_send_packet(context, packet);
    av_packet_free(&packet);
    if (error < 0) {
        LOG_WARN("Error sending H264 packet: {}.", codec_error_name(error));
        return false;
    }

    return true;
}

bool H264DecoderState::receive(uint8_t *data, DecoderSize *size) {
    AVFrame *frame = av_frame_alloc();

    int error = avcodec_receive_frame(context, frame);
    if (error < 0) {
        LOG_WARN("Error receiving H264 frame: {}.", codec_error_name(error));
        av_frame_free(&frame);
        return false;
    }

    if (data) {
        copy_yuv_data_from_frame(frame, data, width_in, height_in, output_yuvp3);
    }

    if (size) {
        *size = { { static_cast<uint32_t>(context->width), static_cast<uint32_t>(context->height) } };
    }

    width_out = frame->width;
    height_out = frame->height;

    pts_out = frame->pts;

    av_frame_free(&frame);
    return true;
}

bool H264DecoderState::receive_picture(uint8_t *data, const H264PictureRequest &request) {
    AVFrame *frame = av_frame_alloc();

    int error = avcodec_receive_frame(context, frame);
    if (error < 0) {
        LOG_WARN("Error receiving H264 frame: {}.", codec_error_name(error));
        av_frame_free(&frame);
        return false;
    }

    if (data) {
        Planes420 src;
        if (get_planes_420(frame, src)) {
            const uint32_t pitch = avc_picture::effective_pitch(request.pitch, request.width);
            if (avc_picture::is_yuv(request.pixel_type)) {
                copy_planes_to_picture(src.planes, data, request.pixel_type, pitch, frame->width, frame->height, request.width, request.height);
            } else {
                const auto extent = avc_picture::copy_extent(frame->width, frame->height, request.width, request.height);
                const bool bt709 = frame->colorspace == AVCOL_SPC_BT709;
                const bool full = frame->color_range == AVCOL_RANGE_JPEG;
                const auto csc = avc_picture::resolve_csc(static_cast<avc_picture::Csc>(request.csc_coefficient), bt709, full);
                avc_picture::convert_yuv420(src.planes, extent.width, extent.height, request.pixel_type, csc, request.alpha,
                    data, static_cast<size_t>(pitch) * avc_picture::bytes_per_pixel(request.pixel_type));
            }
        }
    }

    width_out = frame->width;
    height_out = frame->height;
    pts_out = frame->pts;

    av_frame_free(&frame);
    return true;
}

void H264DecoderState::configure(void *options) {
    auto *opt = static_cast<H264DecoderOptions *>(options);

    pts = static_cast<uint64_t>(opt->pts_upper) << 32u | static_cast<uint64_t>(opt->pts_lower);
    dts = static_cast<uint64_t>(opt->dts_upper) << 32u | static_cast<uint64_t>(opt->dts_lower);
}

void H264DecoderState::set_res(const uint32_t width, const uint32_t height) {
    width_in = width;
    height_in = height;
}

void H264DecoderState::get_res(uint32_t &width, uint32_t &height) {
    width = width_out;
    height = height_out;
}

void H264DecoderState::get_pts(uint32_t &upper, uint32_t &lower) {
    upper = pts_out >> 32u;
    lower = pts_out & 0xFFFFFFFF;
}

void H264DecoderState::set_output_format(bool is_yuv_p3) {
    set_pixel_type(is_yuv_p3 ? avc_picture::kPixelYuv420Raster : avc_picture::kPixelYuv420PackedRaster);
}

void H264DecoderState::set_pixel_type(uint32_t type) {
    pixel_type = type;
    if (avc_picture::is_yuv(type))
        output_yuvp3 = type == avc_picture::kPixelYuv420Raster;
}

H264DecoderState::H264DecoderState(uint32_t width, uint32_t height, VideoCodecKind codec_kind)
    : kind(codec_kind) {
    const AVCodec *codec = avcodec_find_decoder(kind == VideoCodecKind::MPEG4 ? AV_CODEC_ID_MPEG4 : AV_CODEC_ID_H264);
    assert(codec);

    parser = av_parser_init(codec->id);
    assert(parser);
    parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;

    context = avcodec_alloc_context3(codec);
    assert(context);
    context->width = width;
    context->height = height;

    int result = avcodec_open2(context, codec, nullptr);
    assert(result == 0);
}

H264DecoderState::~H264DecoderState() {
    av_parser_close(parser);
}
