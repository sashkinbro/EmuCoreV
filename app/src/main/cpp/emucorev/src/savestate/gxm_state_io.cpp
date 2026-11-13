#include <emucorev/savestate/gxm_state_io.h>
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <gxm/functions.h>
#include <renderer/vulkan/context_snapshot.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <set>
#include <unordered_map>

namespace emucorev::savestate {
namespace {
constexpr uint32_t gxm_program_source_magic = 0x53505847; // "GXPS"
constexpr uint32_t gxm_program_source_version = 1;
constexpr uint32_t max_gxm_program_source_size = 16u * 1024u * 1024u;
constexpr uint64_t max_gxm_program_source_total = 32ull * 1024u * 1024u;
constexpr uint64_t max_gxm_section_size = 64ull * 1024u * 1024u;

template <typename T>
T checked_struct(BufferReader &reader, std::initializer_list<size_t> bools, bool &malformed,
    std::initializer_list<size_t> reserved = {}) {
    std::array<uint8_t, sizeof(T)> raw{};
    T value{};
    if (!reader.bytes(raw.data(), raw.size())) return value;
    for (const size_t offset : reserved) raw[offset] = 0;
    for (const size_t offset : bools) if (raw[offset] > 1) { malformed = true; return value; }
    std::memcpy(&value, raw.data(), raw.size());
    return value;
}
renderer::GxmRecordState read_record(BufferReader &reader, bool &bad) {
    using T = renderer::GxmRecordState;
    return checked_struct<T>(reader, {offsetof(T, is_maskupdate), offsetof(T, is_gamma_corrected), offsetof(T, viewport_flat)}, bad);
}
GxmContextState read_context(BufferReader &reader, bool &bad) {
    using T = GxmContextState;
    // Engine 15 saved this unused, previously uninitialized frontend field.
    // It is reserved, so normalize raw bytes before forming any bool value.
    return checked_struct<T>(reader, {offsetof(T, visibility_enable), offsetof(T, visibility_is_increment), offsetof(T, active)}, bad,
        {offsetof(T, writing_mask)});
}
DisplayCallback read_display(BufferReader &reader, bool &bad) {
    return checked_struct<DisplayCallback>(reader, {offsetof(DisplayCallback, frame_predicted)}, bad);
}
template <typename T>
bool read_array(BufferReader &reader, std::vector<T> &out, uint32_t limit) {
    const uint32_t count = reader.u32();
    if (!reader.ok() || count > limit || count > reader.remaining() / sizeof(T)) return false;
    out.resize(count);
    return reader.bytes(out.data(), out.size() * sizeof(T));
}
}
bool parse_gxm_state(const std::vector<uint8_t> &data, GxmSnapshot &output, std::string &error) {
    if (data.size() > 64u * 1024u * 1024u) { error = "GXM section exceeds limit"; return false; }
    GxmSnapshot parsed;
    bool malformed_bool = false;
    BufferReader reader(data.data(), data.size());

    const SceGxmInitializeParams params = reader.value<SceGxmInitializeParams>();
    const uint32_t global_timestamp = reader.u32();
    const uint32_t last_display_global = reader.u32();
    const Address notification_region = reader.u32();
    const Address last_immediate_context = reader.u32();
    parsed.renderer_context_address = reader.u32();
    parsed.display_queue_thread = reader.i32();
    parsed.display_worker_phase = reader.u32();
    parsed.display_previous_entry = reader.u32();
    if (!reader.ok() || parsed.display_worker_phase > 5) {
        error = "invalid GXM display worker header";
        return false;
    }

    const uint32_t context_count = reader.u32();
    if (!reader.ok() || context_count > 4096) {
        error = "invalid GXM context table";
        return false;
    }
    std::vector<gxm::ContextSnapshot> contexts(context_count);
    for (gxm::ContextSnapshot &context : contexts) {
        context.address = reader.u32();
        context.deferred = reader.boolean();
        context.render_target_address = reader.u32();
        context.renderer_record = read_record(reader, malformed_bool);
        context.state = read_context(reader, malformed_bool);
        context.last_precomputed = reader.boolean();
        context.command_next_free_pos = reader.u64();
        context.alloc_space = Ptr<void>(reader.u32());
        context.alloc_space_end = Ptr<void>(reader.u32());
        context.command_allocator_size = reader.u32();
        context.alloc_space_start = reader.u32();
        context.pending_first = reader.u32(); context.pending_last = reader.u32();
        context.was_vert_default_uniform_reserved = reader.boolean();
        context.was_frag_default_uniform_reserved = reader.boolean();
        const uint32_t list_count = reader.u32();
        if (!reader.ok() || list_count > 65536 || list_count > reader.remaining() / 25) { error = "invalid deferred GXM list table"; return false; }
        context.deferred_lists.resize(list_count);
        for (auto &list : context.deferred_lists) {
            list.guest_address = reader.u32(); list.identity = reader.u64(); list.first = reader.u32(); list.last = reader.u32();
            list.current = reader.boolean();
            const uint32_t ranges = reader.u32();
            if (!reader.ok() || ranges > 65536 || ranges > reader.remaining() / 8) { error = "invalid deferred GXM ranges"; return false; }
            list.ranges.reserve(ranges);
            for (uint32_t i = 0; i < ranges; ++i) {
                const Address start = reader.u32(), end = reader.u32();
                list.ranges.emplace_back(start, end);
            }
        }
    }

    const uint32_t command_count = reader.u32();
    if (!reader.ok() || command_count > 262144 || command_count > reader.remaining() / 161) { error = "invalid pending GXM commands"; return false; }
    parsed.commands.resize(command_count);
    for (auto &command : parsed.commands) {
        command.opcode = static_cast<renderer::CommandOpcode>(reader.u8());
        command.deferred_allocation = reader.boolean(); command.next = reader.u32();
        command.completion_id = reader.u64();
        reader.bytes(command.data.data(), command.data.size());
        command.render_target_address = reader.u32();
        command.has_color_surface = reader.boolean(); command.has_depth_surface = reader.boolean();
        if (command.has_color_surface) command.color_surface = reader.value<SceGxmColorSurface>();
        if (command.has_depth_surface) command.depth_surface = reader.value<SceGxmDepthStencilSurface>();
        command.transfer_color_key_value = reader.u32(); command.transfer_color_key_mask = reader.u32();
        command.transfer_color_key_mode = reader.u32(); command.transfer_src_type = reader.u32();
        command.transfer_dst_type = reader.u32(); command.transfer_fill_color = reader.u32();
        const auto read_image = [&] {
            renderer::TransferImageSnapshot image;
            image.format = reader.u32(); image.address = reader.u32(); image.x = reader.u32(); image.y = reader.u32();
            image.width = reader.u32(); image.height = reader.u32(); image.stride = reader.i32(); return image;
        };
        command.transfer_src = read_image(); command.transfer_dst = read_image();
        command.has_display_frame = reader.boolean();
        command.display_frame.base = reader.u32(); command.display_frame.pitch = reader.u32(); command.display_frame.pixelformat = reader.u32();
        command.display_frame.width = reader.i32(); command.display_frame.height = reader.i32();
        command.new_frame_context_address = reader.u32();
        const uint32_t filter_size = reader.u32();
        if (!reader.ok() || filter_size > 4096 || filter_size > reader.remaining()) { error = "invalid pending screen filter"; return false; }
        command.screen_filter.resize(filter_size); reader.bytes(command.screen_filter.data(), filter_size);
        if (!renderer::validate_command_snapshot(command, error)) return false;
    }

    const uint32_t batch_count = reader.u32();
    if (!reader.ok() || batch_count > 65536 || batch_count > reader.remaining() / 12) { error = "invalid queued renderer batches"; return false; }
    parsed.batches.resize(batch_count);
    for (auto &batch : parsed.batches) { batch.context_address = reader.u32(); batch.first = reader.u32(); batch.last = reader.u32(); }

    const uint32_t finish_count = reader.u32();
    if (!reader.ok() || finish_count > 65536 || finish_count > reader.remaining() / 26) {
        error = "invalid renderer Finish table"; return false;
    }
    parsed.finish_operations.resize(finish_count);
    for (auto &finish : parsed.finish_operations) {
        finish.id = reader.u64(); finish.owner = reader.i32(); finish.context_address = reader.u32();
        finish.phase = static_cast<renderer::FinishPhase>(reader.u32());
        finish.fence_done = reader.boolean(); finish.drain_done = reader.boolean(); finish.result = reader.i32();
    }

    const uint32_t sync_count = reader.u32();
    if (!reader.ok() || sync_count > 65536) {
        error = "invalid GXM sync object table";
        return false;
    }
    std::vector<gxm::SyncObjectSnapshot> sync_objects(sync_count);
    for (gxm::SyncObjectSnapshot &sync : sync_objects) {
        sync.address = reader.u32();
        sync.timestamp_current = reader.u32();
        sync.timestamp_ahead = reader.u32();
        sync.last_display = reader.u32();
        sync.last_operation_global = reader.u32();
    }

    const uint32_t render_target_count = reader.u32();
    if (!reader.ok() || render_target_count > 4096) {
        error = "invalid GXM render target table";
        return false;
    }
    std::vector<gxm::RenderTargetSnapshot> render_targets(render_target_count);
    for (gxm::RenderTargetSnapshot &render_target : render_targets) {
        render_target.address = reader.u32();
        render_target.width = reader.u16();
        render_target.height = reader.u16();
        render_target.scenes_per_frame = reader.u16();
        render_target.driver_mem_block = reader.i32();
        render_target.params = reader.value<SceGxmRenderTargetParams>();
    }

    const uint32_t fragment_program_count = reader.u32();
    if (!reader.ok() || fragment_program_count > 65536) {
        error = "invalid GXM fragment program table";
        return false;
    }
    std::vector<gxm::FragmentProgramSnapshot> fragment_programs(fragment_program_count);
    for (gxm::FragmentProgramSnapshot &program : fragment_programs) {
        program.address = reader.u32();
        program.program = Ptr<const SceGxmProgram>(reader.u32());
        program.has_blend = reader.boolean();
        program.blend = reader.value<SceGxmBlendInfo>();
        program.is_mask_update = reader.boolean();
        program.reference_count = reader.u32();
    }

    const uint32_t vertex_program_count = reader.u32();
    if (!reader.ok() || vertex_program_count > 65536) {
        error = "invalid GXM vertex program table";
        return false;
    }
    std::vector<gxm::VertexProgramSnapshot> vertex_programs(vertex_program_count);
    for (gxm::VertexProgramSnapshot &program : vertex_programs) {
        program.address = reader.u32();
        program.program = Ptr<const SceGxmProgram>(reader.u32());
        program.key_hash = reader.u64();
        program.reference_count = reader.u32();
        if (!read_array(reader, program.attributes, 256)) { error = "invalid vertex attributes"; return false; }
        if (!read_array(reader, program.streams, SCE_GXM_MAX_VERTEX_STREAMS)) { error = "invalid vertex streams"; return false; }
    }

    const uint32_t shader_patcher_count = reader.u32();
    if (!reader.ok() || shader_patcher_count > 65536) {
        error = "invalid GXM shader patcher table";
        return false;
    }
    std::vector<gxm::ShaderPatcherSnapshot> shader_patchers(shader_patcher_count);
    for (gxm::ShaderPatcherSnapshot &patcher : shader_patchers) {
        patcher.address = reader.u32();
        patcher.params = reader.value<SceGxmShaderPatcherParams>();
    }

    const uint32_t display_entry_count = reader.u32();
    if (!reader.ok() || display_entry_count > 16) {
        error = "invalid GXM display queue";
        return false;
    }
    parsed.display_entries.clear();
    parsed.display_entries.reserve(display_entry_count);
    for (uint32_t i = 0; i < display_entry_count; i++)
        parsed.display_entries.push_back(read_display(reader, malformed_bool));

    const uint32_t submission_count = reader.u32();
    if (!reader.ok() || submission_count > 65536 || submission_count > reader.remaining() / 46) { error = "invalid pending display submissions"; return false; }
    std::set<SceUID> producers;
    for (uint32_t i = 0; i < submission_count; ++i) {
        const SceUID producer = reader.i32();
        PendingDisplaySubmission submission;
        submission.callback.data = reader.u32();
        submission.callback.old_sync = Ptr<SceGxmSyncObject>(reader.u32());
        submission.callback.new_sync = Ptr<SceGxmSyncObject>(reader.u32());
        submission.callback.old_sync_timestamp = reader.u32(); submission.callback.new_sync_timestamp = reader.u32();
        submission.callback.frame_predicted = reader.boolean(); submission.has_prediction = reader.boolean();
        submission.prediction.base = Ptr<const void>(reader.u32()); submission.prediction.pitch = reader.u32();
        submission.prediction.pixelformat = reader.u32(); submission.prediction.image_size.x = reader.i32(); submission.prediction.image_size.y = reader.i32();
        if (producer <= 0 || !producers.insert(producer).second) { error = "invalid pending display producer UID"; return false; }
        parsed.display_submissions.emplace_back(producer, std::move(submission));
    }

    std::map<Address, MemoryMapInfo> regions;
    const uint32_t region_count = reader.u32();
    if (!reader.ok() || region_count > 65536 || region_count > reader.remaining() / 16) { error = "invalid GPU memory regions"; return false; }
    for (uint32_t i = 0; i < region_count && reader.ok(); i++) {
        const Address address = reader.u32();
        MemoryMapInfo info{};
        info.offset = reader.u32();
        info.size = reader.u32();
        info.perm = reader.u32();
        if (!regions.emplace(address, info).second) { error = "duplicate GPU memory region"; return false; }
    }

    if (reader.remaining()) {
        const uint32_t magic = reader.u32();
        const uint32_t version = reader.u32();
        const uint32_t fragment_count = reader.u32();
        const uint32_t vertex_count = reader.u32();
        const uint32_t source_count = reader.u32();
        if (!reader.ok() || magic != gxm_program_source_magic || version != gxm_program_source_version
            || fragment_count != fragment_programs.size() || vertex_count != vertex_programs.size()
            || uint64_t(fragment_count) + vertex_count > 131072 || source_count > uint64_t(fragment_count) + vertex_count
            || source_count > 131072) {
            error = "invalid GXM program source extension header";
            return false;
        }
        std::map<Address, size_t> fragment_indices, vertex_indices;
        std::set<Address> all_program_addresses;
        for (size_t i = 0; i < fragment_programs.size(); ++i)
            if (!all_program_addresses.insert(fragment_programs[i].address).second
                || !fragment_indices.emplace(fragment_programs[i].address, i).second) {
                error = "duplicate saved fragment program address";
                return false;
            }
        for (size_t i = 0; i < vertex_programs.size(); ++i)
            if (!all_program_addresses.insert(vertex_programs[i].address).second
                || !vertex_indices.emplace(vertex_programs[i].address, i).second) {
                error = "duplicate saved vertex program address";
                return false;
            }
        std::vector<uint32_t> fragment_source_indices(fragment_programs.size(), UINT32_MAX);
        std::vector<uint32_t> vertex_source_indices(vertex_programs.size(), UINT32_MAX);
        std::vector<bool> source_used(source_count, false);
        for (uint32_t i = 0; i < fragment_count; ++i) {
            const Address address = reader.u32();
            const uint32_t source_index = reader.u32();
            const auto found = fragment_indices.find(address);
            if (!reader.ok() || found == fragment_indices.end() || source_index >= source_count
                || fragment_source_indices[found->second] != UINT32_MAX) {
                error = "invalid GXM fragment source reference";
                return false;
            }
            fragment_source_indices[found->second] = source_index;
            source_used[source_index] = true;
        }
        for (uint32_t i = 0; i < vertex_count; ++i) {
            const Address address = reader.u32();
            const uint32_t source_index = reader.u32();
            const auto found = vertex_indices.find(address);
            if (!reader.ok() || found == vertex_indices.end() || source_index >= source_count
                || vertex_source_indices[found->second] != UINT32_MAX) {
                error = "invalid GXM vertex source reference";
                return false;
            }
            vertex_source_indices[found->second] = source_index;
            source_used[source_index] = true;
        }
        if (std::find(fragment_source_indices.begin(), fragment_source_indices.end(), UINT32_MAX) != fragment_source_indices.end()
            || std::find(vertex_source_indices.begin(), vertex_source_indices.end(), UINT32_MAX) != vertex_source_indices.end()) {
            error = "missing GXM program source reference";
            return false;
        }
        if (std::find(source_used.begin(), source_used.end(), false) != source_used.end()) {
            error = "unreferenced GXM program source blob";
            return false;
        }

        std::vector<std::shared_ptr<const std::vector<uint8_t>>> sources;
        sources.reserve(source_count);
        std::unordered_map<uint64_t, std::vector<uint32_t>> source_hashes;
        uint64_t total_source_bytes = 0;
        for (uint32_t i = 0; i < source_count; ++i) {
            const uint32_t size = reader.u32();
            if (!reader.ok() || !size || size > max_gxm_program_source_size
                || size > reader.remaining() || size > max_gxm_program_source_total - total_source_bytes) {
                error = "invalid GXM fragment source size";
                return false;
            }
            auto source = std::make_shared<std::vector<uint8_t>>(size);
            if (!reader.bytes(source->data(), size)) {
                error = "truncated GXM fragment source";
                return false;
            }
            uint64_t hash = 1469598103934665603ull;
            for (const uint8_t byte : *source) {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            const auto candidates = source_hashes.find(hash);
            if (candidates != source_hashes.end()) {
                for (const uint32_t candidate : candidates->second)
                    if (*sources[candidate] == *source) {
                        error = "duplicate GXM program source blob";
                        return false;
                    }
            }
            total_source_bytes += size;
            source_hashes[hash].push_back(i);
            sources.push_back(std::move(source));
        }
        for (size_t i = 0; i < fragment_programs.size(); ++i)
            fragment_programs[i].program_source = sources[fragment_source_indices[i]];
        for (size_t i = 0; i < vertex_programs.size(); ++i)
            vertex_programs[i].program_source = sources[vertex_source_indices[i]];
    }

    if (malformed_bool || !reader.ok() || reader.remaining() != 0) {
        error = "invalid GXM state";
        return false;
    }

    parsed.params = std::move(params);
    parsed.global_timestamp = std::move(global_timestamp);
    parsed.last_display_global = std::move(last_display_global);
    parsed.notification_region = std::move(notification_region);
    parsed.last_immediate_context = std::move(last_immediate_context);
    parsed.contexts = std::move(contexts);
    parsed.sync_objects = std::move(sync_objects);
    parsed.render_targets = std::move(render_targets);
    parsed.fragment_programs = std::move(fragment_programs);
    parsed.vertex_programs = std::move(vertex_programs);
    parsed.shader_patchers = std::move(shader_patchers);
    parsed.regions = std::move(regions);
    output = std::move(parsed);
    return true;
}

bool append_gxm_program_source_extension(BufferWriter &writer,
    const std::vector<gxm::FragmentProgramSnapshot> &fragments,
    const std::vector<gxm::VertexProgramSnapshot> &vertices, std::string &error) {
    if (fragments.size() > 65536 || vertices.size() > 65536
        || writer.size() > max_gxm_section_size) {
        error = "GXM program source extension exceeds section limit";
        return false;
    }

    std::vector<std::shared_ptr<const std::vector<uint8_t>>> sources;
    std::vector<uint32_t> indices;
    indices.reserve(fragments.size() + vertices.size());
    std::unordered_map<uint64_t, std::vector<uint32_t>> by_hash;
    std::set<Address> fragment_addresses, vertex_addresses, all_program_addresses;
    uint64_t total_source_bytes = 0;
    uint64_t encoded_size = 20 + uint64_t(fragments.size() + vertices.size()) * 8;
    const auto hash_source = [](const std::vector<uint8_t> &bytes) {
        uint64_t hash = 1469598103934665603ull;
        for (const uint8_t byte : bytes) {
            hash ^= byte;
            hash *= 1099511628211ull;
        }
        return hash;
    };
    const auto append_source = [&](Address address, const auto &program, auto &addresses, const char *kind) {
        if (!addresses.insert(address).second || !all_program_addresses.insert(address).second) {
            error = std::string("duplicate saved ") + kind + " program address";
            return false;
        }
        if (!program.program_source || program.program_source->empty()
            || program.program_source->size() > max_gxm_program_source_size) {
            error = std::string("missing or invalid saved ") + kind + " source copy";
            return false;
        }
        const uint64_t hash = hash_source(*program.program_source);
        uint32_t source_index = UINT32_MAX;
        const auto candidates = by_hash.find(hash);
        if (candidates != by_hash.end()) {
            for (const uint32_t candidate : candidates->second) {
                if (*sources[candidate] == *program.program_source) {
                    source_index = candidate;
                    break;
                }
            }
        }
        if (source_index == UINT32_MAX) {
            const uint64_t size = program.program_source->size();
            if (size > max_gxm_program_source_total - total_source_bytes) {
                error = "GXM fragment source copies exceed save-state limit";
                return false;
            }
            source_index = static_cast<uint32_t>(sources.size());
            sources.push_back(program.program_source);
            by_hash[hash].push_back(source_index);
            total_source_bytes += size;
            encoded_size += 4 + size;
        }
        indices.push_back(source_index);
        return true;
    };
    for (const auto &program : fragments)
        if (!append_source(program.address, program, fragment_addresses, "fragment")) return false;
    for (const auto &program : vertices)
        if (!append_source(program.address, program, vertex_addresses, "vertex")) return false;
    if (encoded_size > max_gxm_section_size - writer.size()) {
        error = "GXM program source extension exceeds section limit";
        return false;
    }

    writer.u32(gxm_program_source_magic);
    writer.u32(gxm_program_source_version);
    writer.u32(static_cast<uint32_t>(fragments.size()));
    writer.u32(static_cast<uint32_t>(vertices.size()));
    writer.u32(static_cast<uint32_t>(sources.size()));
    for (size_t i = 0; i < fragments.size(); ++i) {
        writer.u32(fragments[i].address);
        writer.u32(indices[i]);
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        writer.u32(vertices[i].address);
        writer.u32(indices[fragments.size() + i]);
    }
    for (const auto &source : sources) {
        writer.u32(static_cast<uint32_t>(source->size()));
        writer.append(source->data(), source->size());
    }
    return true;
}

namespace {
bool validate_program(Address address, bool fragment, const MemoryImage &memory, std::string &error,
    const std::shared_ptr<const std::vector<uint8_t>> &source = {}) {
    const auto read_at = [&](uint64_t offset, void *destination, size_t size) {
        if (source) {
            if (offset > source->size() || size > source->size() - offset) {
                error = "saved GXP source copy is truncated";
                return false;
            }
            std::memcpy(destination, source->data() + offset, size);
            return true;
        }
        return memory.read_bytes(uint64_t(address) + offset, destination, size, error);
    };
    SceGxmProgram program{};
    if (!address || (address & 3) || !memory.contains(address, 1)
        || !read_at(0, &program, sizeof(program))
        || program.magic != 0x00505847 || program.size < sizeof(program) || program.size > max_gxm_program_source_size
        || program.is_fragment() != fragment
        || (source ? source->size() != program.size : !memory.contains(address, program.size))) {
        error = "invalid saved GXP program header"; return false;
    }
    const auto relative = [&](size_t field, uint32_t offset, uint64_t count, size_t element) {
        const uint64_t begin = uint64_t(field) + offset;
        return begin <= program.size && count <= (program.size - begin) / element;
    };
#define TABLE(field, count, type) if (!relative(offsetof(SceGxmProgram, field), program.field, program.count, sizeof(type))) { error = "saved GXP table exceeds program bounds"; return false; }
    TABLE(primary_program_offset, primary_program_instr_count, uint64_t)
    TABLE(secondary_program_offset, secondary_program_instr_count, uint64_t)
    TABLE(parameters_offset, parameter_count, SceGxmProgramParameter)
    TABLE(literals_offset, literals_count, SceGxmProgramLiteral)
    TABLE(uniform_buffer_offset, uniform_buffer_count, SceGxmUniformBufferInfo)
    TABLE(dependent_sampler_offset, dependent_sampler_count, SceGxmDependentSampler)
    TABLE(texture_buffer_dependent_sampler_offset, texture_buffer_dependent_sampler_count, SceGxmDependentSampler)
    TABLE(container_offset, container_count, SceGxmProgramParameterContainer)
    TABLE(literal_buffer_data_offset, literal_buffer_count, uint32_t)
#undef TABLE
    if (program.secondary_program_offset || program.secondary_program_offset_end || program.secondary_program_instr_count) {
        const uint64_t begin = offsetof(SceGxmProgram, secondary_program_offset) + uint64_t(program.secondary_program_offset);
        const uint64_t end = offsetof(SceGxmProgram, secondary_program_offset_end) + uint64_t(program.secondary_program_offset_end);
        if (begin > end || end > program.size || ((end - begin) & 7)) {
            error = "saved GXP secondary phase exceeds program bounds"; return false;
        }
    }
    if (!program.varyings_offset || !relative(offsetof(SceGxmProgram, varyings_offset), program.varyings_offset, 1, sizeof(SceGxmProgramVertexVaryings))) {
        error = "saved GXP varyings exceed program bounds"; return false;
    }
    SceGxmProgramVertexVaryings varyings{};
    const uint64_t varying_begin = offsetof(SceGxmProgram, varyings_offset) + uint64_t(program.varyings_offset);
    if (!read_at(varying_begin, &varyings, sizeof(varyings))) return false;
    if (fragment) {
        const uint64_t descriptors = varying_begin + offsetof(SceGxmProgramVertexVaryings, vertex_outputs1) + uint64_t(varyings.vertex_outputs1);
        if ((varyings.varyings_count && !varyings.vertex_outputs1) || descriptors > program.size
            || varyings.varyings_count > (program.size - descriptors) / sizeof(SceGxmProgramAttributeDescriptor)
            || varyings.output_param_type > SCE_GXM_PARAMETER_TYPE_AGGREGATE || varyings.output_comp_count > 4) {
            error = "invalid saved fragment varyings"; return false;
        }
        for (uint32_t i = 0; i < varyings.varyings_count; ++i) {
            SceGxmProgramAttributeDescriptor descriptor{};
            if (!read_at(descriptors + i * sizeof(descriptor), &descriptor, sizeof(descriptor))) return false;
            if ((descriptor.attribute_info & 0x40F) != 0xF && descriptor.resource_index >= SCE_GXM_MAX_TEXTURE_UNITS) {
                error = "invalid saved GXP sampler index"; return false;
            }
        }
    }
    for (uint32_t i = 0; i < program.parameter_count; ++i) {
        SceGxmProgramParameter parameter{};
        const uint64_t begin = offsetof(SceGxmProgram, parameters_offset) + uint64_t(program.parameters_offset) + i * sizeof(parameter);
        if (!read_at(begin, &parameter, sizeof(parameter))) return false;
        const int64_t name = int64_t(begin) + parameter.name_offset;
        if (name < 0 || uint64_t(name) >= program.size || parameter.type > SCE_GXM_PARAMETER_TYPE_AGGREGATE || parameter.category > SCE_GXM_PARAMETER_CATEGORY_UNIFORM_BUFFER
            || (parameter.category == SCE_GXM_PARAMETER_CATEGORY_SAMPLER && parameter.resource_index >= SCE_GXM_MAX_TEXTURE_UNITS)) {
            error = "invalid saved GXP parameter"; return false;
        }
        bool terminated = false;
        std::array<char, 256> bytes{};
        for (uint64_t pos = uint64_t(name); pos < program.size && pos - uint64_t(name) < 4096;) {
            const size_t count = std::min<uint64_t>(bytes.size(), program.size - pos);
            if (!read_at(pos, bytes.data(), count)) return false;
            if (std::memchr(bytes.data(), 0, count)) { terminated = true; break; }
            pos += count;
        }
        if (!terminated) { error = "unterminated saved GXP parameter name"; return false; }
    }
    for (uint32_t i = 0; i < program.dependent_sampler_count; ++i) {
        SceGxmDependentSampler sampler{};
        const uint64_t begin = offsetof(SceGxmProgram, dependent_sampler_offset) + uint64_t(program.dependent_sampler_offset) + i * sizeof(sampler);
        if (!read_at(begin, &sampler, sizeof(sampler))) return false;
        if (sampler.resource_index_layout_offset / 4 >= SCE_GXM_MAX_TEXTURE_UNITS) { error = "invalid GXP dependent sampler"; return false; }
    }
    return true;
}

bool matrix_attribute_valid(const SceGxmVertexAttribute &attribute, Address address,
    const MemoryImage &memory, std::string &error,
    const std::shared_ptr<const std::vector<uint8_t>> &source) {
    if (attribute.format != SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED || attribute.regIndex >= 64)
        return false;
    // validate_program has already checked these tables against the complete
    // saved GXP. Regformatted arrays carry their total component count in the
    // vertex descriptor (for example, a float4[3] matrix carries twelve).
    const auto read_at = [&](uint64_t offset, void *destination, size_t size) {
        if (source) {
            if (offset > source->size() || size > source->size() - offset) {
                error = "saved GXP matrix attribute source is truncated";
                return false;
            }
            std::memcpy(destination, source->data() + offset, size);
            return true;
        }
        return memory.read_bytes(uint64_t(address) + offset, destination, size, error);
    };
    SceGxmProgram program{};
    SceGxmProgramVertexVaryings varyings{};
    if (!read_at(0, &program, sizeof(program))
        || !read_at(offsetof(SceGxmProgram, varyings_offset) + program.varyings_offset,
                &varyings, sizeof(varyings))
        || !(varyings.untyped_pa_regs & (uint64_t{1} << attribute.regIndex)))
        return false;
    for (uint32_t i = 0; i < program.parameter_count; ++i) {
        SceGxmProgramParameter parameter{};
        const uint64_t begin = offsetof(SceGxmProgram, parameters_offset)
            + uint64_t(program.parameters_offset) + uint64_t(i) * sizeof(parameter);
        if (!read_at(begin, &parameter, sizeof(parameter)))
            return false;
        if (parameter.category == SCE_GXM_PARAMETER_CATEGORY_ATTRIBUTE
            && parameter.resource_index == attribute.regIndex)
            return uint64_t(parameter.component_count) * parameter.array_size == attribute.componentCount;
    }
    return false;
}
}

bool validate_gxm_state(const GxmSnapshot &state, const MemoryImage &memory, std::string &error) {
    const auto fail = [&](const char *message) { error = message; return false; };
    const auto sizes = gxm::snapshot_object_sizes();
    std::map<uint64_t, uint64_t> objects;
    const auto object = [&](Address address, size_t size, size_t alignment = 8) {
        if (!address || (address & (alignment - 1)) || !memory.contains(address, size)) return false;
        const uint64_t end = uint64_t(address) + size;
        const auto next = objects.lower_bound(address);
        if ((next != objects.end() && next->first < end) || (next != objects.begin() && std::prev(next)->second > address)) return false;
        objects.emplace(address, end); return true;
    };
    std::set<Address> contexts, immediate, targets, vertex, fragment, syncs;
    for (const auto &context : state.contexts) {
        if (!object(context.address, sizes.context) || !contexts.insert(context.address).second
            || context.state.type != (context.deferred ? SCE_GXM_CONTEXT_TYPE_DEFERRED : SCE_GXM_CONTEXT_TYPE_IMMEDIATE))
            return fail("invalid saved GXM context object");
        if (!context.deferred) immediate.insert(context.address);
    }
    for (const auto &sync : state.sync_objects)
        if (!object(sync.address, sizeof(SceGxmSyncObject)) || !syncs.insert(sync.address).second) return fail("invalid saved GXM sync object");
    for (const auto &target : state.render_targets) {
        const auto &p = target.params;
        if (!object(target.address, sizes.render_target) || !targets.insert(target.address).second || !p.width || !p.height
            || p.width > 4096 || p.height > 4096 || !p.scenesPerFrame || p.scenesPerFrame > 64
            || p.multisampleMode > SCE_GXM_MULTISAMPLE_4X || (p.flags & 0xFFFF00EC)
            || target.width != p.width || target.height != p.height || target.scenes_per_frame != p.scenesPerFrame
            || target.driver_mem_block != p.driverMemBlock) return fail("invalid saved GXM render target");
        if (p.flags & SCE_GXM_RENDER_TARGET_MACROTILE_SYNC) {
            const uint32_t columns = (p.flags >> 8) & 7, rows = (p.flags >> 12) & 7;
            if (!columns || columns > 4 || !rows || rows > 4 || p.width / columns == 0 || p.height / rows == 0)
                return fail("invalid saved macroblock render target geometry");
        }
    }
    for (const auto &program : state.fragment_programs) {
        // The guest host-allocation callback can return word-aligned storage,
        // even when the host wrapper contains pointers with wider alignment.
        if (!object(program.address, sizeof(SceGxmFragmentProgram), 4) || !fragment.insert(program.address).second
            || !program.reference_count) return fail("invalid saved fragment program object");
        if (!validate_program(program.program.address(), true, memory, error, program.program_source)) {
            error = std::string("invalid saved fragment program: ") + error;
            return false;
        }
    }
    for (const auto &program : state.vertex_programs) {
        if (!object(program.address, sizeof(SceGxmVertexProgram), 4) || !vertex.insert(program.address).second
            || !program.reference_count) return fail("invalid saved vertex program object");
        if (!validate_program(program.program.address(), false, memory, error, program.program_source)) {
            error = std::string("invalid saved vertex program: ") + error;
            return false;
        }
        for (const auto &attribute : program.attributes)
            if (attribute.streamIndex >= program.streams.size() || attribute.componentCount == 0
                || attribute.format > SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED || attribute.regIndex >= 256
                || (attribute.componentCount > 4 && !matrix_attribute_valid(attribute,
                    program.program.address(), memory, error, program.program_source)))
                return fail("invalid saved vertex attribute");
    }
    const auto optional = [&](Address address, uint64_t length) { return !address || memory.contains(address, length ? length : 1); };
    const auto callback = [&](Address address) { return !address || memory.contains(address & ~Address(1), 2); };
    for (const auto &patcher : state.shader_patchers) {
        const auto &p = patcher.params;
        if (!object(patcher.address, sizes.shader_patcher, 4) || !optional(p.bufferMem.address(), p.bufferMemSize)
            || !optional(p.vertexUsseMem.address(), p.vertexUsseMemSize) || !optional(p.fragmentUsseMem.address(), p.fragmentUsseMemSize)
            || !callback(p.hostAllocCallback.address()) || !callback(p.hostFreeCallback.address())
            || !callback(p.bufferAllocCallback.address()) || !callback(p.bufferFreeCallback.address())
            || !callback(p.vertexUsseAllocCallback.address()) || !callback(p.vertexUsseFreeCallback.address())
            || !callback(p.fragmentUsseAllocCallback.address()) || !callback(p.fragmentUsseFreeCallback.address())) return fail("invalid saved shader patcher");
    }
    const auto color_extent = [&](const SceGxmColorSurface &surface) {
        if (surface.disabled || !surface.data) return true;
        const auto bpp = gxm::bits_per_pixel(gxm::get_base_format(surface.colorFormat));
        if (!bpp || !surface.width || !surface.height || surface.width > 4096 || surface.height > 4096
            || surface.strideInPixels < surface.width || surface.strideInPixels > 8192
            || (surface.surfaceType != SCE_GXM_COLOR_SURFACE_LINEAR && surface.surfaceType != SCE_GXM_COLOR_SURFACE_TILED
                && surface.surfaceType != SCE_GXM_COLOR_SURFACE_SWIZZLED)) return false;
        // This is the exact guest byte footprint used by the color cache.
        return memory.contains(surface.data.address(), uint64_t(surface.strideInPixels) * surface.height * (bpp / 8));
    };
    const auto depth_extent = [&](const SceGxmDepthStencilSurface &surface, Address target_address) {
        if (surface.disabled() || (!surface.depth_data && !surface.stencil_data)) return true;
        const auto target = std::find_if(state.render_targets.begin(), state.render_targets.end(),
            [&](const auto &t) { return t.address == target_address; });
        if (target == state.render_targets.end()) return false;
        const auto format = surface.get_format();
        if (format != SCE_GXM_DEPTH_STENCIL_FORMAT_DF32 && format != SCE_GXM_DEPTH_STENCIL_FORMAT_S8
            && format != SCE_GXM_DEPTH_STENCIL_FORMAT_DF32_S8 && format != SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M
            && format != SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M_S8 && format != SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24
            && format != SCE_GXM_DEPTH_STENCIL_FORMAT_D16) return false;
        uint64_t height = target->height;
        if (target->params.multisampleMode != SCE_GXM_MULTISAMPLE_NONE) height *= 2;
        // A scene may use a narrower guest surface inside a retained larger
        // render target. Its physical row stride is independent of that allocation.
        const uint64_t samples = uint64_t(surface.get_stride()) * height;
        const uint32_t depth_bytes = format == SCE_GXM_DEPTH_STENCIL_FORMAT_D16 ? 2
            : format == SCE_GXM_DEPTH_STENCIL_FORMAT_S8 ? 1 : 4;
        return (!surface.depth_data || memory.contains(surface.depth_data.address(), samples * depth_bytes))
            && (!surface.stencil_data || memory.contains(surface.stencil_data.address(), samples));
    };
    // Program bytes are read again by later draws; guest host-object placement
    // must not overwrite the saved binary during reconstruction.
    const auto binary_disjoint = [&](Address address,
        const std::shared_ptr<const std::vector<uint8_t>> &source) {
        SceGxmProgram program{};
        uint64_t program_size = 0;
        if (source) {
            if (source->size() < sizeof(program)) return false;
            std::memcpy(&program, source->data(), sizeof(program));
            program_size = source->size();
        } else {
            if (!memory.read_bytes(address, &program, sizeof(program), error)) return false;
            program_size = program.size;
        }
        const uint64_t end = uint64_t(address) + program_size;
        for (const auto &[begin, object_end] : objects)
            if (uint64_t(address) < object_end && begin < end) return false;
        return true;
    };
    for (const auto &program : state.vertex_programs)
        if (!binary_disjoint(program.program.address(), program.program_source)) return fail("GXP binary overlaps a reconstructed host object");
    for (const auto &program : state.fragment_programs)
        if (!binary_disjoint(program.program.address(), program.program_source)) return fail("GXP binary overlaps a reconstructed host object");
    if (!gxm::validate_command_graph(state.contexts, state.commands, state.batches, error)) return false;
    if (!renderer::validate_finish_operations(state.finish_operations, state.commands, error)) return false;
    for (const auto &context : state.contexts) {
        for (const auto &list : context.deferred_lists) {
            for (const auto &[start, end] : list.ranges)
                if (!memory.contains(start, uint64_t(end) - start)) return fail("unmapped saved deferred command range");
        }
        if (context.alloc_space_start && (context.alloc_space_start > context.alloc_space.address()
                || !memory.contains(context.alloc_space_start, uint64_t(context.alloc_space.address()) - context.alloc_space_start)))
            return fail("invalid saved deferred allocator cursor");
        const auto &s = context.state;
        if (!color_extent(context.renderer_record.color_surface)
            || !depth_extent(context.renderer_record.depth_stencil_surface, context.render_target_address)) return fail("invalid saved logical surface extent");
        if ((context.render_target_address && !targets.contains(context.render_target_address))
            || (s.vertex_program && !vertex.contains(s.vertex_program.address())) || (s.fragment_program && !fragment.contains(s.fragment_program.address()))
            || (context.renderer_record.vertex_program && !vertex.contains(context.renderer_record.vertex_program.address()))
            || (context.renderer_record.fragment_program && !fragment.contains(context.renderer_record.fragment_program.address()))
            || (s.fragment_sync_object && !syncs.contains(s.fragment_sync_object.address()))
            || !optional(s.vertex_ring_buffer.address(), s.vertex_ring_buffer_size) || !optional(s.fragment_ring_buffer.address(), s.fragment_ring_buffer_size)
            || !optional(s.vdm_buffer.address(), s.vdm_buffer_size) || s.vertex_ring_buffer_used > s.vertex_ring_buffer_size
            || s.fragment_ring_buffer_used > s.fragment_ring_buffer_size
            || !callback(s.vertex_memory_callback.address()) || !callback(s.fragment_memory_callback.address()) || !callback(s.vdm_memory_callback.address())) return fail("invalid saved GXM context references");
        const Address begin = context.alloc_space.address(), end = context.alloc_space_end.address();
        if (end < begin || (!begin && end) || (begin && !memory.contains(begin, uint64_t(end) - begin))) return fail("invalid saved GXM command arena");
        if (context.deferred ? context.command_allocator_size != 0
                             : context.command_allocator_size != (uint64_t(end) - begin) / sizeof(renderer::Command))
            return fail("invalid saved GXM command allocator");
    }
    for (const auto &command : state.commands) {
        const auto read32 = [&](size_t offset) { uint32_t value; std::memcpy(&value, command.data.data() + offset, sizeof(value)); return value; };
        const auto span = [&](Address address, uint64_t length) { return length == 0 || (address && memory.contains(address, length)); };
        std::vector<renderer::CommandGuestRange> ranges;
        if (!renderer::command_snapshot_guest_ranges(command, ranges, error)) return false;
        for (const auto &range : ranges)
            if (!memory.contains(range.address, range.size)) return fail("unmapped pending renderer command extent");
        if (command.new_frame_context_address && !immediate.contains(command.new_frame_context_address))
            return fail("unknown pending NewFrame context");
        switch (command.opcode) {
        case renderer::CommandOpcode::SetContext:
            if (!targets.contains(command.render_target_address)) return fail("unknown pending GXM render target");
            if (command.has_color_surface && !color_extent(command.color_surface)) return fail("invalid pending color surface extent");
            if (command.has_depth_surface && !depth_extent(command.depth_surface, command.render_target_address)) return fail("invalid pending depth surface extent");
            break;
        case renderer::CommandOpcode::Draw: {
            const uint32_t format = read32(4), count = read32(12);
            if ((format != SCE_GXM_INDEX_FORMAT_U16 && format != SCE_GXM_INDEX_FORMAT_U32) || !span(read32(8), uint64_t(count) * (format == SCE_GXM_INDEX_FORMAT_U16 ? 2 : 4)))
                return fail("invalid pending GXM index range");
            break;
        }
        case renderer::CommandOpcode::SignalSyncObject:
        case renderer::CommandOpcode::WaitSyncObject:
            if (!syncs.contains(read32(0))) return fail("unknown pending GXM sync object");
            break;
        case renderer::CommandOpcode::SyncSurfaceData:
            if (!optional(read32(8), 4)) return fail("unmapped pending fragment notification");
            [[fallthrough]];
        case renderer::CommandOpcode::SignalNotification:
        case renderer::CommandOpcode::MidSceneFlush:
            if (!optional(read32(0), 4)) return fail("unmapped pending GXM notification");
            break;
        case renderer::CommandOpcode::SetState: {
            uint16_t tag; std::memcpy(&tag, command.data.data(), sizeof(tag));
            switch (static_cast<renderer::GXMState>(tag)) {
            case renderer::GXMState::Program:
                if (command.data[6] ? !fragment.contains(read32(2)) : !vertex.contains(read32(2))) return fail("unknown pending GXM program");
                break;
            case renderer::GXMState::VertexStream: {
                size_t length; std::memcpy(&length, command.data.data() + 6 + sizeof(size_t), sizeof(length));
                if (!span(read32(2), length)) return fail("unmapped pending vertex stream"); break;
            }
            case renderer::GXMState::UniformBuffer:
                if (!span(read32(2), read32(11))) return fail("unmapped pending uniform buffer"); break;
            case renderer::GXMState::VisibilityBuffer: {
                const Address address = read32(2); const uint32_t stride = read32(6);
                if ((!address && stride) || (address && ((address & 3) || stride < 4 || stride % 4
                    || stride > renderer::vulkan::kMaxVisibilityStrideInSnapshot || !memory.contains(address, stride))))
                    return fail("invalid pending visibility buffer extent");
                break;
            }
            case renderer::GXMState::VisibilityIndex:
                if (read32(2) >= renderer::vulkan::kMaxVisibilityQueriesInSnapshot)
                    return fail("invalid pending visibility query index"); break;
            default: break;
            }
            break;
        }
        default: break;
        }
    }
    if ((state.renderer_context_address && !immediate.contains(state.renderer_context_address))
        || (state.last_immediate_context && !immediate.contains(state.last_immediate_context))
        || !callback(state.params.displayQueueCallback.address())
        || (state.notification_region && !memory.contains(state.notification_region, 4))) return fail("invalid saved GXM global references");
    for (const auto &entry : state.display_entries)
        if (!syncs.contains(entry.old_sync.address()) || !syncs.contains(entry.new_sync.address())
            || !memory.contains(entry.data, state.params.displayQueueCallbackDataSize)) return fail("invalid saved GXM display entry");
    std::set<Address> display_data;
    for (const auto &entry : state.display_entries)
        if (!display_data.insert(entry.data).second) return fail("duplicate queued display callback data");
    for (const auto &[producer, submission] : state.display_submissions) {
        const auto &entry = submission.callback;
        if (!syncs.contains(entry.old_sync.address()) || !syncs.contains(entry.new_sync.address())
            || !memory.contains(entry.data, state.params.displayQueueCallbackDataSize)
            || !display_data.insert(entry.data).second || entry.frame_predicted != submission.has_prediction)
            return fail("invalid prepared display submission");
        if (submission.has_prediction) {
            const auto &frame = submission.prediction;
            if (frame.image_size.x <= 0 || frame.image_size.y <= 0 || frame.image_size.x > 4096 || frame.image_size.y > 4096
                || frame.pitch < uint32_t(frame.image_size.x) || frame.pitch > 8192
                || frame.pixelformat != SCE_DISPLAY_PIXELFORMAT_A8B8G8R8
                || !memory.contains(frame.base.address(), uint64_t(frame.pitch) * frame.image_size.y * 4))
                return fail("invalid prepared display prediction extent");
        }
    }
    uint64_t previous_end = 0;
    for (const auto &[address, region] : state.regions) {
        if ((address & 4095) || region.offset != address || address < previous_end
            || (region.size && !memory.contains(address, region.size))) return fail("invalid saved GPU memory range");
        previous_end = uint64_t(address) + region.size;
    }
    return true;
}
} // namespace emucorev::savestate
