// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

// RWLock state machine shared by the kernel (sync_primitives.cpp) and the host tests.
// Free of emulator state: the owner key is a template parameter (ThreadStatePtr in the kernel,
// a plain id in the tests), the wait queue stays in the kernel and is walked with grant_queued().
//
// Firmware semantics implemented here:
//  - Read and write holds are counted separately per thread.
//  - Read locks are shared. A thread that already holds the lock (read or write) can always take
//    another read hold; a write owner that reads keeps the lock exclusive (other readers wait).
//  - A write lock is exclusive. Re-locking for write by the write owner needs
//    SCE_KERNEL_RW_LOCK_ATTR_RECURSIVE, otherwise it fails with RW_LOCK_RECURSIVE.
//  - A reader asking for write (upgrade): without the recursive attribute it is an error
//    (it could only deadlock); with it the caller is granted once it is the only holder,
//    and waits while other threads still hold read.
//  - Writers are not starved: a new reader waits when a writer is queued. Waiters are woken in
//    queue order (TH_FIFO / TH_PRIO, decided by the WaitQueue); a queued writer that cannot be
//    granted blocks everything queued behind it.
//  - UnlockRead / UnlockWrite only release a hold of their own kind.

#include <map>

enum class RWLockState {
    Unlocked,
    ReadLocked,
    WriteLocked,
};

enum class RWLockAcquire {
    Acquire, // the lock can be taken right now
    Recursive, // forbidden re-lock by a holder: error, never wait
    Busy, // the caller would have to wait (try-lock: fail)
};

enum class RWLockRelease {
    Released,
    NotHeld, // the caller holds nothing on this lock
    WrongKind, // the caller holds the lock, but not in the mode it tries to release
};

struct RWLockHold {
    int read = 0;
    int write = 0;
    constexpr bool any() const { return read > 0 || write > 0; }
};

// Save-state packing of a hold. Old snapshots stored one count per owner (its meaning depended on
// the lock state); a packed hold with write == 0 has the same encoding, so both decode correctly.
constexpr int rwlock_pack_hold(RWLockHold hold) {
    return (hold.write << 16) | (hold.read & 0xFFFF);
}
constexpr RWLockHold rwlock_unpack_hold(int packed, RWLockState saved_state) {
    if ((packed >> 16) != 0)
        return { packed & 0xFFFF, packed >> 16 };
    if (saved_state == RWLockState::WriteLocked)
        return { 0, packed };
    return { packed, 0 };
}

// Immediate-acquisition rule. caller: the caller's current holds; other_holders: another thread
// holds the lock (in any mode); writer_queued: a thread is queued for write.
constexpr RWLockAcquire rwlock_acquire_decision(RWLockState state, bool is_write, bool recursive_attr, RWLockHold caller, bool other_holders, bool writer_queued) {
    if (!is_write) {
        if (caller.any()) // holders may always read again (blocking would self-deadlock)
            return RWLockAcquire::Acquire;
        if (state == RWLockState::Unlocked)
            return RWLockAcquire::Acquire;
        if (state == RWLockState::ReadLocked && !writer_queued)
            return RWLockAcquire::Acquire;
        return RWLockAcquire::Busy;
    }
    if (caller.write > 0)
        return recursive_attr ? RWLockAcquire::Acquire : RWLockAcquire::Recursive;
    if (caller.read > 0) {
        if (!recursive_attr)
            return RWLockAcquire::Recursive;
        return other_holders ? RWLockAcquire::Busy : RWLockAcquire::Acquire;
    }
    return state == RWLockState::Unlocked ? RWLockAcquire::Acquire : RWLockAcquire::Busy;
}

template <typename Key>
struct RWLockCore {
    RWLockState state = RWLockState::Unlocked;
    std::map<Key, RWLockHold> owners;

    RWLockHold hold_of(const Key &key) const {
        const auto it = owners.find(key);
        return it == owners.end() ? RWLockHold{} : it->second;
    }

    bool other_holders(const Key &key) const {
        for (const auto &[owner, hold] : owners)
            if (!(owner == key) && hold.any())
                return true;
        return false;
    }

    void recompute_state() {
        state = RWLockState::Unlocked;
        for (const auto &[owner, hold] : owners) {
            if (hold.write > 0) {
                state = RWLockState::WriteLocked;
                return;
            }
            if (hold.read > 0)
                state = RWLockState::ReadLocked;
        }
    }

    RWLockAcquire decide(const Key &key, bool is_write, bool recursive_attr, bool writer_queued) const {
        return rwlock_acquire_decision(state, is_write, recursive_attr, hold_of(key), other_holders(key), writer_queued);
    }

    void grant(const Key &key, bool is_write) {
        auto &hold = owners[key];
        if (is_write)
            ++hold.write;
        else
            ++hold.read;
        recompute_state();
    }

    RWLockRelease release(const Key &key, bool is_write) {
        const auto it = owners.find(key);
        if (it == owners.end() || !it->second.any())
            return RWLockRelease::NotHeld;
        int &count = is_write ? it->second.write : it->second.read;
        if (count == 0)
            return RWLockRelease::WrongKind;
        --count;
        if (!it->second.any())
            owners.erase(it);
        recompute_state();
        return RWLockRelease::Released;
    }

    void reset() {
        owners.clear();
        state = RWLockState::Unlocked;
    }

    // One pass over the wait queue, in queue order: call grant_queued() for each waiter and wake
    // the ones it accepts (it already recorded their hold).
    struct WakePass {
        bool blocked = false;
    };
    bool grant_queued(WakePass &pass, const Key &key, bool is_write) {
        if (pass.blocked)
            return false;
        if (is_write) {
            const bool free = state == RWLockState::Unlocked
                || (state == RWLockState::ReadLocked && hold_of(key).read > 0 && !other_holders(key)); // upgrade
            pass.blocked = true; // nothing queued behind a writer goes first
            if (!free)
                return false;
            grant(key, true);
            return true;
        }
        if (state == RWLockState::WriteLocked) {
            pass.blocked = true;
            return false;
        }
        grant(key, false);
        return true;
    }
};
