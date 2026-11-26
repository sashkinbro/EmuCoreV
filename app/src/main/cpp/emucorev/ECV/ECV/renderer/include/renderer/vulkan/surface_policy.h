// Surface / texture cache ownership policy (B130, B131).
//
// Header-only and free of Vulkan/guest-memory types on purpose: every rule below is a pure function
// so it can be proven by host unit tests (app/src/test/core-host/surface_policy_tests.cpp) and the
// surface cache only has to call it. The cache itself (surface_cache.cpp) owns the side effects.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace renderer::vulkan::surface_policy {

// ---------------------------------------------------------------------------------------------
// B130: capacity. The old cache had 20 slots and always destroyed the least recently used one.
// ---------------------------------------------------------------------------------------------

// Number of slots of the colour queue and of the depth queue (also the save-state slot count).
// A slot is a few hundred bytes; the memory a surface holds is governed by the byte budget below.
inline constexpr uint32_t max_cache_slots = 64;
// The cache used to hold 20 surfaces. Save states written by those builds carry 20 slots.
inline constexpr uint32_t legacy_cache_slots = 20;

// PROVISIONAL (no device measurement yet): the pre-fix worst case was 20 full-screen surfaces,
// i.e. 20 * 960x544x4 B = 42 MB at 1x and 670 MB at 4x. The budget keeps at least that 1x/2x
// capability on every device and lets big-RAM devices hold more. The runner logs live bytes at every
// eviction (GPUTRACE-free, LOG_INFO_ONCE) so the numbers can be tuned from a device run.
inline constexpr uint64_t budget_min_bytes = 384ull << 20;
inline constexpr uint64_t budget_max_bytes = 1024ull << 20;
inline constexpr uint64_t budget_heap_divisor = 8;

// device_local_heap_bytes == 0 (unknown) yields the minimum.
constexpr uint64_t surface_budget_bytes(uint64_t device_local_heap_bytes) {
    const uint64_t share = device_local_heap_bytes / budget_heap_divisor;
    return std::clamp(share, budget_min_bytes, budget_max_bytes);
}

struct SlotInfo {
    bool occupied = false;
    // rendered into or sampled by the scene that is being recorded right now: its GPU work is not
    // submitted yet, so it can neither be written back nor reused without corrupting that scene
    bool used_this_scene = false;
    // touched during the current frame (rendered or sampled)
    bool used_this_frame = false;
    uint64_t bytes = 0;
};

struct SlotChoice {
    // index into the LRU-ordered input (0 = least recently used); -1 only when the input is empty
    int slot = -1;
    // slots that must also be evicted to get under the byte budget (their indices, in eviction order)
    std::vector<int> also_evict;
    // true when every occupied slot is in use by the current scene and one had to be taken anyway
    bool forced = false;
    // true when the chosen slot is occupied and its content must be evicted (written back) first
    bool slot_occupied = false;
};

// Choose the slot for a new surface of `incoming_bytes`. `lru_order` lists the slots least recently
// used first. Rules (all provable from the inputs):
//  1. a free slot is preferred over evicting anything;
//  2. a surface used by the current scene is never evicted unless nothing else exists (forced);
//  3. surfaces untouched during this frame go before surfaces touched this frame, oldest first;
//  4. while the total (live + incoming) exceeds the budget, further victims are evicted by rule 2/3
//     (an incoming surface larger than the whole budget simply evicts every candidate).
inline SlotChoice choose_slot(const std::vector<SlotInfo> &lru_order, uint64_t incoming_bytes, uint64_t budget_bytes) {
    SlotChoice choice;
    if (lru_order.empty())
        return choice;

    int free_slot = -1;
    uint64_t live = 0;
    for (size_t i = 0; i < lru_order.size(); ++i) {
        if (!lru_order[i].occupied) {
            if (free_slot < 0)
                free_slot = static_cast<int>(i);
        } else {
            live += lru_order[i].bytes;
        }
    }

    // eviction candidates in preference order: not used this frame first, then used this frame; oldest first
    std::vector<int> candidates;
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < lru_order.size(); ++i) {
            const SlotInfo &s = lru_order[i];
            if (!s.occupied || s.used_this_scene)
                continue;
            if ((pass == 0) == s.used_this_frame)
                continue;
            candidates.push_back(static_cast<int>(i));
        }
    }

    std::vector<int> evict;
    size_t next = 0;
    while (live + incoming_bytes > budget_bytes && next < candidates.size()) {
        const int victim = candidates[next++];
        live -= lru_order[victim].bytes;
        evict.push_back(victim);
    }

    if (free_slot >= 0) {
        choice.slot = free_slot;
        choice.also_evict = std::move(evict);
        return choice;
    }

    if (evict.empty() && next < candidates.size())
        evict.push_back(candidates[next++]);

    if (!evict.empty()) {
        choice.slot = evict.front();
        choice.slot_occupied = true;
        choice.also_evict.assign(evict.begin() + 1, evict.end());
        return choice;
    }

    // everything is in use by the scene being recorded
    choice.slot = 0;
    choice.slot_occupied = lru_order[0].occupied;
    choice.forced = true;
    return choice;
}

