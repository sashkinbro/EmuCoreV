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

#include "state_snapshot.h"
#include "video_snapshot.h"

#include <module/module.h>

#include <codec/avc_picture.h>
#include <codec/state.h>
#include <kernel/state.h>
#include <mem/functions.h>
#include <util/lock_and_find.h>

#include <util/tracy.h>
TRACY_MODULE_NAME(SceVideodecUser);

typedef std::shared_ptr<H264DecoderState> H264DecoderPtr;
typedef std::map<SceUID, H264DecoderPtr> H264DecoderStates;

struct VideodecState {
    std::mutex mutex;
    H264DecoderStates decoders;
    // Creation size of every decoder, kept for save states (the live decoder overwrites its own).
    std::map<SceUID, std::pair<uint32_t, uint32_t>> sizes;
};

// SceAvcdecErrorCode (vitasdk vita-headers psp2/videodec.h)
enum {
    SCE_AVCDEC_ERROR_INVALID_TYPE = 0x80620001,
    SCE_AVCDEC_ERROR_INVALID_PARAM = 0x80620002,
    SCE_AVCDEC_ERROR_OUT_OF_MEMORY = 0x80620003,
    SCE_AVCDEC_ERROR_INVALID_COLOR_FORMAT = 0x80620006,
    SCE_AVCDEC_ERROR_INVALID_POINTER = 0x80620009,
};

enum SceVideodecType {
    SCE_VIDEODEC_TYPE_HW_AVCDEC = 0x1001,
    SCE_VIDEODEC_TYPE_HW_M4VDEC = 0x1002,
};

struct SceVideodecTimeStamp {
    uint32_t upper;
    uint32_t lower;
};

enum SceCscCoefficient : uint8_t {
    SCE_AVCDEC_CSC_COEFFICIENT_DEFAULT = 0,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU601 = 1,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU709 = 2,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU601_FULL = 3,
    SCE_AVCDEC_CSC_COEFFICIENT_ITU709_FULL = 4,
};

struct SceAvcdecFrameOptionRGBA {
    uint8_t alpha;
    SceCscCoefficient cscCoefficient;
    uint8_t reserved[14];
};

union SceAvcdecFrameOption {
    uint8_t reserved[16];
    SceAvcdecFrameOptionRGBA rgba;
};

struct SceAvcdecQueryDecoderInfo {
    uint32_t horizontal;
    uint32_t vertical;
    uint32_t numOfRefFrames; //!< Number of reference frames
};

struct SceAvcdecDecoderInfo {
    uint32_t frameMemSize;
};

struct SceAvcdecInfo {
    uint32_t numUnitsInTick;
    uint32_t timeScale;
    uint8_t fixedFrameRateFlag;

    uint8_t aspectRatioIdc;
    uint16_t sarWidth;
    uint16_t sarHeight;

    uint8_t colourPrimaries;
    uint8_t transferCharacteristics;
    uint8_t matrixCoefficients;

    uint8_t videoFullRangeFlag;

    uint8_t picStruct[2];
    uint8_t ctType;

    SceVideodecTimeStamp pts;
};

struct SceAvcdecBuf {
    Ptr<void> pBuf;
    uint32_t size;
};

struct SceAvcdecCtrl {
    SceUID handle;
    SceAvcdecBuf frameBuf;
};

struct SceAvcdecAu {
    SceVideodecTimeStamp pts;
    SceVideodecTimeStamp dts;
    SceAvcdecBuf es;
};

enum ScePixelType : uint32_t {
    SCE_AVCDEC_PIXEL_RGBA8888 = 0x0U, // A:B:G:R=8:8:8:8    32bit   U8U8U8U8_ABGR
    SCE_AVCDEC_PIXEL_RGBA565 = 0x1U, // vita-headers SceAvcdecPixelFormat
    SCE_AVCDEC_PIXEL_RGBA5551 = 0x2U, // vita-headers SceAvcdecPixelFormat
    SCE_AVCDEC_PIXEL_BGRA8888 = 0x8U, // A:R:G:B=8:8:8:8    32bit   U8U8U8U8_ARGB
    SCE_AVCDEC_PIXEL_YUV420_RASTER = 0x10U, // YUV420P3
    SCE_AVCDEC_PIXEL_YUV420_PACKED_RASTER = 0x20U, // YVU420P2
};

struct SceAvcdecFrame {
    ScePixelType pixelType;
    uint32_t framePitch;
    uint32_t frameWidth;
    uint32_t frameHeight;

    uint32_t horizontalSize;
    uint32_t verticalSize;

    uint32_t frameCropLeftOffset;
    uint32_t frameCropRightOffset;
    uint32_t frameCropTopOffset;
    uint32_t frameCropBottomOffset;

