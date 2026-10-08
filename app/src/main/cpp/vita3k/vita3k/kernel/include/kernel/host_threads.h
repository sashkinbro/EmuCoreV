// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <utility>

// Host lifetime is longer than guest registry membership: joining also waits
// for C++ thread-local destructors. Join callbacks never run under our mutex.
class HostThreadRegistry {
public:
    using Token = uint64_t;
    Token next_token() { return next.fetch_add(1, std::memory_order_relaxed); }

    void add(Token token, std::function<void()> join) {
        std::lock_guard lock(mutex);
        const bool inserted = entries.emplace(token, Entry{ std::move(join) }).second;
        assert(inserted);
    }

    void finished(Token token) {
        std::lock_guard lock(mutex);
        if (const auto it = entries.find(token); it != entries.end())
            it->second.finished = true;
        changed.notify_all();
    }

    void join_finished() { drain(false); }
    void join_all() { drain(true); }

private:
    struct Entry {
        std::function<void()> join;
        bool finished = false;
        bool joining = false;
    };
    std::mutex mutex;
    std::condition_variable changed;
    std::map<Token, Entry> entries;
    std::atomic<Token> next{ 1 };

    void drain(bool all) {
        std::unique_lock lock(mutex);
        while (true) {
            auto next = entries.begin();
            for (; next != entries.end(); ++next) {
                if (!next->second.joining && (all || next->second.finished))
                    break;
            }
            if (next == entries.end()) {
                if (!all || entries.empty())
                    return;
                // Another caller owns a join; completion of that join is still
                // part of our barrier, so wait instead of returning early.
                changed.wait(lock);
                continue;
            }
            const Token token = next->first;
            next->second.joining = true;
            auto join = next->second.join;
            lock.unlock();
            join();
            lock.lock();
            entries.erase(token);
            changed.notify_all();
        }
    }
};
