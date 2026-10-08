// Run with the real synchronization helper in both host and shared-core
// targets.
TEST(KernelMutexCache, cached_mutex_does_not_retain_guest_stack_owner) {
    KernelLifecycleEnv env;
    auto owner = lifecycle_thread(env, 11);
    std::weak_ptr<ThreadState> weak_owner = owner;
    auto object = std::make_shared<Mutex>(0);
    object->owner = owner;
    env.kernel.mutexes.emplace(4001, object);
    ASSERT_EQ(mutex_get(env.kernel, "test", 11, 4001, SyncWeight::Heavy), object);
    env.kernel.mutexes.clear();
    env.kernel.threads.clear();
    object.reset();
    owner.reset();
    EXPECT_TRUE(weak_owner.expired());
}
TEST(KernelMutexCache,
    equal_uids_in_distinct_kernels_resolve_to_their_own_object) {
    KernelLifecycleEnv first;
    KernelLifecycleEnv second;
    auto old_object = std::make_shared<Mutex>(0);
    auto new_object = std::make_shared<Mutex>(0);
    first.kernel.mutexes.emplace(4002, old_object);
    second.kernel.mutexes.emplace(4002, new_object);
    ASSERT_EQ(mutex_get(first.kernel, "test", 1, 4002, SyncWeight::Heavy),
        old_object);
    EXPECT_EQ(mutex_get(second.kernel, "test", 1, 4002, SyncWeight::Heavy),
        new_object);
}
TEST(KernelMutexCache,
    shutdown_generation_invalidates_reused_uid_with_old_object_alive) {
    KernelLifecycleEnv env;
    auto old_object = std::make_shared<Mutex>(0);
    auto new_object = std::make_shared<Mutex>(0);
    env.kernel.mutexes.emplace(4003, old_object);
    ASSERT_EQ(mutex_get(env.kernel, "test", 1, 4003, SyncWeight::Heavy),
        old_object);
    env.kernel.sync_cache_generation.fetch_add(1);
    env.kernel.mutexes.at(4003) = new_object;
    EXPECT_EQ(mutex_get(env.kernel, "test", 1, 4003, SyncWeight::Heavy),
        new_object);
}
