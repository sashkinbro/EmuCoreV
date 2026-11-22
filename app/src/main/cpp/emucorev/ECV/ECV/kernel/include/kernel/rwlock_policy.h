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

// Immediate-acquisition rules of an RWLock, shared by the blocking and the try-lock paths.
// Free of emulator state so the rules can be unit tested on the host.

enum class RWLockState {
    Unlocked,
    ReadLocked,
    WriteLocked,
};

enum class RWLockAcquire {
    Acquire, // the lock can be taken right now
    Recursive, // not recursive-capable and already owned by the caller: error, never wait
    Busy, // the caller would have to wait (try-lock: fail)
};

// is_recursive: always true for read locks, the mutex-recursive attribute for write locks.
// owned_by_caller: the calling thread already holds the lock (in either mode).
constexpr RWLockAcquire rwlock_acquire_decision(RWLockState state, bool is_write, bool is_recursive, bool owned_by_caller) {
    if (state == RWLockState::Unlocked // unlocked
        || (!is_write && state == RWLockState::ReadLocked) // read lock wanted while read-locked
        || (is_recursive && owned_by_caller)) // already held and recursion allowed
        return RWLockAcquire::Acquire;
    if (!is_recursive && owned_by_caller)
        return RWLockAcquire::Recursive;
    return RWLockAcquire::Busy;
}
