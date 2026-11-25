// Panel vsync source for the guest vblank (see frame_pacing.h).

#include <display/state.h>

#ifdef __ANDROID__

#include <util/log.h>

#include <android/choreographer.h>
#include <android/looper.h>
#include <pthread.h>

// AChoreographer_postFrameCallback64 is the only vsync callback available on minSdk 29.
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

// Runs an AChoreographer frame callback loop on its own looper thread and hands the vsync
// timestamps to the vblank thread. The callback only stores a timestamp and wakes the waiter.
class ChoreographerVsync final : public PanelVsyncSource {
public:
    ChoreographerVsync() {
        thread = std::thread([this] { run(); });
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [this] { return started; });
    }

    ~ChoreographerVsync() override {
        quit.store(true, std::memory_order_release);
        {
            const std::lock_guard<std::mutex> lock(mutex);
            if (looper)
                ALooper_wake(looper);
        }
        if (thread.joinable())
            thread.join();
    }

    bool wait_next(std::chrono::microseconds timeout, int64_t &timestamp_ns) override {
        std::unique_lock<std::mutex> lock(mutex);
        if (!cv.wait_for(lock, timeout, [this] { return sequence != consumed || quit.load(std::memory_order_acquire); }))
            return false;
        if (sequence == consumed)
            return false;
        consumed = sequence;
        timestamp_ns = last_timestamp_ns;
        return true;
    }

private:
    static void on_frame(int64_t frame_time_nanos, void *data) {
        auto *self = static_cast<ChoreographerVsync *>(data);
        {
            const std::lock_guard<std::mutex> lock(self->mutex);
            self->last_timestamp_ns = frame_time_nanos;
            ++self->sequence;
        }
        self->cv.notify_one();
        if (!self->quit.load(std::memory_order_acquire))
            AChoreographer_postFrameCallback64(AChoreographer_getInstance(), on_frame, self);
    }

    void run() {
        pthread_setname_np(pthread_self(), "ecv-vsync");
        ALooper *own = ALooper_prepare(0);
        AChoreographer *choreographer = AChoreographer_getInstance();
        {
            const std::lock_guard<std::mutex> lock(mutex);
            looper = own;
            started = true;
        }
        cv.notify_all();
        if (!choreographer) {
            LOG_WARN("Panel vsync: no AChoreographer on this thread, guest vblank keeps its timer");
            return;
        }
        AChoreographer_postFrameCallback64(choreographer, on_frame, this);
        while (!quit.load(std::memory_order_acquire))
            ALooper_pollOnce(100, nullptr, nullptr, nullptr);
        const std::lock_guard<std::mutex> lock(mutex);
        looper = nullptr;
    }

    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    ALooper *looper = nullptr;
    bool started = false;
    uint64_t sequence = 0;
    uint64_t consumed = 0;
    int64_t last_timestamp_ns = 0;
    std::atomic<bool> quit{ false };
};

} // namespace

std::unique_ptr<PanelVsyncSource> create_panel_vsync_source() {
    return std::make_unique<ChoreographerVsync>();
}

#else

std::unique_ptr<PanelVsyncSource> create_panel_vsync_source() {
    return nullptr;
}

#endif