    SceAvcdecFrameOption opt;

    Ptr<void> pPicture[2];
};

struct SceAvcdecPicture {
    uint32_t size;
    SceAvcdecFrame frame;
    SceAvcdecInfo info;
};

struct SceAvcdecArrayPicture {
    uint32_t numOfOutput;
    uint32_t numOfElm;
    Ptr<Ptr<SceAvcdecPicture>> pPicture;
};

static_assert(SCE_AVCDEC_PIXEL_RGBA8888 == avc_picture::kPixelRgba8888 && SCE_AVCDEC_PIXEL_RGBA565 == avc_picture::kPixelRgba565
    && SCE_AVCDEC_PIXEL_RGBA5551 == avc_picture::kPixelRgba5551 && SCE_AVCDEC_PIXEL_BGRA8888 == avc_picture::kPixelBgra8888
    && SCE_AVCDEC_PIXEL_YUV420_RASTER == avc_picture::kPixelYuv420Raster
    && SCE_AVCDEC_PIXEL_YUV420_PACKED_RASTER == avc_picture::kPixelYuv420PackedRaster);

static uint32_t codec_type_of(VideoCodecKind kind) {
    return kind == VideoCodecKind::MPEG4 ? SCE_VIDEODEC_TYPE_HW_M4VDEC : SCE_VIDEODEC_TYPE_HW_AVCDEC;
}

static bool query_in_range(const SceAvcdecQueryDecoderInfo *query) {
    return query->horizontal <= 1920 && query->vertical <= 1088 && query->horizontal * query->vertical <= 1280 * 720 && query->numOfRefFrames <= 16;
}

// Looks a handle up and checks that it belongs to the right codec family (AVC handles are not
// valid for the M4v exports and the other way round).
static H264DecoderPtr find_decoder(EmuEnvState &emuenv, const char *export_name, const SceAvcdecCtrl *decoder, VideoCodecKind kind, int &error) {
    error = 0;
    if (!decoder) {
        error = RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
        return nullptr;
    }
    if (decoder->handle == 0) {
        error = RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
        return nullptr;
    }
    const auto state = emuenv.kernel.obj_store.get_shared<VideodecState>();
    if (!state) {
        error = RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
        return nullptr;
    }
    H264DecoderPtr decoder_info = lock_and_find(decoder->handle, state->decoders, state->mutex);
    if (!decoder_info || decoder_info->kind != kind) {
        error = RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
        return nullptr;
    }
    return decoder_info;
}

static int create_decoder_impl(EmuEnvState &emuenv, const char *export_name, VideoCodecKind kind, uint32_t codec_type, SceAvcdecCtrl *decoder, const SceAvcdecQueryDecoderInfo *query) {
    if (codec_type != codec_type_of(kind))
        return RET_ERROR(kind == VideoCodecKind::MPEG4 ? SCE_AVCDEC_ERROR_INVALID_TYPE : SCE_AVCDEC_ERROR_INVALID_PARAM);

    if (!decoder || !query)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);

    if (!query_in_range(query))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    // Absent when the guest never called sceVideodecInitLibrary* in this process (e.g. after a
    // cold save-state restore of an older file) or after sceVideodecTermLibrary.
    const auto state = emuenv.kernel.obj_store.get_shared<VideodecState>();
    if (!state)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
    std::lock_guard<std::mutex> lock(state->mutex);

    auto new_decoder = std::make_shared<H264DecoderState>(query->horizontal, query->vertical, kind);
    if (!new_decoder->context || !new_decoder->parser)
        return RET_ERROR(SCE_AVCDEC_ERROR_OUT_OF_MEMORY);

    SceUID handle = emuenv.kernel.get_next_uid();
    decoder->handle = handle;

    state->decoders[handle] = std::move(new_decoder);
    state->sizes[handle] = { query->horizontal, query->vertical };

    return 0;
}

static int query_mem_size_impl(const char *export_name, VideoCodecKind kind, uint32_t codec_type, const SceAvcdecQueryDecoderInfo *query_info, SceAvcdecDecoderInfo *decoder_info) {
    if (codec_type != codec_type_of(kind))
        return RET_ERROR(kind == VideoCodecKind::MPEG4 ? SCE_AVCDEC_ERROR_INVALID_TYPE : SCE_AVCDEC_ERROR_INVALID_PARAM);

    if (!query_info || !decoder_info)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);

    if (!query_in_range(query_info))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    std::uint32_t numOfRefFrames = query_info->numOfRefFrames;
    if (numOfRefFrames == 0) {
        numOfRefFrames = 1;
    }

    decoder_info->frameMemSize = H264DecoderState::buffer_size({ { query_info->horizontal, query_info->vertical } }) * numOfRefFrames;
    decoder_info->frameMemSize = decoder_info->frameMemSize + 0x3ffffU & 0xfffc0000; // alignment to the next multiple of 0x40000 (256-KiB)

    return 0;
}

