#pragma once

#include <renderer/vulkan/frame_runtime_reset.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

namespace renderer::vulkan {

// Device idle alone does not finish CPU handlers already running on request
// readers. One blocked marker per reader forces every consumer past all older
// requests before the snapshot proceeds.
inline bool wait_for_gpu_snapshot_requests(Queue<WaitThreadRequest> &queue,
    size_t worker_count, std::chrono::steady_clock::duration timeout, std::string &error) {
    if (queue.is_aborted()) {
        error = "GPU request queue is stopped";
        return false;
    }
    if (worker_count == 0) return true;

    struct Barrier {
        std::mutex mutex;
        std::condition_variable changed;
        size_t arrived = 0;
        bool released = false;
    };
    const auto barrier = std::make_shared<Barrier>();
    struct ReleaseOnExit {
        std::shared_ptr<Barrier> barrier;
        ~ReleaseOnExit() {
            {
                const std::lock_guard lock(barrier->mutex);
                barrier->released = true;
            }
            barrier->changed.notify_all();
        }
    } release_on_exit{ barrier };
    const std::weak_ptr<Barrier> weak_barrier = barrier;
    const uint64_t subscription = queue.subscribe_changes([weak_barrier]() {
        if (const auto locked = weak_barrier.lock()) locked->changed.notify_all();
    });
    struct Unsubscribe {
        Queue<WaitThreadRequest> &queue;
        uint64_t token;
        ~Unsubscribe() { queue.unsubscribe_changes(token); }
    } unsubscribe{ queue, subscription };

    for (size_t i = 0; i < worker_count; ++i) {
        auto callback = std::make_unique<CallbackRequestFunction>([barrier]() {
            std::unique_lock lock(barrier->mutex);
            if (barrier->released) return;
            ++barrier->arrived;
            barrier->changed.notify_all();
            barrier->changed.wait(lock, [&]() { return barrier->released; });
        });
        if (!enqueue_gpu_request_callback(queue, std::move(callback), false)) {
            {
                const std::lock_guard lock(barrier->mutex);
                barrier->released = true;
            }
            barrier->changed.notify_all();
            error = "could not enqueue GPU snapshot barrier";
            return false;
        }
    }

    bool completed = false;
    {
        std::unique_lock lock(barrier->mutex);
        completed = barrier->changed.wait_for(lock, timeout, [&]() {
            return barrier->arrived == worker_count || queue.is_aborted();
        }) && !queue.is_aborted() && barrier->arrived == worker_count;
        barrier->released = true;
    }
    barrier->changed.notify_all();
    if (!completed) {
        error = queue.is_aborted()
            ? "GPU request queue stopped during snapshot barrier"
            : "timed out waiting for GPU request readers";
    }
    return completed;
}

} // namespace renderer::vulkan
