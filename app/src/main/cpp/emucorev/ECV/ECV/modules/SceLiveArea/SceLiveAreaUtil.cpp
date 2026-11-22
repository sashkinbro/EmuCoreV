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

#include <module/module.h>

EXPORT(int, sceLiveAreaGetFrameRevision) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceLiveAreaGetFrameUserData) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceLiveAreaGetRevision) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceLiveAreaGetStatus) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceLiveAreaReplaceAllAsync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceLiveAreaReplaceAllSync) {
    return UNIMPLEMENTED();
}

EXPORT(int, sceLiveAreaUpdateFrameAsync) {
    return UNIMPLEMENTED();
}

// The LiveArea frame update (page content replaced by the running title) has no counterpart in the
// emulator: LiveArea is never displayed while a title runs, so the update is accepted and not shown.
EXPORT(int, sceLiveAreaUpdateFrameSync) {
    LOG_WARN_ONCE("{}: the LiveArea frame update is accepted but not displayed", export_name);
    return 0;
}