// ---------------------------------------------------------------------------------------------
// B130/B131: what must happen to a surface's content before its image is destroyed.
// The guest memory is authoritative once the surface is gone; the GPU image must not be the only
// holder of data the guest can still read (through the texture cache, a CPU read or another RT).
// ---------------------------------------------------------------------------------------------

struct WritebackInput {
    bool linear = false;             // only linear surfaces have a defined guest-memory layout here
    bool sync_available = false;     // memory mapping on and surface sync not disabled
    bool cpu_newer = false;          // dirty: the guest wrote the memory after the GPU did
    bool kept_in_sync = false;       // need_surface_sync && !gpu_read_sync_only: every scene already wrote it back
    bool superseded = false;         // a live surface that was rendered later overlaps this one
};

enum class Writeback : uint8_t {
    Do,
    SkipUnsupportedLayout,
    SkipSyncUnavailable,
    SkipCpuNewer,
    SkipAlreadySynced,
    SkipSuperseded,
};

constexpr Writeback decide_writeback(const WritebackInput &in) {
    if (!in.linear)
        return Writeback::SkipUnsupportedLayout;
    if (!in.sync_available)
        return Writeback::SkipSyncUnavailable;
    if (in.cpu_newer)
        return Writeback::SkipCpuNewer;
    if (in.kept_in_sync)
        return Writeback::SkipAlreadySynced;
    if (in.superseded)
        return Writeback::SkipSuperseded;
    return Writeback::Do;
}

// A surface A is superseded by B inside their overlap when B was rendered in a later scene. Writing A
// back would overwrite the newer owner's data.
constexpr bool is_superseded(uint64_t scene_of_a, uint64_t scene_of_b, bool ranges_overlap) {
    return ranges_overlap && scene_of_b > scene_of_a;
}

constexpr bool ranges_overlap(uint64_t begin_a, uint64_t end_a, uint64_t begin_b, uint64_t end_b) {
    return begin_a < end_b && begin_b < end_a;
}

// ---------------------------------------------------------------------------------------------
// B131: CPU-write tracking. A memory trap is one-shot (the whole protected segment opens on the first
// access), the faulting access can start outside the surface and sweep into it, and the trap is only
// re-armed when the cache next looks at the surface. So a trap alone cannot prove "unwritten".
// The proof used instead: after a trap fires, the guest bytes of the surface are compared with their
// hash from the moment the trap was armed; any difference is a CPU write. Without a baseline (surface
// above verify_hash_limit_bytes, or one the GPU keeps writing back) a write fault inside the surface
// range is the proof.
// ---------------------------------------------------------------------------------------------

// Surfaces up to this size keep an arm-time hash of their guest bytes (XXH3 ~ 10 GB/s: <= 0.4 ms).
inline constexpr uint32_t verify_hash_limit_bytes = 4u << 20;

// What a fired trap proves about the surface bytes.
enum class TrapVerdict : uint8_t {
    Untouched,   // nothing happened since arming
    CpuWrote,    // a write fault inside the surface range, or a hash difference
    Unknown,     // fired, cannot be verified (no baseline, or the GPU keeps the memory in sync)
};

struct TrapObservation {
    bool fired = false;
    bool write_inside_range = false;  // the faulting write address was inside the surface bytes
    bool baseline_valid = false;      // arm-time hash exists (surface <= verify_hash_limit_bytes)
    bool gpu_writes_memory = false;   // need_surface_sync: GPU write-back changes the bytes itself
    bool hash_differs = false;        // only meaningful when baseline_valid && !gpu_writes_memory
    bool emulator_wrote = false;      // the emulator's own write-back touched the bytes: the baseline is stale
};

constexpr TrapVerdict judge_trap(const TrapObservation &o) {
    if (!o.fired)
        return TrapVerdict::Untouched;
    // The emulator's own write-back (surface sync) changed the bytes since arming, so the hash cannot
    // separate it from a guest write: only a write fault that was not ours proves a CPU write.
    if (o.emulator_wrote)
        return o.write_inside_range ? TrapVerdict::CpuWrote : TrapVerdict::Untouched;
    // With a baseline the bytes decide, whatever the fault address was: a CPU write that stores the
    // bytes the memory already held (a loader re-writing a texture, a memset to the same value) does
    // not make the GPU image stale.
    if (o.baseline_valid && !o.gpu_writes_memory)
        return o.hash_differs ? TrapVerdict::CpuWrote : TrapVerdict::Untouched;
    if (o.write_inside_range)
        return TrapVerdict::CpuWrote;
    return TrapVerdict::Unknown;
}