// Shared by sceAvcdecDecode, sceAvcdecDecodeNalAu, the WithWorkPicture variants and sceM4vdecDecode.
// nal_units: the access unit holds NAL units that may lack Annex-B start codes (they are added).
static int decode_impl(EmuEnvState &emuenv, const char *export_name, VideoCodecKind kind, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture, bool nal_units) {
    int error = 0;
    const H264DecoderPtr decoder_info = find_decoder(emuenv, export_name, decoder, kind, error);
    if (!decoder_info)
        return error;

    if (!au || !picture)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);

    H264DecoderOptions options = {};
    options.pts_upper = au->pts.upper;
    options.pts_lower = au->pts.lower;
    options.dts_upper = au->dts.upper;
    options.dts_lower = au->dts.lower;

    Ptr<SceAvcdecPicture> *picture_list = picture->pPicture.get(emuenv.mem);
    if (!picture_list)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
    SceAvcdecPicture *pPicture = picture_list[0].get(emuenv.mem);
    if (!pPicture)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
    SceAvcdecFrame &frame = pPicture->frame;

    if (!avc_picture::is_valid_pixel_type(frame.pixelType))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_COLOR_FORMAT);
    const uint32_t pitch = avc_picture::effective_pitch(frame.framePitch, frame.frameWidth);
    const uint64_t needed = avc_picture::picture_buffer_size(frame.pixelType, pitch, frame.frameHeight);
    if (frame.frameWidth == 0 || needed == 0)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
    if (avc_picture::is_rgb(frame.pixelType) && !avc_picture::is_valid_csc(frame.opt.rgba.cscCoefficient))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    uint8_t *output = frame.pPicture[0].cast<uint8_t>().get(emuenv.mem);
    const uint8_t *es_data = au->es.pBuf.cast<uint8_t>().get(emuenv.mem);
    if (!es_data || !output)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
    const uint64_t output_end = static_cast<uint64_t>(frame.pPicture[0].address()) + needed;
    if (output_end > 0xffffffffull || !is_valid_addr_range(emuenv.mem, frame.pPicture[0].address(), static_cast<Address>(output_end)))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);

    decoder_info->set_pixel_type(frame.pixelType);

    std::vector<uint8_t> annexb;
    uint32_t es_size = au->es.size;
    if (nal_units) {
        avc_picture::append_annexb(annexb, es_data, es_size);
        if (annexb.empty())
            return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
        es_data = annexb.data();
        es_size = static_cast<uint32_t>(annexb.size());
    }

    decoder_info->configure(&options);
    const auto send = decoder_info->send(es_data, es_size);
    decoder_info->set_res(frame.frameWidth, frame.frameHeight);
    H264PictureRequest request = {};
    request.pixel_type = frame.pixelType;
    request.pitch = pitch;
    request.width = frame.frameWidth;
    request.height = frame.frameHeight;
    request.alpha = frame.opt.rgba.alpha;
    request.csc_coefficient = static_cast<uint8_t>(frame.opt.rgba.cscCoefficient);
    const bool got_frame = send && decoder_info->receive_picture(output, request);
    if (got_frame) {
        decoder_info->get_res(frame.horizontalSize, frame.verticalSize);
        // FFmpeg already removed the cropped area: the whole picture is visible.
        frame.frameCropLeftOffset = 0;
        frame.frameCropRightOffset = 0;
        frame.frameCropTopOffset = 0;
        frame.frameCropBottomOffset = 0;
        decoder_info->get_pts(pPicture->info.pts.upper, pPicture->info.pts.lower);
        picture->numOfOutput++;
    }

    decoder_info->is_stopped = false;

    return 0;
}

static int decode_stop_impl(EmuEnvState &emuenv, const char *export_name, VideoCodecKind kind, SceAvcdecCtrl *decoder, SceAvcdecArrayPicture *picture) {
    int error = 0;
    const H264DecoderPtr decoder_info = find_decoder(emuenv, export_name, decoder, kind, error);
    if (!decoder_info)
        return error;

    if (!decoder_info->is_stopped) {
        if (!picture)
            return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
        Ptr<SceAvcdecPicture> *picture_list = picture->pPicture.get(emuenv.mem);
        SceAvcdecPicture *pPicture = picture_list ? picture_list[0].get(emuenv.mem) : nullptr;
        if (!pPicture)
            return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);

        // we get the values from the last frame, maybe we should slightly increase the pts value?
        decoder_info->get_res(pPicture->frame.horizontalSize, pPicture->frame.verticalSize);
        decoder_info->get_pts(pPicture->info.pts.upper, pPicture->info.pts.lower);

        picture->numOfOutput = 1;
    } else if (picture) {
        picture->numOfOutput = 0;
    }
    decoder_info->is_stopped = true;

    return 0;
}

