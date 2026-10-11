#include <threads/queue.h>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

TEST(QueueAbort, WakesConsumersProducersAndEmptyWaitersAcrossConcurrentEnrollment) {
    for (unsigned generation = 0; generation < 32; ++generation) {
        Queue<int> empty, full;
        full.maxPendingCount_ = 1;
        full.push(1);
        std::atomic<unsigned> entering = 0;
        std::array<std::promise<void>, 5> completions;
        std::array<std::future<void>, 5> completed;
        std::array<std::thread, 5> workers;
        for (unsigned index = 0; index < workers.size(); ++index) {
            completed[index] = completions[index].get_future();
            workers[index] = std::thread([&, index]() {
                ++entering;
                if (index < 2) empty.pop();
                else if (index < 4) full.push(2);
                else full.wait_empty();
                completions[index].set_value();
            });
        }
        while (entering.load() < workers.size()) std::this_thread::yield();
        // Race abort with enrollment, including consumers that have not yet
        // acquired the predicate lock and producers blocked by capacity.
        empty.abort();
        full.abort();
        bool all_completed = true;
        for (auto &future : completed) {
            if (future.wait_for(std::chrono::milliseconds(200)) != std::future_status::ready) {
                all_completed = false;
                break;
            }
        }
        // A second notification cleans up a missed-wake regression before
        // worker references or their queues leave scope.
        empty.abort();
        full.abort();
        for (auto &worker : workers) worker.join();
        EXPECT_TRUE(all_completed) << "generation " << generation;
        EXPECT_EQ(full.size(), 1u);
    }
}

TEST(QueueAbort, ChangeSubscribersCanReadQueueAfterAbort) {
    Queue<int> queue;
    queue.push(7);
    unsigned notifications = 0;
    bool observed_aborted = false;
    size_t observed_size = 0;
    const auto subscription = queue.subscribe_changes([&]() {
        ++notifications;
        observed_aborted = queue.is_aborted();
        observed_size = queue.size();
    });
    queue.abort();
    queue.unsubscribe_changes(subscription);
    EXPECT_EQ(notifications, 1u);
    EXPECT_TRUE(observed_aborted);
    EXPECT_EQ(observed_size, 1u);
}
