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

// A recursive lock owned by an object token instead of a host thread.
//
// The emulated mono loader lock is held by a guest CPU. A std:: mutex must be
// unlocked by the thread that locked it, so a guest thread torn down while
// holding it (process exit, exec switch) left it owned by a dead thread for
// the rest of the process: every later session waited out the timeout on each
// acquire (B151). Here the owner releases its hold from wherever it is
// destroyed, on any thread. No dependency on the emulator; host-tested.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

class OwnerLock {
public:
    // Returns false on timeout (the caller proceeds unserialised, as before).
    template <class Rep, class Period>
    bool acquire(const void *owner, std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);
        if (owner_ == owner) {
            ++depth_;
            return true;
        }
        if (!released_.wait_for(lock, timeout, [this] { return owner_ == nullptr; }))
            return false;
        owner_ = owner;
        depth_ = 1;
        return true;
    }

    // Returns false when `owner` does not hold the lock.
    bool release(const void *owner) {
        {
            const std::lock_guard lock(mutex_);
            if (owner_ != owner || depth_ == 0)
                return false;
            if (--depth_ != 0)
                return true;
            owner_ = nullptr;
        }
        released_.notify_all();
        return true;
    }

    // Drops every level held by `owner` (its CPU is being destroyed).
    void release_all(const void *owner) {
        {
            const std::lock_guard lock(mutex_);
            if (owner_ != owner)
                return;
            owner_ = nullptr;
            depth_ = 0;
        }
        released_.notify_all();
    }

    bool held() {
        const std::lock_guard lock(mutex_);
        return owner_ != nullptr;
    }

private:
    std::mutex mutex_;
    std::condition_variable released_;
    const void *owner_ = nullptr;
    uint32_t depth_ = 0;
};
