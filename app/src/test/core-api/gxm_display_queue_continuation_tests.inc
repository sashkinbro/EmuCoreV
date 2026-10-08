#include <gxm/display_queue.h>
#include <gxm/savestate.h>
#include <emucorev/savestate/gxm_display_io.h>
#include <cpu/functions.h>

DECL_EXPORT(int, sceGxmDisplayQueueFinish);

namespace {
struct DisplayQueueWaitEnv {
    EmuEnvState env;
    ThreadStatePtr thread;
    std::atomic<unsigned> commits{0};
    PendingDisplaySubmission committed{};
    DisplayQueueWaitEnv() {
        env.mem.memory = Memory(new uint8_t[0x10000]{}, [](uint8_t *p) { delete[] p; });
        env.gxm.params.displayQueueCallbackDataSize = 16;
        env.gxm.params.displayQueueMaxPendingCount = 2;
        env.gxm.display_queue.maxPendingCount_ = 1;
        thread = std::make_shared<ThreadState>(91, env.kernel, env.mem);
        thread->update_status(ThreadStatus::running);
        env.kernel.threads.emplace(thread->id, thread);
        gxm::register_display_queue_wait_handlers(env, commit());
    }
    gxm::DisplaySubmissionCommit commit() {
        return [this](const PendingDisplaySubmission &submission) {
            committed = submission;
            ++commits;
        };
    }
    void prepare() {
        auto pending = std::make_shared<PendingDisplaySubmission>();
        pending->callback = {0x1000, Ptr<SceGxmSyncObject>(0x3000), Ptr<SceGxmSyncObject>(0x3100), 17, 23, true};
        pending->has_prediction = true;
        pending->prediction.base = Ptr<const void>(0x4000);
        pending->prediction.pitch = 32;
        pending->prediction.image_size = {32, 16};
        std::fill_n(Ptr<uint8_t>(0x1000).get(env.mem), 16, 0x69);
        env.gxm.pending_display_submissions.emplace(thread->id, pending);
    }
    bool waiting(unsigned phase) {
        return await_waiter([&] {
            const auto snapshot = thread->capture_snapshot();
            return snapshot.status == ThreadStatus::waiting && snapshot.waits.size() == 1
                && snapshot.waits[0].operation == WaitOperation::gxm_display_queue
                && snapshot.waits[0].args[1] == phase;
        });
    }
    void restore(const ThreadState::Snapshot &snapshot) {
        thread->apply_private_snapshot(snapshot);
        ASSERT_TRUE(thread->restore_wait_queues());
        thread->activate_restored_continuations();
    }
};
}

TEST(GxmDisplayQueueContinuation, full_producer_parks_for_capture_and_restores_prepared_entry_once) {
    DisplayQueueWaitEnv original;
    original.prepare();
    original.env.gxm.display_queue.push({});
    auto pending = lifecycle_async(original.thread, [&] {
        return gxm::wait_display_queue(original.env.kernel, original.env.gxm, original.env.mem,
            *original.thread, false, original.commit());
    });
    ASSERT_TRUE(original.waiting(0));
    EXPECT_EQ(original.env.kernel.stop_world(0, 100ms), 0);
    const auto snapshot = original.thread->capture_snapshot();
    const auto saved = *original.env.gxm.pending_display_submissions.at(original.thread->id);
    EXPECT_EQ(original.commits.load(), 0u);
    original.env.gxm.display_queue.pop();
    EXPECT_EQ(pending.wait_for(20ms), std::future_status::timeout);
    original.env.kernel.resume_world();
    EXPECT_EQ(lifecycle_result(pending, original.thread), 0);
    EXPECT_EQ(original.commits.load(), 1u);

    DisplayQueueWaitEnv restored;
    restored.prepare();
    *restored.env.gxm.pending_display_submissions.at(restored.thread->id) = saved;
    restored.env.gxm.display_queue.push({});
    restored.restore(snapshot);
    const auto record = restored.thread->current_wait_continuation();
    auto resumed = lifecycle_async(restored.thread, [&] { return restored.thread->resume_wait_continuation(record); });
    ASSERT_TRUE(restored.waiting(0));
    restored.env.gxm.display_queue.pop();
    EXPECT_EQ(lifecycle_result(resumed, restored.thread), 0);
    EXPECT_EQ(restored.commits.load(), 1u);
    EXPECT_TRUE(restored.env.gxm.pending_display_submissions.empty());
    ASSERT_EQ(restored.env.gxm.display_queue.size(), 1u);
    const auto entry = restored.env.gxm.display_queue.pop();
    EXPECT_EQ(entry->data, 0x1000u);
    EXPECT_EQ(entry->old_sync_timestamp, 17u);
    EXPECT_EQ(entry->new_sync_timestamp, 23u);
    EXPECT_EQ(restored.committed.prediction.base.address(), 0x4000u);
    EXPECT_EQ(restored.committed.prediction.pitch, 32u);
    EXPECT_EQ(std::vector<uint8_t>(Ptr<uint8_t>(entry->data).get(restored.env.mem),
        Ptr<uint8_t>(entry->data).get(restored.env.mem) + 16), std::vector<uint8_t>(16, 0x69));
}

