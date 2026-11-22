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

#include <chrono>
#include <future>
#include <memory>
#include <renderer/commands.h>
#include <renderer/driver_functions.h>
#include <renderer/state.h>
#include <renderer/types.h>

#include <display/state.h>
#include <renderer/gl/functions.h>
#include <renderer/gl/state.h>
#include <renderer/vulkan/functions.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/types.h>

#include <renderer/functions.h>
#include <mem/functions.h>
#include <util/tracy.h>

namespace renderer {
bool State::run_on_render_thread(std::function<bool()> task, std::chrono::milliseconds timeout) {
    if (!task || !render_thread || render_abort.load(std::memory_order_relaxed))
        return false;

    std::unique_lock lock(render_task_mutex);
    if (render_task_phase != RenderTaskPhase::Idle)
        return false;
    render_task = std::move(task);
    render_task_result = false;
    render_task_phase = RenderTaskPhase::Pending;

    render_task_cv.wait_for(lock, timeout, [&] {
        return render_task_phase == RenderTaskPhase::Finished || render_abort.load(std::memory_order_relaxed);
    });
    if (render_task_phase == RenderTaskPhase::Pending) {
        // never started: withdraw it
        render_task = nullptr;
        render_task_phase = RenderTaskPhase::Idle;
        return false;
    }
    // A running task cannot be abandoned: the render thread always finishes it.
    render_task_cv.wait(lock, [&] { return render_task_phase == RenderTaskPhase::Finished; });
    const bool ok = render_task_result;
    render_task_phase = RenderTaskPhase::Idle;
    return ok;
}

void State::service_render_thread_task() {
    std::function<bool()> task;
    {
        const std::lock_guard lock(render_task_mutex);
        if (render_task_phase != RenderTaskPhase::Pending)
            return;
        task = std::move(render_task);
        render_task = nullptr;
        render_task_phase = RenderTaskPhase::Running;
    }
    bool result = false;
    try {
        result = task();
    } catch (...) {
        result = false;
    }
    {
        const std::lock_guard lock(render_task_mutex);
        render_task_result = result;
        render_task_phase = RenderTaskPhase::Finished;
    }
    render_task_cv.notify_all();
}

void State::enqueue_finish_drain(std::function<void()> completion) {
    if (current_backend == Backend::Vulkan && features.enable_memory_mapping) {
        auto &vk = static_cast<vulkan::VKState &>(*this);
        vk.request_queue.push(vulkan::CallbackRequest{
            new vulkan::CallbackRequestFunction(std::move(completion)), true });
    } else {
        completion();
    }
}
COMMAND(handle_nop) {
    TRACY_FUNC_COMMANDS(handle_nop);
    LOG_DEBUG("[renderer] handle_nop");
    // Signal back to client
    int code_to_finish = helper.pop<int>();
    complete_command(renderer, helper, code_to_finish);
}

COMMAND(handle_signal_sync_object) {
    TRACY_FUNC_COMMANDS(handle_signal_sync_object);
    SceGxmSyncObject *sync = helper.pop<Ptr<SceGxmSyncObject>>().get(mem);
    const uint32_t timestamp = helper.pop<uint32_t>();

    if (features.enable_memory_mapping && config.current_config.high_accuracy) {
        assert(renderer.current_backend == renderer::Backend::Vulkan);
        vulkan::signal_sync_object(dynamic_cast<vulkan::VKState &>(renderer), sync, timestamp);
    } else {
        renderer::subject_done(sync, timestamp);
    }
}

COMMAND(handle_wait_sync_object) {
    TRACY_FUNC_COMMANDS(handle_wait_sync_object);
    SceGxmSyncObject *sync = helper.pop<Ptr<SceGxmSyncObject>>().get(mem);
    const uint32_t timestamp = helper.pop<uint32_t>();

    renderer::wishlist(sync, timestamp);
}

bool write_notification_value(const MemState &mem, const SceGxmNotification &notification) {
    // The address is guest controlled: only write when the whole word is inside guest memory.
    // A null address is legal (Ratchet and clank Trilogy submits one) and silently ignored.
    if (!notification.address)
        return false;
    const Address addr = notification.address.address();
    if (addr > 0xFFFFFFFCu || !is_valid_addr_range(mem, addr, addr + sizeof(uint32_t))) {
        LOG_WARN("Notification address {} is not valid guest memory, ignoring", log_hex(addr));
        return false;
    }
    *notification.address.get(mem) = notification.value;
    return true;
}

void State::wake_all_waiters() {
    // The flag a waiter's predicate reads is already stored. Taking each waiter
    // mutex once orders that store before any waiter that is still between its
    // predicate check and its wait, so the notify below cannot be lost.
    { const std::lock_guard<std::mutex> lock(notification_mutex); }
    notification_ready.notify_all();
    { const std::lock_guard<std::mutex> lock(command_finish_one_mutex); }
    command_finish_one.notify_all();
}

void State::signal_fatal_error(const std::string &message, bool is_device_lost) {
    if (is_device_lost)
        device_lost.store(true);
    if (!fatal_error_raised.exchange(true)) {
        {
            const std::lock_guard<std::mutex> lock(fatal_error_mutex);
            fatal_error_message = message;
        }
        LOG_CRITICAL("[renderer] fatal GPU error{}: {}. Stopping the render session.", is_device_lost ? " (device lost)" : "", message);
    }
    render_abort.store(true);
    set_shell_suspended(false);
    command_buffer_queue.abort();
    abort_backend_queues();
    if (fatal_abort_hook)
        fatal_abort_hook();
    abort_finish_operations(*this);
    wake_all_waiters();
}

COMMAND(handle_notification) {
    TRACY_FUNC_COMMANDS(handle_notification);
    SceGxmNotification notif = helper.pop<SceGxmNotification>();

    {
        std::unique_lock<std::mutex> lock(renderer.notification_mutex);
        write_notification_value(mem, notif);
    }
    renderer.notification_ready.notify_all();
}

COMMAND(handle_set_screen_filter) {
    TRACY_FUNC_COMMANDS(handle_set_screen_filter);
    std::unique_ptr<std::string> filter(helper.pop<std::string *>());

    switch (renderer.current_backend) {
    case Backend::OpenGL:
        dynamic_cast<gl::GLState &>(renderer).set_screen_filter(*filter);
        break;

    case Backend::Vulkan:
        dynamic_cast<vulkan::VKState &>(renderer).screen_renderer.set_filter(*filter);
        break;
    }
}

COMMAND(new_frame) {
    TRACY_FUNC_COMMANDS(new_frame);
    DisplayFrameInfo *next_frame = helper.pop<DisplayFrameInfo *>();
    DisplayState *display = helper.pop<DisplayState *>();

    if (next_frame) {
        // set the predicted frame as the next one to render
        std::lock_guard<std::mutex> guard(display->display_info_mutex);
        display->next_rendered_frame = *next_frame;
        if (next_frame->base)
            renderer.restore_presentation.fresh_frame();
        delete next_frame;

        renderer.should_display = true;
    }

    if (renderer.current_backend == Backend::Vulkan) {
        renderer::Context *active_context = helper.pop<renderer::Context *>();
        if (active_context) {
            vulkan::new_frame(*reinterpret_cast<vulkan::VKContext *>(active_context));
        }
    }
}

// Client side function
void finish(State &state, Context *context) {
    LOG_DEBUG("[renderer] finish: queue={} display={} abort={}", state.command_buffer_queue.size(), state.should_display, state.render_abort.load());
    // Add NOP then wait for it
    renderer::send_single_command(state, context, renderer::CommandOpcode::Nop, true, 1);
    LOG_DEBUG("[renderer] finish: nop processed");

    // unblock game threads if shutting down
    if (state.render_abort.load(std::memory_order_relaxed))
        return;

    // Wait for the VK wait thread to finish processing all pending requests.
    // Push a callback request on the queue and wait for it to be treated
    if (state.current_backend == Backend::Vulkan && state.features.enable_memory_mapping) {
        LOG_DEBUG("[renderer] finish: draining vk wait queue");
        auto &vk_state = static_cast<vulkan::VKState &>(state);
        auto promise = std::make_shared<std::promise<void>>();
        std::future<void> future = promise->get_future();
        vk_state.request_queue.push(vulkan::CallbackRequest{
            new vulkan::CallbackRequestFunction([promise]() { promise->set_value(); }), /* wait_for_gpu = */ true });

        while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready) {
            if (state.render_abort.load(std::memory_order_relaxed) || vk_state.request_queue.is_aborted())
                return;
        }
        LOG_DEBUG("[renderer] finish: vk wait queue drained");
    }
}

