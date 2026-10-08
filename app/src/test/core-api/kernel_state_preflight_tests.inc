#include <emucorev/savestate/kernel_state_io.h>
#include <cpu/functions.h>
TEST(KernelSessionPause, semaphore_wake_stays_parked_until_menu_resume) {
    KernelLifecycleEnv env;
    auto thread = lifecycle_thread(env, 71);
    const auto id = semaphore_create(env.kernel, "test", "session pause", 71, 0, 0, 1);
    auto blocked = lifecycle_async(thread, [&] {
        SceUInt32 timeout = 2'000'000;
        return semaphore_wait(env.kernel, "test", 71, id, 1, &timeout, false);
    });
    ASSERT_TRUE(lifecycle_queued(env.kernel.semaphores.at(id)));
    env.kernel.pause_threads();
    EXPECT_EQ(semaphore_signal(env.kernel, "test", 71, id, 1), 0);
    EXPECT_EQ(blocked.wait_for(30ms), std::future_status::timeout);
    env.kernel.resume_threads();
    EXPECT_EQ(lifecycle_result(blocked, thread), 0);
}

TEST(KernelSessionPause, menu_resume_preserves_an_existing_debugger_suspension) {
    KernelState kernel;
    MemState memory;
    auto thread = std::make_shared<ThreadState>(1, kernel, memory);
    thread->cpu = init_cpu(false, 1, 0, memory);
    ASSERT_NE(thread->cpu, nullptr);
    kernel.threads.emplace(1, thread);
    thread->status = ThreadStatus::running;
    kernel.pause_threads();
    // A real debugger request arriving during menu pause must survive resume.
    thread->suspend();
    thread->status = ThreadStatus::suspended;
    kernel.resume_threads();
    EXPECT_EQ(thread->status, ThreadStatus::suspended);
    EXPECT_TRUE(thread->capture_snapshot().debugger_suspended);
}

TEST(KernelSessionPause, late_thread_publication_inherits_pause_even_when_registry_was_empty) {
    KernelState kernel;
    MemState memory;
    kernel.cpu_opt = false;
    kernel.corenum_allocator.set_max_core_count(4);
    kernel.pause_threads();
    EXPECT_TRUE(kernel.is_threads_paused());
    ThreadState::Snapshot state;
    state.id = 71;
    state.name = "late session-pause child";
    state.entry_point = 0x1234;
    state.priority = 100;
    auto child = kernel.create_thread_from_snapshot(memory, state, true);
    ASSERT_NE(child, nullptr);
    // Ordinary guest start still prepares registers, but no guest quantum may
    // execute until the inherited session reason is removed.
    EXPECT_EQ(child->start(0, {}), 0);
    EXPECT_TRUE(await_waiter([&] {
        const std::lock_guard lock(child->mutex);
        return child->status == ThreadStatus::suspended;
    }));
    EXPECT_EQ(read_pc(*child->cpu), 0x1234u);
    kernel.process_exit(); // Must also unwind a paused worker without resume.
    EXPECT_TRUE(kernel.threads.empty());
    kernel.resume_threads();
    EXPECT_FALSE(kernel.is_threads_paused());
}

