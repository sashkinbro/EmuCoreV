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

#include <algorithm>
#include <cpu/functions.h>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// after windows.h
#include <psapi.h>
#endif
#include <display/functions.h>

#include <dialog/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <kernel/state.h>
#include <renderer/state.h>

#include <chrono>
#include <motion/functions.h>
#include <touch/functions.h>

// Code heavily influenced by PPSSSPP's SceDisplay.cpp

static constexpr int TARGET_FPS = 60;
static constexpr int64_t TARGET_MICRO_PER_FRAME = 1000000LL / TARGET_FPS;
// how many cycles do we need to see before we start predicting the next frame
static constexpr int predict_threshold = 3;
static constexpr int max_expected_swapchain_size = 6;

struct ProcSample {
    uint64_t page_faults = 0;
    uint64_t working_set_mb = 0;
    uint64_t private_mb = 0;
    uint64_t user_us = 0;
    uint64_t kernel_us = 0;
    uint64_t sys_idle_us = 0;
    uint64_t sys_busy_us = 0;
};

static ProcSample sample_process() {
    ProcSample out;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        out.page_faults = pmc.PageFaultCount;
        out.working_set_mb = pmc.WorkingSetSize / (1024 * 1024);
        out.private_mb = pmc.PagefileUsage / (1024 * 1024);
    }
    auto to_us = [](const FILETIME &ft) {
        return ((static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime) / 10;
    };
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        out.kernel_us = to_us(kernel);
        out.user_us = to_us(user);
    }
    FILETIME sys_idle{}, sys_kernel{}, sys_user{};
    if (GetSystemTimes(&sys_idle, &sys_kernel, &sys_user)) {
        out.sys_idle_us = to_us(sys_idle);
        out.sys_busy_us = to_us(sys_kernel) + to_us(sys_user) - to_us(sys_idle);
    }
#endif
    return out;
}

static void freeze_watchdog_thread(EmuEnvState &emuenv) {
    DisplayState &display = emuenv.display;
    ProcSample prev = sample_process();
    while (!display.abort.load()) {
        const auto before = std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto slept_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - before).count();
        const int64_t overshoot = std::max<int64_t>(0, slept_us - 10000);

        const ProcSample now = sample_process();
        if (overshoot > 100000) {
            const double sys_idle_ms = (now.sys_idle_us - prev.sys_idle_us) / 1000.0;
            const double sys_busy_ms = (now.sys_busy_us - prev.sys_busy_us) / 1000.0;
            LOG_WARN("[FREEZE] {:.0f}ms process-wide stall | page_faults +{} (total {}) | working_set {}MB (delta {}MB) | private {}MB | OUR cpu: user {:.0f}ms kernel {:.0f}ms (= {:.0f}% of one core) | SYSTEM cpu: busy {:.0f}ms idle {:.0f}ms across {} cores (= {:.0f}% busy)",
                overshoot / 1000.0,
                now.page_faults - prev.page_faults, now.page_faults,
                now.working_set_mb, static_cast<int64_t>(now.working_set_mb) - static_cast<int64_t>(prev.working_set_mb),
                now.private_mb,
                (now.user_us - prev.user_us) / 1000.0, (now.kernel_us - prev.kernel_us) / 1000.0,
                100.0 * (now.user_us - prev.user_us + now.kernel_us - prev.kernel_us) / static_cast<double>(slept_us),
                sys_busy_ms, sys_idle_ms, std::thread::hardware_concurrency(),
                (sys_busy_ms + sys_idle_ms) > 0.0 ? (100.0 * sys_busy_ms / (sys_busy_ms + sys_idle_ms)) : 0.0);
        }
        prev = now;
    }
}

