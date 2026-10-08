#pragma once
#include <emucorev/savestate/gxm_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/sysmem_state_io.h>
#include <kernel/thread/thread_state.h>
#include <algorithm>
#include <set>

namespace emucorev::savestate {
// GXM's immutable parser validates the fields and framebuffer spans. This
// cross-section pass verifies the guest waiter and allocation ownership before
// teardown; every callback-data allocation has exactly one destructor owner.
inline bool validate_gxm_display_continuations(const GxmSnapshot &gxm,
    const std::vector<ThreadState::Snapshot> &threads, const std::vector<MemorySpanInfo> &spans,
    std::string &error, const SysmemSnapshot *sysmem = nullptr) {
    const auto fail = [&](const char *message) { error = message; return false; };
    const ThreadState::Snapshot *display_thread = nullptr;
    for (const auto &thread : threads)
        if (thread.id == gxm.display_queue_thread) {
            if (display_thread || !thread.registered)
                return fail("display worker does not own a unique registered guest thread");
            display_thread = &thread;
        }
    const auto phase = static_cast<DisplayWorkerPhase>(gxm.display_worker_phase);
    if (gxm.display_worker_phase > uint32_t(DisplayWorkerPhase::Complete))
        return fail("invalid display worker phase");
    if (gxm.display_queue_thread == 0) {
        if (phase != DisplayWorkerPhase::Idle || gxm.display_previous_entry
            || !gxm.display_entries.empty() || !gxm.display_submissions.empty())
            return fail("inactive display worker owns unfinished work");
    } else {
        if (gxm.display_queue_thread < 0 || !display_thread)
            return fail("saved display worker guest thread is missing");
        const uint32_t capacity = std::max(std::min(gxm.params.displayQueueMaxPendingCount, 3U) - 1, 1U);
        if (gxm.display_entries.size() > capacity
            || (phase != DisplayWorkerPhase::Idle && gxm.display_entries.empty()))
            return fail("display worker phase disagrees with its queue front");
        if (phase <= DisplayWorkerPhase::StartCallback) {
            if (display_thread->status != ThreadStatus::dormant || gxm.display_previous_entry
                || !display_thread->waits.empty() || !display_thread->callback_frames.empty())
                return fail("unstarted display callback has a live guest continuation");
        } else if (display_thread->entry_point != gxm.params.displayQueueCallback.address()
            || (phase == DisplayWorkerPhase::Complete && display_thread->status != ThreadStatus::dormant)) {
            return fail("started display callback guest continuation is missing");
        }
        if (phase == DisplayWorkerPhase::WaitNewSync
            && gxm.display_entries.front().old_sync == gxm.display_entries.front().new_sync)
            return fail("display worker repeats the same sync-object wait");
        if (gxm.display_previous_entry) {
            const uint64_t address = gxm.display_previous_entry & ~1U;
            const uint64_t size = (gxm.display_previous_entry & 1U) ? 2 : 4;
            const auto upper = std::upper_bound(spans.begin(), spans.end(), address,
                [](uint64_t key, const MemorySpanInfo &span) { return key < span.address; });
            if (upper == spans.begin() || address + size > uint64_t(std::prev(upper)->address)
                    + uint64_t(std::prev(upper)->page_count) * kMemoryImagePageSize)
                return fail("display callback previous entry point is outside saved memory");
        }
    }
    std::map<SceUID, const PendingDisplaySubmission *> submissions;
    for (const auto &[id, submission] : gxm.display_submissions)
        if (id <= 0 || !submissions.emplace(id, &submission).second)
            return fail("duplicate or invalid display producer owner");
    std::set<SceUID> phase_zero;
    std::set<Address> other_owners;
    for (const auto &thread : threads) {
        other_owners.insert(thread.stack_addr);
        other_owners.insert(thread.tls_addr);
        unsigned display_waits = 0;
        for (const auto &wait : thread.waits) {
            if (wait.operation != WaitOperation::gxm_display_queue)
                continue;
            const auto &a = wait.args;
            if (++display_waits != 1 || !thread.registered || thread.id <= 0
                || a[0] != uint32_t(thread.id) || a[1] > 1 || a[2] > 1 || a[3] != 1
                || a[4] || a[5] || a[6] || a[7] || (a[2] && a[1] != 1)
                || !wait.infinite || wait.timeout_address || wait.callbacks || wait.queued
                || wait.has_result || wait.sequence || wait.phase != WaitPhase::queued
                || wait.target.type != SCE_KERNEL_WAITTYPE_EVENT || wait.target.id != thread.id)
                return fail("invalid display producer continuation");
            if (a[1] == 0) {
                if (!submissions.contains(thread.id) || !phase_zero.insert(thread.id).second)
                    return fail("display capacity wait has no unique prepared entry");
            } else if (submissions.contains(thread.id) || (!a[2] && gxm.params.displayQueueMaxPendingCount != 1)) {
                return fail("completed display submission still owns prepared data");
            }
        }
    }
    if (phase_zero.size() != submissions.size())
        return fail("prepared display entry has no registered capacity waiter");

    const uint64_t pages = (uint64_t(gxm.params.displayQueueCallbackDataSize) + kMemoryImagePageSize - 1)
        / kMemoryImagePageSize;
    std::set<Address> callback_owners;
    const auto callback_allocation = [&](Address address) {
        if (!address || address % kMemoryImagePageSize || !pages || other_owners.contains(address)
            || !callback_owners.insert(address).second)
            return false;
        const auto it = std::lower_bound(spans.begin(), spans.end(), address,
            [](const MemorySpanInfo &span, Address key) { return span.address < key; });
        if (it == spans.end() || it->address != address || it->page_count != pages)
            return false;
        if (sysmem) {
            const uint64_t end = uint64_t(address) + pages * kMemoryImagePageSize;
            for (const auto &[_, block] : sysmem->blocks) {
                const uint64_t begin = block.mappedBase.address();
                if (begin < end && uint64_t(address) < begin + block.mappedSize)
                    return false;
            }
        }
        return true;
    };
    for (const auto &entry : gxm.display_entries)
        if (!callback_allocation(entry.data))
            return fail("queued display callback does not own its exact memory allocation");
    for (const auto &[_, submission] : gxm.display_submissions)
        if (!callback_allocation(submission.callback.data))
            return fail("prepared display callback does not own its exact memory allocation");
    return true;
}

inline bool validate_gxm_display_continuations(const GxmSnapshot &gxm,
    const std::vector<ThreadState::Snapshot> &threads, const MemoryImage &memory,
    std::string &error, const SysmemSnapshot *sysmem = nullptr) {
    return validate_gxm_display_continuations(gxm, threads, memory.spans(), error, sysmem);
}
} // namespace emucorev::savestate
