#include <emucorev/savestate/ngs_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <ngs/modules/atrac9.h>
#include <ngs/modules/compressor.h>
#include <ngs/modules/player.h>
#include <ngs/system.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace emucorev::savestate {
namespace {
uint64_t aligned_size(uint64_t size) { return (size + 3) & ~uint64_t{3}; }

struct Pool {
    uint64_t begin;
    uint64_t end;
};

bool add_pool(std::vector<Pool> &pools, const MemoryImage &memory, uint64_t address, uint64_t size) {
    if (!address || address % 4 || !size || !memory.contains(address, size)) return false;
    const uint64_t end = address + size;
    for (const auto &pool : pools)
        if (address < pool.end && pool.begin < end) return false;
    pools.push_back({address, end});
    return true;
}

bool callback_valid(Address callback, const MemoryImage &memory) {
    // The low bit selects Thumb. user_data remains an arbitrary guest cookie.
    return !callback || memory.contains(callback & ~uint64_t{1}, 2);
}

bool definition_valid(const ngs::VoiceDefinition &definition) {
    return static_cast<uint32_t>(definition.type) < static_cast<uint32_t>(ngs::BussType::BUSS_MAX)
        && definition.output_count > 0 && definition.output_count <= ngs::MAX_OUTPUT_PORT;
}

bool audio_parameters_valid(const ngs::ModuleDataInfo &module, uint32_t id, bool processing, const MemoryImage &memory) {
    if (id != 0x5CE6 && id != 0x5CAA) return true;
    static_assert(sizeof(SceNgsPlayerStates) == sizeof(SceNgsAT9States));
    SceNgsPlayerStates state{};
    if (module.guest_state_data.size() != sizeof(state)) return false;
    std::memcpy(&state, module.guest_state_data.data(), sizeof(state));
    if (state.current_buffer < -1 || state.current_buffer >= 4 || state.current_byte_position_in_buffer < 0) return false;
    // Stopped/paused/bypassed voices can legitimately retain retired guest
    // buffers until the guest publishes new parameters before playing them.
    if (!processing || state.current_buffer == -1) return true;
    const auto check_buffers = [&](const auto &params) {
        if (params.channels < 0 || params.channels > 2 || !std::isfinite(params.playback_frequency)
            || params.playback_frequency < 0 || params.playback_frequency > 192000
            || !std::isfinite(params.playback_scalar) || params.playback_scalar < 0) return false;
        int index = state.current_buffer;
        std::set<int> visited;
        while (index != -1 && visited.insert(index).second) {
            if (index < 0 || index >= 4) return false;
            const auto &buffer = params.buffer_params[index];
            if (buffer.bytes_count < 0
                || (buffer.bytes_count > 0 && (!buffer.buffer || !memory.contains(buffer.buffer.address(), buffer.bytes_count)))) return false;
            // Empty buffers park/end decoding until the guest publishes more.
            if (buffer.bytes_count == 0) break;
            index = buffer.next_buffer_index;
        }
        return true;
    };
    const auto check = [&](const std::vector<uint8_t> &bytes) {
        if (id == 0x5CE6) {
            if (bytes.size() != sizeof(SceNgsPlayerParams)) return false;
            SceNgsPlayerParams params{};
            std::memcpy(&params, bytes.data(), sizeof(params));
            return check_buffers(params) && params.start_buffer >= -1 && params.start_buffer < 4
                && params.start_bytes >= 0 && (params.type == ParameterAudioTypePCM || params.type == ParameterAudioTypeADPCM);
        }
        if (id == 0x5CAA) {
            if (bytes.size() != sizeof(SceNgsAT9Params)) return false;
            SceNgsAT9Params params{};
            std::memcpy(&params, bytes.data(), sizeof(params));
            const uint32_t config = static_cast<uint32_t>(params.config_data);
            return check_buffers(params) && (!config || ((config & 0xff) == 0xfe && ((config >> 9) & 7) <= 5 && !(config & 0x100)));
        }
        return true;
    };
    // A locked live parameter buffer may be only partly edited by the guest;
    // processing reads last_info until unlock. An unlocked last_info can retain
    // retired buffers, so validate only the currently effective parameters.
    if (!check((module.flags & ngs::ModuleData::PARAMS_LOCK) ? module.last_info : module.parameters)) return false;
    return true;
}

bool module_valid(const ngs::ModuleDataInfo &data, const ngs::Module &module, bool output,
    uint32_t granularity, bool processing, const MemoryImage &memory) {
    const uint32_t id = module.module_id();
    const size_t parameters = module.get_buffer_parameter_size();
    const size_t guest = output ? static_cast<size_t>(granularity) * 4 : module.get_guest_state_size();
    if (data.parameters.size() != parameters || (data.flags & ~ngs::ModuleData::PARAMS_LOCK)
        || (!data.last_info.empty() && data.last_info.size() != parameters)
        || ((data.flags & ngs::ModuleData::PARAMS_LOCK) && data.last_info.size() != parameters)
        || !callback_valid(data.callback, memory)) return false;
    // Compressor's guest meters are allocated lazily by get_state, unlike its
    // always-created logical state. Other current modules have fixed/empty state.
    if (id == 0x5CE1) {
        if (!data.guest_state_data.empty() && data.guest_state_data.size() != sizeof(SceNgsCompressorStates)) return false;
    } else if (data.guest_state_data.size() != guest) return false;
    const bool logical = id == 0x5CE6 || id == 0x5CAA || id == 0x5CE3 || id == 0x5CE1;
    if (logical) {
        if (data.logical_state.size() < 9) return false;
        const auto &b = data.logical_state;
        const uint32_t saved_id = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
        if (saved_id != id || !ngs::validate_logical_state_blob(b)) return false;
    } else if (!data.logical_state.empty()) return false;
    return audio_parameters_valid(data, id, processing && !data.is_bypassed, memory);
}

struct RackValidation {
    const ngs::RackInitInfo *rack;
    std::map<uint32_t, const MemspaceBlockAllocator::Block *> allocated;
    std::set<uint32_t> reserved;
    uint32_t input_count = 1;
};

bool validate_blocks(const ngs::RackInitInfo &rack, RackValidation &result) {
    uint64_t cursor = 0;
    std::set<uint32_t> boundaries{0};
    std::vector<uint32_t> zero_offsets;
    for (const auto &block : rack.blocks) {
        if (!block.size) {
            // Old resize-before-insert dummies and zero-parameter allocations
            // consume no storage. The restore path must remove these records.
            if (block.free || block.offset % 4 || block.offset > rack.info.size) return false;
            zero_offsets.push_back(block.offset);
            continue;
        }
        if (block.offset != cursor || block.offset % 4 || uint64_t(block.offset) + block.size > rack.info.size
            || (!block.free && block.size % 4)) return false;
        if (!block.free) result.allocated.emplace(block.offset, &block);
        cursor += block.size;
        boundaries.insert(static_cast<uint32_t>(cursor));
    }
    return cursor == rack.info.size && std::ranges::all_of(zero_offsets, [&](uint32_t offset) { return boundaries.contains(offset); });
}
} // namespace

