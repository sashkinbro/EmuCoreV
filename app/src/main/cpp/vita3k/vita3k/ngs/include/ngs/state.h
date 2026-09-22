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

#include <mem/mempool.h>
#include <mem/ptr.h>
#include <ngs/types.h>

#include <utility>
#include <vector>

struct MemState;

namespace ngs {
struct VoiceDefinition;
struct System;

struct SystemInitInfo {
    Address address = 0;
    Ptr<void> memspace;
    uint32_t memspace_size = 0;
    SceNgsSystemInitParams params{};
    /// Active voices of the system's scheduler, as (rack index, voice index).
    std::vector<std::pair<uint32_t, uint32_t>> queued_voices;
};

struct ModuleDataInfo {
    std::vector<uint8_t> guest_state_data;
    std::vector<uint8_t> parameters;
    std::vector<uint8_t> last_info;
    std::vector<uint8_t> logical_state;
    bool is_bypassed = false;
    uint8_t flags = 0;
};

struct VoiceInfo {
    Address address = 0;
    uint32_t state = 0;
    bool is_pending = false;
    bool is_paused = false;
    bool is_keyed_off = false;
    uint32_t frame_count = 0;
    float implicit_volume_matrix[2][2] = { { 1.0f, 0.0f }, { 0.0f, 1.0f } };
    Address finished_callback = 0;
    Address finished_callback_user_data = 0;
    /// Patch addresses per output port (guest Patch objects).
    std::vector<std::vector<Address>> patches;
    std::vector<ModuleDataInfo> modules;
};

struct RackInitInfo {
    Address address = 0;
    Address system_address = 0;
    Ptr<System> system;
    SceNgsBufferInfo info{};
    SceNgsRackDescription description{};
    /// Host-side mempool allocator state, so restored patches stay allocated.
    std::vector<MemspaceBlockAllocator::Block> blocks;
    std::vector<VoiceInfo> voices;
};

struct State {
    Ptr<VoiceDefinition> definitions;
    std::vector<System *> systems;
    std::vector<SystemInitInfo> system_infos;
    std::vector<RackInitInfo> rack_infos;
};

bool init(State &ngs, MemState &mem);
void deinit(State &ngs, MemState &mem);

void capture_state(State &ngs, const MemState &mem, Ptr<VoiceDefinition> &definitions, std::vector<SystemInitInfo> &system_infos, std::vector<RackInitInfo> &rack_infos);
void restore_state(State &ngs, const MemState &mem, Ptr<VoiceDefinition> definitions, const std::vector<SystemInitInfo> &system_infos, const std::vector<RackInitInfo> &rack_infos);
} // namespace ngs
