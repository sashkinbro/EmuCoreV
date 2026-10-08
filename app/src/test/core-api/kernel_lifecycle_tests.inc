// Shared with the host runner: both execute production primitive/wait code.
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <limits>
#include <thread>
using namespace std::chrono_literals;
namespace {
struct LifecycleFuture {
    ThreadStatePtr thread;
    std::future<SceInt32> future;
    ~LifecycleFuture() {
        if (future.valid()) {
            thread->exit_delete(false);
            future.wait();
        }
    }
    SceInt32 get() { return future.get(); }
    template <class Duration>
    auto wait_for(Duration duration) { return future.wait_for(duration); }
};
template <class Function>
LifecycleFuture lifecycle_async(const ThreadStatePtr &thread, Function function) {
    return { thread, std::async(std::launch::async, std::move(function)) };
}
ThreadStatePtr lifecycle_thread(KernelLifecycleEnv &env, SceUID id) {
    auto thread = std::make_shared<ThreadState>(id, env.kernel, env.mem);
    thread->priority = 100;
    std::lock_guard guard(env.kernel.mutex);
    env.kernel.threads.emplace(id, thread);
    return thread;
}
template <class Primitive>
bool lifecycle_queued(const std::shared_ptr<Primitive> &object) {
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard guard(object->mutex);
            if (!object->waiters.empty())
                return true;
        }
        std::this_thread::yield();
    }
    return false;
}
} // namespace
TEST(KernelLifecycle, deleting_a_blocked_semaphore_wakes_and_unregisters_it) {
    KernelLifecycleEnv env;
    auto thread = lifecycle_thread(env, 1);
    const auto id = semaphore_create(env.kernel, "test", "semaphore", 1, 0, 0, 2);
    const auto object = env.kernel.semaphores.at(id);
    auto waiting = lifecycle_async(thread, [&] {SceUInt32 timeout=200000;return semaphore_wait(env.kernel,"test",1,id,1,&timeout,false); });
    ASSERT_TRUE(lifecycle_queued(object));
    EXPECT_EQ(semaphore_delete(env.kernel, "test", 1, id), SCE_KERNEL_OK);
    EXPECT_EQ(waiting.get(), SCE_KERNEL_ERROR_WAIT_DELETE);
    EXPECT_EQ(env.kernel.semaphores.count(id), 0u);
    EXPECT_EQ(semaphore_signal(env.kernel, "test", 1, id, 1), SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID);
}
TEST(KernelLifecycle, deleting_an_event_flag_reports_its_current_bits) {
    KernelLifecycleEnv env;
    auto thread = lifecycle_thread(env, 1);
    const auto id = eventflag_create(env.kernel, "test", 1, "flag", 0, 4);
    const auto object = env.kernel.eventflags.at(id);
    SceUInt32 bits = 0;
    auto waiting = lifecycle_async(thread, [&] {SceUInt32 timeout=200000;return eventflag_wait(env.kernel,"test",1,id,8,0,&bits,&timeout,false); });
    ASSERT_TRUE(lifecycle_queued(object));
    EXPECT_EQ(eventflag_set(env.kernel, "test", 1, id, 2), 0);
    EXPECT_EQ(eventflag_delete(env.kernel, "test", 1, id), SCE_KERNEL_OK);
    EXPECT_EQ(waiting.get(), SCE_KERNEL_ERROR_WAIT_DELETE);
    EXPECT_EQ(bits, 6u);
    EXPECT_EQ(env.kernel.eventflags.count(id), 0u);
}

