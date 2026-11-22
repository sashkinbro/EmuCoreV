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

// Pure logic of one sceAppUtilSaveDataDataSave file item, independent of the emulator I/O layer
// so it can be unit tested. The file is opened once (without truncation), the buffer is written
// at `offset`, and in FILE_TRUNCATE mode the file is cut to offset + size afterwards.

#include <cstdint>

namespace app_util {

enum class SaveWriteMode {
    File,         // write at offset, keep the rest of the file
    FileTruncate, // write at offset, then truncate the file to offset + size
};

enum class SaveWriteResult {
    Ok,
    BadParameter, // a non-empty write without a source buffer
    IoError,      // open/seek/write/truncate failed or wrote short
};

// Ops must provide:
//   bool open();                                     open for writing, create if missing, no truncation
//   bool seek(std::uint64_t offset);                 absolute seek
//   bool write(const void *data, std::uint64_t size) must write all bytes
//   bool truncate(std::uint64_t length);
//   void close();                                    called once if open() succeeded
// A null buffer is accepted only in FileTruncate mode (pure resize to offset + size).
template <typename Ops>
SaveWriteResult save_data_write(Ops &ops, SaveWriteMode mode, const void *buf, std::uint64_t size, std::uint64_t offset) {
    if (!buf && size != 0 && mode != SaveWriteMode::FileTruncate)
        return SaveWriteResult::BadParameter;

    if (!ops.open())
        return SaveWriteResult::IoError;

    SaveWriteResult result = SaveWriteResult::Ok;
    if (buf && size != 0) {
        if (!ops.seek(offset) || !ops.write(buf, size))
            result = SaveWriteResult::IoError;
    }
    if (result == SaveWriteResult::Ok && mode == SaveWriteMode::FileTruncate && !ops.truncate(offset + size))
        result = SaveWriteResult::IoError;

    ops.close();
    return result;
}

} // namespace app_util