static int decode_flush_impl(EmuEnvState &emuenv, const char *export_name, VideoCodecKind kind, SceAvcdecCtrl *decoder) {
    int error = 0;
    const H264DecoderPtr decoder_info = find_decoder(emuenv, export_name, decoder, kind, error);
    if (!decoder_info)
        return error;

    decoder_info->flush();
    decoder_info->is_stopped = true;

    return 0;
}

static int decode_available_size_impl(EmuEnvState &emuenv, const char *export_name, VideoCodecKind kind, SceAvcdecCtrl *decoder) {
    int error = 0;
    const H264DecoderPtr decoder_info = find_decoder(emuenv, export_name, decoder, kind, error);
    if (!decoder_info)
        return error;

    return H264DecoderState::buffer_size(
        { { decoder_info->get(DecoderQuery::WIDTH), decoder_info->get(DecoderQuery::HEIGHT) } });
}

static int delete_decoder_impl(EmuEnvState &emuenv, const char *export_name, VideoCodecKind kind, SceAvcdecCtrl *decoder) {
    int error = 0;
    if (!find_decoder(emuenv, export_name, decoder, kind, error))
        return error;
    const auto state = emuenv.kernel.obj_store.get_shared<VideodecState>();
    std::lock_guard<std::mutex> lock(state->mutex);
    state->decoders.erase(decoder->handle);
    state->sizes.erase(decoder->handle);

    return 0;
}

// Mode setters: the value is stored (and saved with the state) but FFmpeg has no equivalent
// switch, so it does not change decoding. PROVISIONAL: the (decoder, mode) signature and the
// mode range are undocumented (not in vita-headers), the value is not validated.
static int set_mode_impl(EmuEnvState &emuenv, const char *export_name, SceAvcdecCtrl *decoder, uint32_t mode, uint32_t H264DecoderState::*field) {
    int error = 0;
    const H264DecoderPtr decoder_info = find_decoder(emuenv, export_name, decoder, VideoCodecKind::H264, error);
    if (!decoder_info)
        return error;
    decoder_info.get()->*field = mode;
    return 0;
}

