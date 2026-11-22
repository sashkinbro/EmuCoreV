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

// Pure (FFmpeg/emulator independent) wire format of the "video codecs" save-state section.
// Header-only so the host tests can round-trip it without building the emulator.
//
// Layout (little endian):
//   u8[4]  magic "VCOD"
//   u32    version (1)
//   u8     videodec_initialized   (sceVideodecInitLibrary* was called and not terminated)
//   u8     mjpeg_initialized      (sceJpegInitMJpeg was called and not finished)
//   u16    reserved (0)
//   u32    decoder_count
//   decoder_count x {
//       i32 handle, u32 width, u32 height, u8 output_yuvp3, u8 stopped, u16 reserved(0)
//   }
// Only creation parameters are stored. FFmpeg reference frames and parser state cannot be
// serialised: restored decoders start with fresh contexts and the game resynchronises at the
// next IDR frame.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace video_snapshot {

constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxDecoders = 1024;
// Same limits as sceAvcdecCreateDecoder.
constexpr uint32_t kMaxWidth = 1920;
constexpr uint32_t kMaxHeight = 1088;
constexpr uint32_t kMaxPixels = 1280 * 720;

struct AvcDecoderRecord {
    int32_t handle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool output_yuvp3 = false;
    bool stopped = true;

    bool operator==(const AvcDecoderRecord &) const = default;
};

struct Snapshot {
    bool videodec_initialized = false;
    bool mjpeg_initialized = false;
    std::vector<AvcDecoderRecord> decoders;

    bool operator==(const Snapshot &) const = default;
};

inline void put_u32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(v >> (i * 8)));
}

inline bool serialize(const Snapshot &snapshot, std::vector<uint8_t> &out, std::string &error) {
    error.clear();
    out.clear();
    if (snapshot.decoders.size() > kMaxDecoders) {
        error = "too many video decoder handles";
        return false;
    }
    out.insert(out.end(), { 'V', 'C', 'O', 'D' });
    put_u32(out, kVersion);
    out.push_back(snapshot.videodec_initialized ? 1 : 0);
    out.push_back(snapshot.mjpeg_initialized ? 1 : 0);
    out.push_back(0);
    out.push_back(0);
    put_u32(out, static_cast<uint32_t>(snapshot.decoders.size()));
    for (const auto &d : snapshot.decoders) {
        put_u32(out, static_cast<uint32_t>(d.handle));
        put_u32(out, d.width);
        put_u32(out, d.height);
        out.push_back(d.output_yuvp3 ? 1 : 0);
        out.push_back(d.stopped ? 1 : 0);
        out.push_back(0);
        out.push_back(0);
    }
    return true;
}

namespace detail {
struct Cursor {
    const std::vector<uint8_t> &data;
    size_t pos = 0;
    bool u8(uint8_t &v) {
        if (data.size() - pos < 1)
            return false;
        v = data[pos++];
        return true;
    }
    bool u32(uint32_t &v) {
        if (data.size() - pos < 4)
            return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<uint32_t>(data[pos + i]) << (i * 8);
        pos += 4;
        return true;
    }
};
} // namespace detail

// Strict parser: rejects bad magic/version, non-boolean flags, non-zero reserved bytes,
// out-of-range creation parameters, non-positive/duplicate handles and trailing bytes.
inline bool parse(const std::vector<uint8_t> &blob, Snapshot &out, std::string &error) {
    error.clear();
    out = {};
    detail::Cursor c{ blob };
    auto fail = [&](const char *msg) {
        error = msg;
        out = {};
        return false;
    };
    uint8_t magic[4] = {};
    for (auto &m : magic)
        if (!c.u8(m))
            return fail("truncated video codec snapshot");
    if (magic[0] != 'V' || magic[1] != 'C' || magic[2] != 'O' || magic[3] != 'D')
        return fail("bad video codec snapshot magic");
    uint32_t version = 0, count = 0;
    uint8_t vd = 0, mj = 0, r0 = 0, r1 = 0;
    if (!c.u32(version) || !c.u8(vd) || !c.u8(mj) || !c.u8(r0) || !c.u8(r1) || !c.u32(count))
        return fail("truncated video codec snapshot");
    if (version != kVersion)
        return fail("unsupported video codec snapshot version");
    if (vd > 1 || mj > 1 || r0 != 0 || r1 != 0)
        return fail("invalid video codec snapshot header");
    if (count > kMaxDecoders)
        return fail("too many video decoder handles");
    if (count != 0 && !vd)
        return fail("video decoders without initialised library");
    Snapshot result;
    result.videodec_initialized = vd != 0;
    result.mjpeg_initialized = mj != 0;
    result.decoders.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        AvcDecoderRecord d;
        uint32_t handle = 0;
        uint8_t yuvp3 = 0, stopped = 0, q0 = 0, q1 = 0;
        if (!c.u32(handle) || !c.u32(d.width) || !c.u32(d.height) || !c.u8(yuvp3) || !c.u8(stopped) || !c.u8(q0) || !c.u8(q1))
            return fail("truncated video codec snapshot");
        d.handle = static_cast<int32_t>(handle);
        if (d.handle <= 0 || yuvp3 > 1 || stopped > 1 || q0 != 0 || q1 != 0)
            return fail("invalid video decoder record");
        if (d.width > kMaxWidth || d.height > kMaxHeight || d.width * d.height > kMaxPixels)
            return fail("invalid video decoder dimensions");
        d.output_yuvp3 = yuvp3 != 0;
        d.stopped = stopped != 0;
        for (const auto &prev : result.decoders)
            if (prev.handle == d.handle)
                return fail("duplicate video decoder handle");
        result.decoders.push_back(d);
    }
    if (c.pos != blob.size())
        return fail("trailing bytes in video codec snapshot");
    out = std::move(result);
    return true;
}

} // namespace video_snapshot
