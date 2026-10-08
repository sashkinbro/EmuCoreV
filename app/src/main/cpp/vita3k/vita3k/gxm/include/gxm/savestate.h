// Save-state support for GXM runtime objects.
//
// GXM host objects live inside guest memory. The save-state writer captures
// their guest-visible fields; the loader destroys the live objects, restores
// guest RAM and rebuilds them from the captured snapshots.

#pragma once

#include <gxm/state.h>
#include <gxm/types.h>
#include <mem/ptr.h>
#include <renderer/gxm_types.h>
#include <renderer/types.h>
#include <renderer/command_snapshot.h>

#include <cstdint>
#include <map>
#include <vector>

struct EmuEnvState;

namespace gxm {

struct SnapshotObjectSizes { size_t context, render_target, shader_patcher; };
SnapshotObjectSizes snapshot_object_sizes();
renderer::CommandList *resolve_deferred_command_list(EmuEnvState &, uint64_t identity);

struct DeferredListSnapshot {
    Address guest_address = 0; // guest wrapper, zero for the current open list
    uint64_t identity = 0; // stable opaque guest token, never a host address
    uint32_t first = 0, last = 0;
    bool current = false;
    std::vector<std::pair<Address, Address>> ranges; // stack order, bottom first
};
struct BatchSnapshot { Address context_address = 0; uint32_t first = 0, last = 0; };
struct ContextSnapshot {
    Address address = 0;
    bool deferred = false;
    Address render_target_address = 0;
    renderer::GxmRecordState renderer_record{};
    GxmContextState state{};
    bool last_precomputed = false;
    uint64_t command_next_free_pos = 0;
    Ptr<void> alloc_space;
    Ptr<void> alloc_space_end;
    uint32_t command_allocator_size = 0;
    Address alloc_space_start = 0;
    uint32_t pending_first = 0, pending_last = 0;
    bool was_vert_default_uniform_reserved = false, was_frag_default_uniform_reserved = false;
    std::vector<DeferredListSnapshot> deferred_lists;
};

struct SyncObjectSnapshot {
    Address address = 0;
    uint32_t timestamp_current = 0;
    uint32_t timestamp_ahead = 0;
    uint32_t last_display = 0;
    uint32_t last_operation_global = 0;
};

struct RenderTargetSnapshot {
    Address address = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint16_t scenes_per_frame = 0;
    SceUID driver_mem_block = 0;
    SceGxmRenderTargetParams params{};
};

struct FragmentProgramSnapshot {
    Address address = 0;
    Ptr<const SceGxmProgram> program;
    bool has_blend = false;
    SceGxmBlendInfo blend{};
    bool is_mask_update = false;
    uint32_t reference_count = 1;
};

struct VertexProgramSnapshot {
    Address address = 0;
    Ptr<const SceGxmProgram> program;
    std::vector<SceGxmVertexAttribute> attributes;
    std::vector<SceGxmVertexStream> streams;
    uint64_t key_hash = 0;
    uint32_t reference_count = 1;
};

struct ShaderPatcherSnapshot {
    Address address = 0;
    SceGxmShaderPatcherParams params{};
};

// Called only after guest and renderer quiescence; failure leaves output intact.
bool capture_contexts(EmuEnvState &emuenv, std::vector<ContextSnapshot> &snapshots, std::string &error);
bool capture_contexts(EmuEnvState &, std::vector<ContextSnapshot> &, std::vector<renderer::CommandSnapshot> &, std::string &);
bool capture_contexts(EmuEnvState &, std::vector<ContextSnapshot> &, std::vector<renderer::CommandSnapshot> &, std::vector<BatchSnapshot> &, std::string &);
Address runtime_selected_context_address(EmuEnvState &);
bool activate_saved_batches(EmuEnvState &, Address selected_context, std::string &);
bool validate_command_graph(const std::vector<ContextSnapshot> &, const std::vector<renderer::CommandSnapshot> &, std::string &);
bool validate_command_graph(const std::vector<ContextSnapshot> &, const std::vector<renderer::CommandSnapshot> &, const std::vector<BatchSnapshot> &, std::string &);
std::vector<std::pair<Address, renderer::RenderTarget *>> runtime_render_targets(EmuEnvState &emuenv);
std::vector<std::pair<Address, renderer::Context *>> runtime_contexts(EmuEnvState &emuenv);
std::vector<SyncObjectSnapshot> capture_sync_objects(EmuEnvState &emuenv);
std::vector<RenderTargetSnapshot> capture_render_targets(EmuEnvState &emuenv);
std::vector<FragmentProgramSnapshot> capture_fragment_programs(EmuEnvState &emuenv);
std::vector<VertexProgramSnapshot> capture_vertex_programs(EmuEnvState &emuenv);
std::vector<ShaderPatcherSnapshot> capture_shader_patchers(EmuEnvState &emuenv);

// Destroys live host objects and removes GPU memory mappings. Must run before
// guest RAM is overwritten.
void destroy_runtime_objects(EmuEnvState &emuenv);

// Stop and join even when a restored host is parked behind its bootstrap gate.
void stop_display_queue_host(EmuEnvState &emuenv);

// Recreate the host around the exact guest thread already restored by kernel.
bool restart_display_queue(EmuEnvState &emuenv, bool hold = false);

void restore_memory_regions(EmuEnvState &emuenv, const std::map<Address, MemoryMapInfo> &regions);
// Targets/programs/cache images must already exist. No GPU commands are queued.
bool restore_contexts(EmuEnvState &emuenv, const std::vector<ContextSnapshot> &snapshots, std::string &error);
bool restore_contexts(EmuEnvState &, const std::vector<ContextSnapshot> &, const std::vector<renderer::CommandSnapshot> &, std::string &);
bool restore_contexts(EmuEnvState &, const std::vector<ContextSnapshot> &, const std::vector<renderer::CommandSnapshot> &, const std::vector<BatchSnapshot> &, std::string &);
void restore_sync_objects(EmuEnvState &emuenv, const std::vector<SyncObjectSnapshot> &snapshots);
void restore_render_targets(EmuEnvState &emuenv, const std::vector<RenderTargetSnapshot> &snapshots);
void restore_fragment_programs(EmuEnvState &emuenv, const std::vector<FragmentProgramSnapshot> &snapshots);
void restore_vertex_programs(EmuEnvState &emuenv, const std::vector<VertexProgramSnapshot> &snapshots);
void restore_shader_patchers(EmuEnvState &emuenv, const std::vector<ShaderPatcherSnapshot> &snapshots);

} // namespace gxm