// YUV 4:2:0 picture -> RGB picture. PROVISIONAL signature: neither vita-headers nor the
// firmware exports were available, (src, dst) is assumed; the source is a decoded SceAvcdecPicture
// (YUV pixel type, optional frame crop offsets), the destination describes the RGB buffer
// (pixelType, framePitch/Width/Height, opt.rgba alpha and csc coefficient).
static int csc_impl(EmuEnvState &emuenv, const char *export_name, const SceAvcdecPicture *src, SceAvcdecPicture *dst) {
    if (!src || !dst)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
    const SceAvcdecFrame &sf = src->frame;
    const SceAvcdecFrame &df = dst->frame;
    if (!avc_picture::is_yuv(sf.pixelType) || !avc_picture::is_rgb(df.pixelType))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_COLOR_FORMAT);
    if (!avc_picture::is_valid_csc(df.opt.rgba.cscCoefficient))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    const uint32_t coded_w = sf.horizontalSize ? sf.horizontalSize : sf.frameWidth;
    const uint32_t coded_h = sf.verticalSize ? sf.verticalSize : sf.frameHeight;
    const uint32_t src_pitch = avc_picture::effective_pitch(sf.framePitch, sf.frameWidth ? sf.frameWidth : coded_w);
    const uint32_t src_rows = sf.frameHeight ? sf.frameHeight : coded_h;
    avc_picture::Rect visible;
    if (coded_w == 0 || coded_h == 0 || coded_w > src_pitch || coded_h > src_rows
        || !avc_picture::visible_rect(coded_w, coded_h, sf.frameCropLeftOffset, sf.frameCropRightOffset, sf.frameCropTopOffset, sf.frameCropBottomOffset, visible))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    avc_picture::YuvLayout layout;
    if (!avc_picture::yuv420_layout(sf.pixelType, src_pitch, src_rows, layout))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);
    const uint32_t dst_pitch = avc_picture::effective_pitch(df.framePitch, df.frameWidth);
    const uint64_t dst_size = avc_picture::picture_buffer_size(df.pixelType, dst_pitch, df.frameHeight);
    if (dst_size == 0 || df.frameWidth == 0)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_PARAM);

    const uint8_t *src_base = sf.pPicture[0].cast<uint8_t>().get(emuenv.mem);
    uint8_t *dst_base = df.pPicture[0].cast<uint8_t>().get(emuenv.mem);
    if (!src_base || !dst_base)
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);
    const uint64_t src_end = static_cast<uint64_t>(sf.pPicture[0].address()) + layout.total_size;
    const uint64_t dst_end = static_cast<uint64_t>(df.pPicture[0].address()) + dst_size;
    if (src_end > 0xffffffffull || dst_end > 0xffffffffull
        || !is_valid_addr_range(emuenv.mem, sf.pPicture[0].address(), static_cast<Address>(src_end))
        || !is_valid_addr_range(emuenv.mem, df.pPicture[0].address(), static_cast<Address>(dst_end)))
        return RET_ERROR(SCE_AVCDEC_ERROR_INVALID_POINTER);

    avc_picture::Yuv420Planes planes;
    planes.y_pitch = layout.luma_pitch;
    planes.y = src_base + static_cast<size_t>(visible.y) * layout.luma_pitch + visible.x;
    planes.chroma_pitch = layout.chroma_pitch;
    const size_t chroma_row = static_cast<size_t>(visible.y / 2) * layout.chroma_pitch;
    if (sf.pixelType == avc_picture::kPixelYuv420Raster) {
        planes.u = src_base + layout.chroma_offset + chroma_row + visible.x / 2;
        planes.v = src_base + layout.v_offset + chroma_row + visible.x / 2;
        planes.chroma_step = 1;
    } else {
        planes.u = src_base + layout.chroma_offset + chroma_row + visible.x;
        planes.v = planes.u + 1;
        planes.chroma_step = 2;
    }

    const bool bt709 = src->info.matrixCoefficients == 1;
    const bool full_range = src->info.videoFullRangeFlag != 0;
    const auto csc = avc_picture::resolve_csc(static_cast<avc_picture::Csc>(df.opt.rgba.cscCoefficient), bt709, full_range);
    const auto extent = avc_picture::copy_extent(visible.width, visible.height, df.frameWidth, df.frameHeight);
    avc_picture::convert_yuv420(planes, extent.width, extent.height, df.pixelType, csc, df.opt.rgba.alpha, dst_base,
        static_cast<size_t>(dst_pitch) * avc_picture::bytes_per_pixel(df.pixelType));
    return 0;
}

EXPORT(int, sceAvcdecCreateDecoder, uint32_t codec_type, SceAvcdecCtrl *decoder, const SceAvcdecQueryDecoderInfo *query) {
    TRACY_FUNC(sceAvcdecCreateDecoder, codec_type, decoder, query);
    return create_decoder_impl(emuenv, export_name, VideoCodecKind::H264, codec_type, decoder, query);
}

