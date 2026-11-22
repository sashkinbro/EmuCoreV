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

// sceNgsSystemLock / sceNgsSystemUnlock protocol.
//
// A guest thread that holds the system lock has exclusive access to the voice
// parameter buffers with respect to the NGS update (sceNgsSystemUpdate): an
// update neither starts nor is in flight while another thread holds the lock,
// so a multi-field parameter write cannot be observed half done.
//
// The lock state lives in a side table keyed by the System address instead of
// in VoiceScheduler: System has to stay inside the legacy 256-byte guest
// allocation. The state is guarded by the scheduler mutex (the caller owns the
// unique_lock passed in) and waited on with the scheduler condition variable,
// so the lock composes with the existing is_updating handshake. This header
// has no dependency on the emulator and is covered by host tests.

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace ngs {

struct ParamLockState {
    int32_t owner = 0;
    uint32_t depth = 0;

    bool blocks(const int32_t thread_id) const {
        return depth != 0 && owner != thread_id;
    }
};

// Blocks until no other thread holds the lock and no update by another thread
// is in flight, then takes (or re-enters) the lock. `lk` owns the mutex guarding
// `state`. `update_active_by_other` is evaluated under that mutex.
template <class Lock, class Cv, class UpdatePred, class BeforeBlock>
void param_lock_acquire(ParamLockState &state, Lock &lk, Cv &cv, const int32_t thread_id, UpdatePred &&update_active_by_other, BeforeBlock &&before_block) {
    while (state.blocks(thread_id) || update_active_by_other()) {
        before_block();
        cv.wait(lk);
    }
    state.owner = thread_id;
    state.depth++;
}

// Called at the top of an update, before it marks itself as updating.
template <class Lock, class Cv, class BeforeBlock>
void param_lock_wait_for_update(ParamLockState &state, Lock &lk, Cv &cv, const int32_t thread_id, BeforeBlock &&before_block) {
    while (state.blocks(thread_id)) {
        before_block();
        cv.wait(lk);
    }
}

// The system lock is not owned by a thread: games lock it on one thread and unlock it on
// another (Dead or Alive 5 Plus locks in its game thread and unlocks from a worker). Ignoring
// such an unlock kept the lock held forever and deadlocked the NGS update and the game.
// `owner` only lets the locking thread re-enter and run its own update while it holds the lock.
// Returns false only for an unbalanced unlock (lock not held at all).
template <class Cv>
bool param_lock_release(ParamLockState &state, Cv &cv, const int32_t /*thread_id*/) {
    if (state.depth == 0)
        return false;
    if (--state.depth == 0)
        state.owner = 0;
    cv.notify_all();
    return true;
}

// The returned reference stays valid until system_param_lock_erase(system).
inline ParamLockState &system_param_lock(const void *system) {
    static std::mutex table_mutex;
    static std::unordered_map<const void *, ParamLockState> table;
    const std::lock_guard<std::mutex> guard(table_mutex);
    return table[system];
}

inline void system_param_lock_erase(const void *system) {
    // same table as system_param_lock(); implemented through it to share the statics
    ParamLockState &state = system_param_lock(system);
    state = ParamLockState();
}

} // namespace ngs
