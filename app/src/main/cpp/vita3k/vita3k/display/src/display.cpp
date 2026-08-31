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
#include <cstring>
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

#include <config/state.h>
#include <dialog/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <gxm/state.h>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <mem/functions.h>
#include <mem/ptr.h>
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
        // Periodic thread dump for diagnosing partial hangs
        if (emuenv.cfg.hang_dump_seconds > 0) {
            static uint64_t next_forced_dump_vblank = 0;
            const uint64_t vblanks_now = emuenv.display.vblank_count.load();
            const uint64_t period = static_cast<uint64_t>(emuenv.cfg.hang_dump_seconds) * 60;
            if (period > 0 && vblanks_now >= next_forced_dump_vblank && !emuenv.kernel.is_threads_paused()) {
                next_forced_dump_vblank = vblanks_now + period;
                LOG_ERROR("PERIODIC THREAD DUMP (hang-dump-seconds={}) — vblank {}", emuenv.cfg.hang_dump_seconds, vblanks_now);
                emuenv.kernel.log_thread_hang_dump();
            }
        }

        // Hang watchdog - if there's no framebuffer flip for ~10s (600 vblanks) while unpaused then dump guest threads
        {
            static uint64_t last_dumped_setframe = ~0ull;
            static uint64_t last_break_vblanks = 0;
            static uint64_t last_provable_break_setframe = ~0ull;
            static uint64_t last_provable_dryrun_setframe = ~0ull;
            static uint64_t last_progress_value = 0;
            static uint64_t last_progress_change_vblank = 0;
            static std::vector<SceUID> nudged_this_stall;
            static uint64_t last_wake_value = 0;
            static uint64_t last_wake_change_vblank = 0;
            static bool never_flip_redumped = false;
            static uint64_t last_seen_vblanks = 0;
            const uint64_t setframe = emuenv.display.last_setframe_vblank_count.load();
            const uint64_t vblanks = emuenv.display.vblank_count.load();
            // vblank_count restarts at 0 each game session (DisplayState::deinit); these statics outlive it, so reset them with it
            if (vblanks < last_seen_vblanks) {
                last_dumped_setframe = ~0ull;
                last_break_vblanks = 0;
                last_provable_break_setframe = ~0ull;
                last_provable_dryrun_setframe = ~0ull;
                last_progress_value = 0;
                last_progress_change_vblank = 0;
                last_wake_value = 0;
                last_wake_change_vblank = 0;
                nudged_this_stall.clear();
                never_flip_redumped = false;
            }
            last_seen_vblanks = vblanks;
            const bool unpaused = !emuenv.kernel.is_threads_paused();
            const bool never_flipped = (setframe == 0);
            const uint64_t stall_vblanks = (vblanks > setframe) ? (vblanks - setframe) : 0;
            const uint64_t progress_now = emuenv.renderer ? emuenv.renderer->progress_counter.load(std::memory_order_relaxed) : 0;
            if (progress_now != last_progress_value) {
                last_progress_value = progress_now;
                last_progress_change_vblank = vblanks;
            }
            const uint64_t wakes_now = emuenv.kernel.thread_wake_counter.load(std::memory_order_relaxed);
            if (wakes_now != last_wake_value) {
                last_wake_value = wakes_now;
                last_wake_change_vblank = vblanks;
            }
            const uint64_t renderer_idle_vblanks = vblanks - last_progress_change_vblank;
            const uint64_t guest_idle_vblanks = vblanks - last_wake_change_vblank;

            if (unpaused && vblanks > 0 && (vblanks % 600) == 0)
                LOG_INFO("[FLIPTRACE] vblank={} SetFrameBuf calls={} accepted={} queue: entries_done={} depth={} worker_state={} renderer_progress={}",
                    vblanks, emuenv.display.setframe_call_count.load(), emuenv.display.setframe_accept_count.load(),
                    emuenv.gxm.display_entries_done.load(), emuenv.gxm.display_queue.size(), emuenv.gxm.display_worker_state.load(),
                    emuenv.renderer ? emuenv.renderer->progress_counter.load(std::memory_order_relaxed) : 0);

            // Full 10s dump for diagnostics (once per distinct stall).
            if (stall_vblanks > 600 && unpaused && last_dumped_setframe != setframe) {
                last_dumped_setframe = setframe; // one dump per distinct stall
                LOG_ERROR("HANG WATCHDOG: no framebuffer flip for {} vblanks{} (renderer idle {}, guest wake-idle {}, SetFrameBuf calls={} accepted={}) — dumping guest threads",
                    stall_vblanks, never_flipped ? " (game NEVER flipped since boot)" : "", renderer_idle_vblanks, guest_idle_vblanks,
                    emuenv.display.setframe_call_count.load(), emuenv.display.setframe_accept_count.load());
                emuenv.kernel.log_thread_hang_dump();
                LOG_ERROR("HANG DISPLAY QUEUE: depth={} worker_state={} (0=idle-waiting-entry 1=wait-old-sync 2=wait-new-sync 3=running-callback) entries_done={}",
                    emuenv.gxm.display_queue.size(), emuenv.gxm.display_worker_state.load(), emuenv.gxm.display_entries_done.load());
                emuenv.kernel.log_eventflag_history();
            }
            if (never_flipped && stall_vblanks > 3600 && unpaused && !never_flip_redumped) {
                never_flip_redumped = true;
                LOG_ERROR("HANG WATCHDOG: STILL no first flip after {} vblanks (SetFrameBuf calls={}) — re-dumping for movement comparison",
                    stall_vblanks, emuenv.display.setframe_call_count.load());
                emuenv.kernel.log_thread_hang_dump();
                LOG_ERROR("HANG DISPLAY QUEUE: depth={} worker_state={} (0=idle-waiting-entry 1=wait-old-sync 2=wait-new-sync 3=running-callback) entries_done={}",
                    emuenv.gxm.display_queue.size(), emuenv.gxm.display_worker_state.load(), emuenv.gxm.display_entries_done.load());
                emuenv.kernel.log_eventflag_history();
            }
            // Cycle breaker
            constexpr uint64_t PROVABLE_DRYRUN_VBLANKS = 120;
            constexpr uint64_t PROVABLE_BREAK_VBLANKS = 3600;
            const int64_t now_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const bool world_stop_quiet = (now_epoch_ms - emuenv.kernel.last_world_stop_epoch_ms.load(std::memory_order_relaxed)) >= 5000;
            // deferred-unmap collapses remaps WITHOUT world-stops, blinding the veto above; transitions are stamped regardless
            const bool mem_transition_quiet = !emuenv.renderer
                || (now_epoch_ms - emuenv.renderer->last_mem_transition_epoch_ms.load(std::memory_order_relaxed)) >= 5000;
            if (!never_flipped && stall_vblanks > PROVABLE_DRYRUN_VBLANKS && unpaused && renderer_idle_vblanks > PROVABLE_DRYRUN_VBLANKS && last_provable_dryrun_setframe != setframe) {
                last_provable_dryrun_setframe = setframe;
                emuenv.kernel.try_break_provable_evf_cycle(true);
            }
            if (!never_flipped && stall_vblanks > PROVABLE_BREAK_VBLANKS && unpaused && renderer_idle_vblanks > PROVABLE_BREAK_VBLANKS && world_stop_quiet && mem_transition_quiet && last_provable_break_setframe != setframe) {
#ifdef _WIN32
                if (!IsDebuggerPresent())
#endif
                {
                    const int broken = emuenv.kernel.try_break_provable_evf_cycle();
                    if (broken > 0) {
                        last_provable_break_setframe = setframe;
                        LOG_ERROR("HANG WATCHDOG: provable-cycle breaker released {} dead event flag(s) after {} vblanks", broken, stall_vblanks);
                    }
                }
            }

            // Deadlock breaker for stuck event flags
            constexpr uint64_t BREAK_STALL_VBLANKS = 300; // ~5s at 60Hz
            constexpr uint64_t BREAK_SLOW_VBLANKS = 900; // ~15s fallback when guest threads still wake
            if (!never_flipped && stall_vblanks > BREAK_STALL_VBLANKS && unpaused
                && (last_break_vblanks == 0 || vblanks - last_break_vblanks > BREAK_STALL_VBLANKS)) {
                last_break_vblanks = vblanks;
                const bool full_wedge = renderer_idle_vblanks > BREAK_STALL_VBLANKS && guest_idle_vblanks > BREAK_STALL_VBLANKS;
                const bool partial_wedge = stall_vblanks > BREAK_SLOW_VBLANKS && renderer_idle_vblanks > BREAK_SLOW_VBLANKS;
                if (!full_wedge && !partial_wedge) {
                    LOG_WARN("HANG WATCHDOG: no flip for {} vblanks but still alive (renderer executed a command {} vblanks ago, a guest thread woke {} vblanks ago) — breaker suppressed", stall_vblanks, renderer_idle_vblanks, guest_idle_vblanks);
                } else {
                    // dump BEFORE mutating anything so the log always records what was stuck on what
                    LOG_ERROR("HANG WATCHDOG: no flip for {} vblanks, renderer idle {}, guest wake-idle {} — dumping guest threads, then breaking", stall_vblanks, renderer_idle_vblanks, guest_idle_vblanks);
                    emuenv.kernel.log_thread_hang_dump();
#ifdef _WIN32
                    // Debug speed makes a normal load look exactly like a wedge hang
                    if (IsDebuggerPresent()) {
                        LOG_ERROR("HANG WATCHDOG: breaker SUPPRESSED - debugger attached; heuristics must not mutate guest state under a human. Break in and inspect instead.");
                        continue;
                    }
#endif
                    const int nudged = emuenv.kernel.try_break_frame_sync_deadlock(nudged_this_stall);
                    if (nudged > 0)
                        LOG_ERROR("HANG WATCHDOG: deadlock breaker nudged {} stuck event flag(s) after {} vblanks stalled", nudged, stall_vblanks);
                    else
                        LOG_ERROR("HANG WATCHDOG: genuine-looking stall but nothing to nudge ({} flag(s) already nudged this stall)", nudged_this_stall.size());
                }
            }
            if (stall_vblanks == 0) {
                last_break_vblanks = 0;
                nudged_this_stall.clear();
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
    setframe_call_count = 0;
    setframe_accept_count = 0;

    fps_hack = false;
    // pretty sure we set this on game boot
    fullscreen = false;
}
