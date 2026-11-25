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
#include <cassert>
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
#endif
#include <display/functions.h>
#include <util/frame_pacing.h>

#include <cheat/functions.h>
#include <config/state.h>
#include <dialog/state.h>
#include <display/state.h>
#include <emuenv/state.h>
#include <gxm/state.h>
#include <kernel/state.h>
#include <kernel/input_read_continuation.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <mem/functions.h>
#include <mem/ptr.h>
#include <renderer/state.h>

#include <chrono>
#include <motion/functions.h>
#include <touch/functions.h>

// Code heavily influenced by PPSSSPP's SceDisplay.cpp

// how many cycles do we need to see before we start predicting the next frame
static constexpr int predict_threshold = 3;
static constexpr int max_expected_swapchain_size = 6;

static void vblank_sync_thread(EmuEnvState &emuenv) {
    DisplayState &display = emuenv.display;

    const cheat::JitInvalidate invalidate_jit = [&emuenv](uint32_t address, size_t size) {
        emuenv.kernel.invalidate_jit_cache(address, size);
    };

    // Hang-watchdog bookkeeping. It belongs to this vblank thread, which lives exactly
    // one session (start_sync_thread / DisplayState::deinit), so every session starts
    // it fresh instead of inheriting the previous session's stall state (B151).
    struct HangWatchdog {
        uint64_t next_forced_dump_vblank = 0;
        uint64_t last_dumped_setframe = ~0ull;
        uint64_t last_break_vblanks = 0;
        uint64_t last_provable_break_setframe = ~0ull;
        uint64_t last_progress_value = 0;
        uint64_t last_progress_change_vblank = 0;
        std::vector<SceUID> nudged_this_stall;
        uint64_t last_wake_value = 0;
        uint64_t last_wake_change_vblank = 0;
        bool never_flip_redumped = false;
        uint64_t last_seen_vblanks = 0;
    } watchdog;

    frame_pacing::VblankFollower follower;
    bool vsync_stalled = false;
#ifndef NDEBUG
    frame_pacing::IntervalHistogram vblank_histogram;
    auto last_vblank = std::chrono::steady_clock::time_point{};
    auto histogram_start = std::chrono::steady_clock::now();
    const auto pacing_trace_vblank = [&]() {
        // debug builds only: guest vblank interval histogram, logged every 5 s (ECVPACE)
        const auto now = std::chrono::steady_clock::now();
        if (last_vblank != std::chrono::steady_clock::time_point{})
            vblank_histogram.add(std::chrono::duration<double, std::milli>(now - last_vblank).count());
        last_vblank = now;
        if (now - histogram_start >= std::chrono::seconds(5)) {
            LOG_INFO("ECVPACE vblank {}", vblank_histogram.format());
            vblank_histogram.clear();
            histogram_start = now;
        }
    };
#endif
    while (!display.abort.load()) {
        struct ProducerIteration {
            DisplayState &display;
            bool entered;
            explicit ProducerIteration(DisplayState &display)
                : display(display)
                , entered(display.begin_vblank_update()) {}
            ~ProducerIteration() {
                if (entered)
                    display.end_vblank_update();
            }
        } iteration(display);
        if (!iteration.entered)
            break;
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

            // Cheat codes are re-applied on every vblank, the same way the Vita cheat plugins do.
            cheat::apply(emuenv.cheat, emuenv.mem, invalidate_jit);

            // Notify Vblank callback in each VBLANK start
            for (auto &[_, cb] : display.vblank_callbacks)
                cb->event_notify(cb->get_notifier_id());

            display.vblank_waiters.wake_if([&](auto &waiter) {
                return waiter.entry.target_vcount <= display.vblank_count;
            });
        }
#ifndef NDEBUG
        // Periodic thread dump for diagnosing partial hangs
        if (emuenv.cfg.hang_dump_seconds > 0) {
            uint64_t &next_forced_dump_vblank = watchdog.next_forced_dump_vblank;
            const uint64_t vblanks_now = emuenv.display.vblank_count.load();
            const uint64_t period = static_cast<uint64_t>(emuenv.cfg.hang_dump_seconds) * 60;
            if (period > 0 && vblanks_now >= next_forced_dump_vblank && !emuenv.kernel.is_threads_paused()) {
                next_forced_dump_vblank = vblanks_now + period;
                LOG_ERROR("PERIODIC THREAD DUMP (hang-dump-seconds={}) — vblank {}", emuenv.cfg.hang_dump_seconds, vblanks_now);
                emuenv.kernel.log_thread_hang_dump();
            }
        }

#endif
        // Hang watchdog - if there's no framebuffer flip for ~10s (600 vblanks) while unpaused then dump guest threads
        {
            const uint64_t setframe = emuenv.display.last_setframe_vblank_count.load();
            const uint64_t vblanks = emuenv.display.vblank_count.load();
            // A save-state load rewinds vblank_count within the session: start the bookkeeping over with it
            if (vblanks < watchdog.last_seen_vblanks)
                watchdog = HangWatchdog{};
            watchdog.last_seen_vblanks = vblanks;
#ifndef NDEBUG
            uint64_t &last_dumped_setframe = watchdog.last_dumped_setframe;
            bool &never_flip_redumped = watchdog.never_flip_redumped;
#endif
            uint64_t &last_break_vblanks = watchdog.last_break_vblanks;
            uint64_t &last_provable_break_setframe = watchdog.last_provable_break_setframe;
            uint64_t &last_progress_value = watchdog.last_progress_value;
            uint64_t &last_progress_change_vblank = watchdog.last_progress_change_vblank;
            std::vector<SceUID> &nudged_this_stall = watchdog.nudged_this_stall;
            uint64_t &last_wake_value = watchdog.last_wake_value;
            uint64_t &last_wake_change_vblank = watchdog.last_wake_change_vblank;
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

#ifndef NDEBUG
            // Full 10s dump for diagnostics (once per distinct stall).
            if (stall_vblanks > 600 && unpaused && last_dumped_setframe != setframe) {
                last_dumped_setframe = setframe; // one dump per distinct stall
                LOG_ERROR("HANG WATCHDOG: no framebuffer flip for {} vblanks{} (renderer idle {}, guest wake-idle {}) — dumping guest threads",
                    stall_vblanks, never_flipped ? " (game NEVER flipped since boot)" : "", renderer_idle_vblanks, guest_idle_vblanks);
                emuenv.kernel.log_thread_hang_dump();
            }
            if (never_flipped && stall_vblanks > 3600 && unpaused && !never_flip_redumped) {
                never_flip_redumped = true;
                LOG_ERROR("HANG WATCHDOG: STILL no first flip after {} vblanks — re-dumping for movement comparison",
                    stall_vblanks);
                emuenv.kernel.log_thread_hang_dump();
            }
#endif
            // Cycle breaker
            constexpr uint64_t PROVABLE_BREAK_VBLANKS = 3600;
            const int64_t now_epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const bool world_stop_quiet = (now_epoch_ms - emuenv.kernel.last_world_stop_epoch_ms.load(std::memory_order_relaxed)) >= 5000;
            // deferred-unmap collapses remaps WITHOUT world-stops, blinding the veto above; transitions are stamped regardless
            const bool mem_transition_quiet = !emuenv.renderer
                || (now_epoch_ms - emuenv.renderer->last_mem_transition_epoch_ms.load(std::memory_order_relaxed)) >= 5000;
            if (!emuenv.kernel.is_system_process && !never_flipped && stall_vblanks > PROVABLE_BREAK_VBLANKS && unpaused && renderer_idle_vblanks > PROVABLE_BREAK_VBLANKS && world_stop_quiet && mem_transition_quiet && last_provable_break_setframe != setframe) {
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
            if (!emuenv.kernel.is_system_process && !never_flipped && stall_vblanks > BREAK_STALL_VBLANKS && unpaused
                && (last_break_vblanks == 0 || vblanks - last_break_vblanks > BREAK_STALL_VBLANKS)) {
                last_break_vblanks = vblanks;
                const bool full_wedge = renderer_idle_vblanks > BREAK_STALL_VBLANKS && guest_idle_vblanks > BREAK_STALL_VBLANKS;
                const bool partial_wedge = stall_vblanks > BREAK_SLOW_VBLANKS && renderer_idle_vblanks > BREAK_SLOW_VBLANKS;
                if (!full_wedge && !partial_wedge) {
                    LOG_WARN("HANG WATCHDOG: no flip for {} vblanks but still alive (renderer executed a command {} vblanks ago, a guest thread woke {} vblanks ago) — breaker suppressed", stall_vblanks, renderer_idle_vblanks, guest_idle_vblanks);
                } else {
#ifndef NDEBUG
                    // dump BEFORE mutating anything so the log always records what was stuck on what
                    LOG_ERROR("HANG WATCHDOG: no flip for {} vblanks, renderer idle {}, guest wake-idle {} — dumping guest threads, then breaking", stall_vblanks, renderer_idle_vblanks, guest_idle_vblanks);
                    emuenv.kernel.log_thread_hang_dump();
#endif
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

#ifndef NDEBUG
        pacing_trace_vblank();
#endif
        const auto time_ms = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        const int64_t interval = 1000000LL / display.refresh_hz;
        const auto time_left = interval - (time_ms % interval);

        // Guest vblank follows the panel: the next tick is the panel vsync on which the 60 Hz accumulator
        // overflows, so a flip made after a tick is always presented on the same refresh cadence.
        if (display.panel_vsync && display.follow_panel_vsync && emuenv.cfg.current_config.v_sync) {
            // The timer drives the guest (exactly as before) while the refresh rate is still being
            // measured and while the source delivers nothing (app hidden, compositor stalled).
            constexpr auto vsync_silence = std::chrono::milliseconds(50);
            bool timer_mode = vsync_stalled || !follower.locked();
            auto deadline = std::chrono::steady_clock::now() + (timer_mode ? std::chrono::microseconds(time_left) : std::chrono::duration_cast<std::chrono::microseconds>(vsync_silence));
            bool tick_due = false;
            while (!display.abort.load()) {
                const auto now = std::chrono::steady_clock::now();
                if (now >= deadline)
                    break;
                int64_t timestamp_ns = 0;
                if (!display.panel_vsync->wait_next(std::chrono::duration_cast<std::chrono::microseconds>(deadline - now), timestamp_ns))
                    break;
                if (vsync_stalled) {
                    vsync_stalled = false;
                    follower.resync();
                }
                // Several vblanks due at once (missed vsyncs) are not replayed, the timer never did either.
                const bool due = follower.on_vsync(timestamp_ns) > 0;
                if (timer_mode) {
                    if (!follower.locked())
                        continue;
                    timer_mode = false;
                    deadline = std::chrono::steady_clock::now() + vsync_silence;
                } else {
                    deadline = std::chrono::steady_clock::now() + vsync_silence;
                }
                if (due) {
                    tick_due = true;
                    break;
                }
            }
            if (!tick_due && !timer_mode && !display.abort.load()) {
                // no vsync for 50 ms: fall back to the timer until they return
                vsync_stalled = true;
                follower.resync();
            }
            continue;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(time_left));
    }
}

void start_sync_thread(EmuEnvState &emuenv) {
    register_display_wait_continuations(emuenv.display, emuenv.kernel);
    if (emuenv.display.follow_panel_vsync && !emuenv.display.panel_vsync)
        emuenv.display.panel_vsync = create_panel_vsync_source();
    emuenv.display.vblank_thread = std::make_unique<std::thread>(vblank_sync_thread, std::ref(emuenv));
}

void wait_vblank(DisplayState &display, const ThreadStatePtr &wait_thread, const uint64_t target_vcount, const bool is_cb) {
    if (!wait_thread) {
        return;
    }

    WaitContinuationScope continuation(*wait_thread, WaitOperation::display_vblank,
        { static_cast<uint32_t>(target_vcount), static_cast<uint32_t>(target_vcount >> 32) }, nullptr, is_cb, Deadline::max());
    std::unique_lock<std::mutex> lock(display.mutex);

    if (!continuation.resuming && target_vcount <= display.vblank_count)
        return;

    // Nothing runs after the wait, so a thread exiting during it needs no handling here
    (void)display.vblank_waiters.wait(lock, wait_thread, { SCE_KERNEL_WAITTYPE_EVENT }, { target_vcount }, Deadline::max(), is_cb);
}

void register_display_wait_continuations(DisplayState &display, KernelState &kernel) {
    kernel.wait_resume_handlers[WaitOperation::display_vblank] = {
        [&display, &kernel](ThreadState &thread, const std::shared_ptr<WaitContinuation> &record) {
            const auto &args = record->state.args;
            const uint64_t target = uint64_t(args[0]) | (uint64_t(args[1]) << 32);
            auto self = kernel.get_thread(thread.id);
            const std::lock_guard lock(display.mutex);
            display.vblank_waiters.restore(self, { target }, record);
            auto detach = std::move(record->detach_restored_waiter);
            record->detach_restored_waiter = [&display, detach = std::move(detach)] {
                const std::lock_guard lock(display.mutex);
                detach();
            };
            return true;
        },
        [&display, &kernel](ThreadState &thread, const std::shared_ptr<WaitContinuation> &record) -> SceInt32 {
            const auto &state = record->state;
            const uint64_t target = uint64_t(state.args[0]) | (uint64_t(state.args[1]) << 32);
            const uint32_t input_nid = input_read_import_at_wait(state.context, [&](uint64_t address, void *out, size_t size) {
                // The stub is twelve bytes, so at most two allocation pages.
                if (address + size > UINT32_MAX
                    || !is_valid_addr(thread.get_mem(), address)
                    || !is_valid_addr(thread.get_mem(), address + size - 1))
                    return false;
                std::memcpy(out, Ptr<void>(address).get(thread.get_mem()), size);
                return true;
            });
            wait_vblank(display, kernel.get_thread(thread.id), target, state.callbacks);
            if (!display.abort.load() && input_nid) {
                if (!thread.cpu || !kernel.call_import)
                    return static_cast<SceInt32>(SCE_KERNEL_ERROR_INVALID_ARGUMENT);
                const InputReadResumeScope completion;
                kernel.call_import(*thread.cpu, input_nid, thread.id);
                return static_cast<SceInt32>(read_reg(*thread.cpu, 0));
            }
            // Matches display_wait's completion in modules/SceDisplay/SceDisplay.h.
            return display.abort.load() ? static_cast<SceInt32>(0x80290008U) : SCE_KERNEL_OK;
        }
    };
}

void clear_display_waiters_for_restore(DisplayState &display) {
    const std::lock_guard lock(display.mutex);
    display.vblank_waiters = {};
    display.vblank_callbacks.clear();
}

bool DisplayState::begin_vblank_update() {
    std::unique_lock lock(producer_mutex);
    producer_changed.wait(lock, [&] { return abort.load() || (producer_freeze_count == 0 && !shell_suspended); });
    if (abort.load())
        return false;
    assert(!producer_active);
    producer_active = true;
    return true;
}
void DisplayState::end_vblank_update() {
    const std::lock_guard lock(producer_mutex);
    assert(producer_active);
    producer_active = false;
    producer_changed.notify_all();
}
void DisplayState::set_shell_suspended(bool suspended) {
    const std::lock_guard lock(producer_mutex);
    shell_suspended = suspended;
    producer_changed.notify_all();
}
bool DisplayState::freeze_vblank_producer(std::chrono::milliseconds budget) {
    std::unique_lock lock(producer_mutex);
    ++producer_freeze_count;
    if (producer_changed.wait_for(lock, budget, [&] { return !producer_active; }))
        return true;
    --producer_freeze_count;
    producer_changed.notify_all();
    return false;
}
void DisplayState::resume_vblank_producer() {
    const std::lock_guard lock(producer_mutex);
    assert(producer_freeze_count != 0);
    --producer_freeze_count;
    producer_changed.notify_all();
}
ScopedVblankFreeze::ScopedVblankFreeze(DisplayState &display, std::chrono::milliseconds budget) {
    if (display.freeze_vblank_producer(budget))
        this->display = &display;
}
ScopedVblankFreeze::~ScopedVblankFreeze() {
    if (display)
        display->resume_vblank_producer();
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
        if (frame.base)
            emuenv.renderer->restore_presentation.fresh_frame();
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
        if (frame.base)
            emuenv.renderer->restore_presentation.fresh_frame();
        emuenv.renderer->should_display = true;
    }

    // let predict_next_image reset the cycle if necessary
    display.predicted_cycles_seen = std::min(display.predicted_cycles_seen, 1U);
}

void DisplayState::deinit() {
    abort = true;
    producer_changed.notify_all();
    if (vblank_thread && vblank_thread->joinable())
        vblank_thread->join();

    vblank_thread.reset();
    panel_vsync.reset();
    abort = false;

    {
        const std::lock_guard<std::mutex> guard(mutex);
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
    shell_suspended = false;
    refresh_hz = 60;
    follow_panel_vsync = false;
    // pretty sure we set this on game boot
    fullscreen = false;
}