EXPORT(int, sceAvcdecCreateDecoderInternal) {
    TRACY_FUNC(sceAvcdecCreateDecoderInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecCreateDecoderNongameapp) {
    TRACY_FUNC(sceAvcdecCreateDecoderNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecCsc, const SceAvcdecPicture *src, SceAvcdecPicture *dst) {
    TRACY_FUNC(sceAvcdecCsc, src, dst);
    return csc_impl(emuenv, export_name, src, dst);
}

EXPORT(int, sceAvcdecCscInternal) {
    TRACY_FUNC(sceAvcdecCscInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecode, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceAvcdecDecode, decoder, au, picture);
    return decode_impl(emuenv, export_name, VideoCodecKind::H264, decoder, au, picture, false);
}

EXPORT(int, sceAvcdecDecodeAuInternal) {
    TRACY_FUNC(sceAvcdecDecodeAuInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAuNalAuInternal) {
    TRACY_FUNC(sceAvcdecDecodeAuNalAuInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAuNalAuNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeAuNalAuNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAuNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeAuNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeAvailableSize, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceAvcdecDecodeAvailableSize, decoder);
    return decode_available_size_impl(emuenv, export_name, VideoCodecKind::H264, decoder);
}

EXPORT(int, sceAvcdecDecodeFlush, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceAvcdecDecodeFlush, decoder);
    return decode_flush_impl(emuenv, export_name, VideoCodecKind::H264, decoder);
}

EXPORT(int, sceAvcdecDecodeGetPictureInternal) {
    TRACY_FUNC(sceAvcdecDecodeGetPictureInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeGetPictureNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeGetPictureNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeGetPictureWithWorkPictureInternal) {
    TRACY_FUNC(sceAvcdecDecodeGetPictureWithWorkPictureInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeNalAu, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceAvcdecDecodeNalAu, decoder, au, picture);
    return decode_impl(emuenv, export_name, VideoCodecKind::H264, decoder, au, picture, true);
}

EXPORT(int, sceAvcdecDecodeNalAuWithWorkPicture, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture, SceAvcdecArrayPicture *work_picture) {
    TRACY_FUNC(sceAvcdecDecodeNalAuWithWorkPicture, decoder, au, picture, work_picture);
    return decode_impl(emuenv, export_name, VideoCodecKind::H264, decoder, au, picture, true);
}

EXPORT(int, sceAvcdecDecodeSetTrickModeNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeSetTrickModeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeSetUserDataSei1FieldMemSizeNongameapp) {
    TRACY_FUNC(sceAvcdecDecodeSetUserDataSei1FieldMemSizeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecDecodeStop, SceAvcdecCtrl *decoder, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceAvcdecDecodeStop, decoder, picture);
    return decode_stop_impl(emuenv, export_name, VideoCodecKind::H264, decoder, picture);
}

EXPORT(int, sceAvcdecDecodeStopWithWorkPicture, SceAvcdecCtrl *decoder, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceAvcdecDecodeStopWithWorkPicture, decoder, picture);
    return decode_stop_impl(emuenv, export_name, VideoCodecKind::H264, decoder, picture);
}

EXPORT(int, sceAvcdecDecodeWithWorkPicture, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture, SceAvcdecArrayPicture *work_picture) {
    TRACY_FUNC(sceAvcdecDecodeWithWorkPicture, decoder, au, picture, work_picture);
    return decode_impl(emuenv, export_name, VideoCodecKind::H264, decoder, au, picture, false);
}

EXPORT(int, sceAvcdecDeleteDecoder, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceAvcdecDeleteDecoder, decoder);
    return delete_decoder_impl(emuenv, export_name, VideoCodecKind::H264, decoder);
}

EXPORT(int, sceAvcdecGetSeiPictureTimingInternal) {
    TRACY_FUNC(sceAvcdecGetSeiPictureTimingInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecGetSeiUserDataNongameapp) {
    TRACY_FUNC(sceAvcdecGetSeiUserDataNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecQueryDecoderMemSize, uint32_t codec_type, const SceAvcdecQueryDecoderInfo *query_info, SceAvcdecDecoderInfo *decoder_info) {
    TRACY_FUNC(sceAvcdecQueryDecoderMemSize, codec_type, query_info, decoder_info);
    return query_mem_size_impl(export_name, VideoCodecKind::H264, codec_type, query_info, decoder_info);
}

EXPORT(int, sceAvcdecQueryDecoderMemSizeInternal) {
    TRACY_FUNC(sceAvcdecQueryDecoderMemSizeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecQueryDecoderMemSizeNongameapp) {
    TRACY_FUNC(sceAvcdecQueryDecoderMemSizeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecRegisterCallbackInternal) {
    TRACY_FUNC(sceAvcdecRegisterCallbackInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecRegisterCallbackNongameapp) {
    TRACY_FUNC(sceAvcdecRegisterCallbackNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetDecodeMode, SceAvcdecCtrl *decoder, uint32_t mode) {
    TRACY_FUNC(sceAvcdecSetDecodeMode, decoder, mode);
    return set_mode_impl(emuenv, export_name, decoder, mode, &H264DecoderState::decode_mode);
}

EXPORT(int, sceAvcdecSetDecodeModeInternal) {
    TRACY_FUNC(sceAvcdecSetDecodeModeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetInterlacedStreamMode, SceAvcdecCtrl *decoder, uint32_t mode) {
    TRACY_FUNC(sceAvcdecSetInterlacedStreamMode, decoder, mode);
    return set_mode_impl(emuenv, export_name, decoder, mode, &H264DecoderState::interlaced_stream_mode);
}

EXPORT(int, sceAvcdecSetLowDelayModeNongameapp) {
    TRACY_FUNC(sceAvcdecSetLowDelayModeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecSetRecoveryPointSEIMode, SceAvcdecCtrl *decoder, uint32_t mode) {
    TRACY_FUNC(sceAvcdecSetRecoveryPointSEIMode, decoder, mode);
    return set_mode_impl(emuenv, export_name, decoder, mode, &H264DecoderState::recovery_point_sei_mode);
}

EXPORT(int, sceAvcdecUnregisterCallbackInternal) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackNongameapp) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackWithCbidInternal) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackWithCbidInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAvcdecUnregisterCallbackWithCbidNongameapp) {
    TRACY_FUNC(sceAvcdecUnregisterCallbackWithCbidNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceM4vdecCreateDecoder, uint32_t codec_type, SceAvcdecCtrl *decoder, const SceAvcdecQueryDecoderInfo *query) {
    TRACY_FUNC(sceM4vdecCreateDecoder, codec_type, decoder, query);
    return create_decoder_impl(emuenv, export_name, VideoCodecKind::MPEG4, codec_type, decoder, query);
}

EXPORT(int, sceM4vdecCreateDecoderInternal, uint32_t codec_type, SceAvcdecCtrl *decoder, const SceAvcdecQueryDecoderInfo *query) {
    TRACY_FUNC(sceM4vdecCreateDecoderInternal, codec_type, decoder, query);
    return create_decoder_impl(emuenv, export_name, VideoCodecKind::MPEG4, codec_type, decoder, query);
}

EXPORT(int, sceM4vdecCsc, const SceAvcdecPicture *src, SceAvcdecPicture *dst) {
    TRACY_FUNC(sceM4vdecCsc, src, dst);
    return csc_impl(emuenv, export_name, src, dst);
}

EXPORT(int, sceM4vdecDecode, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceM4vdecDecode, decoder, au, picture);
    return decode_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder, au, picture, false);
}

EXPORT(int, sceM4vdecDecodeAvailableSize, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceM4vdecDecodeAvailableSize, decoder);
    return decode_available_size_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder);
}

EXPORT(int, sceM4vdecDecodeFlush, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceM4vdecDecodeFlush, decoder);
    return decode_flush_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder);
}

EXPORT(int, sceM4vdecDecodeStop, SceAvcdecCtrl *decoder, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceM4vdecDecodeStop, decoder, picture);
    return decode_stop_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder, picture);
}

