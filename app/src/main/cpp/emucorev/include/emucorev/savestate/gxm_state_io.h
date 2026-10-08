#pragma once
#include <gxm/savestate.h>
#include <gxm/display_queue.h>
#include <renderer/finish.h>
namespace emucorev::savestate {
class MemoryImage;
struct GxmSnapshot {
    SceGxmInitializeParams params{};
    uint32_t global_timestamp = 0, last_display_global = 0;
    Address notification_region = 0, last_immediate_context = 0, renderer_context_address = 0;
    SceUID display_queue_thread = 0;
    uint32_t display_worker_phase = 0;
    Address display_previous_entry = 0;
    std::vector<gxm::ContextSnapshot> contexts;
    std::vector<renderer::CommandSnapshot> commands;
    std::vector<gxm::BatchSnapshot> batches;
    std::vector<renderer::FinishSnapshot> finish_operations;
    std::vector<gxm::SyncObjectSnapshot> sync_objects;
    std::vector<gxm::RenderTargetSnapshot> render_targets;
    std::vector<gxm::FragmentProgramSnapshot> fragment_programs;
    std::vector<gxm::VertexProgramSnapshot> vertex_programs;
    std::vector<gxm::ShaderPatcherSnapshot> shader_patchers;
    std::vector<DisplayCallback> display_entries;
    std::vector<std::pair<SceUID, PendingDisplaySubmission>> display_submissions;
    std::map<Address, MemoryMapInfo> regions;
};
bool parse_gxm_state(const std::vector<uint8_t> &, GxmSnapshot &, std::string &);
bool validate_gxm_state(const GxmSnapshot &, const MemoryImage &, std::string &);
}
