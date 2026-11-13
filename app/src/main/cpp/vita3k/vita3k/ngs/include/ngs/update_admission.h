// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <limits>
#include <mutex>
#include <utility>

namespace ngs {

// Prevents a save from pausing the guest while sceNgsSystemUpdate has a live
// C++ scheduler stack suspended in a guest callback. The save bridge closes
// admission and drains existing calls before it pauses the session.
class UpdateAdmission {
public:
    enum class WaitKind : std::size_t {
        RackRelease,
        SystemRelease,
    };

    enum class DrainStatus {
        drained,
        timed_out,
        already_closed,
    };

    class UpdateLease {
    public:
        UpdateLease() = default;
        UpdateLease(const UpdateLease &) = delete;
        UpdateLease &operator=(const UpdateLease &) = delete;

        UpdateLease(UpdateLease &&other) noexcept
            : admission_(std::exchange(other.admission_, nullptr)) {}

        UpdateLease &operator=(UpdateLease &&other) noexcept {
            if (this != &other) {
                release();
                admission_ = std::exchange(other.admission_, nullptr);
            }
            return *this;
        }

        ~UpdateLease() {
            release();
        }

        explicit operator bool() const {
            return admission_ != nullptr;
        }

    private:
        friend class UpdateAdmission;
        explicit UpdateLease(UpdateAdmission *admission)
            : admission_(admission) {}

        void release() {
            if (admission_) {
                admission_->leave_update();
                admission_ = nullptr;
            }
        }

        UpdateAdmission *admission_ = nullptr;
    };

    class SnapshotLease {
    public:
        SnapshotLease() = default;
        SnapshotLease(const SnapshotLease &) = delete;
        SnapshotLease &operator=(const SnapshotLease &) = delete;

        SnapshotLease(SnapshotLease &&other) noexcept
            : admission_(std::exchange(other.admission_, nullptr)) {}

        SnapshotLease &operator=(SnapshotLease &&other) noexcept {
            if (this != &other) {
                release();
                admission_ = std::exchange(other.admission_, nullptr);
            }
            return *this;
        }

        ~SnapshotLease() {
            release();
        }

        explicit operator bool() const {
            return admission_ != nullptr;
        }

    private:
        friend class UpdateAdmission;
        explicit SnapshotLease(UpdateAdmission *admission)
            : admission_(admission) {}

        void release() {
            if (admission_) {
                admission_->open_updates();
                admission_ = nullptr;
            }
        }

        UpdateAdmission *admission_ = nullptr;
    };

    class WaitLease {
    public:
        WaitLease() = default;
        WaitLease(const WaitLease &) = delete;
        WaitLease &operator=(const WaitLease &) = delete;

        WaitLease(WaitLease &&other) noexcept
            : admission_(std::exchange(other.admission_, nullptr)), kind_(other.kind_) {}

        WaitLease &operator=(WaitLease &&other) noexcept {
            if (this != &other) {
                release();
                admission_ = std::exchange(other.admission_, nullptr);
                kind_ = other.kind_;
            }
            return *this;
        }

        ~WaitLease() {
            release();
        }

    private:
        friend class UpdateAdmission;
        WaitLease(UpdateAdmission *admission, WaitKind kind)
            : admission_(admission), kind_(kind) {}

        void release() {
            if (admission_) {
                admission_->leave_wait(kind_);
                admission_ = nullptr;
            }
        }

        UpdateAdmission *admission_ = nullptr;
        WaitKind kind_ = WaitKind::RackRelease;
    };

    UpdateLease try_enter_update() {
        std::lock_guard lock(mutex_);
        if (closed_ || active_updates_ == std::numeric_limits<std::size_t>::max())
            return {};
        ++active_updates_;
        return UpdateLease(this);
    }

    // Closes admission first, then waits for every already-admitted call to
    // return. On timeout it reopens admission before returning. The caller
    // must keep `lease` alive across session pause and capture.
    DrainStatus close_and_drain(std::chrono::milliseconds timeout, SnapshotLease &lease) {
        if (lease)
            return DrainStatus::already_closed;

        std::unique_lock lock(mutex_);
        if (closed_)
            return DrainStatus::already_closed;
        closed_ = true;

        if (!drained_.wait_for(lock, timeout, [this] { return active_updates_ == 0; })) {
            closed_ = false;
            lock.unlock();
            drained_.notify_all();
            return DrainStatus::timed_out;
        }

        lease = SnapshotLease(this);
        return DrainStatus::drained;
    }

    std::size_t active_update_count() {
        const std::lock_guard lock(mutex_);
        return active_updates_;
    }

    WaitLease track_wait(WaitKind kind) {
        active_waits_[static_cast<std::size_t>(kind)].fetch_add(1, std::memory_order_relaxed);
        return WaitLease(this, kind);
    }

    std::size_t active_wait_count(WaitKind kind) const {
        return active_waits_[static_cast<std::size_t>(kind)].load(std::memory_order_relaxed);
    }

private:
    void leave_update() {
        std::lock_guard lock(mutex_);
        if (active_updates_ > 0 && --active_updates_ == 0)
            drained_.notify_all();
    }

    void open_updates() {
        {
            std::lock_guard lock(mutex_);
            closed_ = false;
        }
        drained_.notify_all();
    }

    void leave_wait(WaitKind kind) {
        active_waits_[static_cast<std::size_t>(kind)].fetch_sub(1, std::memory_order_relaxed);
    }

    std::mutex mutex_;
    std::condition_variable drained_;
    std::size_t active_updates_ = 0;
    bool closed_ = false;
    std::array<std::atomic<std::size_t>, 2> active_waits_{};
};

inline UpdateAdmission &system_update_admission() {
    static UpdateAdmission admission;
    return admission;
}

} // namespace ngs