namespace {
SceInt32 lifecycle_result(LifecycleFuture &future, const ThreadStatePtr &thread) {
    const auto state = future.wait_for(1s);
    EXPECT_EQ(state, std::future_status::ready);
    if (state != std::future_status::ready)
        thread->exit_delete(false);
    return future.get();
}
void lifecycle_memory(KernelLifecycleEnv &env) {
    env.mem.memory = Memory(new uint8_t[4096]{}, [](uint8_t *p) { delete[] p; });
}
} // namespace
TEST(KernelLifecycle, simple_event_cancel_preserves_pattern_then_delete_wakes) {
    KernelLifecycleEnv env;
    auto thread = lifecycle_thread(env, 1);
    const auto id = simple_event_create(env.kernel, env.mem, "test", "event", 1, 0, 2);
    const auto object = env.kernel.simple_events.at(id);
    for (bool cancel : { true, false }) {
        auto waiting = lifecycle_async(thread, [&] {SceUInt32 timeout=1000000;return simple_event_waitorpoll(env.kernel,"test",1,id,4,nullptr,nullptr,&timeout,true,false); });
        ASSERT_TRUE(lifecycle_queued(object));
        SceUInt32 count = 99;
        EXPECT_EQ(cancel ? simple_event_cancel(env.kernel, "test", id, &count) : simple_event_delete(env.kernel, "test", 1, id), 0);
        EXPECT_EQ(lifecycle_result(waiting, thread), cancel ? SCE_KERNEL_ERROR_WAIT_CANCEL : SCE_KERNEL_ERROR_WAIT_DELETE);
        if (cancel) {
            EXPECT_EQ(count, 1u);
            EXPECT_EQ(object->pattern, 2u);
        }
    }
    EXPECT_FALSE(object->lock());
    EXPECT_EQ(simple_event_cancel(env.kernel, "test", id, nullptr), SCE_KERNEL_ERROR_UNKNOWN_EVENT_ID);
}
TEST(KernelLifecycle, timer_cancel_via_event_and_delete_end_the_manual_wait_loop) {
    KernelLifecycleEnv env;
    auto thread = lifecycle_thread(env, 1);
    const auto id = timer_create(env.kernel, env.mem, "test", "timer", 1, 0);
    const auto object = env.kernel.timers.at(id);
    SceKernelSysClock interval = 1000000000;
    ASSERT_EQ(timer_set(env.kernel, "test", 1, id, 1, &interval, 1), 0);
    ASSERT_EQ(timer_start(env.kernel, "test", 1, id), 0);
    for (bool cancel : { true, false }) {
        auto waiting = lifecycle_async(thread, [&] { return timer_waitorpoll(env.kernel, "test", 1, id, 0, nullptr, nullptr, nullptr, true, false); });
        ASSERT_TRUE(lifecycle_queued(object));
        SceUInt32 count = 99;
        EXPECT_EQ(cancel ? simple_event_cancel(env.kernel, "test", id, &count) : timer_delete(env.kernel, "test", id), 0);
        EXPECT_EQ(lifecycle_result(waiting, thread), cancel ? SCE_KERNEL_ERROR_WAIT_CANCEL : SCE_KERNEL_ERROR_WAIT_DELETE);
        if (cancel) {
            EXPECT_EQ(count, 1u);
            EXPECT_FALSE(object->is_repeat);
            EXPECT_FALSE(object->is_pulse);
            EXPECT_EQ(object->event_interval, 0u);
            EXPECT_EQ(object->next_event, std::numeric_limits<uint64_t>::max());
        }
    }
    EXPECT_EQ(timer_delete(env.kernel, "test", id), SCE_KERNEL_ERROR_UNKNOWN_TIMER_ID);
    EXPECT_FALSE(object->lock());
}
TEST(KernelLifecycle, mutex_cancel_validates_before_waking_and_resets_ownership) {
    KernelLifecycleEnv env;
    auto owner = lifecycle_thread(env, 1);
    auto waiter = lifecycle_thread(env, 2);
    const auto id = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, 1, {}, SyncWeight::Heavy);
    const auto object = env.kernel.mutexes.at(id);
    auto waiting = lifecycle_async(waiter, [&] {SceUInt32 timeout=1000000;return mutex_lock(env.kernel,env.mem,"test",2,id,1,&timeout,SyncWeight::Heavy,false); });
    ASSERT_TRUE(lifecycle_queued(object));
    SceUInt32 count = 99;
    EXPECT_EQ(mutex_cancel(env.kernel, "test", 1, id, 2, &count), SCE_KERNEL_ERROR_ILLEGAL_COUNT);
    EXPECT_EQ(count, 99u);
    EXPECT_EQ(waiting.wait_for(10ms), std::future_status::timeout);
    EXPECT_EQ(mutex_cancel(env.kernel, "test", 1, id, 0, &count), 0);
    EXPECT_EQ(lifecycle_result(waiting, waiter), SCE_KERNEL_ERROR_WAIT_CANCEL);
    EXPECT_EQ(count, 1u);
    EXPECT_EQ(object->lock_count, 0);
    EXPECT_FALSE(object->owner);
    EXPECT_EQ(mutex_cancel(env.kernel, "test", 2, id, -1, nullptr), 0);
    EXPECT_EQ(object->lock_count, 1);
    EXPECT_EQ(object->owner, waiter);
    EXPECT_EQ(mutex_delete(env.kernel, "test", 1, id, SyncWeight::Heavy), 0);
    EXPECT_EQ(mutex_try_lock(env.kernel, env.mem, "test", 1, id, 1, SyncWeight::Heavy), SCE_KERNEL_ERROR_UNKNOWN_MUTEX_ID);
    EXPECT_FALSE(object->lock());
}
TEST(KernelLifecycle, deleted_lightweight_mutex_never_writes_its_former_work_area) {
    KernelLifecycleEnv env;
    lifecycle_memory(env);
    auto owner = lifecycle_thread(env, 1);
    auto waiter = lifecycle_thread(env, 2);
    const Ptr<SceKernelLwMutexWork> work(0x100);
    const auto id = mutex_create(env.kernel, env.mem, "test", "lwmutex", 1, 0, 1, work, SyncWeight::Light);
    const auto object = env.kernel.lwmutexes.at(id);
    EXPECT_EQ(mutex_get(env.kernel, "test", 1, id, SyncWeight::Light), object);
    auto waiting = lifecycle_async(waiter, [&] {SceUInt32 timeout=1000000;return mutex_lock(env.kernel,env.mem,"test",2,id,1,&timeout,SyncWeight::Light,false); });
    ASSERT_TRUE(lifecycle_queued(object));
    std::memset(work.get(env.mem), 0x5A, sizeof(SceKernelLwMutexWork));
    EXPECT_EQ(mutex_delete(env.kernel, "test", 1, id, SyncWeight::Light), 0);
    EXPECT_EQ(lifecycle_result(waiting, waiter), SCE_KERNEL_ERROR_WAIT_DELETE);
    const auto *bytes = reinterpret_cast<const uint8_t *>(work.get(env.mem));
    for (size_t i = 0; i < sizeof(SceKernelLwMutexWork); ++i)
        EXPECT_EQ(bytes[i], 0x5A);
    EXPECT_EQ(mutex_try_lock(env.kernel, env.mem, "test", 1, id, 1, SyncWeight::Light), SCE_KERNEL_ERROR_UNKNOWN_LW_MUTEX_ID);
    EXPECT_FALSE(object->lock());
}
TEST(KernelLifecycle, rwlock_cancel_counts_readers_and_writers_and_can_take_write_ownership) {
    KernelLifecycleEnv env;
    auto owner = lifecycle_thread(env, 1);
    auto reader = lifecycle_thread(env, 2);
    auto writer = lifecycle_thread(env, 3);
    const auto id = rwlock_create(env.kernel, env.mem, "test", "rwlock", 1, 0);
    const auto object = env.kernel.rwlocks.at(id);
    ASSERT_EQ(rwlock_lock(env.kernel, env.mem, "test", 1, id, nullptr, true, false), 0);
    auto read = lifecycle_async(reader, [&] {SceUInt32 timeout=1000000;return rwlock_lock(env.kernel,env.mem,"test",2,id,&timeout,false,false); });
    ASSERT_TRUE(lifecycle_queued(object));
    auto write = lifecycle_async(writer, [&] {SceUInt32 timeout=1000000;return rwlock_lock(env.kernel,env.mem,"test",3,id,&timeout,true,false); });
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < deadline) {
        std::lock_guard guard(object->mutex);
        if (object->waiters.size() == 2)
            break;
    }
    SceUInt32 readers = 99, writers = 99;
    EXPECT_EQ(rwlock_cancel(env.kernel, "test", 1, id, &readers, &writers, SCE_KERNEL_RW_LOCK_CANCEL_WITH_WRITE_LOCK), 0);
    EXPECT_EQ(lifecycle_result(read, reader), SCE_KERNEL_ERROR_WAIT_CANCEL);
    EXPECT_EQ(lifecycle_result(write, writer), SCE_KERNEL_ERROR_WAIT_CANCEL);
    EXPECT_EQ(readers, 1u);
    EXPECT_EQ(writers, 1u);
    EXPECT_EQ(object->state, RWLockState::WriteLocked);
    EXPECT_EQ(object->owners.size(), 1u);
    EXPECT_TRUE(object->owners.contains(owner));
    auto deleted = lifecycle_async(reader, [&] {SceUInt32 timeout=1000000;return rwlock_lock(env.kernel,env.mem,"test",2,id,&timeout,false,false); });
    ASSERT_TRUE(lifecycle_queued(object));
    EXPECT_EQ(rwlock_delete(env.kernel, env.mem, "test", 1, id), 0);
    EXPECT_EQ(lifecycle_result(deleted, reader), SCE_KERNEL_ERROR_WAIT_DELETE);
    EXPECT_FALSE(object->lock());
}
TEST(KernelLifecycle, msgpipe_cancel_clears_data_and_wakes_both_queues_and_delete_does_the_same) {
    KernelLifecycleEnv env;
    auto sender = lifecycle_thread(env, 1);
    auto receiver = lifecycle_thread(env, 2);
    for (bool cancel : { true, false }) {
        const auto id = msgpipe_create(env.kernel, "test", "pipe", 1, 0, 4);
        const auto object = env.kernel.msgpipes.at(id);
        const std::array<uint8_t, 4> data = { 1, 2, 3, 4 };
        std::array<uint8_t, 4> output{};
        ASSERT_EQ(msgpipe_send(env.kernel, "test", 1, id, 0, data.data(), 2, nullptr, false), 2u);
        auto send = lifecycle_async(sender, [&] {SceUInt32 timeout=1000000;return static_cast<SceInt32>(msgpipe_send(env.kernel,"test",1,id,SCE_KERNEL_MSG_PIPE_MODE_FULL,data.data(),3,&timeout,false)); });
        auto receive = lifecycle_async(receiver, [&] {SceUInt32 timeout=1000000;return static_cast<SceInt32>(msgpipe_recv(env.kernel,"test",2,id,SCE_KERNEL_MSG_PIPE_MODE_FULL,output.data(),3,&timeout,false)); });
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        bool queued = false;
        while (std::chrono::steady_clock::now() < deadline) {
            std::lock_guard guard(object->mutex);
            if (!object->senders.empty() && !object->receivers.empty()) {
                queued = true;
                break;
            }
        }
        ASSERT_TRUE(queued);
        SceUInt32 sends = 99, receives = 99;
        EXPECT_EQ(cancel ? msgpipe_cancel(env.kernel, "test", id, &sends, &receives) : msgpipe_delete(env.kernel, "test", 1, id), 0);
        EXPECT_EQ(lifecycle_result(send, sender), cancel ? SCE_KERNEL_ERROR_WAIT_CANCEL : SCE_KERNEL_ERROR_WAIT_DELETE);
        EXPECT_EQ(lifecycle_result(receive, receiver), cancel ? SCE_KERNEL_ERROR_WAIT_CANCEL : SCE_KERNEL_ERROR_WAIT_DELETE);
        if (cancel) {
            EXPECT_EQ(sends, 1u);
            EXPECT_EQ(receives, 1u);
            EXPECT_EQ(object->data_buffer.Used(), 0u);
            EXPECT_EQ(msgpipe_delete(env.kernel, "test", 1, id), 0);
        }
        EXPECT_FALSE(object->lock());
    }
}
TEST(KernelLifecycle, deleting_a_condition_does_not_reacquire_a_mutex_owned_elsewhere) {
    for (auto weight : { SyncWeight::Heavy, SyncWeight::Light }) {
        KernelLifecycleEnv env;
        lifecycle_memory(env);
        auto waiter = lifecycle_thread(env, 1);
        auto owner = lifecycle_thread(env, 2);
        const auto mutex_id = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, 1, weight == SyncWeight::Light ? Ptr<SceKernelLwMutexWork>(0x100) : Ptr<SceKernelLwMutexWork>{}, weight);
        const auto cond_id = condvar_create(env.kernel, env.mem, "test", "condition", 1, 0, mutex_id, Ptr<SceKernelLwCondWork>(0x200), weight);
        const auto condition = weight == SyncWeight::Light ? env.kernel.lwcondvars.at(cond_id) : env.kernel.condvars.at(cond_id);
        auto waiting = lifecycle_async(waiter, [&] {SceUInt32 timeout=1000000;return condvar_wait(env.kernel,env.mem,"test",1,cond_id,&timeout,weight,false); });
        ASSERT_TRUE(lifecycle_queued(condition));
        ASSERT_EQ(mutex_lock(env.kernel, env.mem, "test", 2, mutex_id, 1, nullptr, weight, false), 0);
        EXPECT_EQ(condvar_delete(env.kernel, "test", 2, cond_id, weight), 0);
        EXPECT_EQ(lifecycle_result(waiting, waiter), weight == SyncWeight::Light ? SCE_KERNEL_ERROR_WAIT_DELETE_LW_COND : SCE_KERNEL_ERROR_WAIT_DELETE_COND);
        EXPECT_EQ(mutex_delete(env.kernel, "test", 2, mutex_id, weight), 0);
        EXPECT_FALSE(condition->lock());
    }
}
TEST(KernelLifecycle, condition_timeout_reacquires_untimed_and_mutex_delete_cancel_use_distinct_errors) {
    for (int scenario = 0; scenario < 4; ++scenario) {
        KernelLifecycleEnv env;
        auto waiter = lifecycle_thread(env, 1);
        auto owner = lifecycle_thread(env, 2);
        const auto mutex_id = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, 1, {}, SyncWeight::Heavy);
        const auto mutex = env.kernel.mutexes.at(mutex_id);
        const auto cond_id = condvar_create(env.kernel, env.mem, "test", "condition", 1, 0, mutex_id, {}, SyncWeight::Heavy);
        const auto condition = env.kernel.condvars.at(cond_id);
        auto waiting = lifecycle_async(waiter, [&] {SceUInt32 timeout=scenario==0?50000:1000000;return condvar_wait(env.kernel,env.mem,"test",1,cond_id,&timeout,SyncWeight::Heavy,false); });
        ASSERT_TRUE(lifecycle_queued(condition));
        ASSERT_EQ(mutex_lock(env.kernel, env.mem, "test", 2, mutex_id, 1, nullptr, SyncWeight::Heavy, false), 0);
        if (scenario != 0) {
            if (scenario == 3)
                EXPECT_EQ(mutex_delete(env.kernel, "test", 2, mutex_id, SyncWeight::Heavy), 0);
            EXPECT_EQ(condvar_signal(env.kernel, "test", 2, cond_id, Condvar::SignalTarget(Condvar::SignalTarget::Type::All), SyncWeight::Heavy), 0);
        }
        if (scenario != 3)
            ASSERT_TRUE(lifecycle_queued(mutex));
        if (scenario == 0) {
            EXPECT_EQ(waiting.wait_for(10ms), std::future_status::timeout);
            EXPECT_EQ(mutex_unlock(env.kernel, env.mem, "test", 2, mutex_id, 1, SyncWeight::Heavy), 0);
        } else if (scenario == 1)
            EXPECT_EQ(mutex_cancel(env.kernel, "test", 2, mutex_id, 1, nullptr), 0);
        else if (scenario == 2)
            EXPECT_EQ(mutex_delete(env.kernel, "test", 2, mutex_id, SyncWeight::Heavy), 0);
        EXPECT_EQ(lifecycle_result(waiting, waiter), scenario == 0 ? SCE_KERNEL_ERROR_WAIT_TIMEOUT : scenario == 1 ? SCE_KERNEL_ERROR_WAIT_CANCEL_MUTEX
                                                                                                                   : SCE_KERNEL_ERROR_WAIT_DELETE_MUTEX);
        if (scenario == 0)
            EXPECT_EQ(mutex->owner, waiter);
        if (scenario < 2)
            EXPECT_EQ(mutex_delete(env.kernel, "test", 2, mutex_id, SyncWeight::Heavy), 0);
        EXPECT_EQ(condvar_delete(env.kernel, "test", 2, cond_id, SyncWeight::Heavy), 0);
    }
}

TEST(KernelLifecycle, deletion_releases_the_registry_lock_before_waiting_for_the_object) {
    KernelLifecycleEnv env;
    const auto id = semaphore_create(env.kernel, "test", "semaphore", 0, 0, 0, 2);
    const auto saved = env.kernel.semaphores.at(id);
    std::unique_lock object_lock(saved->mutex);
    auto deletion = std::async(std::launch::async, [&] { return semaphore_delete(env.kernel, "test", 0, id); });
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    bool extracted = false;
    while (std::chrono::steady_clock::now() < deadline) {
        if (env.kernel.mutex.try_lock()) {
            extracted = env.kernel.semaphores.count(id) == 0;
            env.kernel.mutex.unlock();
            if (extracted)
                break;
        }
        std::this_thread::yield();
    }
    const auto blocked = deletion.wait_for(10ms);
    object_lock.unlock();
    EXPECT_TRUE(extracted);
    EXPECT_EQ(blocked, std::future_status::timeout);
    EXPECT_EQ(deletion.get(), 0);
    EXPECT_FALSE(saved->lock());
    EXPECT_EQ(semaphore_signal(env.kernel, "test", 0, id, 1), SCE_KERNEL_ERROR_UNKNOWN_SEMA_ID);
}
