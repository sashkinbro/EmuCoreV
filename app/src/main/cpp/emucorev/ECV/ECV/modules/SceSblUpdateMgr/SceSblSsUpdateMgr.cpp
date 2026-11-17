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

#include "SceSblSsUpdateMgr.h"
#include <kernel/types.h>
#include <io/io.h>
#include <mem/functions.h>
#include <packages/firmware_info.h>

EXPORT(int, sceSblUsAllocateBuffer) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsCheckSystemIntegrity) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsExtractSpackage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsGetApplicableVersion) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsGetExtractSpackage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsGetSpkgInfo, int package_type, Ptr<SceSblUsSpkgInfo> info) {
    if (!info || info.address() < emuenv.mem.host_page_size || uint64_t{ info.address() } + sizeof(SceSblUsSpkgInfo) > UINT32_MAX
        || !is_valid_addr_range(emuenv.mem, info.address(), info.address() + sizeof(SceSblUsSpkgInfo)))
        return RET_ERROR(SCE_KERNEL_ERROR_ILLEGAL_ADDR);
    if (package_type <= 0 || package_type > 28)
        return RET_ERROR(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
    fs::ifstream input(firmware::package_version_path(emuenv.vita_fs_path, package_type));
    uint64_t version = 0;
    if (!(input >> std::hex >> version) || version > UINT32_MAX)
        return RET_ERROR(SCE_ERROR_ERRNO_ENOENT);
    input >> std::ws;
    if (!input.eof())
        return RET_ERROR(SCE_ERROR_ERRNO_ENOENT);
    // Report the installed component's own version, not the overall firmware
    // version. In particular, SA0/font packages have an independent version.
    *info.get(emuenv.mem) = SceSblUsSpkgInfo{ sizeof(SceSblUsSpkgInfo), static_cast<uint32_t>(version), {}, 0 };
    return 0;
}

EXPORT(int, sceSblUsGetStatus) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsGetUpdateMode) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsInformUpdateFinished) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsInformUpdateOngoing) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsInformUpdateStarted) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsInspectSpackage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsPowerControl) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsReleaseBuffer) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsSetSwInfoBin) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsSetSwInfoInt) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsSetSwInfoStr) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsSetUpdateMode) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsUpdateSpackage) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsVerifyPup) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsVerifyPupAdditionalSign) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsVerifyPupHeader) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsVerifyPupSegment) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsVerifyPupSegmentById) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceSblUsVerifyPupWatermark) {
    return UNIMPLEMENTED();
}