TEST(GxmDisplayQueueContinuation, single_pending_completion_restores_without_reenqueuing_or_new_frame) {
    DisplayQueueWaitEnv original;
    original.env.gxm.params.displayQueueMaxPendingCount = 1;
    original.prepare();
    auto pending = lifecycle_async(original.thread, [&] {
        return gxm::wait_display_queue(original.env.kernel, original.env.gxm, original.env.mem,
            *original.thread, false, original.commit());
    });
    ASSERT_TRUE(original.waiting(1));
    const auto snapshot = original.thread->capture_snapshot();
    EXPECT_EQ(original.commits.load(), 1u);
    EXPECT_TRUE(original.env.gxm.pending_display_submissions.empty());
    original.env.gxm.display_queue.pop();
    EXPECT_EQ(lifecycle_result(pending, original.thread), 0);
    DisplayQueueWaitEnv restored;
    restored.env.gxm.params.displayQueueMaxPendingCount = 1;
    restored.env.gxm.display_queue.push({});
    restored.restore(snapshot);
    const auto record = restored.thread->current_wait_continuation();
    auto resumed = lifecycle_async(restored.thread, [&] { return restored.thread->resume_wait_continuation(record); });
    ASSERT_TRUE(restored.waiting(1));
    EXPECT_EQ(restored.commits.load(), 0u);
    restored.env.gxm.display_queue.pop();
    EXPECT_EQ(lifecycle_result(resumed, restored.thread), 0);
    EXPECT_EQ(restored.commits.load(), 0u);
    EXPECT_EQ(restored.env.gxm.display_queue.size(), 0u);
}

TEST(GxmDisplayQueueContinuation, real_finish_export_restores_its_empty_queue_wait) {
    DisplayQueueWaitEnv original;
    original.env.gxm.display_queue.push({});
    auto pending = lifecycle_async(original.thread, [&] {
        return export_sceGxmDisplayQueueFinish(original.env, original.thread->id, "test");
    });
    ASSERT_TRUE(original.waiting(1));
    const auto snapshot = original.thread->capture_snapshot();
    EXPECT_EQ(snapshot.waits[0].args[2], 1u);
    original.env.gxm.display_queue.pop();
    EXPECT_EQ(lifecycle_result(pending, original.thread), 0);
    DisplayQueueWaitEnv restored;
    restored.env.gxm.display_queue.push({});
    restored.restore(snapshot);
    const auto record = restored.thread->current_wait_continuation();
    auto resumed = lifecycle_async(restored.thread, [&] { return restored.thread->resume_wait_continuation(record); });
    ASSERT_TRUE(restored.waiting(1));
    restored.env.gxm.display_queue.pop();
    EXPECT_EQ(lifecycle_result(resumed, restored.thread), 0);
    EXPECT_EQ(restored.commits.load(), 0u);
}

TEST(GxmDisplayQueueContinuation, queue_pop_before_wait_retains_the_guest_wake) {
    DisplayQueueWaitEnv original;
    original.env.gxm.display_queue.push({});
    const std::weak_ptr<ThreadState> weak = original.thread;
    const auto token = original.env.gxm.display_queue.subscribe_changes([weak] {
        if (const auto thread = weak.lock()) thread->wake();
    });
    original.env.gxm.display_queue.pop();
    auto wait = lifecycle_async(original.thread, [&] {
        const auto result = original.thread->wait({SCE_KERNEL_WAITTYPE_EVENT, original.thread->id},
            std::chrono::steady_clock::now() + 100ms, false);
        return result ? *result : guest_result(result);
    });
    EXPECT_EQ(lifecycle_result(wait, original.thread), 0);
    original.env.gxm.display_queue.unsubscribe_changes(token);
}

