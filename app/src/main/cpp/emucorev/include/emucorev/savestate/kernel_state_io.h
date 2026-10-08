// Pure kernel save-state decoding. Both preflight and restore use these
// parsers.
#pragma once
#include <emucorev/savestate/state_io.h>
#include <functional>
#include <kernel/state.h>
#include <kernel/sync_primitives.h>
#include <kernel/thread/thread_state.h>
#include <limits>
#include <map>
#include <set>
#include <type_traits>

namespace emucorev::savestate {
inline std::map<SceUID, ThreadStatePtr> collect_retained_lock_owners(KernelState &kernel) {
    std::map<SceUID, ThreadStatePtr> retained_owners;
    const auto retain_owner = [&](const auto &object) {
        using Object = typename std::decay_t<decltype(object)>::element_type;
        if constexpr (std::is_same_v<Object, Mutex>) {
            if (object->owner)
                retained_owners.emplace(object->owner->id, object->owner);
        } else if constexpr (std::is_same_v<Object, RWLock>) {
            for (const auto &[owner, _] : object->owners)
                if (owner)
                    retained_owners.emplace(owner->id, owner);
        } else if constexpr (std::is_same_v<Object, Condvar>) {
            if (object->associated_mutex && object->associated_mutex->owner)
                retained_owners.emplace(object->associated_mutex->owner->id, object->associated_mutex->owner);
        }
    };
    for (const auto &[_, object] : kernel.mutexes)
        retain_owner(object);
    for (const auto &[_, object] : kernel.lwmutexes)
        retain_owner(object);
    for (const auto &[_, object] : kernel.rwlocks)
        retain_owner(object);
    for (const auto &[_, object] : kernel.condvars)
        retain_owner(object);
    for (const auto &[_, object] : kernel.lwcondvars)
        retain_owner(object);
    for (const auto &[_, thread] : kernel.threads) {
        for (const auto &object : thread->saved_sync_objects()) {
            if (auto mutex = std::dynamic_pointer_cast<Mutex>(object))
                retain_owner(mutex);
            if (auto rwlock = std::dynamic_pointer_cast<RWLock>(object))
                retain_owner(rwlock);
            if (auto cond = std::dynamic_pointer_cast<Condvar>(object))
                retain_owner(cond);
        }
    }
    return retained_owners;
}

// Call only after host producers and guests have reached their capture boundary.
// The specialized GXM worker is recreated separately and cannot inherit ownership
// of a lock through a replacement guest UID.
inline bool validate_excluded_thread_lock_ownership(KernelState &kernel, SceUID excluded_thread, std::string &error) {
    if (excluded_thread > 0 && collect_retained_lock_owners(kernel).contains(excluded_thread)) {
        error = "GXM display callback retains a guest lock across its capture boundary";
        return false;
    }
    return true;
}

inline bool capture_thread_snapshots(KernelState &kernel, SceUID excluded_thread,
    std::vector<ThreadState::Snapshot> &snapshots, std::string &error) {
    snapshots.clear();
    auto retained_owners = collect_retained_lock_owners(kernel);
    if (excluded_thread > 0 && retained_owners.contains(excluded_thread)) {
        error = "GXM display callback retains a guest lock across its capture boundary";
        return false;
    }
    const auto capture = [&](const ThreadStatePtr &thread, bool registered) {
        const auto id = thread->id;
        auto snapshot = thread->capture_snapshot();
        // ThreadState normalizes its transient session pause without clearing
        // the independent guest debugger and VM suspension reasons.
        snapshot.registered = registered;
        for (const auto &wait : snapshot.waits) {
            if ((wait.operation == WaitOperation::display_vblank || wait.operation == WaitOperation::audio_output
                    || wait.operation == WaitOperation::gxm_display_queue || wait.operation == WaitOperation::renderer_finish)
                && !kernel.wait_resume_handlers.contains(wait.operation)) {
                error = "module wait has no durable continuation handler (operation "
                    + std::to_string(static_cast<unsigned>(wait.operation)) + ", thread "
                    + std::to_string(id) + ": " + snapshot.name + ")";
                return false;
            }
            if (wait.operation == WaitOperation::callback_dispatch && !restartable_callback_import(wait.args[0])) {
                error = "callback dispatch caller has no durable completion handler";
                return false;
            }
            if (wait.operation == WaitOperation::creation_import && !restartable_creation_import(wait.args[0])) {
                error = "thread creation caller has no durable completion handler (thread " + std::to_string(id) + ")";
                return false;
            }
        }
        for (const auto &frame : snapshot.callback_frames) {
            if (!kernel.can_restore_callback_frame(frame)) {
                error = "callback caller has no durable completion handler (thread " + std::to_string(id) + ")";
                return false;
            }
        }
        snapshots.push_back(std::move(snapshot));
        return true;
    };
    for (const auto &[id, thread] : kernel.threads) {
        if (thread && id != excluded_thread && !capture(thread, true))
            return false;
        retained_owners.erase(id);
    }
    for (const auto &[_, thread] : retained_owners)
        if (!capture(thread, false))
            return false;
    return true;
}

inline void write_wait_continuation(BufferWriter &buffer,
    const WaitContinuationSnapshot &wait) {
    buffer.u64(wait.frame_sequence);
    buffer.u8(static_cast<uint8_t>(wait.operation));
    buffer.u8(static_cast<uint8_t>(wait.phase));
    for (auto arg : wait.args)
        buffer.u32(arg);
    buffer.u32(wait.timeout_address);
    buffer.boolean(wait.callbacks);
    buffer.boolean(wait.infinite);
    buffer.u64(wait.remaining_us);
    buffer.value(wait.context);
    buffer.u32(wait.target.type);
    buffer.i32(wait.target.id);
    buffer.i32(wait.priority);
    buffer.u64(wait.sequence);
    buffer.boolean(wait.queued);
    buffer.boolean(wait.has_result);
    buffer.i32(wait.result);
    buffer.i32(wait.condition_result);
}

inline void
write_thread_snapshots(BufferWriter &buffer,
    const std::vector<ThreadState::Snapshot> &snapshots) {
    buffer.u32(2); // Includes retained, unregistered lock-owner threads.
    buffer.u32(static_cast<uint32_t>(snapshots.size()));
    for (const auto &snapshot : snapshots) {
        buffer.i32(snapshot.id);
        buffer.boolean(snapshot.registered);
        buffer.str(snapshot.name);
        buffer.u32(snapshot.entry_point);
        buffer.u32(snapshot.stack_addr);
        buffer.i32(snapshot.stack_size);
        buffer.u32(snapshot.tls_addr);
        buffer.i32(snapshot.priority);
        buffer.i32(snapshot.affinity_mask);
        buffer.u64(snapshot.start_tick);
        buffer.u64(snapshot.last_vblank_waited);
        buffer.u32(static_cast<uint32_t>(snapshot.status));
        buffer.u32(snapshot.returned_value);
        buffer.value(snapshot.context);
        buffer.value(snapshot.init_context);
        buffer.boolean(snapshot.signal_pending);
        buffer.boolean(snapshot.wake_pending);
        buffer.boolean(snapshot.callbacks_pending);
        buffer.boolean(snapshot.debugger_suspended);
        buffer.i32(snapshot.callback_cursor);
        buffer.boolean(snapshot.exit_requested);
        buffer.boolean(snapshot.delete_requested);
        buffer.boolean(snapshot.vm_suspended);
        buffer.boolean(snapshot.single_stepping);
        buffer.boolean(snapshot.run_start_callback);
        buffer.boolean(snapshot.run_end_callback);
        buffer.boolean(snapshot.is_processing_callbacks);
        buffer.list<SceUID>(snapshot.callback_uids,
            [&buffer](SceUID uid) { buffer.i32(uid); });
        buffer.list<WaitContinuationSnapshot>(
            snapshot.waits,
            [&buffer](const auto &wait) { write_wait_continuation(buffer, wait); });
        buffer.list<CallbackContinuationSnapshot>(
            snapshot.callback_frames, [&buffer](const auto &frame) {
                buffer.u64(frame.frame_sequence);
                buffer.value(frame.previous_context);
                buffer.u32(frame.previous_tpidruro);
                buffer.u8(static_cast<uint8_t>(frame.purpose));
                buffer.i32(frame.callback_uid);
                buffer.boolean(frame.guest_returned);
                buffer.u32(frame.result);
                buffer.u32(frame.external_tag);
                for (auto value : frame.completion)
                    buffer.u32(value);
            });
    }
}

inline WaitContinuationSnapshot read_wait_continuation(BufferReader &reader) {
    WaitContinuationSnapshot wait;
    wait.frame_sequence = reader.u64();
    wait.operation = static_cast<WaitOperation>(reader.u8());
    wait.phase = static_cast<WaitPhase>(reader.u8());
    for (auto &arg : wait.args)
        arg = reader.u32();
    wait.timeout_address = reader.u32();
    wait.callbacks = reader.boolean();
    wait.infinite = reader.boolean();
    wait.remaining_us = reader.u64();
    wait.context = reader.value<CPUContext>();
    wait.target.type = reader.u32();
    wait.target.id = reader.i32();
    wait.priority = reader.i32();
    wait.sequence = reader.u64();
    wait.queued = reader.boolean();
    wait.has_result = reader.boolean();
    wait.result = reader.i32();
    wait.condition_result = reader.i32();
    return wait;
}

inline bool parse_thread_snapshots(const std::vector<uint8_t> &data,
    std::vector<ThreadState::Snapshot> &deferred,
    std::string &error) {
    BufferReader reader(data.data(), data.size());
    if (reader.u32() != 2) {
        error = "unsupported kernel continuation schema";
        return false;
    }
    const uint32_t count = reader.u32();
    if (!reader.ok() || count > 65536 || count > reader.remaining() / 600) {
        error = "invalid thread table";
        return false;
    }
    deferred.clear();
    for (uint32_t i = 0; i < count && reader.ok(); ++i) {
        ThreadState::Snapshot snapshot;
        snapshot.id = reader.i32();
        snapshot.registered = reader.boolean();
        snapshot.name = reader.str();
        snapshot.entry_point = reader.u32();
        snapshot.stack_addr = reader.u32();
        snapshot.stack_size = reader.i32();
        snapshot.tls_addr = reader.u32();
        snapshot.priority = reader.i32();
        snapshot.affinity_mask = reader.i32();
        snapshot.start_tick = reader.u64();
        snapshot.last_vblank_waited = reader.u64();
        snapshot.status = static_cast<ThreadStatus>(reader.u32());
        snapshot.returned_value = reader.u32();
        snapshot.context = reader.value<CPUContext>();
        snapshot.init_context = reader.value<CPUContext>();
        snapshot.signal_pending = reader.boolean();
        snapshot.wake_pending = reader.boolean();
        snapshot.callbacks_pending = reader.boolean();
        snapshot.debugger_suspended = reader.boolean();
        snapshot.callback_cursor = reader.i32();
        snapshot.exit_requested = reader.boolean();
        snapshot.delete_requested = reader.boolean();
        snapshot.vm_suspended = reader.boolean();
        snapshot.single_stepping = reader.boolean();
        snapshot.run_start_callback = reader.boolean();
        snapshot.run_end_callback = reader.boolean();
        snapshot.is_processing_callbacks = reader.boolean();
        snapshot.callback_uids = reader.list<SceUID>([&reader] { return reader.i32(); });
        snapshot.waits = reader.list<WaitContinuationSnapshot>(
            [&reader] { return read_wait_continuation(reader); });
        snapshot.callback_frames = reader.list<CallbackContinuationSnapshot>([&reader] {
            CallbackContinuationSnapshot frame;
            frame.frame_sequence = reader.u64();
            frame.previous_context = reader.value<CPUContext>();
            frame.previous_tpidruro = reader.u32();
            frame.purpose = static_cast<CallbackPurpose>(reader.u8());
            frame.callback_uid = reader.i32();
            frame.guest_returned = reader.boolean();
            frame.result = reader.u32();
            frame.external_tag = reader.u32();
            for (auto &value : frame.completion)
                value = reader.u32();
            return frame;
        });
        if (snapshot.waits.size() > 64 || snapshot.callback_frames.size() > 64) {
            error = "invalid continuation depth";
            return false;
        }
        for (const auto &wait : snapshot.waits) {
            if (wait.operation == WaitOperation::none || wait.operation > WaitOperation::renderer_finish || wait.phase > WaitPhase::complete || wait.remaining_us > std::numeric_limits<SceUInt32>::max()) {
                error = "invalid wait continuation";
                return false;
            }
        }
        deferred.push_back(std::move(snapshot));
    }
    if (!reader.ok() || reader.remaining()) {
        error = "invalid thread table";
        return false;
    }
    return true;
}

inline bool
parse_saved_callbacks(BufferReader &reader,
    std::vector<KernelState::SavedCallback> &callbacks,
    std::string &error) {
    callbacks.clear();
    const auto count = reader.u32();
    if (count > 65536) {
        error = "invalid callback count";
        return false;
    }
    std::set<SceUID> ids;
    for (uint32_t i = 0; i < count && reader.ok(); ++i) {
        KernelState::SavedCallback saved;
        saved.uid = reader.i32();
        if (saved.uid <= 0 || !ids.insert(saved.uid).second) {
            error = "duplicate or invalid callback UID";
            return false;
        }
        if (!reader.boolean())
            continue;
        saved.owner = reader.i32();
        saved.name = reader.str();
        saved.function = reader.u32();
        saved.userdata = reader.u32();
        saved.notification.num_notifications = reader.u32();
        saved.notification.notification_arg = reader.i32();
        saved.notification.notifier_id = reader.i32();
        if (saved.owner <= 0 || saved.name.size() > 4096) {
            error = "invalid callback record";
            return false;
        }
        callbacks.push_back(std::move(saved));
    }
    if (!reader.ok()) {
        error = "invalid callback table";
        return false;
    }
    return true;
}

enum class SyncSnapshotKind : uint8_t {
    event,
    timer,
    semaphore,
    mutex,
    lw_mutex,
    cond,
    lw_cond,
    rwlock,
    event_flag,
    msgpipe
};
struct SyncObjectHeader {
    SceUID uid = 0;
    bool registered = false;
    SceUID object_uid = 0;
    SceUInt32 attr = 0;
    bool deleted = false;
    char name[KERNELOBJECT_MAX_NAME_LENGTH + 1]{};
};
struct ParsedSyncObject {
    SyncObjectHeader header;
    SyncSnapshotKind kind;
    // Family-specific scalar payload, in the same order as its wire fields.
    std::array<uint64_t, 8> values{};
    std::vector<std::pair<SceUID, int>> owners;
    std::vector<char> bytes;
};
struct ParsedKernelSync {
    uint64_t saved_time = 0;
    std::vector<ParsedSyncObject> objects;
};
inline SyncObjectHeader read_sync_header(BufferReader &reader) {
    SyncObjectHeader header;
    header.uid = reader.i32();
    header.registered = reader.boolean();
    header.object_uid = reader.i32();
    header.attr = reader.u32();
    header.deleted = reader.boolean();
    reader.bytes(header.name, sizeof(header.name));
    return header;
}
inline bool parse_kernel_sync(const std::vector<uint8_t> &data,
    ParsedKernelSync &parsed, std::string &error) {
    BufferReader reader(data.data(), data.size());
    parsed = {};
    if (reader.u32() != 2) {
        error = "unsupported kernel synchronization schema";
        return false;
    }
    parsed.saved_time = reader.u64();
    std::set<SceUID> ids;
    for (unsigned family = 0;
        family <= static_cast<unsigned>(SyncSnapshotKind::msgpipe); ++family) {
        const auto count = reader.u32();
        if (!reader.ok() || count > 65536 || count > reader.remaining() / 47) {
            error = "invalid synchronization object count";
            return false;
        }
        for (uint32_t i = 0; i < count && reader.ok(); ++i) {
            ParsedSyncObject object;
            object.kind = static_cast<SyncSnapshotKind>(family);
            object.header = read_sync_header(reader);
            const auto &header = object.header;
            if (header.uid <= 0 || header.uid != header.object_uid || !ids.insert(header.uid).second || header.name[KERNELOBJECT_MAX_NAME_LENGTH] != 0 || (header.registered && header.deleted) || (!header.registered && !header.deleted)) {
                error = "invalid synchronization object identity";
                return false;
            }
            auto &v = object.values;
            switch (object.kind) {
            case SyncSnapshotKind::event:
                v[0] = reader.u32();
                v[1] = reader.u64();
                v[2] = reader.boolean();
                v[3] = reader.boolean();
                break;
            case SyncSnapshotKind::timer:
                for (int j = 0; j < 4; ++j)
                    v[j] = reader.boolean();
                for (int j = 4; j < 8; ++j)
                    v[j] = reader.u64();
                break;
            case SyncSnapshotKind::semaphore:
                for (int j = 0; j < 3; ++j)
                    v[j] = static_cast<uint32_t>(reader.i32());
                if (static_cast<int32_t>(v[0]) <= 0 || static_cast<int32_t>(v[1]) < 0 || v[1] > v[0] || static_cast<int32_t>(v[2]) < 0 || v[2] > v[0]) {
                    error = "invalid semaphore counts";
                    return false;
                }
                break;
            case SyncSnapshotKind::mutex:
            case SyncSnapshotKind::lw_mutex:
                for (int j = 0; j < 3; ++j)
                    v[j] = static_cast<uint32_t>(reader.i32());
                v[3] = reader.u32();
                if (static_cast<int32_t>(v[0]) < 0 || static_cast<int32_t>(v[1]) < 0 || ((v[1] == 0) != (v[2] == 0)) || (object.kind == SyncSnapshotKind::mutex && v[3] != 0) || (object.kind == SyncSnapshotKind::lw_mutex && v[3] == 0)) {
                    error = "invalid mutex state";
                    return false;
                }
                break;
            case SyncSnapshotKind::cond:
            case SyncSnapshotKind::lw_cond:
                v[0] = static_cast<uint32_t>(reader.i32());
                break;
            case SyncSnapshotKind::rwlock: {
                v[0] = reader.u32();
                const auto owners = reader.u32();
                if (v[0] > static_cast<uint32_t>(RWLockState::WriteLocked) || owners > 65536 || owners > reader.remaining() / 8 || (v[0] == 0) != (owners == 0) || (v[0] == 2 && owners != 1)) {
                    error = "invalid RW lock owners";
                    return false;
                }
                std::set<SceUID> owner_ids;
                for (uint32_t j = 0; j < owners && reader.ok(); ++j) {
                    const auto uid = reader.i32();
                    const auto depth = reader.i32();
                    if (uid <= 0 || depth <= 0 || !owner_ids.insert(uid).second) {
                        error = "invalid RW lock owner";
                        return false;
                    }
                    object.owners.emplace_back(uid, depth);
                }
                break;
            }
            case SyncSnapshotKind::event_flag:
                v[0] = static_cast<uint32_t>(reader.i32());
                break;
            case SyncSnapshotKind::msgpipe:
                v[0] = reader.u64();
                v[1] = reader.u64();
                if (v[0] > (16u << 20) || v[1] > v[0] || v[1] > reader.remaining()) {
                    error = "invalid message pipe size";
                    return false;
                }
                object.bytes.resize(static_cast<size_t>(v[1]));
                reader.bytes(object.bytes.data(), object.bytes.size());
                break;
            }
            parsed.objects.push_back(std::move(object));
        }
    }
    if (!reader.ok() || reader.remaining()) {
        error = "invalid kernel synchronization state";
        return false;
    }
    return true;
}

// The caller supplies coverage against the immutable, decoded saved RAM image.
// No live MemState, registry, thread or object is touched by validation.
using SavedGuestRange = std::function<bool(Address, size_t)>;
using ExternalCallbackTag = std::function<bool(uint32_t)>;
inline bool validate_kernel_continuations(
    const std::vector<ThreadState::Snapshot> &threads,
    const std::vector<KernelState::SavedCallback> &callbacks,
    const ParsedKernelSync &sync, const SavedGuestRange &valid_range,
    const ExternalCallbackTag &external_tag, std::string &error) {
    const auto fail = [&](const char *message) {
        error = message;
        return false;
    };
    const auto optional_range = [&](Address address, size_t size) {
        return address == 0 || valid_range(address, size);
    };
    const auto context_valid = [&](const CPUContext &context) {
        const auto sp = context.get_sp();
        return optional_range(context.get_pc() & ~Address{ 1 },
                   context.thumb() ? 2 : 4)
            && (sp == 0 || valid_range(sp - 1, 1)) && optional_range(context.tpidruro, 4);
    };
    std::map<SceUID, const ThreadState::Snapshot *> thread_ids;
    std::map<SceUID, const ParsedSyncObject *> object_ids;
    std::map<SceUID, const KernelState::SavedCallback *> callback_ids;
    std::set<SceUID> all_ids;
    std::vector<std::pair<uint64_t, uint64_t>> owned_ranges;
    for (const auto &thread : threads) {
        if (thread.id <= 0 || !all_ids.insert(thread.id).second || (thread.registered && thread.delete_requested) || thread.name.size() > 4096 || thread.stack_size <= 0 || !valid_range(thread.stack_addr, static_cast<size_t>(thread.stack_size)) || !valid_range(thread.tls_addr, 0x800) || (thread.registered && (!optional_range(thread.entry_point & ~Address{ 1 }, 2) || !context_valid(thread.context) || !context_valid(thread.init_context))))
            return fail("invalid thread identity or guest context");
        if (!thread.registered && (!thread.waits.empty() || !thread.callback_frames.empty() || thread.is_processing_callbacks))
            return fail("retained owner has an executable continuation");
        switch (thread.status) {
        case ThreadStatus::running:
        case ThreadStatus::waiting:
        case ThreadStatus::dormant:
        case ThreadStatus::suspended:
            break;
        default:
            return fail("invalid saved thread status");
        }
        const auto own_range = [&](Address start, uint64_t size) {
            const uint64_t end = uint64_t(start) + size;
            for (const auto &[other_start, other_end] : owned_ranges)
                if (start < other_end && other_start < end)
                    return false;
            owned_ranges.emplace_back(start, end);
            return true;
        };
        if (!own_range(thread.stack_addr, static_cast<uint32_t>(thread.stack_size)) || !own_range(thread.tls_addr, 0x800))
            return fail("overlapping thread stack or TLS ownership");
        thread_ids.emplace(thread.id, &thread);
    }
    for (const auto &object : sync.objects) {
        if (!all_ids.insert(object.header.uid).second)
            return fail("duplicate kernel UID across object families");
        object_ids.emplace(object.header.uid, &object);
    }
    for (const auto &callback : callbacks) {
        if (!all_ids.insert(callback.uid).second || !optional_range(callback.function & ~Address{ 1 }, 2))
            return fail("invalid callback identity or function");
        callback_ids.emplace(callback.uid, &callback);
    }
    std::set<SceUID> owner_references;
    for (const auto &object : sync.objects) {
        const auto &v = object.values;
        switch (object.kind) {
        case SyncSnapshotKind::mutex:
        case SyncSnapshotKind::lw_mutex:
            if (v[2])
                owner_references.insert(static_cast<SceUID>(v[2]));
            if (v[2] && !thread_ids.contains(static_cast<SceUID>(v[2])))
                return fail("missing mutex owner");
            if (v[3] && !valid_range(static_cast<Address>(v[3]), sizeof(SceKernelLwMutexWork)))
                return fail("invalid LW mutex work area");
            break;
        case SyncSnapshotKind::cond:
        case SyncSnapshotKind::lw_cond: {
            const auto it = object_ids.find(static_cast<SceUID>(v[0]));
            const auto expected = object.kind == SyncSnapshotKind::cond
                ? SyncSnapshotKind::mutex
                : SyncSnapshotKind::lw_mutex;
            if (it == object_ids.end() || it->second->kind != expected)
                return fail("missing or mismatched condition mutex");
            break;
        }
        case SyncSnapshotKind::rwlock:
            for (const auto &[uid, _] : object.owners) {
                owner_references.insert(uid);
                if (!thread_ids.contains(uid))
                    return fail("missing RW lock owner");
            }
            break;
        default:
            break;
        }
    }
    for (const auto &thread : threads)
        if (!thread.registered && !owner_references.contains(thread.id))
            return fail("retained thread has no lock owner reference");
    std::set<uint64_t> queue_sequences;
    for (const auto &thread : threads) {
        std::set<SceUID> callback_refs;
        for (const auto uid : thread.callback_uids) {
            // Deleted callbacks deliberately retain their cursor slot until dispatch
            // advances.
            if (uid <= 0 || !callback_refs.insert(uid).second)
                return fail("invalid thread callback list");
            if (const auto it = callback_ids.find(uid);
                it != callback_ids.end() && it->second->owner != thread.id)
                return fail("callback owner mismatch");
        }
        if (thread.callback_cursor && !callback_refs.contains(thread.callback_cursor))
            return fail("missing callback cursor slot");
        if (thread.status == ThreadStatus::waiting && thread.waits.empty())
            return fail("waiting thread has no durable continuation");
        std::set<uint64_t> frames;
        uint64_t last = 0;
        for (const auto &wait : thread.waits) {
            if (wait.frame_sequence == 0 || wait.frame_sequence <= last || wait.frame_sequence == std::numeric_limits<uint64_t>::max() || !frames.insert(wait.frame_sequence).second || !context_valid(wait.context) || !optional_range(wait.timeout_address, sizeof(SceUInt32)) || wait.phase == WaitPhase::complete)
                return fail("invalid wait frame or timeout address");
            last = wait.frame_sequence;
            const bool cond = wait.operation == WaitOperation::cond || wait.operation == WaitOperation::lw_cond;
            if (wait.phase == WaitPhase::reacquire_mutex && (!cond || !wait.infinite || (wait.condition_result != SCE_KERNEL_OK && wait.condition_result != SCE_KERNEL_ERROR_WAIT_TIMEOUT)))
                return fail("invalid condition reacquisition phase");
            const auto &a = wait.args;
            const SceUID uid = static_cast<SceUID>(a[0]);
            SceUInt32 target_type = 0;
            SyncSnapshotKind kind = SyncSnapshotKind::event;
            bool object_wait = true;
            switch (wait.operation) {
            case WaitOperation::delay:
                target_type = SCE_KERNEL_WAITTYPE_DELAY;
                object_wait = false;
                break;
            case WaitOperation::signal:
                target_type = SCE_KERNEL_WAITTYPE_SIGNAL;
                object_wait = false;
                if (!wait.infinite)
                    return fail("timed signal continuation");
                break;
            case WaitOperation::display_vblank:
                object_wait = false;
                target_type = SCE_KERNEL_WAITTYPE_EVENT;
                if (!wait.infinite || wait.timeout_address)
                    return fail("invalid vblank deadline");
                break;
            case WaitOperation::audio_output:
                object_wait = false;
                target_type = SCE_KERNEL_WAITTYPE_EVENT;
                if (static_cast<int32_t>(a[0]) < 0 || a[2] > 2 || a[3] == 0 || a[4] == 0 || a[5] == 0 || a[7] != 1
                    || wait.timeout_address || (a[2] == 0 && !optional_range(a[1], a[4])))
                    return fail("invalid audio output continuation");
                break;
            case WaitOperation::gxm_display_queue:
                object_wait = false;
                target_type = SCE_KERNEL_WAITTYPE_EVENT;
                if (uid != thread.id || a[1] > 1 || a[2] > 1 || a[3] != 1
                    || a[4] || a[5] || a[6] || a[7] || (a[2] == 1 && a[1] != 1)
                    || !wait.infinite || wait.timeout_address || wait.callbacks
                    || wait.queued || wait.has_result || wait.sequence)
                    return fail("invalid GXM display queue continuation");
                break;
            case WaitOperation::renderer_finish:
                object_wait = false;
                target_type = SCE_KERNEL_WAITTYPE_EVENT;
                if ((!a[0] && !a[1]) || a[2] != 1 || a[3] || a[4] || a[5] || a[6] || a[7]
                    || !wait.infinite || wait.timeout_address || wait.callbacks || wait.queued || wait.has_result || wait.sequence
                    || wait.phase != WaitPhase::queued)
                    return fail("invalid renderer Finish continuation");
                break;
            case WaitOperation::callback_dispatch:
                object_wait = false;
                if (!restartable_callback_import(a[0]) || !wait.infinite || wait.timeout_address)
                    return fail("invalid callback dispatch import");
                break;
            case WaitOperation::creation_import:
                object_wait = false;
                if (!restartable_creation_import(a[0]) || !wait.infinite)
                    return fail("unsupported parked creation caller");
                break;
            case WaitOperation::thread_end:
                target_type = SCE_KERNEL_WAITTYPE_WAITTHEND;
                object_wait = false;
                if (((!thread_ids.contains(uid) || !thread_ids.at(uid)->registered) && !(wait.has_result && !wait.queued)) || !optional_range(a[1], 4))
                    return fail("invalid thread-end continuation");
                break;
            case WaitOperation::event:
            case WaitOperation::timer:
                kind = wait.operation == WaitOperation::event ? SyncSnapshotKind::event
                                                              : SyncSnapshotKind::timer;
                target_type = SCE_KERNEL_WAITTYPE_EVENT;
                if (!optional_range(a[2], 4) || !optional_range(a[3], 8))
                    return fail("invalid event output address");
                break;
            case WaitOperation::mutex:
            case WaitOperation::lw_mutex:
                kind = wait.operation == WaitOperation::mutex
                    ? SyncSnapshotKind::mutex
                    : SyncSnapshotKind::lw_mutex;
                target_type = wait.operation == WaitOperation::mutex
                    ? SCE_KERNEL_WAITTYPE_MUTEX
                    : SCE_KERNEL_WAITTYPE_LW_MUTEX;
                if (static_cast<int32_t>(a[1]) <= 0)
                    return fail("invalid mutex wait count");
                break;
            case WaitOperation::rw_read:
            case WaitOperation::rw_write:
                kind = SyncSnapshotKind::rwlock;
                target_type = SCE_KERNEL_WAITTYPE_RW_LOCK;
                break;
            case WaitOperation::semaphore:
                kind = SyncSnapshotKind::semaphore;
                target_type = SCE_KERNEL_WAITTYPE_SEMAPHORE;
                if (static_cast<int32_t>(a[1]) <= 0)
                    return fail("invalid semaphore wait count");
                break;
            case WaitOperation::cond:
            case WaitOperation::lw_cond:
                kind = wait.operation == WaitOperation::cond
                    ? SyncSnapshotKind::cond
                    : SyncSnapshotKind::lw_cond;
                target_type = wait.operation == WaitOperation::cond
                    ? (wait.phase == WaitPhase::reacquire_mutex
                              ? SCE_KERNEL_WAITTYPE_COND_MUTEX
                              : SCE_KERNEL_WAITTYPE_COND_SIGNAL)
                    : (wait.phase == WaitPhase::reacquire_mutex
                              ? SCE_KERNEL_WAITTYPE_LW_COND_LW_MUTEX
                              : SCE_KERNEL_WAITTYPE_LW_COND_SIGNAL);
                break;
            case WaitOperation::event_flag:
                kind = SyncSnapshotKind::event_flag;
                target_type = SCE_KERNEL_WAITTYPE_EVENTFLAG;
                if (!optional_range(a[3], 4))
                    return fail("invalid event flag output address");
                break;
            case WaitOperation::pipe_send:
            case WaitOperation::pipe_recv:
                kind = SyncSnapshotKind::msgpipe;
                target_type = SCE_KERNEL_WAITTYPE_MSG_PIPE;
                if (a[3] == 0 || !valid_range(a[2], a[3]) || !optional_range(a[4], 4) || a[5] > 1)
                    return fail("invalid message pipe continuation buffer");
                break;
            default:
                return fail("unknown wait operation");
            }
            if ((wait.target.type & 0x7fffffffU) != target_type || wait.target.id != (wait.operation == WaitOperation::delay || wait.operation == WaitOperation::signal || wait.operation == WaitOperation::creation_import || wait.operation == WaitOperation::display_vblank || wait.operation == WaitOperation::callback_dispatch || wait.operation == WaitOperation::renderer_finish ? 0 : uid))
                return fail("wait target does not match continuation");
            if (wait.operation != WaitOperation::delay && wait.operation != WaitOperation::signal && wait.operation != WaitOperation::creation_import && wait.operation != WaitOperation::audio_output && wait.operation != WaitOperation::gxm_display_queue && wait.operation != WaitOperation::callback_dispatch && wait.operation != WaitOperation::renderer_finish) {
                if (wait.queued == wait.has_result || wait.sequence == 0 || wait.sequence == std::numeric_limits<uint64_t>::max() || !queue_sequences.insert(wait.sequence).second)
                    return fail("invalid saved queue result or ordering");
            } else if (wait.queued || wait.has_result)
                return fail("unexpected queue state on non-object wait");
            if (object_wait) {
                const auto it = object_ids.find(uid);
                if (it == object_ids.end() || it->second->kind != kind || (it->second->header.deleted && wait.queued))
                    return fail("missing or mismatched wait object");
                if (cond && it->second->values[0] != a[1])
                    return fail("condition continuation mutex mismatch");
            }
        }
        last = 0;
        for (const auto &frame : thread.callback_frames) {
            if (frame.frame_sequence == 0 || frame.frame_sequence <= last || frame.frame_sequence == std::numeric_limits<uint64_t>::max() || !frames.insert(frame.frame_sequence).second || !context_valid(frame.previous_context) || !optional_range(frame.previous_tpidruro, 4) || frame.purpose > CallbackPurpose::external || frame.purpose == CallbackPurpose::direct)
                return fail("invalid callback continuation frame");
            last = frame.frame_sequence;
            if (frame.purpose == CallbackPurpose::thread_end) {
                const auto status = static_cast<ThreadStatus>(frame.completion[0]);
                if (status != ThreadStatus::running && status != ThreadStatus::waiting && status != ThreadStatus::dormant && status != ThreadStatus::suspended)
                    return fail("invalid thread-end callback completion status");
            }
            if (frame.purpose == CallbackPurpose::notification && (!thread.is_processing_callbacks || frame.callback_uid <= 0))
                return fail("invalid notification callback continuation");
            if (frame.purpose == CallbackPurpose::external && (!frame.external_tag || !external_tag || !external_tag(frame.external_tag)))
                return fail("missing external callback continuation handler");
        }
    }
    return true;
}

} // namespace emucorev::savestate