// ---------------------------------------------------------------------------------------------
// B131: who owns a guest address when a texture is bound.
// ---------------------------------------------------------------------------------------------

enum class TextureSource : uint8_t { Surface, TextureCache };

// The GPU image of a surface may only stand in for guest memory while the GPU is the last writer.
// There is no age-based grace: a detected CPU write to the surface hands the address to the texture
// cache immediately (the old rule kept sampling the stale image for two more frames).
constexpr TextureSource texture_source(bool cpu_newer) {
    return cpu_newer ? TextureSource::TextureCache : TextureSource::Surface;
}

// The requested texture range must lie inside the surface (the 4 byte slack covers the gbuffer
// second-component read at base + 4 that the cache has always allowed).
constexpr bool texture_fits_surface(uint64_t surface_base, uint64_t surface_bytes, uint64_t texture_address, uint64_t texture_bytes) {
    return texture_address >= surface_base && texture_address + texture_bytes <= surface_base + surface_bytes + 4;
}

// ---------------------------------------------------------------------------------------------
// B131: what a framebuffer request does to an existing overlapping surface.
// ---------------------------------------------------------------------------------------------

enum class FramebufferAction : uint8_t {
    Reuse,
    ReplaceWithWriteback,   // same base, bigger/other layout or format: old content is saved, new surface restores it
    RetireWithWriteback,    // different base inside the old range ("probably freed"), or the new one runs past it
};

struct FramebufferRequest {
    bool same_base = false;
    bool new_range_inside_old = false;   // request end <= old end (+4 slack)
    bool extent_or_layout_changed = false;  // taller, other stride or tiling
    bool wider_or_other_format = false;
};

constexpr FramebufferAction framebuffer_action(const FramebufferRequest &r) {
    if (!r.same_base)
        return FramebufferAction::RetireWithWriteback;
    if (r.extent_or_layout_changed || r.wider_or_other_format || !r.new_range_inside_old)
        return FramebufferAction::ReplaceWithWriteback;
    return FramebufferAction::Reuse;
}

// ---------------------------------------------------------------------------------------------
// B131: initial contents of a new surface. A range that was written back when its previous surface
// was retired is authoritative in guest memory, so the new surface must start from it instead of
// transparent black.
// ---------------------------------------------------------------------------------------------

struct PreservedRange {
    uint32_t begin = 0;
    uint32_t end = 0;
    uint64_t frame = 0;
};

// How long a retired range stays authoritative for new surfaces (frames). PROVISIONAL.
inline constexpr uint64_t preserved_range_lifetime_frames = 600;
inline constexpr size_t preserved_range_capacity = 16;

inline void remember_preserved(std::vector<PreservedRange> &ranges, PreservedRange range) {
    ranges.push_back(range);
    if (ranges.size() > preserved_range_capacity)
        ranges.erase(ranges.begin());
}

// True when [begin, end) overlaps a live preserved range; every overlapped or expired entry is consumed.
inline bool consume_preserved(std::vector<PreservedRange> &ranges, uint32_t begin, uint32_t end, uint64_t frame) {
    bool hit = false;
    for (auto it = ranges.begin(); it != ranges.end();) {
        const bool expired = frame > it->frame && frame - it->frame > preserved_range_lifetime_frames;
        const bool overlaps = ranges_overlap(begin, end, it->begin, it->end);
        if (overlaps && !expired)
            hit = true;
        if (overlaps || expired)
            it = ranges.erase(it);
        else
            ++it;
    }
    return hit;
}

// B156: which surfaces of a save state keep their GPU image bytes. `bytes` lists the image bytes of
// each candidate, most important first (the last scene's surfaces, then most recently used). A
// candidate is kept while it fits the budget; one that does not fit is skipped and smaller, less
// important ones may still use the room. The sum of the kept bytes never exceeds budget_bytes.
inline std::vector<bool> select_snapshot_images(const std::vector<uint64_t> &bytes, uint64_t budget_bytes) {
    std::vector<bool> keep(bytes.size(), false);
    uint64_t remaining = budget_bytes;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (bytes[i] <= remaining) {
            keep[i] = true;
            remaining -= bytes[i];
        }
    }
    return keep;
}

} // namespace renderer::vulkan::surface_policy
