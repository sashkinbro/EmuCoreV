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

// Pure bookkeeping for the HLE async file I/O. Operations are executed synchronously when they are
// submitted; their result is parked here per file descriptor until the guest collects it with
// sceIoComplete. The sceIoCancel prototype in vita-headers ("Cancel an asynchronous operation on a file
// descriptor") confirms the per-fd model. No emulator dependencies so it can be unit tested on the host.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>

namespace async_io {

// PROVISIONAL: vita-headers has no sceIoComplete prototype, so the "no async result pending" error is not
// proven. EINVAL (0x80010016) is used.
constexpr int32_t ERR_NO_PENDING = static_cast<int32_t>(0x80010016);

// A well-behaved guest keeps at most one async op per fd outstanding; this only bounds fire-and-forget callers.
constexpr size_t MAX_PENDING_PER_FD = 256;

class AsyncOpTable {
public:
    // Parks the result of an operation that already ran. Oldest entries are dropped past the cap.
    void push(int32_t fd, int64_t result) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto &queue = ops_[fd];
        queue.push_back(result);
        if (queue.size() > MAX_PENDING_PER_FD)
            queue.pop_front();
    }

    // Collects (and removes) the oldest result of fd. Returns false when nothing is pending.
    bool pop(int32_t fd, int64_t &result) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = ops_.find(fd);
        if (it == ops_.end())
            return false;
        result = it->second.front();
        it->second.pop_front();
        if (it->second.empty())
            ops_.erase(it);
        return true;
    }

    // Reads the oldest result of fd without removing it.
    bool peek(int32_t fd, int64_t &result) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = ops_.find(fd);
        if (it == ops_.end())
            return false;
        result = it->second.front();
        return true;
    }

    size_t pending(int32_t fd) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = ops_.find(fd);
        return it == ops_.end() ? 0 : it->second.size();
    }

    // Drops everything parked for fd (the descriptor was closed synchronously).
    void discard(int32_t fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        ops_.erase(fd);
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        ops_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::map<int32_t, std::deque<int64_t>> ops_;
};

// sceIoSet/GetPriority family: the values are stored and returned, they do not influence scheduling.
// PROVISIONAL: the real default priority is not proven (not in vita-headers); 0 is used, and the
// "thread default" is a single process-wide value.
constexpr int32_t DEFAULT_PRIORITY = 0;

class PriorityTable {
public:
    int32_t get(int32_t fd) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = fd_priority_.find(fd);
        return it == fd_priority_.end() ? thread_default_ : it->second;
    }
    void set(int32_t fd, int32_t priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        fd_priority_[fd] = priority;
    }
    void forget(int32_t fd) {
        std::lock_guard<std::mutex> lock(mutex_);
        fd_priority_.erase(fd);
    }
    int32_t process_default() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return process_default_;
    }
    void set_process_default(int32_t priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        process_default_ = priority;
    }
    int32_t thread_default() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return thread_default_;
    }
    void set_thread_default(int32_t priority) {
        std::lock_guard<std::mutex> lock(mutex_);
        thread_default_ = priority;
    }

private:
    mutable std::mutex mutex_;
    std::map<int32_t, int32_t> fd_priority_;
    int32_t process_default_ = DEFAULT_PRIORITY;
    int32_t thread_default_ = DEFAULT_PRIORITY;
};

} // namespace async_io
