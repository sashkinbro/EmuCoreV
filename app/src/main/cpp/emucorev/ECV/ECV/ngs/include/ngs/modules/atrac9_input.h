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

// Validation of guest-controlled ATRAC9 voice input. Header-only and free of kernel/memory
// dependencies so that the host tests can run it.

#include <cstdint>
#include <cstring>

namespace ngs {

// Buffer chain index read from guest memory: the index itself when it names one of the
// `count` buffer slots, otherwise -1 (end of the chain).
inline int32_t at9_valid_buffer_index(int32_t index, int32_t count) {
    return (index >= 0 && index < count) ? index : -1;
}

// Some games feed whole .at9 files, RIFF header included. Returns the offset of the first byte
// behind the "data" chunk header, or -1 when the header is truncated, a chunk size is negative or
// runs past `avail`, or there is no data chunk. Never reads more than `avail` bytes.
inline int64_t at9_riff_data_offset(const uint8_t *input, uint32_t avail) {
    if (avail < 12 || std::memcmp(input, "RIFF", 4) != 0 || std::memcmp(input + 8, "WAVE", 4) != 0)
        return -1;

    int64_t offset = 12;
    while (true) {
        // a chunk header is a 4-byte identifier followed by the chunk size (minus 8) in an int32
        if (static_cast<int64_t>(avail) - offset < 8)
            return -1;
        if (std::memcmp(input + offset, "data", 4) == 0)
            return offset + 8;

        int32_t chunk_size;
        std::memcpy(&chunk_size, input + offset + 4, sizeof(chunk_size));
        if (chunk_size < 0 || offset + 8 + static_cast<int64_t>(chunk_size) > static_cast<int64_t>(avail))
            return -1;
        offset += 8 + static_cast<int64_t>(chunk_size);
    }
}

} // namespace ngs
