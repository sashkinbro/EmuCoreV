// Host tests for the RWLock state machine shared with the kernel (kernel/rwlock_policy.h):
// immediate-acquisition rules, per-kind hold accounting, unlock checks and the queue wake pass.
// Sim mirrors rwlock_lock / rwlock_try_lock / rwlock_unlock / rwlock_wake_waiters with thread ids
// and a FIFO queue instead of ThreadStatePtr and WaitQueue.
#include <kernel/rwlock_policy.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace {
constexpr auto Acquire = RWLockAcquire::Acquire;
constexpr auto Recursive = RWLockAcquire::Recursive;
constexpr auto Busy = RWLockAcquire::Busy;
constexpr RWLockHold none{};
constexpr RWLockHold reader{ 1, 0 };
constexpr RWLockHold writer{ 0, 1 };

enum Result {
    Ok,
    Waiting,
    FailedToLock,
    RecursiveErr,
    FailedToUnlock,
    NotOwned,
};

struct Sim {
    RWLockCore<int> core;
    bool recursive_attr = false;
    struct Waiter {
        int thread;
        bool is_write;
    };
    std::vector<Waiter> queue; // FIFO
    std::vector<int> woken;

    bool writer_queued() const {
        return std::any_of(queue.begin(), queue.end(), [](const Waiter &w) { return w.is_write; });
    }
    void wake_pass() {
        RWLockCore<int>::WakePass pass;
        for (auto it = queue.begin(); it != queue.end();) {
            if (core.grant_queued(pass, it->thread, it->is_write)) {
                woken.push_back(it->thread);
                it = queue.erase(it);
            } else {
                ++it;
            }
        }
    }
    Result lock(int t, bool w) {
        switch (core.decide(t, w, recursive_attr, writer_queued())) {
        case RWLockAcquire::Acquire:
            core.grant(t, w);
            return Ok;
        case RWLockAcquire::Recursive:
            return RecursiveErr;
        case RWLockAcquire::Busy:
            queue.push_back({ t, w });
            return Waiting;
        }
        return Ok;
    }
    Result try_lock(int t, bool w) {
        switch (core.decide(t, w, recursive_attr, writer_queued())) {
        case RWLockAcquire::Acquire:
            core.grant(t, w);
            return Ok;
        case RWLockAcquire::Recursive:
            return RecursiveErr;
        case RWLockAcquire::Busy:
            return FailedToLock;
        }
        return Ok;
    }
    Result unlock(int t, bool w) {
        switch (core.release(t, w)) {
        case RWLockRelease::NotHeld:
            return FailedToUnlock;
        case RWLockRelease::WrongKind:
            return NotOwned;
        case RWLockRelease::Released:
            break;
        }
        wake_pass();
        return Ok;
    }
    // a queued waiter gives up (timeout / cancel)
    void give_up(int t) {
        bool was_writer = false;
        for (auto it = queue.begin(); it != queue.end(); ++it) {
            if (it->thread == t) {
                was_writer = it->is_write;
                queue.erase(it);
                break;
            }
        }
        if (was_writer)
            wake_pass();
    }
    bool holds(int t) const { return core.hold_of(t).any(); }
    bool is_woken(int t) const { return std::find(woken.begin(), woken.end(), t) != woken.end(); }
};
} // namespace

TEST(RWLockPolicy, UnlockedAlwaysAcquires) {
    for (bool w : { false, true })
        for (bool r : { false, true })
            EXPECT_EQ(rwlock_acquire_decision(RWLockState::Unlocked, w, r, none, false, false), Acquire);
}

TEST(RWLockPolicy, ReadersShareUnlessWriterQueued) {
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, false, false, none, true, false), Acquire);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, false, false, none, true, true), Busy);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, true, none, true, false), Busy);
}

TEST(RWLockPolicy, WriteLockedBlocksOthers) {
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, false, true, none, true, false), Busy);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, true, true, none, true, false), Busy);
}

