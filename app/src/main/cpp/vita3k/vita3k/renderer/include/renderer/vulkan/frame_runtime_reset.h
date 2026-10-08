#pragma once

#include <renderer/vulkan/types.h>
#include <threads/queue.h>
#include <thread>

namespace renderer::vulkan {
// The old contexts/workers must be gone and the device idle. Drop every fence
// alias first: a target's deferred destruction can be owned by another frame.
template <typename Frames, typename Drain, typename ResetPools>
void reset_frame_runtime_after_idle(Frames &frames, Drain drain, ResetPools reset_pools) {
    for (auto &frame : frames) frame.rendered_fences.clear();
    for (auto &frame : frames) drain(frame);
    for (auto &frame : frames) {
        reset_pools(frame);
        for (auto &descriptor : frame.vert_descriptors) descriptor.descriptors_idx = 0;
        for (auto &descriptor : frame.frag_descriptors) descriptor.descriptors_idx = 0;
        frame.color_descriptor.descriptors_idx = 0;
        frame.frame_timestamp = 0;
    }
}

// Joining a context waits for its queue reader. Abort only after GPU completion
// and the old notification/writeback barrier, before destroying any context.
template <typename Drain>
void stop_gpu_requests_after_idle(Queue<WaitThreadRequest> &queue, Drain drain) {
    drain();
    queue.abort();
}

// A queue marker observes extraction order, not completion by every consumer.
// Join all old readers before any mapped buffer is released or replaced.
template <typename Workers>
bool join_gpu_request_workers_after_stop(Workers &workers, std::string &error) {
    for (auto *worker : workers) {
        if (worker->joinable() && worker->get_id() == std::this_thread::get_id()) {
            error = "GPU request teardown cannot join its own worker";
            return false;
        }
    }
    try {
        for (auto *worker : workers) if (worker->joinable()) worker->join();
        return true;
    } catch (const std::exception &exception) {
        error = std::string("GPU request worker join failed: ") + exception.what();
        return false;
    }
}

// Queue::push silently discards an aborted request. Retain host callback
// ownership unless publication actually succeeds, including an abort race.
inline bool enqueue_gpu_request_callback(Queue<WaitThreadRequest> &queue,
    std::unique_ptr<CallbackRequestFunction> callback, bool wait_for_gpu) {
    if (!queue.try_push(CallbackRequest{ callback.get(), wait_for_gpu })) return false;
    callback.release();
    return true;
}

// Only after all old queue readers have joined. Pending callbacks belong to the
// discarded session and must release their host payload without running again.
inline void reset_gpu_requests_after_join(Queue<WaitThreadRequest> &queue) {
    for (auto &request : queue.snapshot_items())
        if (auto *callback = std::get_if<CallbackRequest>(&request)) delete callback->callback;
    queue.reset();
}
}
