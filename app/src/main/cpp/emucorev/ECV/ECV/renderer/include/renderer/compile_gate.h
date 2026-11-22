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

// One-shot "compile once, everybody else waits" slot (no emulator dependencies, unit tested on the host).
// The first thread to acquire() an empty slot becomes its owner and must call finish() or fail().
// Every other thread blocks on a condition variable (no spinning) until the owner is done and then
// receives the result, a failure included, so a failed compilation can never leave waiters hanging.

#include <condition_variable>
#include <mutex>

namespace renderer {

template <typename T>
class CompileGate {
public:
    enum class Status {
        Owner, // the caller must compile and then call finish() or fail()
        Ready, // value is valid
        Failed // permanently failed, value is not valid
    };

    struct Acquired {
        Status status;
        T value;
    };

    Acquired acquire() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return state_ != State::Compiling; });
        switch (state_) {
        case State::Ready:
            return { Status::Ready, value_ };
        case State::Failed:
            return { Status::Failed, T() };
        default:
            state_ = State::Compiling;
            return { Status::Owner, T() };
        }
    }

    // The owner is done: publish the result and wake every waiter.
    void finish(const T &value) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            value_ = value;
            state_ = State::Ready;
        }
        cv_.notify_all();
    }

    // The owner failed. permanent: this and every later acquire() reports Failed.
    // Otherwise the slot becomes empty again: one of the woken waiters becomes the next owner and retries.
    void fail(bool permanent) {
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            state_ = permanent ? State::Failed : State::Empty;
            value_ = T();
        }
        cv_.notify_all();
    }

    // Non blocking: the value if the slot is ready.
    bool try_get(T &out) {
        const std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != State::Ready)
            return false;
        out = value_;
        return true;
    }

private:
    enum class State {
        Empty,
        Compiling,
        Ready,
        Failed
    };

    std::mutex mutex_;
    std::condition_variable cv_;
    State state_ = State::Empty;
    T value_ = T();
};

} // namespace renderer