static void vblank_sync_thread(EmuEnvState &emuenv) {
    DisplayState &display = emuenv.display;
    std::thread watchdog(freeze_watchdog_thread, std::ref(emuenv));

    while (!display.abort.load()) {
        {
            const std::lock_guard<std::mutex> guard(display.mutex);

            {
                const std::lock_guard<std::mutex> guard_info(display.display_info_mutex);
                ++display.vblank_count;

                // in this case, even though no new game frames are being rendered, we still need to update the screen
                if (emuenv.kernel.is_threads_paused() || (emuenv.common_dialog.status == SCE_COMMON_DIALOG_STATUS_RUNNING))
                    // only display the UI/common dialog at 30 fps
                    // this is necessary so that the command buffer processing doesn't get starved
                    // with vsync enabled and a screen with a refresh rate of 60Hz or less
                    if (display.vblank_count % 2 == 0)
                        emuenv.renderer->should_display = true;
            }

            // maybe we should also use a mutex for this part, but it shouldn't be an issue
            touch_vsync_update(emuenv);
            refresh_motion(emuenv.motion, emuenv.ctrl);

            // Notify Vblank callback in each VBLANK start
            for (auto &[_, cb] : display.vblank_callbacks)
                cb->event_notify(cb->get_notifier_id());

            for (std::size_t i = 0; i < display.vblank_wait_infos.size();) {
                auto &vblank_wait_info = display.vblank_wait_infos[i];
                if (vblank_wait_info.target_vcount <= display.vblank_count) {
                    ThreadStatePtr target_wait = vblank_wait_info.target_thread;

                    target_wait->update_status(ThreadStatus::run);
                    display.vblank_wait_infos.erase(display.vblank_wait_infos.begin() + i);
                } else {
                    i++;
                }
            }
        }
        // Hang watchdog: no framebuffer flip for ~10s (600 vblanks) while unpaused -> dump guest threads once per stall.
        {
            static uint64_t last_dumped_setframe = ~0ull;
            const uint64_t setframe = emuenv.display.last_setframe_vblank_count.load();
            const uint64_t vblanks = emuenv.display.vblank_count.load();
            if (setframe > 0 && vblanks > setframe && (vblanks - setframe) > 600
                && !emuenv.kernel.is_threads_paused() && last_dumped_setframe != setframe) {
                last_dumped_setframe = setframe; // one dump per distinct stall
                LOG_ERROR("HANG WATCHDOG: no framebuffer flip for {} vblanks — dumping guest threads", vblanks - setframe);
                emuenv.kernel.log_thread_hang_dump();
            }
        }

        const auto time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const auto time_left = TARGET_MICRO_PER_FRAME - (time_ms % TARGET_MICRO_PER_FRAME);
        std::this_thread::sleep_for(std::chrono::microseconds(time_left));
    }

    watchdog.join();
}

void start_sync_thread(EmuEnvState &emuenv) {
    emuenv.display.vblank_thread = std::make_unique<std::thread>(vblank_sync_thread, std::ref(emuenv));
}

void wait_vblank(DisplayState &display, KernelState &kernel, const ThreadStatePtr &wait_thread, const uint64_t target_vcount, const bool is_cb) {
    if (!wait_thread) {
        return;
    }

    {
        auto thread_lock = std::unique_lock(wait_thread->mutex);

        {
            const std::lock_guard<std::mutex> guard(display.mutex);

            if (target_vcount <= display.vblank_count)
                return;

            wait_thread->update_status(ThreadStatus::wait);
            display.vblank_wait_infos.push_back({ wait_thread, target_vcount });
        }

        wait_thread->status_cond.wait(thread_lock, [&]() {
            return wait_thread->status == ThreadStatus::run;
        });
    }

    if (is_cb) {
        for (auto &[_, cb] : display.vblank_callbacks) {
            if (cb->get_owner_thread_id() == wait_thread->id) {
                std::string name = cb->get_name();
                cb->execute(kernel, [name]() {
                });
            }
        }
    }
}

static void reset_swapchain_cycle(DisplayState &display, Address sync_object) {
    display.predicted_frames.resize(1);
    display.predicted_frames[0].sync_object = sync_object;
    display.predicted_frame_position = 0;
    display.predicted_cycles_seen = 0;
}

