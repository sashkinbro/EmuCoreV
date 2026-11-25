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

#include <kernel/callback.h>
#include <kernel/thread/wait_queue.h>
#include <mem/ptr.h>
#include <util/types.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

enum SceDisplayPixelFormat {
    SCE_DISPLAY_PIXELFORMAT_A8B8G8R8 = 0x00000000U
};

struct VBlankWaitEntry {
    uint64_t target_vcount;
};

struct DisplayFrameInfo {
    Ptr<const void> base;
    uint32_t pitch = 0;
    uint32_t pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    SceIVector2 image_size = { 0, 0 };
};

struct PredictedDisplayFrame {
    DisplayFrameInfo frame_info;
    Address sync_object;
};

// Source of the host panel's vsync (Android Choreographer). The guest vblank follows it when present.
struct PanelVsyncSource {
    virtual ~PanelVsyncSource() = default;
    // Blocks until the next panel vsync or the timeout. On success sets the vsync timestamp
    // (CLOCK_MONOTONIC, ns) and returns true; false means no vsync arrived in time (app hidden, source stopped).
    virtual bool wait_next(std::chrono::microseconds timeout, int64_t &timestamp_ns) = 0;
};

// Starts a vsync source for this platform; nullptr where none exists.
std::unique_ptr<PanelVsyncSource> create_panel_vsync_source();

struct DisplayState {
    int viewport_drawable_w = 0;
    int viewport_drawable_h = 0;
    float viewport_x = 0;
    float viewport_y = 0;
    float viewport_w = 0;
    float viewport_h = 0;

    // next frame as seen by SceDisplay
    DisplayFrameInfo sce_frame;

    std::mutex display_info_mutex;
    // the next frame which will actually be displayed by the renderer
    DisplayFrameInfo next_rendered_frame;

    std::mutex mutex;
    std::unique_ptr<std::thread> vblank_thread;
    std::mutex producer_mutex;
    std::condition_variable producer_changed;
    unsigned producer_freeze_count = 0;
    bool producer_active = false;
    bool shell_suspended = false; // Protected by producer_mutex; games never enable it.
    int refresh_hz = 60;
    // Set by the session before the vblank thread starts: lock guest vblank to the panel vsync while v-sync is on.
    bool follow_panel_vsync = false;
    std::unique_ptr<PanelVsyncSource> panel_vsync;
    void set_shell_suspended(bool suspended);
    bool begin_vblank_update();
    void end_vblank_update();
    bool freeze_vblank_producer(std::chrono::milliseconds budget);
    void resume_vblank_producer();
    std::atomic<bool> abort{ false };
    std::atomic<bool> imgui_render{ true };
    std::atomic<bool> fullscreen{ false };
    std::atomic<std::uint64_t> vblank_count{ 0 };
    WaitQueue<VBlankWaitEntry> vblank_waiters;
    std::atomic<uint64_t> last_setframe_vblank_count = 0;
    std::map<SceUID, CallbackPtr> vblank_callbacks{};

    // if set to true, make sceDisplayWaitVblankStartMulti/sceDisplayWaitSetFrameBufMulti behave as sceDisplayWaitVblankStart/sceDisplayWaitSetFrameBuf
    // this allows some game running at 30fps to run at 60fps without any issue
    // however this is not always the case, some games may not be affected (if they look at the Vcount)
    // or run twice as fast (if they only rely on these function calls for their timings)
    bool fps_hack = false;

    // should contain the list of sync objects / swapchain images (in the order they appear in the cycle)
    std::vector<PredictedDisplayFrame> predicted_frames;
    // position in the predicted_frame cycle (the -1 is needed)
    uint32_t predicted_frame_position = -1;
    // how many times the same predicted_frames cycle has been seen (if it is below some threshold, do not predict the frame)
    uint32_t predicted_cycles_seen = 0;
    // should the next call to sceDisplaySetFrameBuf do something
    std::atomic<bool> predicting = false;

    std::atomic<Address> current_sync_object;

    void deinit();
};