bool validate_ngs_state(const NgsSnapshot &snapshot, const MemoryImage &memory, std::string &error) {
    error.clear();
    const auto fail = [&](const char *message) { error = message; return false; };
    if (!snapshot.definitions && snapshot.systems.empty() && snapshot.racks.empty()) return true;
    std::vector<Pool> pools;
    constexpr size_t definition_count = static_cast<size_t>(ngs::BussType::BUSS_MAX);
    if (!add_pool(pools, memory, snapshot.definitions, definition_count * sizeof(ngs::VoiceDefinition)))
        return fail("invalid NGS definitions range");
    std::array<ngs::VoiceDefinition, definition_count> definitions{};
    if (!memory.read_bytes(snapshot.definitions, definitions.data(), sizeof(definitions), error)) return false;
    for (size_t i = 0; i < definitions.size(); ++i)
        if (!definition_valid(definitions[i]) || static_cast<size_t>(definitions[i].type) != i)
            return fail("invalid NGS builtin voice definition");

    std::map<Address, const ngs::SystemInitInfo *> systems;
    std::map<Address, std::vector<const ngs::RackInitInfo *>> system_racks;
    for (const auto &system : snapshot.systems) {
        const auto &p = system.params;
        if (system.address != system.memspace.address() || system.memspace_size < aligned_size(sizeof(ngs::System))
            || !add_pool(pools, memory, system.address, system.memspace_size) || !systems.emplace(system.address, &system).second
            || p.max_racks < 0 || p.max_racks > 4096 || p.max_voices <= 0 || p.max_voices > 65536
            || p.granularity <= 0 || p.granularity > 65536 || p.sample_rate <= 0 || p.sample_rate > 192000)
            return fail("invalid NGS system layout or parameters");
    }
    std::map<Address, RackValidation> racks;
    std::map<Address, const ngs::RackInitInfo *> voices;
    std::vector<Address> rack_definitions;
    uint64_t input_buffer_bytes = 0;
    for (const auto &rack : snapshot.racks) {
        const auto system = systems.find(rack.system_address);
        const auto &d = rack.description;
        if (system == systems.end() || rack.system.address() != rack.system_address || rack.address != rack.info.data.address()
            || !add_pool(pools, memory, rack.address, rack.info.size) || racks.contains(rack.address)
            || d.voice_count <= 0 || d.voice_count > 4096 || rack.voices.size() != static_cast<size_t>(d.voice_count)
            || d.channels_per_voice < 0 || d.channels_per_voice > 2 || d.max_patches_per_input < 0 || d.max_patches_per_input > 4096
            || d.patches_per_output < 0 || d.patches_per_output > 4096 || !d.definition || d.definition.address() % 4)
            return fail("invalid NGS rack association or layout");
        ngs::VoiceDefinition definition{};
        if (!memory.read_bytes(d.definition.address(), &definition, sizeof(definition), error)) return false;
        if (!definition_valid(definition)) return fail("unknown NGS rack voice definition");
        rack_definitions.push_back(d.definition.address());
        std::vector<std::unique_ptr<ngs::Module>> modules;
        ngs::apply_voice_definition(&definition, modules); // Factory only; no decoder/runtime or guest objects.
        if (modules.empty()) return fail("empty NGS module factory");
        auto [it, inserted] = racks.emplace(rack.address, RackValidation{&rack, {}, {}, 1});
        auto &validated = it->second;
        if (!validate_blocks(rack, validated)) return fail("invalid NGS allocator partition");
        validated.input_count = std::max<size_t>(1, std::ranges::count_if(modules, [](const auto &module) {
            return module->module_id() == ngs::input_mixer_module_id;
        }));
        input_buffer_bytes += uint64_t(system->second->params.granularity) * 8 * validated.input_count * rack.voices.size();
        if (input_buffer_bytes > (256ull << 20)) return fail("NGS restored mixer inputs exceed bounded capacity");
        uint64_t cursor = 0;
        const auto reserve = [&](uint64_t size) {
            if (!size) return true;
            size = aligned_size(size);
            const auto block = validated.allocated.find(static_cast<uint32_t>(cursor));
            if (cursor + size > rack.info.size || block == validated.allocated.end() || block->second->size != size) return false;
            validated.reserved.insert(static_cast<uint32_t>(cursor));
            cursor += size;
            return true;
        };
        if (!reserve(sizeof(ngs::Rack))) return fail("NGS rack object is not allocated");
        for (const auto &voice : rack.voices) {
            if (uint64_t(rack.address) + cursor != voice.address || !voices.emplace(voice.address, &rack).second
                || !reserve(sizeof(ngs::Voice)) || voice.state > ngs::VOICE_STATE_UNLOADING
                || !callback_valid(voice.finished_callback, memory) || voice.patches.size() != ngs::MAX_OUTPUT_PORT
                || voice.modules.size() != modules.size()) return fail("invalid NGS voice layout");
            for (const auto &row : voice.implicit_volume_matrix)
                for (float value : row) if (!std::isfinite(value)) return fail("invalid NGS voice volume");
            for (const auto &port : voice.patches)
                if (port.size() != static_cast<size_t>(d.patches_per_output)) return fail("invalid NGS voice patch slots");
            for (size_t m = 0; m < modules.size(); ++m) {
                const auto &data = voice.modules[m];
                const uint64_t address = uint64_t(rack.address) + cursor;
                if (!module_valid(data, *modules[m], definition.type == ngs::BussType::BUSS_MASTER && m == 1,
                        system->second->params.granularity,
                        (voice.state == ngs::VOICE_STATE_ACTIVE || voice.state == ngs::VOICE_STATE_FINALIZING) && !voice.is_paused, memory)
                    || !reserve(modules[m]->get_buffer_parameter_size()) || !reserve(modules[m]->get_buffer_parameter_size()))
                    return fail("invalid NGS module state or parameter allocation");
                if (!data.parameters.empty()) {
                    std::vector<uint8_t> saved(data.parameters.size());
                    if (!memory.read_bytes(address, saved.data(), saved.size(), error)) return false;
                    if (saved != data.parameters) return fail("NGS parameters disagree with saved guest bytes");
                }
            }
        }
        system_racks[rack.system_address].push_back(&rack);
    }

    for (Address definition : rack_definitions) {
        // init_system/init_rack replace objects in these pools before the
        // definition is used. Custom definitions must survive reconstruction.
        for (size_t i = 1; i < pools.size(); ++i)
            if (uint64_t(definition) < pools[i].end && pools[i].begin < uint64_t(definition) + sizeof(ngs::VoiceDefinition))
                return fail("NGS voice definition aliases a reconstructed pool");
    }

    std::set<Address> patches;
    for (const auto &rack : snapshot.racks) {
        auto &validated = racks.at(rack.address);
        for (const auto &voice : rack.voices)
            for (size_t port = 0; port < voice.patches.size(); ++port)
                for (size_t slot = 0; slot < voice.patches[port].size(); ++slot) {
                    const Address address = voice.patches[port][slot];
                    if (!address) continue;
                    if (address < rack.address || address % 4 || !patches.insert(address).second) return fail("invalid NGS patch address");
                    const uint32_t offset = address - rack.address;
                    const auto allocation = validated.allocated.find(offset);
                    if (allocation == validated.allocated.end() || allocation->second->size != aligned_size(sizeof(ngs::Patch))
                        || validated.reserved.contains(offset)) return fail("NGS patch is not owned allocated storage");
                    ngs::Patch patch{};
                    if (!memory.read_bytes(address, &patch, sizeof(patch), error)) return false;
                    // Removed patches retain their slot allocation and stale destination.
                    if (patch.output_sub_index == -1) continue;
                    const auto dest = voices.find(patch.dest.address());
                    if (patch.source.address() != voice.address || patch.output_index != static_cast<int32_t>(port)
                        || patch.output_sub_index != static_cast<int32_t>(slot) || dest == voices.end()
                        || dest->second->system_address != rack.system_address || patch.dest_index < 0
                        || static_cast<uint32_t>(patch.dest_index) >= racks.at(dest->second->address).input_count)
                        return fail("invalid NGS patch routing");
                    for (const auto &row : patch.volume_matrix)
                        for (float value : row) if (!std::isfinite(value)) return fail("invalid NGS patch volume");
                }
    }
    for (const auto &system : snapshot.systems) {
        const auto &created = system_racks[system.address];
        std::set<std::pair<uint32_t, uint32_t>> queued;
        for (const auto &entry : system.queued_voices) {
            // init_system currently resizes null slots, then init_rack pushes.
            if (entry.first < static_cast<uint32_t>(system.params.max_racks)) return fail("NGS queue points to a null rack slot");
            const uint64_t index = uint64_t(entry.first) - system.params.max_racks;
            if (index >= created.size() || entry.second >= created[index]->voices.size() || !queued.insert(entry).second)
                return fail("invalid NGS scheduler queue reference");
        }
    }
    return true;
}
} // namespace emucorev::savestate