DisplayFrameInfo *predict_next_image(EmuEnvState &emuenv, Address sync_object) {
    auto &display = emuenv.display;
    std::lock_guard<std::mutex> lock(display.display_info_mutex);

    if (display.predicted_cycles_seen >= predict_threshold) {
        // just check that the next sync_object in line is the one we expect
        display.predicted_frame_position = (display.predicted_frame_position + 1) % display.predicted_frames.size();
        if (display.predicted_frames[display.predicted_frame_position].sync_object != sync_object)
            // bad, this isn't what we expect
            reset_swapchain_cycle(display, sync_object);

    } else if (display.predicted_cycles_seen >= 1) {
        display.predicted_frame_position = (display.predicted_frame_position + 1) % display.predicted_frames.size();

        if (display.predicted_frame_position == 0)
            display.predicted_cycles_seen++;

        if (display.predicted_frames[display.predicted_frame_position].sync_object != sync_object)
            // bad, this isn't what we expect
            reset_swapchain_cycle(display, sync_object);
    } else {
        // check if we have a cycle
        bool has_cycle = false;
        for (int idx = 0; idx < display.predicted_frames.size(); idx++) {
            if (display.predicted_frames[idx].sync_object == sync_object) {
                // we found a cycle
                has_cycle = true;
                display.predicted_frames.erase(display.predicted_frames.begin(), display.predicted_frames.begin() + idx);
                display.predicted_frame_position = 0;
                display.predicted_cycles_seen = 1;
                break;
            }
        }

        if (!has_cycle) {
            // predicted_frame_position is initialized to -1, so this is fine
            display.predicted_frame_position++;
            if (display.predicted_frame_position == display.predicted_frames.size()) {
                // keep the last max_expected_swapchain_size frames for the swapchain cycle
                if (display.predicted_frames.size() == max_expected_swapchain_size) {
                    display.predicted_frame_position = 0;
                } else {
                    display.predicted_frames.resize(display.predicted_frames.size() + 1);
                }
            }
            display.predicted_frames[display.predicted_frame_position].sync_object = sync_object;
        }
    }

    bool predict = display.predicted_cycles_seen >= predict_threshold;
    DisplayFrameInfo *frame = nullptr;
    if (predict) {
        // set the next framebuffer image here
        frame = new DisplayFrameInfo;
        *frame = display.predicted_frames[display.predicted_frame_position].frame_info;
    }

    return frame;
}

void update_prediction(EmuEnvState &emuenv, DisplayFrameInfo &frame) {
    auto &display = emuenv.display;
    std::lock_guard<std::mutex> lock(display.display_info_mutex);
    Address sync_object = display.current_sync_object;

    if (!display.predicting) {
        display.next_rendered_frame = frame;
        emuenv.renderer->should_display = true;
    }

    for (auto &pred_frame : display.predicted_frames) {
        if (pred_frame.sync_object != sync_object)
            continue;

        if (memcmp(&pred_frame.frame_info, &frame, sizeof(DisplayFrameInfo)) == 0)
            // we got what we expected, fine
            return;

        pred_frame.frame_info = frame;
        break;
    }

    if (display.predicting) {
        LOG_TRACE("Mispredicted the next swapchain image");
        display.next_rendered_frame = frame;
        emuenv.renderer->should_display = true;
    }

    // let predict_next_image reset the cycle if necessary
    display.predicted_cycles_seen = std::min(display.predicted_cycles_seen, 1U);
}

void DisplayState::deinit() {
    abort = true;
    if (vblank_thread && vblank_thread->joinable())
        vblank_thread->join();

    vblank_thread.reset();
    abort = false;

    {
        const std::lock_guard<std::mutex> guard(mutex);
        vblank_wait_infos.clear();
        vblank_callbacks.clear();
    }

    {
        const std::lock_guard<std::mutex> guard(display_info_mutex);
        sce_frame = {};
        next_rendered_frame = {};
    }

    predicted_frames.clear();
    predicted_frame_position = static_cast<uint32_t>(-1);
    predicted_cycles_seen = 0;
    predicting = false;
    current_sync_object = 0;

    vblank_count = 0;
    last_setframe_vblank_count = 0;

    fps_hack = false;
    // pretty sure we set this on game boot
    fullscreen = false;
}