TEST(KernelStatePreflight, session_pause_is_removed_from_slots_but_real_debugger_suspension_survives) {
    using namespace emucorev::savestate;
    KernelState kernel;
    MemState memory;
    auto thread = std::make_shared<ThreadState>(1, kernel, memory);
    thread->cpu = init_cpu(false, 1, 0, memory);
    ASSERT_NE(thread->cpu, nullptr);
    thread->status = ThreadStatus::running;
    kernel.threads.emplace(1, thread);
    kernel.pause_threads();
    // No guest CPU is executed: model its acknowledged pause boundary.
    thread->status = ThreadStatus::suspended;
    std::vector<ThreadState::Snapshot> saved;
    std::string error;
    ASSERT_TRUE(capture_thread_snapshots(kernel, 0, saved, error)) << error;
    ASSERT_EQ(saved.size(), 1u);
    EXPECT_EQ(saved[0].status, ThreadStatus::running);
    EXPECT_FALSE(saved[0].debugger_suspended);
    EXPECT_EQ(thread->status, ThreadStatus::suspended);
    EXPECT_FALSE(thread->capture_snapshot().debugger_suspended);
    kernel.resume_threads();
    thread->suspend();
    thread->status = ThreadStatus::suspended;
    ASSERT_TRUE(capture_thread_snapshots(kernel, 0, saved, error)) << error;
    EXPECT_EQ(saved[0].status, ThreadStatus::suspended);
    EXPECT_TRUE(saved[0].debugger_suspended);
}
TEST(KernelStatePreflight, releasing_session_pause_does_not_restart_completed_or_waiting_threads) {
    KernelState kernel;
    MemState memory;
    auto thread = std::make_shared<ThreadState>(1, kernel, memory);
    thread->cpu = init_cpu(false, 1, 0, memory);
    ASSERT_NE(thread->cpu, nullptr);
    kernel.threads.emplace(1, thread);
    for (const auto completed_status : { ThreadStatus::dormant, ThreadStatus::waiting }) {
        thread->status = ThreadStatus::running;
        kernel.pause_threads();
        // A callback/HLE operation can complete after the halt request.
        thread->status = completed_status;
        kernel.resume_threads();
        EXPECT_EQ(thread->status, completed_status);
        EXPECT_FALSE(thread->capture_snapshot().debugger_suspended);
        EXPECT_FALSE(kernel.is_threads_paused());
    }
}
TEST(KernelStatePreflight, excluded_display_uid_cannot_silently_lose_lock_ownership) {
    using namespace emucorev::savestate;
    KernelLifecycleEnv env;
    auto display = continuation_thread(env, 10);
    auto guest = continuation_thread(env, 11);
    auto mutex = std::make_shared<Mutex>(0);
    mutex->uid = 500;
    mutex->owner = display;
    mutex->lock_count = 1;
    env.kernel.mutexes.emplace(500, mutex);
    std::string error;
    EXPECT_FALSE(validate_excluded_thread_lock_ownership(env.kernel, display->id, error));
    std::vector<ThreadState::Snapshot> snapshots;
    EXPECT_FALSE(capture_thread_snapshots(env.kernel, display->id, snapshots, error));
    EXPECT_TRUE(snapshots.empty());
    mutex->owner = guest;
    EXPECT_TRUE(validate_excluded_thread_lock_ownership(env.kernel, display->id, error));
    env.kernel.mutexes.clear();
    auto rw = std::make_shared<RWLock>(0);
    rw->owners.emplace(display, 1);
    env.kernel.rwlocks.emplace(501, rw);
    EXPECT_FALSE(validate_excluded_thread_lock_ownership(env.kernel, display->id, error));
    env.kernel.rwlocks.clear();
    // A condition can retain a deleted mutex outside the visible UID map.
    auto condition = std::make_shared<Condvar>(0);
    condition->associated_mutex = mutex;
    mutex->owner = display;
    mutex->deleted = true;
    env.kernel.condvars.emplace(502, condition);
    EXPECT_FALSE(validate_excluded_thread_lock_ownership(env.kernel, display->id, error));
    env.kernel.condvars.clear();
    // Likewise, an in-flight logical waiter may be its last strong owner.
    const auto wait = guest->begin_wait_continuation(WaitOperation::mutex, { 500, 1 }, nullptr, false, Deadline::max());
    wait->object = mutex;
    EXPECT_FALSE(validate_excluded_thread_lock_ownership(env.kernel, display->id, error));
    guest->end_wait_continuation(wait);
    EXPECT_TRUE(validate_excluded_thread_lock_ownership(env.kernel, display->id, error));
}
namespace {
struct KernelPreflightFixture {
    std::vector<ThreadState::Snapshot> threads;
    emucorev::savestate::ParsedKernelSync sync;
    KernelPreflightFixture() {
        ThreadState::Snapshot thread;
        thread.id = 1;
        thread.name = "worker";
        thread.stack_addr = 0x1000;
        thread.stack_size = 0x1000;
        thread.tls_addr = 0x3000;
        thread.status = ThreadStatus::waiting;
        WaitContinuationSnapshot wait;
        wait.frame_sequence = 1;
        wait.operation = WaitOperation::semaphore;
        wait.args = { 500, 1 };
        wait.target = { SCE_KERNEL_WAITTYPE_SEMAPHORE, 500 };
        wait.sequence = 10;
        wait.queued = true;
        thread.waits.push_back(wait);
        threads.push_back(thread);
        emucorev::savestate::ParsedSyncObject object;
        object.kind = emucorev::savestate::SyncSnapshotKind::semaphore;
        object.header.uid = object.header.object_uid = 500;
        object.header.registered = true;
        object.values = { 2, 0, 0 };
        sync.objects.push_back(object);
    }
    bool valid(std::string &error) {
        return emucorev::savestate::validate_kernel_continuations(threads, {}, sync, [](Address address, size_t size) { return address >= 0x1000 && size <= 0x4000 && uint64_t(address) + size <= 0x5000; }, [](uint32_t) { return false; }, error);
    }
};
} // namespace
TEST(KernelStatePreflight, production_thread_codec_preserves_queue_and_callback_frames) {
    using namespace emucorev::savestate;
    KernelPreflightFixture fixture;
    auto &thread = fixture.threads[0];
    thread.is_processing_callbacks = true;
    CallbackContinuationSnapshot callback;
    callback.frame_sequence = 2;
    callback.purpose = CallbackPurpose::notification;
    callback.callback_uid = 99;
    thread.callback_frames.push_back(callback);
    WaitContinuationSnapshot inner;
    inner.frame_sequence = 3;
    inner.operation = WaitOperation::signal;
    inner.target = { SCE_KERNEL_WAITTYPE_SIGNAL, 0 };
    thread.waits.push_back(inner);
    BufferWriter writer;
    write_thread_snapshots(writer, fixture.threads);
    std::vector<ThreadState::Snapshot> decoded;
    std::string error;
    ASSERT_TRUE(parse_thread_snapshots(writer.data(), decoded, error)) << error;
    ASSERT_EQ(decoded.size(), 1u);
    ASSERT_EQ(decoded[0].waits.size(), 2u);
    EXPECT_EQ(decoded[0].waits[0].sequence, 10u);
    EXPECT_EQ(decoded[0].callback_frames[0].frame_sequence, 2u);
    fixture.threads = std::move(decoded);
    EXPECT_TRUE(fixture.valid(error)) << error;
    auto truncated = writer.data();
    truncated.pop_back();
    EXPECT_FALSE(parse_thread_snapshots(truncated, decoded, error));
    auto trailing = writer.data();
    trailing.push_back(0);
    EXPECT_FALSE(parse_thread_snapshots(trailing, decoded, error));
}
TEST(KernelStatePreflight, malformed_cross_references_and_queue_states_are_rejected_without_live_mutation) {
    KernelLifecycleEnv live;
    auto sentinel = continuation_thread(live, 1234);
    const auto count = live.kernel.threads.size();
    std::string error;
    KernelPreflightFixture valid;
    ASSERT_TRUE(valid.valid(error)) << error;
    auto bad = valid;
    bad.threads[0].waits[0].args[0] = 501;
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.threads[0].waits[0].has_result = true;
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.threads[0].waits[0].timeout_address = 0xfffffffcu;
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.threads[0].context.set_sp(0x9000);
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.threads.push_back(bad.threads[0]);
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.sync.objects[0].header.uid = 1;
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.threads[0].waits[0].phase = WaitPhase::reacquire_mutex;
    EXPECT_FALSE(bad.valid(error));
    bad = valid;
    bad.threads[0].waits[0].sequence = 0;
    EXPECT_FALSE(bad.valid(error));
    EXPECT_EQ(live.kernel.threads.size(), count);
    EXPECT_EQ(live.kernel.get_thread(1234), sentinel);
}
TEST(KernelStatePreflight, deleted_completed_object_is_valid_but_deleted_queued_object_is_not) {
    KernelPreflightFixture fixture;
    std::string error;
    fixture.sync.objects[0].header.registered = false;
    fixture.sync.objects[0].header.deleted = true;
    EXPECT_FALSE(fixture.valid(error));
    fixture.threads[0].waits[0].queued = false;
    fixture.threads[0].waits[0].has_result = true;
    fixture.threads[0].waits[0].result = SCE_KERNEL_ERROR_WAIT_DELETE;
    EXPECT_TRUE(fixture.valid(error)) << error;
}
TEST(KernelStatePreflight, production_sync_decoder_rejects_invalid_boolean_and_duplicate_object_ids) {
    using namespace emucorev::savestate;
    const auto encode = [](bool duplicate) {
        BufferWriter writer;
        writer.u32(2);
        writer.u64(123);
        writer.u32(0);
        writer.u32(0);
        writer.u32(duplicate ? 2 : 1);
        for (int i = 0; i < (duplicate ? 2 : 1); ++i) {
            writer.i32(500);
            writer.boolean(true);
            writer.i32(500);
            writer.u32(0);
            writer.boolean(false);
            std::array<char, KERNELOBJECT_MAX_NAME_LENGTH + 1> name{};
            writer.append(name.data(), name.size());
            writer.i32(2);
            writer.i32(0);
            writer.i32(0);
        }
        for (int family = 3; family < 10; ++family)
            writer.u32(0);
        return writer.data();
    };
    ParsedKernelSync parsed;
    std::string error;
    auto bytes = encode(false);
    ASSERT_TRUE(parse_kernel_sync(bytes, parsed, error)) << error;
    ASSERT_EQ(parsed.objects.size(), 1u);
    EXPECT_FALSE(parse_kernel_sync(encode(true), parsed, error));
    // schema/time + two empty family counts + semaphore count + UID.
    bytes[4 + 8 + 4 + 4 + 4 + 4] = 2;
    EXPECT_FALSE(parse_kernel_sync(bytes, parsed, error));
}

