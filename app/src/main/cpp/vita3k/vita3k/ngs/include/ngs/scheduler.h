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

#include <util/types.h>

#include <mem/ptr.h>

#include <condition_variable>
#include <queue>
#include <utility>
#include <vector>

struct MemState;
struct KernelState;

struct SceNgsPatchSetupInfo;

namespace ngs {
struct Voice;
struct Patch;
struct Rack;
struct System;
struct State;

enum class RackReleaseAction : std::uint8_t {
    Immediate,
    ReleaseFinishedCallback,
    ReleaseModuleCallback,
    Wait,
    RejectReentrant,
    RejectAlreadyPending,
    DeferCallback,
};

enum class PendingType {
    ReleaseRack
    // TODO add voice operations during system lock
};

enum class UpdateCallbackKind : std::uint8_t {
    None,
    ModuleProcess,
    VoiceFinished,
    RackReleaseCompletion,
};

inline const char *update_callback_kind_name(UpdateCallbackKind kind) {
    switch (kind) {
    case UpdateCallbackKind::ModuleProcess: return "module-process";
    case UpdateCallbackKind::VoiceFinished: return "voice-finished";
    case UpdateCallbackKind::RackReleaseCompletion: return "rack-release-completion";
    default: return "none";
    }
}

struct OperationPending {
    PendingType type;
    System *system;
    union {
        // ReleaseRack
        struct {
            State *state;
            Rack *rack;
            // can't use a ptr, otherwise the default constructor is deleted
            Address callback;
        } release_data;
    };
};

struct VoiceScheduler {
    std::vector<Voice *> queue;
    std::queue<OperationPending> operations_pending;

    std::recursive_mutex mutex;
    std::condition_variable_any condvar;
    bool is_updating = false;
    SceUID update_thread_id = 0;
    UpdateCallbackKind callback_kind = UpdateCallbackKind::None;
    bool abort_update_after_callback = false;

    struct CallbackContextScope {
        VoiceScheduler &scheduler;
        UpdateCallbackKind previous;

        CallbackContextScope(VoiceScheduler &scheduler, UpdateCallbackKind kind)
            : scheduler(scheduler)
            , previous(scheduler.callback_kind) {
            scheduler.callback_kind = kind;
        }

        ~CallbackContextScope() {
            scheduler.callback_kind = previous;
        }
    };

    // Caller must hold mutex. A release from this thread is re-entering the
    // active update through a guest callback and cannot wait for itself.
    bool is_update_thread(SceUID thread_id) const {
        return is_updating && update_thread_id == thread_id;
    }

    RackReleaseAction rack_release_action(SceUID thread_id, bool has_callback) const {
        return rack_release_action(thread_id, has_callback, false);
    }

    RackReleaseAction rack_release_action(SceUID thread_id, bool has_callback, bool release_already_pending) const {
        if (release_already_pending)
            return RackReleaseAction::RejectAlreadyPending;
        if (!is_updating)
            return RackReleaseAction::Immediate;
        if (has_callback)
            return RackReleaseAction::DeferCallback;
        if (is_update_thread(thread_id) && callback_kind == UpdateCallbackKind::VoiceFinished)
            return RackReleaseAction::ReleaseFinishedCallback;
        if (is_update_thread(thread_id) && callback_kind == UpdateCallbackKind::ModuleProcess)
            return RackReleaseAction::ReleaseModuleCallback;
        // Completion callbacks run after their pending operation has been
        // removed. A guest may reuse the released buffer there and immediately
        // release the new Rack; no update queue entry still refers to the old one.
        if (is_update_thread(thread_id) && callback_kind == UpdateCallbackKind::RackReleaseCompletion)
            return RackReleaseAction::Immediate;
        if (is_update_thread(thread_id))
            return RackReleaseAction::RejectReentrant;
        return RackReleaseAction::Wait;
    }

    bool has_pending_rack_release(const Rack *rack) const {
        auto pending = operations_pending;
        while (!pending.empty()) {
            const OperationPending &operation = pending.front();
            if (operation.type == PendingType::ReleaseRack && operation.release_data.rack == rack)
                return true;
            pending.pop();
        }
        return false;
    }

    bool consume_update_abort_after_callback() {
        return std::exchange(abort_update_after_callback, false);
    }

    void request_update_abort_after_callback() {
        abort_update_after_callback = true;
    }

    void wait_for_update_to_finish(std::unique_lock<std::recursive_mutex> &lock) {
        condvar.wait(lock, [this] { return !is_updating; });
    }

protected:
    void deque_insert(const MemState &mem, Voice *voice);

    bool resort_to_respect_dependencies(const MemState &mem, Voice *source);

    std::int32_t get_position(Voice *v);

public:
    bool deque_voice(Voice *voice);

    /// Put a voice back into the processing queue after a save-state load,
    /// without re-keying it (its module state was restored).
    bool requeue_voice(const MemState &mem, Voice *voice);

    bool play(const MemState &mem, Voice *voice);
    bool pause(const MemState &mem, Voice *voice);
    bool resume(const MemState &mem, Voice *voice);
    bool stop(const MemState &mem, Voice *voice);
    bool off(const MemState &mem, Voice *voice);

    void update(KernelState &kern, const MemState &mem, const SceUID thread_id);

    Ptr<Patch> patch(const MemState &mem, SceNgsPatchSetupInfo *info);
};
} // namespace ngs
