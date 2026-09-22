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

#include <cstdint>
#include <map>
#include <vector>

struct EmuEnvState;

namespace gxm {

struct ContextSnapshot {
    Address address = 0;
    bool deferred = false;
    GxmContextState state{};
    bool last_precomputed = false;
    uint64_t command_next_free_pos = 0;
    Ptr<void> alloc_space;
    Ptr<void> alloc_space_end;
    uint32_t command_allocator_size = 0;
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

std::vector<ContextSnapshot> capture_contexts(EmuEnvState &emuenv);
std::vector<SyncObjectSnapshot> capture_sync_objects(EmuEnvState &emuenv);
std::vector<RenderTargetSnapshot> capture_render_targets(EmuEnvState &emuenv);
std::vector<FragmentProgramSnapshot> capture_fragment_programs(EmuEnvState &emuenv);
std::vector<VertexProgramSnapshot> capture_vertex_programs(EmuEnvState &emuenv);
std::vector<ShaderPatcherSnapshot> capture_shader_patchers(EmuEnvState &emuenv);

// Destroys live host objects and removes GPU memory mappings. Must run before
// guest RAM is overwritten.
void destroy_runtime_objects(EmuEnvState &emuenv);

// The display queue is driven by a host thread holding a guest thread context;
// recreate both after a save-state load.
void restart_display_queue(EmuEnvState &emuenv);

void restore_memory_regions(EmuEnvState &emuenv, const std::map<Address, MemoryMapInfo> &regions);
void restore_contexts(EmuEnvState &emuenv, const std::vector<ContextSnapshot> &snapshots);
void restore_sync_objects(EmuEnvState &emuenv, const std::vector<SyncObjectSnapshot> &snapshots);
void restore_render_targets(EmuEnvState &emuenv, const std::vector<RenderTargetSnapshot> &snapshots);
void restore_fragment_programs(EmuEnvState &emuenv, const std::vector<FragmentProgramSnapshot> &snapshots);
void restore_vertex_programs(EmuEnvState &emuenv, const std::vector<VertexProgramSnapshot> &snapshots);
void restore_shader_patchers(EmuEnvState &emuenv, const std::vector<ShaderPatcherSnapshot> &snapshots);

} // namespace gxm