int wait_for_status(State &state, int *status, int signal, bool wake_on_equal) {
    std::unique_lock<std::mutex> lock(state.command_finish_one_mutex);
    const bool wake_on_unequal = !wake_on_equal;
    if ((*status == signal) ^ wake_on_unequal) {
        // Signaled, return
        return *status;
    }

    // unblock threads if shutting down
    state.command_finish_one.wait(lock, [&]() {
        return state.render_abort.load(std::memory_order_relaxed)
            || ((*status == signal) ^ wake_on_unequal);
    });
    return *status;
}

SyncWaitResult wishlist(SceGxmSyncObject *sync_object, const uint32_t timestamp, const int32_t timeout_micros) {
    std::unique_lock<std::mutex> lock(sync_object->lock);
    if (sync_object->timestamp_current < timestamp) {
        const auto &pred = [&]() {
            return sync_object->being_deleted || sync_object->timestamp_current >= timestamp;
        };

        if (timeout_micros == -1) {
            sync_object->cond.wait(lock, pred);
        } else if (!sync_object->cond.wait_for(lock, std::chrono::microseconds(timeout_micros), pred)) {
            return SyncWaitResult::TimedOut;
        }
    }
    if (sync_object->being_deleted)
        return SyncWaitResult::Shutdown;
    return SyncWaitResult::Ready;
}

void subject_done(SceGxmSyncObject *sync_object, const uint32_t timestamp) {
    assert(sync_object->timestamp_ahead >= timestamp);
    {
        std::unique_lock<std::mutex> lock(sync_object->lock);
        sync_object->timestamp_current = std::max(sync_object->timestamp_current.load(), timestamp);
    }
    // maybe notify_one is enough
    sync_object->cond.notify_all();
}

void submit_command_list(State &state, renderer::Context *context, CommandList &command_list) {
    command_list.context = context;
    state.command_buffer_queue.push(std::move(command_list));
}
} // namespace renderer
