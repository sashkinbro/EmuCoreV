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

#include <gxm/types.h>
#include <mem/ptr.h>
#include <renderer/gxm_types.h>
#include <renderer/snapshot_gate.h>
#include <threads/queue.h>

#include <atomic>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

struct EmuEnvState;
struct SceGxmContext;
struct SceGxmRenderTarget;
struct PendingDisplaySubmission;

enum class DisplayWorkerPhase : uint32_t {
    Idle,
    WaitOldSync,
    WaitNewSync,
    StartCallback,
    WaitCallback,
    Complete
};

struct SceGxmInitializeParams {
    uint32_t flags = 0;
    uint32_t displayQueueMaxPendingCount = 0;
    Ptr<void> displayQueueCallback;
    uint32_t displayQueueCallbackDataSize = 0;
    uint32_t parameterBufferSize = 0;
};

struct DisplayCallback {
    Address data;
    Ptr<SceGxmSyncObject> old_sync;
    Ptr<SceGxmSyncObject> new_sync;
    uint32_t old_sync_timestamp;
    uint32_t new_sync_timestamp;
    bool frame_predicted;
};

struct MemoryMapInfo {
    Address offset;
    std::uint32_t size;
    std::uint32_t perm;
};

// Host-side creation parameters for objects whose host data cannot survive a
// process restart. The save-state engine rebuilds renderer objects from these.
struct FragmentProgramInfo {
    Ptr<const SceGxmProgram> program;
    bool has_blend = false;
    SceGxmBlendInfo blend{};
    bool is_mask_update = false;
};

struct VertexProgramInfo {
    Ptr<const SceGxmProgram> program;
    std::vector<SceGxmVertexAttribute> attributes;
    std::vector<SceGxmVertexStream> streams;
    uint64_t key_hash = 0;
};

struct GxmState {
    SceGxmInitializeParams params;

    renderer::SnapshotGate display_snapshot_gate;
    Queue<DisplayCallback> display_queue;
    std::atomic<int> display_worker_state{ 0 };
    std::atomic<uint32_t> display_entries_done{ 0 };
    SceUID display_queue_thread;
    std::thread display_host_thread;
    DisplayWorkerPhase display_phase = DisplayWorkerPhase::Idle;
    Address display_previous_entry_point = 0;
    uint32_t display_sync_wait_ticks = 0;
    std::mutex display_submissions_mutex;
    std::map<SceUID, std::shared_ptr<PendingDisplaySubmission>> pending_display_submissions;

    // global timestamp used by sync objects
    std::atomic<uint32_t> global_timestamp{ 1 };
    // last display operation, as given by the global timestamp
    uint32_t last_display_global = 0;

    Ptr<uint32_t> notification_region;

    std::mutex sync_objects_mutex;
    std::unordered_set<SceGxmSyncObject *> sync_objects;

    std::map<Address, MemoryMapInfo> memory_mapped_regions;
    std::mutex callback_lock;
    std::unordered_map<SceGxmContext *, Address> immediate_contexts;
    Address last_immediate_context = 0;
    std::unordered_map<SceGxmContext *, Address> deferred_contexts;
    std::unordered_map<SceGxmRenderTarget *, Address> render_targets;

    std::unordered_map<Address, FragmentProgramInfo> fragment_programs;
    std::unordered_map<Address, VertexProgramInfo> vertex_programs;
    std::unordered_map<Address, SceGxmShaderPatcherParams> shader_patchers;

    // Pending display queue entries restored by a save-state load.
    std::vector<DisplayCallback> restored_display_queue;

    void stop_display_host() {
        display_snapshot_gate.stop();
        display_queue.abort();
        if (display_host_thread.joinable())
            display_host_thread.join();
    }

    void deinit() {
        stop_display_host();

        {
            const std::lock_guard<std::mutex> lock(sync_objects_mutex);
            sync_objects.clear();
        }

        memory_mapped_regions.clear();
        display_queue.reset();
        pending_display_submissions.clear();
        display_snapshot_gate.reset();
        params = {};
        display_queue_thread = 0;
        display_phase = DisplayWorkerPhase::Idle;
        display_previous_entry_point = 0;
        display_sync_wait_ticks = 0;
        global_timestamp = 1;
        last_display_global = 0;
        notification_region = Ptr<uint32_t>(0);
        immediate_contexts.clear();
        last_immediate_context = 0;
        deferred_contexts.clear();
        render_targets.clear();
        fragment_programs.clear();
        vertex_programs.clear();
        shader_patchers.clear();
    }
};