EXPORT(int, sceM4vdecDecodeStopWithWorkPicture, SceAvcdecCtrl *decoder, SceAvcdecArrayPicture *picture) {
    TRACY_FUNC(sceM4vdecDecodeStopWithWorkPicture, decoder, picture);
    return decode_stop_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder, picture);
}

EXPORT(int, sceM4vdecDecodeWithWorkPicture, SceAvcdecCtrl *decoder, const SceAvcdecAu *au, SceAvcdecArrayPicture *picture, SceAvcdecArrayPicture *work_picture) {
    TRACY_FUNC(sceM4vdecDecodeWithWorkPicture, decoder, au, picture, work_picture);
    return decode_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder, au, picture, false);
}

EXPORT(int, sceM4vdecDeleteDecoder, SceAvcdecCtrl *decoder) {
    TRACY_FUNC(sceM4vdecDeleteDecoder, decoder);
    return delete_decoder_impl(emuenv, export_name, VideoCodecKind::MPEG4, decoder);
}

EXPORT(int, sceM4vdecQueryDecoderMemSize, uint32_t codec_type, const SceAvcdecQueryDecoderInfo *query_info, SceAvcdecDecoderInfo *decoder_info) {
    TRACY_FUNC(sceM4vdecQueryDecoderMemSize, codec_type, query_info, decoder_info);
    return query_mem_size_impl(export_name, VideoCodecKind::MPEG4, codec_type, query_info, decoder_info);
}

EXPORT(int, sceM4vdecQueryDecoderMemSizeInternal, uint32_t codec_type, const SceAvcdecQueryDecoderInfo *query_info, SceAvcdecDecoderInfo *decoder_info) {
    TRACY_FUNC(sceM4vdecQueryDecoderMemSizeInternal, codec_type, query_info, decoder_info);
    return query_mem_size_impl(export_name, VideoCodecKind::MPEG4, codec_type, query_info, decoder_info);
}

