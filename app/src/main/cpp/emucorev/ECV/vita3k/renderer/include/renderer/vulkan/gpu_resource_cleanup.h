#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

namespace renderer::vulkan {

// Count consumers that have actually entered their request loop. A freshly
// created thread need not have started yet; synchronous cleanup remains safe.
struct ActiveGpuRequestReader {
    std::atomic<size_t> &count;
    explicit ActiveGpuRequestReader(std::atomic<size_t> &count) : count(count) {
        count.fetch_add(1, std::memory_order_acq_rel);
    }
    ActiveGpuRequestReader(const ActiveGpuRequestReader &) = delete;
    ActiveGpuRequestReader &operator=(const ActiveGpuRequestReader &) = delete;
    ~ActiveGpuRequestReader() { count.fetch_sub(1, std::memory_order_acq_rel); }
};

// Submitted resources must also be released when a queued callback is discarded
// after all readers have joined. The device and command pool must still be alive.
class GpuResourceCleanup {
    std::function<void()> cleanup;
    std::function<void()> report_failure;
    std::atomic<bool> completed{ false };

public:
    GpuResourceCleanup(std::function<void()> cleanup, std::function<void()> report_failure)
        : cleanup(std::move(cleanup)), report_failure(std::move(report_failure)) {}
    ~GpuResourceCleanup() noexcept {
        try {
            run();
        } catch (...) {
            // Destruction cannot propagate a Vulkan failure. Report it without
            // retrying a fence/pool operation whose device may already be lost.
            try { report_failure(); } catch (...) {}
        }
    }
    void run() {
        if (!completed.exchange(true, std::memory_order_acq_rel)) cleanup();
    }
};

template <typename Publish>
void dispatch_gpu_resource_cleanup(bool has_readers, Publish publish,
    std::function<void()> cleanup, std::function<void()> report_failure) {
    const auto owner = std::make_shared<GpuResourceCleanup>(std::move(cleanup), std::move(report_failure));
    if (has_readers) {
        auto callback = std::make_unique<std::function<void()>>([owner]() { owner->run(); });
        if (publish(callback.get())) {
            callback.release();
            return;
        }
    }
    // No reader, or shutdown won publication. Keep the actual GPU copy and wait
    // for that copy's own fence before releasing its host resources.
    owner->run();
}

} // namespace renderer::vulkan
