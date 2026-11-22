// Host tests for the RWLock immediate-acquisition rules shared by the blocking and the try-lock
// paths (kernel/rwlock_policy.h), plus a small model of rwlock_try_lock built on them.
#include <kernel/rwlock_policy.h>

#include <gtest/gtest.h>

#include <map>

namespace {
constexpr auto Acquire = RWLockAcquire::Acquire;
constexpr auto Recursive = RWLockAcquire::Recursive;
constexpr auto Busy = RWLockAcquire::Busy;

// Mirrors rwlock_try_lock's bookkeeping with thread ids instead of ThreadStatePtr.
struct Model {
    RWLockState state = RWLockState::Unlocked;
    std::map<int, int> owners;
    bool recursive_attr = false;
    // 0 = ok, 1 = FAILED_TO_LOCK, 2 = RECURSIVE
    int try_lock(int thread, bool is_write) {
        const bool is_recursive = !is_write || recursive_attr;
        switch (rwlock_acquire_decision(state, is_write, is_recursive, owners.contains(thread))) {
        case RWLockAcquire::Acquire:
            owners[thread]++;
            state = is_write ? RWLockState::WriteLocked : RWLockState::ReadLocked;
            return 0;
        case RWLockAcquire::Recursive:
            return 2;
        case RWLockAcquire::Busy:
            return 1;
        }
        return -1;
    }
};
} // namespace

TEST(RWLockPolicy, UnlockedAlwaysAcquires) {
    for (bool w : { false, true })
        for (bool r : { false, true })
            EXPECT_EQ(rwlock_acquire_decision(RWLockState::Unlocked, w, r, false), Acquire);
}

TEST(RWLockPolicy, ReadersShareReadLock) {
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, false, true, false), Acquire);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, false, false), Busy);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, true, false), Busy);
}

TEST(RWLockPolicy, WriteLockedBlocksOthers) {
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, false, true, false), Busy);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, true, false, false), Busy);
}

TEST(RWLockPolicy, OwnerRecursion) {
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, true, true, true), Acquire);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, true, false, true), Recursive);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, false, true), Recursive);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, false, true, true), Acquire); // read is always recursive
}

TEST(RWLockTryModel, WriterExcludesOthersAndReaders) {
    Model m;
    EXPECT_EQ(m.try_lock(1, true), 0);
    EXPECT_EQ(m.try_lock(2, true), 1);
    EXPECT_EQ(m.try_lock(2, false), 1);
    EXPECT_EQ(m.try_lock(1, true), 2); // non-recursive owner
}

TEST(RWLockTryModel, ReadersShareButWriterFails) {
    Model m;
    EXPECT_EQ(m.try_lock(1, false), 0);
    EXPECT_EQ(m.try_lock(2, false), 0);
    EXPECT_EQ(m.try_lock(3, true), 1);
    EXPECT_EQ(m.owners.size(), 2u);
}

TEST(RWLockTryModel, RecursiveWriterCountsUp) {
    Model m;
    m.recursive_attr = true;
    EXPECT_EQ(m.try_lock(1, true), 0);
    EXPECT_EQ(m.try_lock(1, true), 0);
    EXPECT_EQ(m.owners[1], 2);
    EXPECT_EQ(m.try_lock(2, true), 1);
}
