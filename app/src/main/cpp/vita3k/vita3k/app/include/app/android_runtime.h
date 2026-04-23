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

struct EmuEnvState;
struct Config;

namespace app {

int get_overlay_display_mask(const Config &cfg);
void attach_runtime_emuenv(EmuEnvState *emuenv);
void set_controller_overlay_state(int overlay_mask, bool edit = false, bool reset = false);
void set_controller_overlay_scale(float scale);
void set_controller_overlay_opacity(int opacity);
void set_performance_overlay_state(bool enabled, int32_t detail, int32_t position);

} // namespace app