EXPORT(int, sceVideodecInitLibrary) {
    TRACY_FUNC(sceVideodecInitLibrary);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryInternal) {
    TRACY_FUNC(sceVideodecInitLibraryInternal);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryNongameapp) {
    TRACY_FUNC(sceVideodecInitLibraryNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecInitLibraryWithUnmapMem) {
    TRACY_FUNC(sceVideodecInitLibraryWithUnmapMem);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryWithUnmapMemInternal) {
    TRACY_FUNC(sceVideodecInitLibraryWithUnmapMemInternal);
    emuenv.kernel.obj_store.create<VideodecState>();
    return 0;
}

EXPORT(int, sceVideodecInitLibraryWithUnmapMemNongameapp) {
    TRACY_FUNC(sceVideodecInitLibraryWithUnmapMemNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecQueryInstanceNongameapp) {
    TRACY_FUNC(sceVideodecQueryInstanceNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecQueryMemSize) {
    TRACY_FUNC(sceVideodecQueryMemSize);
    STUBBED("estimated size, real one is unknown");
    return 0x40000;
}

EXPORT(int, sceVideodecQueryMemSizeInternal) {
    TRACY_FUNC(sceVideodecQueryMemSizeInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecQueryMemSizeNongameapp) {
    TRACY_FUNC(sceVideodecQueryMemSizeNongameapp);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecSetConfig) {
    TRACY_FUNC(sceVideodecSetConfig);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecSetConfigInternal) {
    TRACY_FUNC(sceVideodecSetConfigInternal);
    return UNIMPLEMENTED();
}

EXPORT(int, sceVideodecTermLibrary) {
    TRACY_FUNC(sceVideodecTermLibrary);
    emuenv.kernel.obj_store.erase<VideodecState>();
    return 0;
}

namespace videocodec {
struct PreparedState {
    video_snapshot::Snapshot snapshot;
    H264DecoderStates decoders;
};

void clear_state(EmuEnvState &emuenv) {
    emuenv.kernel.obj_store.erase<VideodecState>();
    mjpegstate::set_initialized(emuenv, false);
}

bool capture_state(EmuEnvState &emuenv, std::vector<uint8_t> &blob, std::string &error) {
    video_snapshot::Snapshot snapshot;
    if (const auto state = emuenv.kernel.obj_store.get_shared<VideodecState>()) {
        const std::lock_guard<std::mutex> lock(state->mutex);
        snapshot.videodec_initialized = true;
        for (const auto &[handle, decoder] : state->decoders) {
            const auto size = state->sizes.find(handle);
            if (!decoder || size == state->sizes.end()) {
                error = "video decoder has no creation parameters";
                return false;
            }
            video_snapshot::AvcDecoderRecord record;
            record.handle = handle;
            record.width = size->second.first;
            record.height = size->second.second;
            record.output_yuvp3 = decoder->output_yuvp3;
            record.stopped = decoder->is_stopped;
            record.pixel_type = decoder->pixel_type;
            record.decode_mode = decoder->decode_mode;
            record.recovery_point_sei_mode = decoder->recovery_point_sei_mode;
            record.interlaced_stream_mode = decoder->interlaced_stream_mode;
            record.codec = decoder->kind == VideoCodecKind::MPEG4 ? 1 : 0;
            snapshot.decoders.push_back(record);
        }
    }
    snapshot.mjpeg_initialized = mjpegstate::is_initialized(emuenv);
    return video_snapshot::serialize(snapshot, blob, error);
}

bool prepare_state(const std::vector<uint8_t> &blob, std::shared_ptr<PreparedState> &prepared, std::string &error) {
    prepared.reset();
    auto candidate = std::make_shared<PreparedState>();
    if (!video_snapshot::parse(blob, candidate->snapshot, error))
        return false;
    // Fresh FFmpeg contexts: reference frames cannot be serialised, the game resyncs at the next IDR.
    for (const auto &record : candidate->snapshot.decoders) {
        auto decoder = std::make_shared<H264DecoderState>(record.width, record.height,
            record.codec == 1 ? VideoCodecKind::MPEG4 : VideoCodecKind::H264);
        if (!decoder->context || !decoder->parser) {
            error = "cannot create video decoder";
            return false;
        }
        decoder->set_output_format(record.output_yuvp3);
        decoder->set_pixel_type(record.pixel_type);
        decoder->decode_mode = record.decode_mode;
        decoder->recovery_point_sei_mode = record.recovery_point_sei_mode;
        decoder->interlaced_stream_mode = record.interlaced_stream_mode;
        decoder->is_stopped = record.stopped;
        candidate->decoders.emplace(record.handle, std::move(decoder));
    }
    prepared = std::move(candidate);
    return true;
}

std::vector<int32_t> decoder_handles(const PreparedState &prepared) {
    std::vector<int32_t> handles;
    handles.reserve(prepared.snapshot.decoders.size());
    for (const auto &record : prepared.snapshot.decoders)
        handles.push_back(record.handle);
    return handles;
}

void apply_state(EmuEnvState &emuenv, std::shared_ptr<PreparedState> prepared) {
    if (!prepared)
        return;
    mjpegstate::set_initialized(emuenv, prepared->snapshot.mjpeg_initialized);
    if (!prepared->snapshot.videodec_initialized) {
        emuenv.kernel.obj_store.erase<VideodecState>();
        return;
    }
    emuenv.kernel.obj_store.create<VideodecState>();
    const auto state = emuenv.kernel.obj_store.get_shared<VideodecState>();
    const std::lock_guard<std::mutex> lock(state->mutex);
    state->decoders = std::move(prepared->decoders);
    state->sizes.clear();
    for (const auto &record : prepared->snapshot.decoders)
        state->sizes[record.handle] = { record.width, record.height };
}

} // namespace videocodec
