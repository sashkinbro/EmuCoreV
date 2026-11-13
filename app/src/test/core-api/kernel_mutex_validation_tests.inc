// Hardware-observed count/ownership errors for both mutex classes.
// Include after kernel_lifecycle_tests.inc in the shared or host harness.
// SPDX-License-Identifier: GPL-2.0-or-later

namespace {
Ptr<SceKernelLwMutexWork> validation_work(SyncWeight weight) {
    return weight == SyncWeight::Light ? Ptr<SceKernelLwMutexWork>(0x100) : Ptr<SceKernelLwMutexWork>{};
}
MutexPtr validation_mutex(KernelLifecycleEnv &env, SceUID uid, SyncWeight weight) {
    return weight == SyncWeight::Light ? env.kernel.lwmutexes.at(uid) : env.kernel.mutexes.at(uid);
}
std::array<uint8_t, sizeof(SceKernelLwMutexWork)> validation_work_bytes(KernelLifecycleEnv &env) {
    std::array<uint8_t, sizeof(SceKernelLwMutexWork)> bytes;
    std::memcpy(bytes.data(), Ptr<SceKernelLwMutexWork>(0x100).get(env.mem), bytes.size());
    return bytes;
}
} // namespace

TEST(KernelMutexValidation, InvalidCreationPreservesWorkareaAndRegistry) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        for (int count : {-1, 2}) {
            KernelLifecycleEnv env;
            lifecycle_memory(env);
            lifecycle_thread(env, 1);
            std::memset(Ptr<SceKernelLwMutexWork>(0x100).get(env.mem), 0x5A, sizeof(SceKernelLwMutexWork));
            const auto before = validation_work_bytes(env);
            EXPECT_EQ(static_cast<uint32_t>(mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, count, validation_work(weight), weight)), 0x8002000Fu);
            EXPECT_TRUE(env.kernel.mutexes.empty());
            EXPECT_TRUE(env.kernel.lwmutexes.empty());
            EXPECT_EQ(validation_work_bytes(env), before);
        }
    }
}

TEST(KernelMutexValidation, RecursiveInitialCountTwoCanBeReleasedOneAtATime) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        KernelLifecycleEnv env;
        lifecycle_memory(env);
        auto owner = lifecycle_thread(env, 1);
        const auto uid = mutex_create(env.kernel, env.mem, "test", "mutex", 1, SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 2, validation_work(weight), weight);
        ASSERT_GE(uid, 0);
        EXPECT_EQ(mutex_unlock(env.kernel, env.mem, "test", 1, uid, 1, weight), 0);
        EXPECT_EQ(validation_mutex(env, uid, weight)->owner, owner);
        EXPECT_EQ(validation_mutex(env, uid, weight)->lock_count, 1);
        EXPECT_EQ(mutex_unlock(env.kernel, env.mem, "test", 1, uid, 1, weight), 0);
        EXPECT_EQ(validation_mutex(env, uid, weight)->owner, nullptr);
        EXPECT_EQ(validation_mutex(env, uid, weight)->lock_count, 0);
    }
}

TEST(KernelMutexValidation, InvalidBlockingAndTryLockCountsCannotAcquireOrChangeTimeout) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        for (bool only_try : {false, true}) {
            for (int count : {0, -1, 2}) {
                SCOPED_TRACE(count);
                KernelLifecycleEnv env;
                lifecycle_memory(env);
                lifecycle_thread(env, 1);
                const auto uid = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, 0, validation_work(weight), weight);
                const auto before = validation_work_bytes(env);
                SceUInt32 timeout = 12345;
                const auto result = only_try ? mutex_try_lock(env.kernel, env.mem, "test", 1, uid, count, weight)
                                             : mutex_lock(env.kernel, env.mem, "test", 1, uid, count, &timeout, weight, false);
                EXPECT_EQ(static_cast<uint32_t>(result), 0x8002000Fu);
                EXPECT_EQ(timeout, 12345u);
                EXPECT_EQ(validation_mutex(env, uid, weight)->owner, nullptr);
                EXPECT_EQ(validation_mutex(env, uid, weight)->lock_count, 0);
                EXPECT_TRUE(validation_mutex(env, uid, weight)->waiters.empty());
                EXPECT_EQ(validation_work_bytes(env), before);
            }
        }
    }
}