TEST(GxmDisplayWorker, waiting_callback_can_park_host_without_releasing_queue_or_data) {
    EmuEnvState env;
    ASSERT_TRUE(init(env.mem, false));
    const Address sync_address = alloc(env.mem, sizeof(SceGxmSyncObject), "display test sync");
    ASSERT_NE(sync_address, 0u);
    auto *sync = new (Ptr<SceGxmSyncObject>(sync_address).get(env.mem)) SceGxmSyncObject{};
    sync->timestamp_current = 10;
    sync->timestamp_ahead = 11;
    const Address data = alloc(env.mem, 16, "display test callback data");
    ASSERT_NE(data, 0u);
    auto display = std::make_shared<ThreadState>(79, env.kernel, env.mem);
    display->cpu = init_cpu(false, 79, 0, env.mem);
    ASSERT_NE(display->cpu, nullptr);
    env.kernel.threads.emplace(79, display);
    env.gxm.display_queue_thread = 79;
    env.gxm.params.displayQueueCallback = decltype(env.gxm.params.displayQueueCallback)(0x1234);
    env.gxm.display_previous_entry_point = 0x4568;
    env.gxm.display_phase = DisplayWorkerPhase::WaitCallback;
    env.gxm.display_queue.push({data, Ptr<SceGxmSyncObject>(sync_address),
        Ptr<SceGxmSyncObject>(sync_address), 10, 10, false});
    auto private_state = display->capture_snapshot();
    private_state.entry_point = 0x1234;
    private_state.status = ThreadStatus::running;
    display->apply_private_snapshot(private_state);
    write_reg(*display->cpu, 0, 0x12345678);
    const auto semaphore = semaphore_create(env.kernel, "test", "display callback", 79, 0, 0, 1);
    auto blocked = lifecycle_async(display, [&] {
        SceUInt32 timeout = 2'000'000;
        return semaphore_wait(env.kernel, "test", 79, semaphore, 1, &timeout, false);
    });
    ASSERT_TRUE(lifecycle_queued(env.kernel.semaphores.at(semaphore)));
    std::atomic<bool> stop_worker = false;
    auto host = std::async(std::launch::async, [&] {
        while (!stop_worker.load()) {
            env.gxm.display_snapshot_gate.boundary(true);
            if (stop_worker.load() || !gxm::process_display_queue_step(env)) break;
        }
    });
    const bool parked = env.gxm.display_snapshot_gate.acquire(200ms);
    EXPECT_TRUE(parked);
    EXPECT_EQ(env.gxm.display_phase, DisplayWorkerPhase::WaitCallback);
    EXPECT_EQ(env.gxm.display_queue.size(), 1u);
    EXPECT_TRUE(is_valid_addr(env.mem, data));
    EXPECT_EQ(sync->timestamp_current.load(), 10u);
    const auto saved = display->capture_snapshot();
    EXPECT_EQ(saved.waits.size(), 1u);
    EXPECT_EQ(read_reg(*display->cpu, 0), 0x12345678u);
    stop_worker = true;
    env.gxm.display_snapshot_gate.stop();
    host.get();
    EXPECT_EQ(semaphore_signal(env.kernel, "test", 79, semaphore, 1), 0);
    EXPECT_EQ(lifecycle_result(blocked, display), 0);
    // A restored host attaches this existing guest, polls its completion and
    // never starts the callback again or overwrites its return register.
    write_reg(*display->cpu, 0, 0x99);
    {
        const std::lock_guard lock(display->mutex);
        display->update_status(ThreadStatus::dormant);
    }
    ASSERT_TRUE(gxm::process_display_queue_step(env));
    EXPECT_EQ(env.gxm.display_phase, DisplayWorkerPhase::Complete);
    EXPECT_EQ(env.gxm.display_queue.size(), 1u);
    EXPECT_TRUE(is_valid_addr(env.mem, data));
    EXPECT_EQ(sync->timestamp_current.load(), 10u);
    ASSERT_TRUE(gxm::process_display_queue_step(env));
    EXPECT_EQ(env.gxm.display_phase, DisplayWorkerPhase::Idle);
    EXPECT_EQ(env.gxm.display_queue.size(), 0u);
    EXPECT_FALSE(is_valid_addr(env.mem, data));
    EXPECT_EQ(sync->timestamp_current.load(), 11u);
    EXPECT_EQ(env.gxm.display_entries_done.load(), 1u);
    EXPECT_EQ(display->capture_snapshot().entry_point, 0x4568u);
    EXPECT_EQ(read_reg(*display->cpu, 0), 0x99u);
    EXPECT_EQ(env.kernel.get_thread(79), display);
    sync->~SceGxmSyncObject();
}

