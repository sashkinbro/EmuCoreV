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

#include <module/module.h>

#include <boost/describe/enum.hpp>
#include <cstdint>

struct SceAudiodecInfoAt9 {
    uint32_t config_data;
    uint32_t channels;
    uint32_t bit_rate;
    uint32_t sample_rate;
    uint32_t super_frame_size;
    uint32_t frames_in_super_frame;
};

struct SceAudiodecInfoMp3 {
    uint32_t channels;
    uint32_t version;
};

struct SceAudiodecInfoAac {
    uint32_t is_adts;
    uint32_t channels;
    uint32_t sample_rate;
    uint32_t is_sbr;
};

struct SceAudiodecInfoCelp {
    uint32_t excitation_mode;
    uint32_t sample_rate;
    uint32_t bit_rate;
    uint32_t lost_count;
};

struct SceAudiodecInfo {
    uint32_t size;
    union {
        SceAudiodecInfoAt9 at9;
        SceAudiodecInfoMp3 mp3;
        SceAudiodecInfoAac aac;
        SceAudiodecInfoCelp celp;
    };
};

struct SceAudiodecCtrl {
    uint32_t size;
    SceUID handle;
    Ptr<uint8_t> es_data;
    uint32_t es_size_used;
    uint32_t es_size_max;
    Ptr<uint8_t> pcm_data;
    uint32_t pcm_size_given;
    uint32_t pcm_size_max;
    uint32_t word_length;
    Ptr<SceAudiodecInfo> info;
};

static_assert(sizeof(SceAudiodecCtrl) == 0x28);

enum SceAudiodecCodec : uint32_t {
    SCE_AUDIODEC_TYPE_AT9 = 0x1003,
    SCE_AUDIODEC_TYPE_MP3 = 0x1004,
    SCE_AUDIODEC_TYPE_AAC = 0x1005,
    SCE_AUDIODEC_TYPE_CELP = 0x1006,
};
BOOST_DESCRIBE_ENUM(SceAudiodecCodec, SCE_AUDIODEC_TYPE_AT9, SCE_AUDIODEC_TYPE_MP3, SCE_AUDIODEC_TYPE_AAC, SCE_AUDIODEC_TYPE_CELP)

struct SceAudiodecInitStreamParam {
    SceUInt32 size;
    SceUInt32 totalStreams;
};

struct SceAudiodecInitChParam {
    SceUInt32 size;
    SceUInt32 totalCh;
};

union SceAudiodecInitParam {
    SceUInt32 size;
    SceAudiodecInitChParam at9;
    SceAudiodecInitStreamParam mp3;
    SceAudiodecInitStreamParam aac;
    SceAudiodecInitStreamParam celp;
};

DECL_EXPORT(SceInt32, sceAudiodecInitLibrary, SceAudiodecCodec codecType, SceAudiodecInitParam *pInitParam);
DECL_EXPORT(SceInt32, sceAudiodecTermLibrary, SceAudiodecCodec codecType);
DECL_EXPORT(int, sceAudiodecCreateDecoder, SceAudiodecCtrl *ctrl, SceAudiodecCodec codec);
DECL_EXPORT(int, sceAudiodecDecode, SceAudiodecCtrl *ctrl);
DECL_EXPORT(int, sceAudiodecDecodeNFrames, SceAudiodecCtrl *ctrl, SceUInt32 nFrames);
DECL_EXPORT(int, sceAudiodecPartlyDecode, SceAudiodecCtrl *ctrl, SceUInt32 samples_offset, SceUInt32 samples_to_decode);
DECL_EXPORT(int, sceAudiodecClearContext, SceAudiodecCtrl *ctrl);
DECL_EXPORT(int, sceAudiodecDeleteDecoder, SceAudiodecCtrl *ctrl);