TEST(KernelMutexValidation, InvalidUnlockCountsCannotChangeOwnershipOrWakeWaiter) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        KernelLifecycleEnv env;
        lifecycle_memory(env);
        auto owner = lifecycle_thread(env, 1), waiter = lifecycle_thread(env, 2);
        const auto uid = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, 1, validation_work(weight), weight);
        const auto mutex = validation_mutex(env, uid, weight);
        auto waiting = lifecycle_async(waiter, [&] { return mutex_lock(env.kernel, env.mem, "test", 2, uid, 1, nullptr, weight, false); });
        ASSERT_TRUE(lifecycle_queued(mutex));
        const auto before = validation_work_bytes(env);
        for (int count : {0, -1, 2}) {
            SCOPED_TRACE(count);
            EXPECT_EQ(static_cast<uint32_t>(mutex_unlock(env.kernel, env.mem, "test", 1, uid, count, weight)), 0x8002000Fu);
            EXPECT_EQ(validation_work_bytes(env), before);
            std::lock_guard guard(mutex->mutex);
            EXPECT_EQ(mutex->owner, owner);
            EXPECT_EQ(mutex->lock_count, 1);
            EXPECT_EQ(mutex->waiters.size(), 1u);
        }
        EXPECT_EQ(waiting.wait_for(10ms), std::future_status::timeout);
        EXPECT_EQ(mutex_unlock(env.kernel, env.mem, "test", 1, uid, 1, weight), 0);
        EXPECT_EQ(lifecycle_result(waiting, waiter), 0);
        EXPECT_EQ(mutex->owner, waiter);
    }
}

TEST(KernelMutexValidation, UnlockedAndForeignUnlockReportClassSpecificErrorsWithoutMutation) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        for (bool held : {false, true}) {
            KernelLifecycleEnv env;
            lifecycle_memory(env);
            auto owner = lifecycle_thread(env, 1), other = lifecycle_thread(env, 2);
            const auto uid = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, held ? 1 : 0, validation_work(weight), weight);
            const auto before = validation_work_bytes(env);
            const auto result = mutex_unlock(env.kernel, env.mem, "test", held ? 2 : 1, uid, 1, weight);
            EXPECT_EQ(static_cast<uint32_t>(result), weight == SyncWeight::Light ? 0x80028186u : 0x80028146u);
            EXPECT_EQ(validation_work_bytes(env), before);
            EXPECT_EQ(validation_mutex(env, uid, weight)->owner, held ? owner : nullptr);
            EXPECT_EQ(validation_mutex(env, uid, weight)->lock_count, held ? 1 : 0);
        }
    }
}

TEST(KernelMutexValidation, ValidRecursiveMultiCountLockAndUnlockRemainSupported) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        KernelLifecycleEnv env;
        lifecycle_memory(env);
        auto owner = lifecycle_thread(env, 1);
        const auto uid = mutex_create(env.kernel, env.mem, "test", "mutex", 1, SCE_KERNEL_MUTEX_ATTR_RECURSIVE, 0, validation_work(weight), weight);
        ASSERT_GE(uid, 0);
        EXPECT_EQ(mutex_lock(env.kernel, env.mem, "test", 1, uid, 2, nullptr, weight, false), 0);
        EXPECT_EQ(mutex_try_lock(env.kernel, env.mem, "test", 1, uid, 1, weight), 0);
        EXPECT_EQ(validation_mutex(env, uid, weight)->owner, owner);
        EXPECT_EQ(validation_mutex(env, uid, weight)->lock_count, 3);
        EXPECT_EQ(mutex_unlock(env.kernel, env.mem, "test", 1, uid, 3, weight), 0);
        EXPECT_EQ(validation_mutex(env, uid, weight)->owner, nullptr);
        EXPECT_EQ(validation_mutex(env, uid, weight)->lock_count, 0);
    }
}
