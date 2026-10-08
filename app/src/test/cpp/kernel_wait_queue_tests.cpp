// Host regression tests for the production wait queue; no emulator or device required.
#include <kernel/thread/wait.h>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <cassert>
#include <iostream>

// The queue only depends on this boundary of ThreadState. Simulate its wake transport,
// while exercising the production queue and deadline/result helpers directly.
struct ThreadState {
    int priority = 100;
    std::mutex mutex;
    std::condition_variable cv;
    bool pending = false;
    bool exiting = false;
    std::function<void()> on_wait;
    WaitResult wait(WaitTarget, Deadline deadline, bool) {
        if (on_wait) on_wait();
        std::unique_lock lock(mutex);
        if (!cv.wait_until(lock, deadline, [&] { return pending || exiting; }))
            return SCE_KERNEL_ERROR_WAIT_TIMEOUT;
        if (exiting) return std::unexpected{ThreadExiting{}};
        pending = false;
        return SCE_KERNEL_OK;
    }
    void wake() { std::lock_guard lock(mutex); pending = true; cv.notify_all(); }
};
#include <kernel/thread/wait_queue.h>
std::shared_ptr<void> take_restored_wait_node(const ThreadStatePtr &, WaitTarget) { return {}; }
std::shared_ptr<WaitContinuation> get_wait_continuation(const ThreadStatePtr &) { return {}; }
using namespace std::chrono_literals;

int main() {
    auto thread = std::make_shared<ThreadState>();
    std::mutex mutex;
    std::unique_lock lock(mutex);
    WaitQueue<int> queue;
    auto result = queue.wait(lock, thread, {}, 7, std::chrono::steady_clock::now()+2ms, false);
    assert(result && *result == SCE_KERNEL_ERROR_WAIT_TIMEOUT);
    assert(queue.empty());
    thread->exiting = true;
    result = queue.wait(lock, thread, {}, 7, Deadline::max(), false);
    assert(!result && queue.empty());
    thread->exiting = false;
    thread->on_wait = [&] {
        std::lock_guard guard(mutex);
        assert(queue.front()->entry == 7);
        queue.wake(*queue.front(), SCE_KERNEL_ERROR_WAIT_CANCEL);
    };
    result = queue.wait(lock, thread, {}, 7, Deadline::max(), false);
    assert(result && *result == SCE_KERNEL_ERROR_WAIT_CANCEL && queue.empty());
    thread->on_wait = {};
    WaitQueue<int> ordered(SCE_KERNEL_ATTR_TH_PRIO);
    auto high = std::make_shared<ThreadState>(); high->priority = 70;
    WaitQueue<int>::Waiter low{.thread=thread, .priority=100, .entry=1};
    WaitQueue<int>::Waiter first{.thread=high, .priority=70, .entry=2};
    WaitQueue<int>::Waiter second{.thread=high, .priority=70, .entry=3};
    ordered.push(low); ordered.push(first); ordered.push(second);
    assert(ordered.front()->entry == 2); ordered.wake(*ordered.front());
    assert(ordered.front()->entry == 3); ordered.wake(*ordered.front());
    assert(ordered.front()->entry == 1); ordered.wake(*ordered.front());
    assert(ordered.empty());
    std::cout << "WaitQueue timeout, exiting cleanup, handover and priority/FIFO tests passed\n";
}
