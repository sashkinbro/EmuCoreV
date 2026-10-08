// Shared with the host harness; the Android path calls user and driver exports.
#include <functional>
#include <vector>

TEST(KernelUidClass, reports_all_registered_object_classes_and_rejects_deleted_uids) {
    KernelUidClassEnv env;
    lifecycle_memory(env);
    auto thread = lifecycle_thread(env, 10000);
    struct Entry {
        SceUID id;
        SceInt32 expected;
        std::function<void()> remove;
    };
    std::vector<Entry> objects;
    const auto add = [&](SceUID id, SceInt32 expected, std::function<void()> remove) {
        EXPECT_GT(id, 0);
        objects.push_back({ id, expected, std::move(remove) });
    };
    add(thread->id, 1, [&] { std::lock_guard lock(env.kernel.mutex); env.kernel.threads.erase(thread->id); });
    const auto sema = semaphore_create(env.kernel, "test", "sema", thread->id, 0, 0, 2);
    add(sema, 2, [&] { EXPECT_EQ(semaphore_delete(env.kernel, "test", thread->id, sema), 0); });
    const auto evf = eventflag_create(env.kernel, "test", thread->id, "evf", 0, 0);
    add(evf, 3, [&] { EXPECT_EQ(eventflag_delete(env.kernel, "test", thread->id, evf), 0); });
    const auto mutex = mutex_create(env.kernel, env.mem, "test", "mutex", thread->id, 0, 0, {}, SyncWeight::Heavy);
    add(mutex, 4, [&] { EXPECT_EQ(mutex_delete(env.kernel, "test", thread->id, mutex, SyncWeight::Heavy), 0); });
    const auto cond = condvar_create(env.kernel, env.mem, "test", "cond", thread->id, 0, mutex, {}, SyncWeight::Heavy);
    add(cond, 5, [&] { EXPECT_EQ(condvar_delete(env.kernel, "test", thread->id, cond, SyncWeight::Heavy), 0); });
    const auto timer = timer_create(env.kernel, env.mem, "test", "timer", thread->id, 0);
    add(timer, 6, [&] { EXPECT_EQ(timer_delete(env.kernel, "test", timer), 0); });
    const auto pipe = msgpipe_create(env.kernel, "test", "pipe", thread->id, 0, 4);
    add(pipe, 7, [&] { EXPECT_EQ(msgpipe_delete(env.kernel, "test", thread->id, pipe), 0); });
    const auto callback = env.kernel.get_next_uid();
    {
        std::lock_guard lock(env.kernel.mutex);
        env.kernel.callbacks.emplace(callback, std::make_shared<Callback>(callback, thread, "callback", Ptr<SceKernelCallbackFunction>{}, Ptr<void>{}));
    }
    add(callback, 8, [&] { EXPECT_TRUE(env.kernel.delete_callback(callback)); });
    const auto lwmutex = mutex_create(env.kernel, env.mem, "test", "lwmutex", thread->id, 0, 0, Ptr<SceKernelLwMutexWork>(0x100), SyncWeight::Light);
    add(lwmutex, 10, [&] { EXPECT_EQ(mutex_delete(env.kernel, "test", thread->id, lwmutex, SyncWeight::Light), 0); });
    const auto lwcond = condvar_create(env.kernel, env.mem, "test", "lwcond", thread->id, 0, lwmutex, Ptr<SceKernelLwCondWork>(0x200), SyncWeight::Light);
    add(lwcond, 11, [&] { EXPECT_EQ(condvar_delete(env.kernel, "test", thread->id, lwcond, SyncWeight::Light), 0); });
    const auto rwlock = rwlock_create(env.kernel, env.mem, "test", "rwlock", thread->id, 0);
    add(rwlock, 12, [&] { EXPECT_EQ(rwlock_delete(env.kernel, env.mem, "test", thread->id, rwlock), 0); });
    const auto event = simple_event_create(env.kernel, env.mem, "test", "event", thread->id, 0, 0);
    add(event, 13, [&] { EXPECT_EQ(simple_event_delete(env.kernel, "test", thread->id, event), 0); });
    for (const auto &object : objects) {
        EXPECT_EQ(uid_class_query(env, object.id, false), object.expected);
        EXPECT_EQ(uid_class_query(env, object.id, true), object.expected);
    }
    for (const auto &object : objects) {
        object.remove();
        EXPECT_EQ(uid_class_query(env, object.id, false), SCE_KERNEL_ERROR_UNKNOWN_UID);
        EXPECT_EQ(uid_class_query(env, object.id, true), SCE_KERNEL_ERROR_UNKNOWN_UID);
    }
    EXPECT_EQ(uid_class_query(env, 0, false), SCE_KERNEL_ERROR_UNKNOWN_UID);
    EXPECT_EQ(uid_class_query(env, -1, true), SCE_KERNEL_ERROR_UNKNOWN_UID);
    EXPECT_EQ(uid_class_query(env, 0x123456, false), SCE_KERNEL_ERROR_UNKNOWN_UID);
}