TEST(GxmDisplayWorker, frozen_start_and_bootstrap_gate_leave_callback_context_untouched) {
    DisplayQueueWaitEnv fixture;
    fixture.thread->cpu = init_cpu(false, fixture.thread->id, 0, fixture.env.mem);
    ASSERT_NE(fixture.thread->cpu, nullptr);
    {
        const std::lock_guard lock(fixture.thread->mutex);
        fixture.thread->update_status(ThreadStatus::dormant);
    }
    write_reg(*fixture.thread->cpu, 0, 0x11223344);
    fixture.thread->request_world_stop();
    Address previous = 0x55;
    EXPECT_FALSE(fixture.thread->try_start_guest_function(0x1234, 0x678, previous));
    EXPECT_EQ(previous, 0x55u);
    EXPECT_EQ(read_reg(*fixture.thread->cpu, 0), 0x11223344u);
    fixture.thread->resume_from_world();
    fixture.thread->suspend_and_wait();
    EXPECT_FALSE(fixture.thread->try_start_guest_function(0x1234, 0x678, previous));
    EXPECT_EQ(read_reg(*fixture.thread->cpu, 0), 0x11223344u);
    fixture.thread->resume_if_suspended();
    fixture.env.kernel.pause_threads();
    EXPECT_FALSE(fixture.thread->try_start_guest_function(0x1234, 0x678, previous));
    EXPECT_EQ(fixture.thread->status, ThreadStatus::dormant);
    EXPECT_EQ(read_reg(*fixture.thread->cpu, 0), 0x11223344u);
    fixture.env.kernel.resume_threads();
    auto &gate = fixture.env.gxm.display_snapshot_gate;
    gate.reset(true);
    std::promise<void> at_boundary;
    auto starter = std::async(std::launch::async, [&] {
        at_boundary.set_value();
        gate.boundary(true);
        return fixture.thread->try_start_guest_function(0x1234, 0x678, previous);
    });
    at_boundary.get_future().wait();
    EXPECT_EQ(starter.wait_for(20ms), std::future_status::timeout);
    EXPECT_EQ(read_reg(*fixture.thread->cpu, 0), 0x11223344u);
    gate.release();
    EXPECT_TRUE(starter.get());
    EXPECT_EQ(read_reg(*fixture.thread->cpu, 0), 0x678u);
    EXPECT_EQ(read_pc(*fixture.thread->cpu), 0x1234u);
}