TEST(RWLockPolicy, OwnerRules) {
    // write owner re-locking for write
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, true, true, writer, false, false), Acquire);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, true, false, writer, false, false), Recursive);
    // write owner reading: always allowed, even with a queued writer
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::WriteLocked, false, false, writer, false, true), Acquire);
    // reader reading again: allowed even with a queued writer (blocking would self-deadlock)
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, false, false, reader, true, true), Acquire);
    // upgrade
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, false, reader, false, false), Recursive);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, true, reader, false, false), Acquire);
    EXPECT_EQ(rwlock_acquire_decision(RWLockState::ReadLocked, true, true, reader, true, false), Busy);
}

TEST(RWLockPolicy, HoldPackingIsBackwardCompatible) {
    // old snapshots: one count per owner, meaning taken from the lock state
    EXPECT_EQ(rwlock_unpack_hold(2, RWLockState::WriteLocked).write, 2);
    EXPECT_EQ(rwlock_unpack_hold(2, RWLockState::WriteLocked).read, 0);
    EXPECT_EQ(rwlock_unpack_hold(3, RWLockState::ReadLocked).read, 3);
    const RWLockHold back = rwlock_unpack_hold(rwlock_pack_hold({ 2, 1 }), RWLockState::WriteLocked);
    EXPECT_EQ(back.read, 2);
    EXPECT_EQ(back.write, 1);
    EXPECT_EQ(rwlock_unpack_hold(rwlock_pack_hold({ 3, 0 }), RWLockState::ReadLocked).read, 3);
}

// Tearaway (PCSA00144) "Resource Loading": the write owner takes a read hold while other threads
// are queued for read. The old code switched the lock to ReadLocked there, letting queued and new
// readers run inside the writer's critical section.
TEST(RWLockSim, WriteOwnerReadingKeepsLockExclusive) {
    Sim s;
    EXPECT_EQ(s.lock(148, true), Ok);
    EXPECT_EQ(s.lock(8, false), Waiting);
    EXPECT_EQ(s.lock(145, false), Waiting);
    EXPECT_EQ(s.lock(149, true), Waiting);
    EXPECT_EQ(s.lock(148, false), Ok); // the owner reads
    EXPECT_EQ(s.core.state, RWLockState::WriteLocked);
    EXPECT_EQ(s.try_lock(150, false), FailedToLock); // a new reader must not get in
    EXPECT_EQ(s.unlock(148, false), Ok); // the owner drops its read hold: still write-locked
    EXPECT_EQ(s.core.state, RWLockState::WriteLocked);
    EXPECT_TRUE(s.woken.empty());
    EXPECT_FALSE(s.holds(8));
    EXPECT_EQ(s.unlock(148, true), Ok); // queued readers first (FIFO), the writer waits for them
    EXPECT_TRUE(s.is_woken(8));
    EXPECT_TRUE(s.is_woken(145));
    EXPECT_FALSE(s.is_woken(149));
    EXPECT_EQ(s.core.state, RWLockState::ReadLocked);
    EXPECT_EQ(s.unlock(8, false), Ok);
    EXPECT_FALSE(s.is_woken(149));
    EXPECT_EQ(s.unlock(145, false), Ok);
    EXPECT_TRUE(s.is_woken(149));
    EXPECT_EQ(s.core.state, RWLockState::WriteLocked);
}

TEST(RWLockSim, DowngradeWriteThenReadReleases) {
    Sim s;
    EXPECT_EQ(s.lock(1, true), Ok);
    EXPECT_EQ(s.lock(1, false), Ok);
    EXPECT_EQ(s.lock(2, false), Waiting);
    EXPECT_EQ(s.unlock(1, true), Ok); // downgrade: 1 keeps reading, 2 may join
    EXPECT_EQ(s.core.state, RWLockState::ReadLocked);
    EXPECT_TRUE(s.is_woken(2));
    EXPECT_EQ(s.unlock(1, false), Ok);
    EXPECT_EQ(s.unlock(2, false), Ok);
    EXPECT_EQ(s.core.state, RWLockState::Unlocked);
}

