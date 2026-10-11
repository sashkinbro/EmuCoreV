#include <ngs/update_admission.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

TEST(NgsUpdateAdmission, SnapshotGateDrainsExistingCallsAndRejectsNewOnes) {
    using namespace std::chrono_literals;
    ngs::UpdateAdmission admission;
    auto update = admission.try_enter_update();
    ASSERT_TRUE(update);

    std::promise<void> gate_attempting;
    std::promise<void> release_gate;
    auto release = release_gate.get_future().share();
    std::atomic<bool> drained{false};
    std::thread saver([&] {
        ngs::UpdateAdmission::SnapshotLease snapshot;
        gate_attempting.set_value();
        const auto status = admission.close_and_drain(10s, snapshot);
        drained = status == ngs::UpdateAdmission::DrainStatus::drained;
        if (drained)
            release.wait();
    });

    gate_attempting.get_future().wait();
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    bool gate_closed = false;
    while (std::chrono::steady_clock::now() < deadline) {
        auto concurrent_update = admission.try_enter_update();
        if (!concurrent_update) {
            gate_closed = true;
            break;
        }
        std::this_thread::yield();
    }
    EXPECT_TRUE(gate_closed);
    EXPECT_FALSE(drained.load()); // The first update is still executing.

    update = {};
    const auto drain_deadline = std::chrono::steady_clock::now() + 2s;
    while (!drained.load() && std::chrono::steady_clock::now() < drain_deadline)
        std::this_thread::yield();
    EXPECT_TRUE(drained.load());
    EXPECT_FALSE(admission.try_enter_update()); // Gate remains closed until RAII release.

    release_gate.set_value();
    saver.join();
    EXPECT_TRUE(admission.try_enter_update());
}

TEST(NgsUpdateAdmission, TimedOutDrainReopensAdmission) {
    using namespace std::chrono_literals;
    ngs::UpdateAdmission admission;
    auto update = admission.try_enter_update();
    ASSERT_TRUE(update);

    ngs::UpdateAdmission::SnapshotLease snapshot;
    EXPECT_EQ(admission.close_and_drain(1ms, snapshot), ngs::UpdateAdmission::DrainStatus::timed_out);
    EXPECT_FALSE(snapshot);
    EXPECT_TRUE(admission.try_enter_update());
}

TEST(NgsUpdateAdmission, ConcurrentSnapshotGateDoesNotReleaseAnotherOwnersGate) {
    using namespace std::chrono_literals;
    ngs::UpdateAdmission admission;
    ngs::UpdateAdmission::SnapshotLease first;
    ASSERT_EQ(admission.close_and_drain(1s, first), ngs::UpdateAdmission::DrainStatus::drained);
    ASSERT_TRUE(first);

    ngs::UpdateAdmission::SnapshotLease second;
    EXPECT_EQ(admission.close_and_drain(1ms, second), ngs::UpdateAdmission::DrainStatus::already_closed);
    EXPECT_FALSE(second);
    EXPECT_FALSE(admission.try_enter_update());

    first = {};
    EXPECT_TRUE(admission.try_enter_update());
}

TEST(NgsUpdateAdmission, ReentrantUpdateLeasesDrainOnlyAfterEveryCallReturns) {
    using namespace std::chrono_literals;
    ngs::UpdateAdmission admission;
    auto outer = admission.try_enter_update();
    auto nested = admission.try_enter_update();
    ASSERT_TRUE(outer);
    ASSERT_TRUE(nested);

    ngs::UpdateAdmission::SnapshotLease snapshot;
    EXPECT_EQ(admission.close_and_drain(1ms, snapshot), ngs::UpdateAdmission::DrainStatus::timed_out);

    nested = {};
    EXPECT_EQ(admission.close_and_drain(1ms, snapshot), ngs::UpdateAdmission::DrainStatus::timed_out);
    outer = {};
    EXPECT_EQ(admission.close_and_drain(1s, snapshot), ngs::UpdateAdmission::DrainStatus::drained);
}
