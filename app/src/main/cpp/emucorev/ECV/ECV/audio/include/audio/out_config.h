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

// Pure validation helpers for the sceAudioOut configuration queries/setters. Values follow
// vita-headers psp2common/audioout.h (SceAudioOutConfigType, SceAudioOutAlcMode,
// SceAudioOutEffectType, port types). Return codes are SceAudioOutErrorCode values (as int).

namespace audio_out_config {

constexpr int ERR_INVALID_PORT = static_cast<int>(0x80260003);
constexpr int ERR_INVALID_PORT_TYPE = static_cast<int>(0x8026000A);
constexpr int ERR_INVALID_FX_TYPE = static_cast<int>(0x8026000B);
constexpr int ERR_INVALID_CONF_TYPE = static_cast<int>(0x8026000C);

constexpr int CONFIG_LEN = 0;
constexpr int CONFIG_FREQ = 1;
constexpr int CONFIG_MODE = 2;

constexpr int PORT_TYPE_BGM = 1;
constexpr int PORT_TYPE_VOICE = 2;

constexpr int ALC_MODE_MAX = 2; // SCE_AUDIO_ALC_MODE_MAX is not itself a valid mode
constexpr int EFFECT_TYPE_UNIQUE = 4; // last SceAudioOutEffectType

// Value of the requested property of an open port, or ERR_INVALID_CONF_TYPE.
constexpr int get_config(int config_type, int len, int freq, int mode) {
    switch (config_type) {
    case CONFIG_LEN:
        return len;
    case CONFIG_FREQ:
        return freq;
    case CONFIG_MODE:
        return mode;
    }
    return ERR_INVALID_CONF_TYPE;
}

// Provisional: the SDK only says "negative error" for a bad ALC mode / effect preset.
constexpr int check_alc_mode(int mode) { return (mode >= 0 && mode < ALC_MODE_MAX) ? 0 : ERR_INVALID_CONF_TYPE; }
constexpr int check_effect_type(int type) { return (type >= 0 && type <= EFFECT_TYPE_UNIQUE) ? 0 : ERR_INVALID_FX_TYPE; }
// The compressed profile only exists for BGM and voice ports.
constexpr int check_compress_port_type(int port_type) { return (port_type == PORT_TYPE_BGM || port_type == PORT_TYPE_VOICE) ? 0 : ERR_INVALID_PORT_TYPE; }

} // namespace audio_out_config