TEST(KernelStatePreflight, creation_admission_preserves_preimport_context_and_rejects_module_callers) {
    using namespace emucorev::savestate;
    KernelPreflightFixture fixture;
    std::string error;
    auto &wait = fixture.threads[0].waits[0];
    wait.operation = WaitOperation::creation_import;
    wait.args = { 0xC5C11EE7U };
    wait.target = {};
    wait.queued = false;
    wait.sequence = 0;
    wait.context.set_sp(0x2000);
    wait.context.cpu_registers[0] = 0x1234;
    ASSERT_TRUE(fixture.valid(error)) << error;
    BufferWriter writer;
    write_thread_snapshots(writer, fixture.threads);
    std::vector<ThreadState::Snapshot> decoded;
    ASSERT_TRUE(parse_thread_snapshots(writer.data(), decoded, error));
    EXPECT_EQ(decoded[0].waits[0].context.get_sp(), 0x2000u);
    EXPECT_EQ(decoded[0].waits[0].context.cpu_registers[0], 0x1234u);
    wait.args[0] = 0xdeadbeef;
    EXPECT_FALSE(fixture.valid(error));
}

TEST(KernelStatePreflight, hidden_owner_may_own_locks_but_has_no_executable_waits_or_thread_uid) {
    KernelPreflightFixture fixture;
    std::string error;
    auto owner = fixture.threads[0];
    owner.id = 7;
    owner.registered = false;
    owner.stack_addr = 0x4000;
    owner.stack_size = 0x800;
    owner.tls_addr = 0x4800;
    owner.delete_requested = true;
    owner.status = ThreadStatus::dormant;
    owner.waits.clear();
    owner.context.set_pc(0xdeadbeef); // Historical CPU state is never executed.
    fixture.threads.push_back(owner);
    emucorev::savestate::ParsedSyncObject mutex;
    mutex.kind = emucorev::savestate::SyncSnapshotKind::mutex;
    mutex.header.uid = mutex.header.object_uid = 501;
    mutex.header.registered = true;
    mutex.values = { 0, 1, 7, 0 };
    fixture.sync.objects.push_back(mutex);
    ASSERT_TRUE(fixture.valid(error)) << error;
    fixture.threads.back().waits.push_back(fixture.threads[0].waits[0]);
    EXPECT_FALSE(fixture.valid(error));
}

TEST(KernelStatePreflight, initial_callback_dispatch_requires_known_restartable_import) {
    KernelPreflightFixture fixture;
    std::string error;
    auto &wait = fixture.threads[0].waits[0];
    wait.operation = WaitOperation::callback_dispatch;
    wait.args = { check_callback_import, 3 };
    wait.target = {};
    wait.queued = false;
    wait.sequence = 0;
    EXPECT_TRUE(fixture.valid(error)) << error;
    wait.args[0] = 0xdeadbeef;
    EXPECT_FALSE(fixture.valid(error));
}
