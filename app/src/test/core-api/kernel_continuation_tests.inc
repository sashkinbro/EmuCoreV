#include <emucorev/savestate/kernel_state_io.h>
// Actual production wait scopes, queues, snapshot application and typed resume.
// No guest CPU/JIT is needed; the same cases are intended for host and Android.
namespace {
ThreadStatePtr continuation_thread(KernelLifecycleEnv &env, SceUID uid) {
    return lifecycle_thread(env, uid);
}
ThreadStatePtr continuation_restore(KernelLifecycleEnv &env, const ThreadState::Snapshot &snapshot) {
    auto thread = continuation_thread(env, snapshot.id);
    thread->apply_private_snapshot(snapshot);
    return thread;
}
SceInt32 continuation_resume(const ThreadStatePtr &thread) {
    return thread->resume_wait_continuation(thread->current_wait_continuation());
}
} // namespace
TEST(KernelContinuation, completed_semaphore_grant_is_not_consumed_twice) {
    KernelLifecycleEnv original;
    auto guest = continuation_thread(original, 1);
    const auto uid = semaphore_create(original.kernel, "test", "semaphore", 1, 0, 0, 2);
    auto object = original.kernel.semaphores.at(uid);
    auto pending = lifecycle_async(guest, [&] { return semaphore_wait(original.kernel, "test", 1, uid, 1, nullptr, false); });
    ASSERT_TRUE(lifecycle_queued(object));
    guest->request_world_stop();
    ASSERT_EQ(semaphore_signal(original.kernel, "test", 1, uid, 1), 0);
    const auto snapshot = guest->capture_snapshot();
    guest->exit_delete(false);
    pending.get();
    ASSERT_EQ(snapshot.waits.size(), 1u);
    ASSERT_TRUE(snapshot.waits[0].has_result);
    EXPECT_FALSE(snapshot.waits[0].queued);
    KernelLifecycleEnv restored;
    auto saved = std::make_shared<Semaphore>(0);
    saved->uid = uid;
    saved->max = 2;
    saved->val = 0;
    restored.kernel.semaphores.emplace(uid, saved);
    auto thread = continuation_restore(restored, snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    thread->activate_restored_continuations();
    EXPECT_EQ(continuation_resume(thread), 0);
    EXPECT_EQ(saved->val, 0);
    EXPECT_TRUE(saved->waiters.empty());
    EXPECT_TRUE(thread->saved_wait_continuations().empty());
}
TEST(KernelContinuation, restored_waiters_keep_original_fifo_order_before_workers_start) {
    KernelLifecycleEnv env;
    auto first = continuation_thread(env, 20), second = continuation_thread(env, 10);
    auto object = std::make_shared<Semaphore>(0);
    object->uid = 500;
    object->max = 2;
    env.kernel.semaphores.emplace(500, object);
    const auto snapshot = [](SceUID uid, uint64_t order) {
        ThreadState::Snapshot thread;
        thread.id = uid;
        thread.priority = 100;
        thread.status = ThreadStatus::waiting;
        WaitContinuationSnapshot wait;
        wait.frame_sequence = 1;
        wait.operation = WaitOperation::semaphore;
        wait.args = { 500, 1 };
        wait.target = { SCE_KERNEL_WAITTYPE_SEMAPHORE, 500 };
        wait.priority = 100;
        wait.sequence = order;
        wait.queued = true;
        thread.waits.push_back(wait);
        return thread;
    };
    first->apply_private_snapshot(snapshot(20, 1));
    second->apply_private_snapshot(snapshot(10, 2));
    // Rebuild in reverse UID/order, as a normal thread map decoder does.
    ASSERT_TRUE(second->restore_wait_queues());
    ASSERT_TRUE(first->restore_wait_queues());
    EXPECT_EQ(object->waiters.front()->thread, first);
    ASSERT_EQ(semaphore_signal(env.kernel, "test", 1, 500, 1), 0);
    first->activate_restored_continuations();
    second->activate_restored_continuations();
    EXPECT_EQ(continuation_resume(first), 0);
    EXPECT_EQ(object->waiters.front()->thread, second);
    ASSERT_EQ(semaphore_signal(env.kernel, "test", 1, 500, 1), 0);
    EXPECT_EQ(continuation_resume(second), 0);
    EXPECT_EQ(object->val, 0);
}
TEST(KernelContinuation, deleted_event_keeps_completion_bits_without_reappearing_in_uid_table) {
    KernelLifecycleEnv original;
    lifecycle_memory(original);
    auto guest = continuation_thread(original, 1);
    auto *bits = Ptr<SceUInt32>(0x200).get(original.mem);
    const auto uid = eventflag_create(original.kernel, "test", 1, "event", 0, 5);
    auto object = original.kernel.eventflags.at(uid);
    auto pending = lifecycle_async(guest, [&] { return eventflag_wait(original.kernel, "test", 1, uid, 8, 0, bits, nullptr, false); });
    ASSERT_TRUE(lifecycle_queued(object));
    guest->request_world_stop();
    ASSERT_EQ(eventflag_delete(original.kernel, "test", 1, uid), 0);
    const auto snapshot = guest->capture_snapshot();
    guest->exit_delete(false);
    pending.get();
    ASSERT_EQ(snapshot.waits.size(), 1u);
    KernelLifecycleEnv restored;
    lifecycle_memory(restored);
    auto retired = std::make_shared<EventFlag>(0);
    retired->uid = uid;
    retired->flags = 5;
    retired->deleted = true;
    restored.kernel.snapshot_objects.emplace(uid, retired);
    auto thread = continuation_restore(restored, snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    thread->activate_restored_continuations();
    EXPECT_EQ(continuation_resume(thread), SCE_KERNEL_ERROR_WAIT_DELETE);
    EXPECT_EQ(*Ptr<SceUInt32>(0x200).get(restored.mem), 5u);
    EXPECT_TRUE(restored.kernel.eventflags.empty());
}
TEST(KernelContinuation, timed_condition_resumes_untimed_reacquisition_without_unlocking_again) {
    KernelLifecycleEnv env;
    lifecycle_memory(env);
    auto waiter = continuation_thread(env, 1), owner = continuation_thread(env, 2);
    auto mutex = std::make_shared<Mutex>(0);
    mutex->uid = 501;
    mutex->lock_count = 1;
    mutex->owner = owner;
    auto cond = std::make_shared<Condvar>(0);
    cond->uid = 502;
    cond->associated_mutex = mutex;
    env.kernel.mutexes.emplace(501, mutex);
    env.kernel.condvars.emplace(502, cond);
    ThreadState::Snapshot snapshot;
    snapshot.id = 1;
    snapshot.priority = 100;
    snapshot.status = ThreadStatus::waiting;
    WaitContinuationSnapshot wait;
    wait.frame_sequence = 1;
    wait.operation = WaitOperation::cond;
    wait.phase = WaitPhase::reacquire_mutex;
    wait.args = { 502, 501 };
    wait.timeout_address = 0x200;
    wait.condition_result = SCE_KERNEL_ERROR_WAIT_TIMEOUT;
    wait.target = { SCE_KERNEL_WAITTYPE_COND_MUTEX, 502 };
    wait.priority = 100;
    wait.sequence = 1;
    wait.queued = true;
    snapshot.waits.push_back(wait);
    waiter->apply_private_snapshot(snapshot);
    ASSERT_TRUE(waiter->restore_wait_queues());
    waiter->activate_restored_continuations();
    auto pending = lifecycle_async(waiter, [&] { return continuation_resume(waiter); });
    EXPECT_EQ(pending.wait_for(20ms), std::future_status::timeout);
    EXPECT_EQ(mutex->owner, owner);
    ASSERT_EQ(mutex_unlock(env.kernel, env.mem, "test", 2, 501, 1, SyncWeight::Heavy), 0);
    EXPECT_EQ(lifecycle_result(pending, waiter), SCE_KERNEL_ERROR_WAIT_TIMEOUT);
    EXPECT_EQ(mutex->owner, waiter);
    EXPECT_EQ(mutex->lock_count, 1);
}

TEST(KernelContinuation, resumed_msgpipe_bridge_writes_result_and_returns_ok) {
    KernelLifecycleEnv original;
    lifecycle_memory(original);
    auto guest = continuation_thread(original, 1);
    const auto uid = msgpipe_create(original.kernel, "test", "pipe", 1, 0, 8);
    auto object = original.kernel.msgpipes.at(uid);
    auto *buffer = Ptr<char>(0x200).get(original.mem);
    auto *result = Ptr<SceSize>(0x210).get(original.mem);
    auto pending = lifecycle_async(guest, [&] { return static_cast<SceInt32>(msgpipe_recv(original.kernel, "test", 1, uid,
                                                    SCE_KERNEL_MSG_PIPE_MODE_FULL, buffer, 3, nullptr, false, result, true)); });
    const auto until = std::chrono::steady_clock::now() + 1s;
    bool queued = false;
    while (std::chrono::steady_clock::now() < until) {
        {
            const std::lock_guard lock(object->mutex);
            queued = !object->receivers.empty();
        }
        if (queued)
            break;
        std::this_thread::yield();
    }
    if (!queued) {
        guest->exit_delete(false);
        pending.get();
        FAIL() << "pipe receiver did not queue";
    }
    guest->request_world_stop();
    const char data[] = "abc";
    ASSERT_EQ(msgpipe_send(original.kernel, "test", 1, uid, SCE_KERNEL_MSG_PIPE_MODE_FULL, data, 3, nullptr, false), 3u);
    const auto snapshot = guest->capture_snapshot();
    guest->exit_delete(false);
    pending.get();
    KernelLifecycleEnv restored;
    lifecycle_memory(restored);
    auto pipe = std::make_shared<MsgPipe>(0, 8);
    pipe->uid = uid;
    pipe->data_buffer.Insert(data, 3);
    restored.kernel.msgpipes.emplace(uid, pipe);
    auto thread = continuation_restore(restored, snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    thread->activate_restored_continuations();
    EXPECT_EQ(continuation_resume(thread), SCE_KERNEL_OK);
    EXPECT_EQ(*Ptr<SceSize>(0x210).get(restored.mem), 3u);
    EXPECT_EQ(std::memcmp(Ptr<char>(0x200).get(restored.mem), data, 3), 0);
    EXPECT_EQ(pipe->data_buffer.Used(), 0u);
}

TEST(KernelContinuation, restored_callback_notifications_target_new_owner_and_survive_private_snapshot) {
    KernelLifecycleEnv env;
    auto obsolete = continuation_thread(env, 1);
    env.kernel.threads.erase(1);
    auto owner = continuation_thread(env, 1);
    KernelState::SavedCallback saved;
    saved.uid = 99;
    saved.owner = 1;
    saved.name = "callback";
    saved.function = 0x1000;
    saved.notification.num_notifications = 2;
    saved.notification.notification_arg = 7;
    env.kernel.snapshot_callbacks.push_back(saved);
    env.kernel.restore_snapshot_callbacks();
    auto cb = env.kernel.callbacks.at(99);
    ThreadState::Snapshot snapshot;
    snapshot.id = 1;
    snapshot.callback_uids = { 99 };
    owner->apply_private_snapshot(snapshot);
    EXPECT_TRUE(owner->capture_snapshot().callbacks_pending);
    cb->direct_notify(42);
    EXPECT_TRUE(owner->capture_snapshot().callbacks_pending);
    EXPECT_FALSE(obsolete->capture_snapshot().callbacks_pending);
    EXPECT_EQ(cb->get_num_notifications(), 3u);
    const auto notification = cb->take_notification();
    ASSERT_TRUE(notification);
    EXPECT_EQ(notification->count, 3u);
    EXPECT_EQ(notification->arg, 42);
    EXPECT_EQ(cb->get_num_notifications(), 0u);
}
TEST(KernelContinuation, callback_with_expired_owner_remains_inert_after_uid_reuse) {
    KernelLifecycleEnv env;
    KernelState::SavedCallback saved;
    saved.uid = 99;
    saved.owner = 7;
    saved.name = "expired";
    env.kernel.snapshot_callbacks.push_back(saved);
    env.kernel.restore_snapshot_callbacks();
    auto later = continuation_thread(env, 7);
    auto cb = env.kernel.callbacks.at(99);
    cb->direct_notify(42);
    EXPECT_EQ(cb->get_owner_thread_id(), 7);
    EXPECT_FALSE(later->capture_snapshot().callbacks_pending);
}

TEST(KernelContinuation, hidden_lock_owner_retains_stack_and_tls_until_last_lock_is_deleted) {
    KernelLifecycleEnv env;
    ASSERT_TRUE(init(env.mem, false));
    const auto stack = alloc(env.mem, 4096, "retained stack");
    const auto tls = alloc(env.mem, 0x800, "retained TLS");
    ASSERT_NE(stack, 0u);
    ASSERT_NE(tls, 0u);
    ThreadState::Snapshot snapshot;
    snapshot.id = 7;
    snapshot.registered = false;
    snapshot.name = "exited";
    snapshot.stack_addr = stack;
    snapshot.stack_size = 4096;
    snapshot.tls_addr = tls;
    snapshot.delete_requested = true;
    auto owner = env.kernel.create_retained_thread_from_snapshot(env.mem, snapshot);
    owner->apply_private_snapshot(snapshot);
    EXPECT_FALSE(owner->cpu);
    EXPECT_FALSE(env.kernel.get_thread(7));
    auto mutex = std::make_shared<Mutex>(0);
    mutex->uid = 500;
    mutex->owner = owner;
    mutex->lock_count = 1;
    auto rwlock = std::make_shared<RWLock>(0);
    rwlock->uid = 501;
    rwlock->state = RWLockState::ReadLocked;
    rwlock->owners.emplace(owner, 1);
    env.kernel.mutexes.emplace(500, mutex);
    env.kernel.rwlocks.emplace(501, rwlock);
    std::vector<ThreadState::Snapshot> captured;
    std::string error;
    EXPECT_TRUE(emucorev::savestate::capture_thread_snapshots(env.kernel, 0, captured, error)) << error;
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_FALSE(captured[0].registered);
    EXPECT_EQ(captured[0].stack_addr, stack);
    EXPECT_EQ(captured[0].tls_addr, tls);
    std::weak_ptr<ThreadState> lifetime = owner;
    env.kernel.snapshot_thread_owners.clear();
    owner.reset();
    EXPECT_TRUE(is_valid_addr(env.mem, stack));
    EXPECT_TRUE(is_valid_addr(env.mem, tls));
    EXPECT_EQ(mutex_delete(env.kernel, "test", 1, 500, SyncWeight::Heavy), SCE_KERNEL_OK);
    mutex.reset();
    EXPECT_FALSE(lifetime.expired());
    EXPECT_TRUE(is_valid_addr(env.mem, stack));
    EXPECT_EQ(rwlock_delete(env.kernel, env.mem, "test", 1, 501), SCE_KERNEL_OK);
    rwlock.reset();
    EXPECT_TRUE(lifetime.expired());
    EXPECT_FALSE(is_valid_addr(env.mem, stack));
    EXPECT_FALSE(is_valid_addr(env.mem, tls));
    deinit_mem(env.mem);
}

TEST(KernelContinuation, deleting_restored_worker_before_helper_entry_detaches_queued_heap_node) {
    KernelLifecycleEnv env;
    auto object = std::make_shared<Semaphore>(0);
    object->uid = 500;
    object->max = 1;
    env.kernel.semaphores.emplace(500, object);
    auto thread = continuation_thread(env, 1);
    ThreadState::Snapshot snapshot;
    snapshot.id = 1;
    snapshot.status = ThreadStatus::waiting;
    WaitContinuationSnapshot wait;
    wait.operation = WaitOperation::semaphore;
    wait.args = { 500, 1 };
    wait.frame_sequence = 1;
    wait.sequence = 1;
    wait.queued = true;
    wait.target = { SCE_KERNEL_WAITTYPE_SEMAPHORE, 500 };
    snapshot.waits.push_back(wait);
    thread->apply_private_snapshot(snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    ASSERT_EQ(object->waiters.size(), 1u);
    thread->exit_delete(false);
    thread->clear_wait_continuations();
    EXPECT_TRUE(object->waiters.empty());
    EXPECT_EQ(semaphore_signal(env.kernel, "test", 1, 500, 1), SCE_KERNEL_OK);
    std::weak_ptr<ThreadState> lifetime = thread;
    env.kernel.threads.erase(1);
    thread.reset();
    EXPECT_TRUE(lifetime.expired());
}

TEST(KernelContinuation, frozen_deadline_snapshot_does_not_age_during_serialization) {
    KernelLifecycleEnv env;
    auto guest = continuation_thread(env, 1);
    const auto uid = semaphore_create(env.kernel, "test", "semaphore", 1, 0, 0, 1);
    auto object = env.kernel.semaphores.at(uid);
    auto pending = lifecycle_async(guest, [&] { SceUInt32 timeout = 200000; return semaphore_wait(env.kernel, "test", 1, uid, 1, &timeout, false); });
    ASSERT_TRUE(lifecycle_queued(object));
    ASSERT_EQ(env.kernel.stop_world(0, 1s), 0);
    const auto first = guest->capture_snapshot();
    std::this_thread::sleep_for(25ms);
    const auto later = guest->capture_snapshot();
    guest->exit_delete(false);
    pending.get();
    env.kernel.resume_world();
    ASSERT_EQ(first.waits.size(), 1u);
    ASSERT_EQ(later.waits.size(), 1u);
    EXPECT_EQ(first.waits[0].remaining_us, later.waits[0].remaining_us);
    EXPECT_GT(first.waits[0].remaining_us, 0u);
}

TEST(KernelContinuation, check_callback_resumes_saved_processed_count) {
    KernelLifecycleEnv env;
    auto thread = continuation_thread(env, 1);
    ThreadState::Snapshot snapshot;
    snapshot.id = 1;
    snapshot.status = ThreadStatus::waiting;
    WaitContinuationSnapshot dispatch;
    dispatch.frame_sequence = 1;
    dispatch.operation = WaitOperation::callback_dispatch;
    dispatch.args = { check_callback_import, 3 };
    snapshot.waits.push_back(dispatch);
    thread->apply_private_snapshot(snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    thread->activate_restored_continuations();
    EXPECT_EQ(continuation_resume(thread), 3);
    EXPECT_TRUE(thread->saved_wait_continuations().empty());
}
TEST(KernelContinuation, completed_notification_restore_keeps_self_notification_for_next_dispatch) {
    KernelLifecycleEnv env;
    auto thread = continuation_thread(env, 1);
    auto callback = std::make_shared<Callback>(99, thread, "callback", Ptr<SceKernelCallbackFunction>(0x1000), Ptr<void>{});
    env.kernel.callbacks.emplace(99, callback);
    callback->direct_notify(42);
    ThreadState::Snapshot snapshot;
    snapshot.id = 1;
    snapshot.status = ThreadStatus::waiting;
    snapshot.is_processing_callbacks = true;
    snapshot.callback_uids = { 99 };
    snapshot.callbacks_pending = true;
    WaitContinuationSnapshot dispatch;
    dispatch.frame_sequence = 1;
    dispatch.operation = WaitOperation::callback_dispatch;
    dispatch.args = { check_callback_import, 2 };
    snapshot.waits.push_back(dispatch);
    CallbackContinuationSnapshot frame;
    frame.frame_sequence = 2;
    frame.purpose = CallbackPurpose::notification;
    frame.callback_uid = 99;
    frame.guest_returned = true;
    snapshot.callback_frames.push_back(frame);
    thread->apply_private_snapshot(snapshot);
    ASSERT_TRUE(thread->restore_wait_queues());
    thread->activate_restored_continuations();
    thread->resume_restored_continuations();
    EXPECT_EQ(callback->get_num_notifications(), 1u);
    EXPECT_EQ(callback->get_notify_arg(), 42);
    EXPECT_TRUE(thread->saved_wait_continuations().empty());
    EXPECT_FALSE(thread->capture_snapshot().is_processing_callbacks);
    EXPECT_TRUE(thread->capture_snapshot().callback_frames.empty());
}