TEST(GxmDisplayWorker, failed_restore_stops_and_joins_bootstrap_held_host_without_starting_guest) {
    DisplayQueueWaitEnv fixture;
    fixture.thread->cpu = init_cpu(false, fixture.thread->id, 0, fixture.env.mem);
    ASSERT_NE(fixture.thread->cpu, nullptr);
    write_reg(*fixture.thread->cpu, 0, 0x56789abc);
    {
        const std::lock_guard lock(fixture.thread->mutex);
        fixture.thread->update_status(ThreadStatus::waiting);
    }
    fixture.env.gxm.display_queue_thread = fixture.thread->id;
    fixture.env.gxm.display_phase = DisplayWorkerPhase::WaitCallback;
    fixture.env.gxm.restored_display_queue.push_back({0x1000, {}, {}, 1, 1, false});
    std::fill_n(Ptr<uint8_t>(0x1000).get(fixture.env.mem), 16, 0x77);
    EXPECT_TRUE(gxm::restart_display_queue(fixture.env, true));
    EXPECT_TRUE(fixture.env.gxm.display_host_thread.joinable());
    EXPECT_EQ(fixture.env.kernel.threads.size(), 1u);
    EXPECT_EQ(fixture.env.kernel.get_thread(fixture.thread->id), fixture.thread);
    EXPECT_EQ(fixture.env.gxm.display_queue.size(), 1u);
    gxm::stop_display_queue_host(fixture.env);
    EXPECT_FALSE(fixture.env.gxm.display_host_thread.joinable());
    EXPECT_TRUE(fixture.env.gxm.display_queue.is_aborted());
    EXPECT_EQ(fixture.env.gxm.display_phase, DisplayWorkerPhase::WaitCallback);
    EXPECT_EQ(fixture.thread->status, ThreadStatus::waiting);
    EXPECT_EQ(read_reg(*fixture.thread->cpu, 0), 0x56789abcu);
    EXPECT_EQ(fixture.env.gxm.display_entries_done.load(), 0u);
    EXPECT_EQ(fixture.env.gxm.display_queue.size(), 1u);
    EXPECT_EQ(*Ptr<uint8_t>(0x1000).get(fixture.env.mem), 0x77u);
}

TEST(GxmDisplayWorker, restore_replaces_old_queue_capacity_and_reports_missing_guest) {
    DisplayQueueWaitEnv fixture;
    fixture.env.gxm.display_queue_thread = fixture.thread->id;
    fixture.env.gxm.params.displayQueueMaxPendingCount = 3;
    fixture.env.gxm.display_queue.maxPendingCount_ = 1; // Previous session.
    fixture.env.gxm.restored_display_queue = {{}, {}};
    EXPECT_TRUE(gxm::restart_display_queue(fixture.env, true));
    EXPECT_EQ(fixture.env.gxm.display_queue.maxPendingCount_, 2u);
    EXPECT_EQ(fixture.env.gxm.display_queue.size(), 2u);
    gxm::stop_display_queue_host(fixture.env);
    fixture.env.gxm.params.displayQueueMaxPendingCount = 2;
    fixture.env.gxm.display_queue.maxPendingCount_ = 99;
    fixture.env.gxm.restored_display_queue = {{}};
    EXPECT_TRUE(gxm::restart_display_queue(fixture.env, true));
    EXPECT_EQ(fixture.env.gxm.display_queue.maxPendingCount_, 1u);
    EXPECT_EQ(fixture.env.gxm.display_queue.size(), 1u);
    EXPECT_FALSE(fixture.env.gxm.display_queue.try_push({}));
    gxm::stop_display_queue_host(fixture.env);
    fixture.env.gxm.display_queue_thread = 555;
    EXPECT_FALSE(gxm::restart_display_queue(fixture.env, true));
    EXPECT_FALSE(fixture.env.gxm.display_host_thread.joinable());
}

namespace {
struct DisplayProducerPreflight {
    emucorev::savestate::GxmSnapshot gxm;
    ThreadState::Snapshot thread;
    ThreadState::Snapshot display_thread;
    std::vector<emucorev::savestate::MemorySpanInfo> spans{{0x1000, 1}, {0x3000, 1}, {0x5000, 1}};
    std::string error;
    DisplayProducerPreflight() {
        gxm.params.displayQueueCallbackDataSize = 16;
        gxm.params.displayQueueMaxPendingCount = 2;
        gxm.display_queue_thread = 79;
        display_thread.id = 79;
        display_thread.registered = true;
        display_thread.status = ThreadStatus::dormant;
        thread.id = 91;
        thread.registered = true;
        thread.stack_addr = 0x3000;
        thread.tls_addr = 0x5000;
        WaitContinuationSnapshot wait;
        wait.operation = WaitOperation::gxm_display_queue;
        wait.args = {91, 0, 0, 1, 0, 0, 0, 0};
        wait.target = {SCE_KERNEL_WAITTYPE_EVENT, 91};
        thread.waits.push_back(wait);
        PendingDisplaySubmission pending;
        pending.callback.data = 0x1000;
        gxm.display_submissions.emplace_back(91, pending);
    }
    bool valid(const emucorev::savestate::SysmemSnapshot *sysmem = nullptr) {
        return emucorev::savestate::validate_gxm_display_continuations(gxm, {thread, display_thread}, spans, error, sysmem);
    }
};
}

