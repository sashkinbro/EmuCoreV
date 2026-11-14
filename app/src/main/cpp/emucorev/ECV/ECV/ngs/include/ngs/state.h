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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct MemState;

namespace ngs {
struct VoiceDefinition;
struct System;
struct Rack;
struct Voice;

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
    Address callback = 0;
    Address user_data = 0;
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
    /// Runtime identity distinguishes a Rack from a later placement-new reuse
    /// of the same guest buffer while a release waiter is asleep.
    uint64_t instance_id = 0;
    Address system_address = 0;
    Ptr<System> system;
    SceNgsBufferInfo info{};
    SceNgsRackDescription description{};
    /// Host-side mempool allocator state, so restored patches stay allocated.
    std::vector<MemspaceBlockAllocator::Block> blocks;
    std::vector<VoiceInfo> voices;
};

inline bool rack_buffer_ranges_overlap(Address first_address, uint32_t first_size, Address second_address, uint32_t second_size) {
    const uint64_t first_begin = first_address;
    const uint64_t second_begin = second_address;
    const uint64_t first_end = first_begin + first_size;
    const uint64_t second_end = second_begin + second_size;
    return first_begin < second_end && second_begin < first_end;
}

struct RackBufferRange {
    Address address = 0;
    uint32_t size = 0;
};

// Runtime-only integrity metadata. Rack is placement-constructed in guest
// memory, so its host std::vector control block can be overwritten by guest
// writes. Keep a byte-for-byte copy outside guest memory to detect that before
// capture follows the vector's backing pointer.
struct RackVectorIntegrity {
    Address address = 0;
    Address system_address = 0;
    uint32_t buffer_size = 0;
    Rack *object = nullptr;
    uint32_t voice_count = 0;
    uint32_t voice_capacity = 0;
    uintptr_t voice_data = 0;
    uint32_t rack_object_reservation = 0;
    uint32_t voice_vector_offset = 0;
    uint32_t allocated_bytes = 0;
    uint32_t allocation_high_water = 0;
    std::array<std::byte, sizeof(std::vector<Ptr<Voice>>)> vector_object{};

    void capture(Rack *rack, Address rack_address, uint32_t rack_buffer_size, Address owner_system_address, const std::vector<Ptr<Voice>> &voices,
        uint32_t object_reservation, uint32_t vector_offset, uint32_t total_allocated_bytes, uint32_t allocation_end) {
        address = rack_address;
        system_address = owner_system_address;
        buffer_size = rack_buffer_size;
        object = rack;
        voice_count = static_cast<uint32_t>(voices.size());
        voice_capacity = static_cast<uint32_t>(voices.capacity());
        voice_data = reinterpret_cast<uintptr_t>(voices.data());
        rack_object_reservation = object_reservation;
        voice_vector_offset = vector_offset;
        allocated_bytes = total_allocated_bytes;
        allocation_high_water = allocation_end;
        std::memcpy(vector_object.data(), &voices, sizeof(voices));
    }

    bool matches(const Rack *candidate, const std::vector<Ptr<Voice>> &voices) const {
        return candidate == object
            && std::memcmp(vector_object.data(), &voices, sizeof(voices)) == 0;
    }
};

struct State {
    Ptr<VoiceDefinition> definitions;
    std::vector<System *> systems;
    std::vector<SystemInitInfo> system_infos;
    mutable std::mutex rack_infos_mutex;
    std::vector<RackInitInfo> rack_infos;
    std::vector<RackVectorIntegrity> rack_vector_integrity;
    uint64_t next_rack_instance_id = 1;

    std::optional<uint64_t> rack_instance_id(Address address) const {
        const std::lock_guard lock(rack_infos_mutex);
        const auto info = std::find_if(rack_infos.begin(), rack_infos.end(), [address](const RackInitInfo &entry) {
            return entry.address == address;
        });
        if (info == rack_infos.end())
            return std::nullopt;
        return info->instance_id;
    }

    bool rack_address_in_use(Address address) const {
        const std::lock_guard lock(rack_infos_mutex);
        return std::any_of(rack_infos.begin(), rack_infos.end(), [address](const RackInitInfo &info) {
            return info.address == address;
        });
    }

    std::optional<RackBufferRange> overlapping_rack_buffer(Address address, uint32_t size) const {
        const std::lock_guard lock(rack_infos_mutex);
        const auto info = std::find_if(rack_infos.begin(), rack_infos.end(), [address, size](const RackInitInfo &entry) {
            return rack_buffer_ranges_overlap(address, size, entry.address, entry.info.size);
        });
        if (info == rack_infos.end())
            return std::nullopt;
        return RackBufferRange{info->address, info->info.size};
    }
};

bool init(State &ngs, MemState &mem);
void deinit(State &ngs, MemState &mem);

// Requires the guest world to be stopped. Returns false rather than waiting on a
// scheduler inside an unfinished update/callback; outputs remain unchanged.
bool capture_state(State &ngs, const MemState &mem, Ptr<VoiceDefinition> &definitions, std::vector<SystemInitInfo> &system_infos, std::vector<RackInitInfo> &rack_infos, std::string *error = nullptr);
// Pure preflight for the versioned module payload; does not construct decoders,
// access guest memory, or change the live mixer. Empty means a stateless module.
bool validate_logical_state_blob(const std::vector<uint8_t> &blob);
bool restore_state(State &ngs, const MemState &mem, Ptr<VoiceDefinition> definitions, const std::vector<SystemInitInfo> &system_infos, const std::vector<RackInitInfo> &rack_infos);
} // namespace ngs
