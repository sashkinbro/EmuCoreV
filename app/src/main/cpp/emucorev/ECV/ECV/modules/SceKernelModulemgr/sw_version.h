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

// Pure helpers for sceKernelGetSystemSwVersion: the SceKernelFwInfo layout and the system software
// version the emulator reports. No emulator state; used by SceModulemgr.cpp and the host tests.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sw_version {

// SceKernelFwInfo of the SDK (0x28 bytes).
struct FwInfo {
    std::uint32_t size;
    char version_string[0x1C];
    std::uint32_t version;
    std::uint32_t unk_24;
};
static_assert(sizeof(FwInfo) == 0x28, "FwInfo is an invalid size");

// PROVISIONAL: the emulator has no single firmware version of its own (the installed PUP's version is
// not kept after installation), so the last retail system software is reported: 3.74.
inline constexpr std::uint32_t MAJOR = 3;
inline constexpr std::uint32_t MINOR = 740;
inline constexpr std::uint32_t BUILD = 11;
inline constexpr char VERSION_STRING[] = "3.740.011";

// "3.740.011" is packed as 0x03740011: major, then the three-digit minor and build as BCD digits.
constexpr std::uint32_t pack(std::uint32_t major, std::uint32_t minor, std::uint32_t build) {
    const auto bcd = [](std::uint32_t v) {
        std::uint32_t r = 0;
        for (int shift = 0; v != 0; shift += 4, v /= 10)
            r |= (v % 10) << shift;
        return r;
    };
    // minor 740 -> 0x740 would overflow its byte, so the packed form keeps only its leading two digits.
    return (major << 24) | (((bcd(minor) >> 4) & 0xFF) << 16) | (bcd(build) & 0xFFFF);
}
inline constexpr std::uint32_t VERSION = pack(MAJOR, MINOR, BUILD);
static_assert(VERSION == 0x03740011, "system software version packing");

inline void fill(FwInfo &info) {
    std::memset(&info, 0, sizeof(info));
    info.size = sizeof(FwInfo);
    std::memcpy(info.version_string, VERSION_STRING, sizeof(VERSION_STRING));
    info.version = VERSION;
}

} // namespace sw_version