TEST(RWLockSim, UnlockOnlyReleasesItsOwnKind) {
    Sim s;
    EXPECT_EQ(s.unlock(1, false), FailedToUnlock);
    EXPECT_EQ(s.lock(1, true), Ok);
    EXPECT_EQ(s.unlock(1, false), NotOwned); // UnlockRead on a write hold
    EXPECT_EQ(s.core.state, RWLockState::WriteLocked);
    EXPECT_EQ(s.unlock(2, true), FailedToUnlock); // not the owner
    EXPECT_EQ(s.unlock(1, true), Ok);
    EXPECT_EQ(s.lock(1, false), Ok);
    EXPECT_EQ(s.unlock(1, true), NotOwned); // UnlockWrite on a read hold
    EXPECT_EQ(s.core.state, RWLockState::ReadLocked);
}

TEST(RWLockSim, QueuedWriterIsNotStarved) {
    Sim s;
    EXPECT_EQ(s.lock(1, false), Ok);
    EXPECT_EQ(s.lock(2, true), Waiting);
    EXPECT_EQ(s.lock(3, false), Waiting); // a new reader queues behind the writer
    EXPECT_EQ(s.lock(1, false), Ok); // but a current reader may read again
    EXPECT_EQ(s.unlock(1, false), Ok);
    EXPECT_FALSE(s.is_woken(2));
    EXPECT_EQ(s.unlock(1, false), Ok);
    EXPECT_TRUE(s.is_woken(2));
    EXPECT_FALSE(s.is_woken(3));
    EXPECT_EQ(s.unlock(2, true), Ok);
    EXPECT_TRUE(s.is_woken(3));
}

TEST(RWLockSim, WriterGivingUpReleasesReadersBehindIt) {
    Sim s;
    EXPECT_EQ(s.lock(1, false), Ok);
    EXPECT_EQ(s.lock(2, true), Waiting);
    EXPECT_EQ(s.lock(3, false), Waiting);
    s.give_up(2);
    EXPECT_TRUE(s.is_woken(3));
    EXPECT_EQ(s.core.state, RWLockState::ReadLocked);
}

TEST(RWLockSim, RecursiveWriteNeedsAttribute) {
    Sim s;
    EXPECT_EQ(s.lock(1, true), Ok);
    EXPECT_EQ(s.lock(1, true), RecursiveErr);
    Sim r;
    r.recursive_attr = true;
    EXPECT_EQ(r.lock(1, true), Ok);
    EXPECT_EQ(r.lock(1, true), Ok);
    EXPECT_EQ(r.core.hold_of(1).write, 2);
    EXPECT_EQ(r.unlock(1, true), Ok);
    EXPECT_EQ(r.core.state, RWLockState::WriteLocked);
    EXPECT_EQ(r.unlock(1, true), Ok);
    EXPECT_EQ(r.core.state, RWLockState::Unlocked);
}

TEST(RWLockSim, UpgradeOnlyAsSoleReader) {
    Sim s;
    EXPECT_EQ(s.lock(1, false), Ok);
    EXPECT_EQ(s.lock(1, true), RecursiveErr); // not recursive: it could only deadlock, error instead
    Sim r;
    r.recursive_attr = true;
    EXPECT_EQ(r.lock(1, false), Ok);
    EXPECT_EQ(r.lock(2, false), Ok);
    EXPECT_EQ(r.lock(1, true), Waiting); // 2 still reads: the old code granted the write here
    EXPECT_EQ(r.core.state, RWLockState::ReadLocked);
    EXPECT_EQ(r.unlock(2, false), Ok);
    EXPECT_TRUE(r.is_woken(1));
    EXPECT_EQ(r.core.state, RWLockState::WriteLocked);
    EXPECT_EQ(r.core.hold_of(1).read, 1);
    EXPECT_EQ(r.core.hold_of(1).write, 1);
    Sim t;
    t.recursive_attr = true;
    EXPECT_EQ(t.lock(1, false), Ok);
    EXPECT_EQ(t.lock(1, true), Ok); // the sole reader upgrades at once
}

TEST(RWLockSim, TryLockMatchesBlockingRules) {
    Sim s;
    EXPECT_EQ(s.try_lock(1, true), Ok);
    EXPECT_EQ(s.try_lock(2, true), FailedToLock);
    EXPECT_EQ(s.try_lock(2, false), FailedToLock);
    EXPECT_EQ(s.try_lock(1, true), RecursiveErr);
    EXPECT_EQ(s.try_lock(1, false), Ok);
    EXPECT_TRUE(s.queue.empty());
}
