// One-waiter condition signaling preserves the queue's FIFO/priority policy.
// Include after kernel_lifecycle_tests.inc in the shared or host harness.
// SPDX-License-Identifier: GPL-2.0-or-later

TEST(KernelConditionSignal, SignalAnyWakesOneInQueueOrderAndSignalAllWakesTheRest) {
    for (auto weight : {SyncWeight::Heavy, SyncWeight::Light}) {
        for (bool priority_order : {false, true}) {
            KernelLifecycleEnv env;
            lifecycle_memory(env);
            auto first = lifecycle_thread(env, 1), second = lifecycle_thread(env, 2), owner = lifecycle_thread(env, 3);
            first->priority = 200;
            second->priority = 100;
            const auto uid = mutex_create(env.kernel, env.mem, "test", "mutex", 1, 0, 1,
                weight == SyncWeight::Light ? Ptr<SceKernelLwMutexWork>(0x100) : Ptr<SceKernelLwMutexWork>{}, weight);
            const auto cid = condvar_create(env.kernel, env.mem, "test", "condition", 1,
                priority_order ? SCE_KERNEL_ATTR_TH_PRIO : 0, uid, Ptr<SceKernelLwCondWork>(0x200), weight);
            const auto cond = weight == SyncWeight::Light ? env.kernel.lwcondvars.at(cid) : env.kernel.condvars.at(cid);
            EXPECT_EQ(condvar_signal(env.kernel, "test", 1, cid, Condvar::SignalTarget(Condvar::SignalTarget::Type::Any), weight), 0);
            auto wait = [&](int id) {
                const auto result = condvar_wait(env.kernel, env.mem, "test", id, cid, nullptr, weight, false);
                if (!result)
                    mutex_unlock(env.kernel, env.mem, "test", id, uid, 1, weight);
                return result;
            };
            auto a = lifecycle_async(first, [&] { return wait(1); });
            ASSERT_TRUE(lifecycle_queued(cond));
            ASSERT_EQ(mutex_lock(env.kernel, env.mem, "test", 2, uid, 1, nullptr, weight, false), 0);
            auto b = lifecycle_async(second, [&] { return wait(2); });
            const auto until = std::chrono::steady_clock::now() + 1s;
            bool both = false;
            while (std::chrono::steady_clock::now() < until) {
                {
                    std::lock_guard guard(cond->mutex);
                    if (cond->waiters.size() == 2) {
                        both = true;
                        break;
                    }
                }
                std::this_thread::yield();
            }
            ASSERT_TRUE(both);
            ASSERT_EQ(mutex_lock(env.kernel, env.mem, "test", 3, uid, 1, nullptr, weight, false), 0);
            EXPECT_EQ(condvar_signal(env.kernel, "test", 3, cid, Condvar::SignalTarget(Condvar::SignalTarget::Type::Any), weight), 0);
            {
                std::lock_guard guard(cond->mutex);
                EXPECT_EQ(cond->waiters.size(), 1u);
                if (auto *remaining = cond->waiters.front())
                    EXPECT_EQ(remaining->thread->id, priority_order ? 1 : 2);
            }
            EXPECT_EQ(condvar_signal(env.kernel, "test", 3, cid, Condvar::SignalTarget(Condvar::SignalTarget::Type::All), weight), 0);
            {
                std::lock_guard guard(cond->mutex);
                EXPECT_TRUE(cond->waiters.empty());
            }
            EXPECT_EQ(mutex_unlock(env.kernel, env.mem, "test", 3, uid, 1, weight), 0);
            EXPECT_EQ(lifecycle_result(a, first), 0);
            EXPECT_EQ(lifecycle_result(b, second), 0);
        }
    }
}
