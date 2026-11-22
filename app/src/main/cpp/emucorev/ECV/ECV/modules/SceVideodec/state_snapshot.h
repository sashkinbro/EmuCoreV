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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct EmuEnvState;

// Save-state support for the guest-initialised video codec libraries (Videodec/AVC and
// MJpeg). Cold restore skips guest init, so without this their object-store state would
// be missing. Wire format: see video_snapshot.h.
namespace videocodec {

struct PreparedState;

// Older files have no section: drop any live state so no stale decoders leak across timelines.
void clear_state(EmuEnvState &emuenv);

bool capture_state(EmuEnvState &emuenv, std::vector<uint8_t> &blob, std::string &error);
// Parses the blob and builds fresh host decoders without touching the running session.
bool prepare_state(const std::vector<uint8_t> &blob, std::shared_ptr<PreparedState> &prepared, std::string &error);
std::vector<int32_t> decoder_handles(const PreparedState &prepared);
void apply_state(EmuEnvState &emuenv, std::shared_ptr<PreparedState> prepared);

} // namespace videocodec

// Implemented by SceJpegUser.cpp: MJpeg keeps no state besides "initialised" (the decoder is
// re-configured on every call), so it is carried in the same section.
namespace mjpegstate {
bool is_initialized(EmuEnvState &emuenv);
void set_initialized(EmuEnvState &emuenv, bool initialized);
} // namespace mjpegstate