TEST(GxmDisplayQueuePreflight, prepared_entry_requires_exactly_its_registered_capacity_waiter) {
    DisplayProducerPreflight fixture;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.thread.registered = false;
    EXPECT_FALSE(fixture.valid());
    fixture.thread.registered = true;
    fixture.thread.waits.clear();
    EXPECT_FALSE(fixture.valid());
    DisplayProducerPreflight duplicate;
    duplicate.gxm.display_submissions.push_back(duplicate.gxm.display_submissions.front());
    EXPECT_FALSE(duplicate.valid());
}

TEST(GxmDisplayQueuePreflight, submitted_and_finish_phases_cannot_own_or_repeat_a_prepared_entry) {
    DisplayProducerPreflight fixture;
    fixture.thread.waits[0].args[1] = 1;
    EXPECT_FALSE(fixture.valid());
    fixture.gxm.display_submissions.clear();
    EXPECT_FALSE(fixture.valid()); // AddEntry's phase 1 only exists in single-pending mode.
    fixture.gxm.params.displayQueueMaxPendingCount = 1;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.thread.waits[0].args[2] = 1;
    fixture.gxm.params.displayQueueMaxPendingCount = 2;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.thread.waits[0].args[1] = 0;
    EXPECT_FALSE(fixture.valid());
}

TEST(GxmDisplayQueuePreflight, callback_data_cannot_alias_interior_stack_tls_queue_or_sysmem_owners) {
    DisplayProducerPreflight fixture;
    auto &data = fixture.gxm.display_submissions[0].second.callback.data;
    data = 0x1100;
    EXPECT_FALSE(fixture.valid());
    data = 0x3000;
    EXPECT_FALSE(fixture.valid());
    data = 0x5000;
    EXPECT_FALSE(fixture.valid());
    data = 0x1000;
    fixture.gxm.display_entries.push_back(fixture.gxm.display_submissions[0].second.callback);
    EXPECT_FALSE(fixture.valid());
    fixture.gxm.display_entries.clear();
    fixture.spans[0].page_count = 2;
    EXPECT_FALSE(fixture.valid());
    fixture.spans[0].page_count = 1;
    emucorev::savestate::SysmemSnapshot sysmem;
    KernelMemBlock block{};
    block.mappedBase = Ptr<void>(0x1000);
    block.mappedSize = 0x1000;
    sysmem.blocks.emplace(1, block);
    EXPECT_FALSE(fixture.valid(&sysmem));
    sysmem.blocks.clear();
    EXPECT_TRUE(fixture.valid(&sysmem)) << fixture.error;
}

TEST(GxmDisplayQueuePreflight, worker_phase_requires_its_existing_guest_and_unconsumed_front) {
    DisplayProducerPreflight fixture;
    fixture.gxm.display_queue_thread = 80;
    EXPECT_FALSE(fixture.valid());
    fixture.gxm.display_queue_thread = 79;
    fixture.gxm.display_worker_phase = uint32_t(DisplayWorkerPhase::WaitCallback);
    EXPECT_FALSE(fixture.valid()); // No queue front.
    fixture.gxm.display_entries.push_back({0x7000, {}, {}, 0, 0, false});
    fixture.spans.push_back({0x7000, 1});
    fixture.gxm.params.displayQueueCallback = decltype(fixture.gxm.params.displayQueueCallback)(0x1234);
    fixture.display_thread.entry_point = 0x1234;
    fixture.display_thread.status = ThreadStatus::waiting;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.gxm.display_worker_phase = uint32_t(DisplayWorkerPhase::Complete);
    EXPECT_FALSE(fixture.valid());
    fixture.display_thread.status = ThreadStatus::dormant;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.gxm.display_previous_entry = 0x9000;
    EXPECT_FALSE(fixture.valid());
    fixture.gxm.display_previous_entry = 0x1000;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.gxm.display_worker_phase = uint32_t(DisplayWorkerPhase::StartCallback);
    EXPECT_FALSE(fixture.valid()); // Unstarted callbacks cannot have a previous entry.
}
