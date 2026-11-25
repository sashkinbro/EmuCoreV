// EmuCoreV: visibility (occlusion) buffer model of the Vulkan renderer (P8).
//
// Pure logic, no Vulkan calls, so that it can be unit tested on the host.
//
// GXM visibility: each face (front, back) has an enable bit, an entry index into the visibility buffer and an operation
// (increment = count the samples that pass, set = flag the entry as visible). Draws accumulate into the entry.
//
// Vulkan: one occlusion query counts a whole draw (both faces) and a query slot must not be begun twice between resets.
// So the renderer gives every activation (a stretch of draws with the same face state) its own query slot, copies the slots
// into a host readable buffer at the end of the scene and the CPU adds the slots of each entry up and writes the entries
// into the guest buffer (the same path with and without memory mapping).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace renderer::vulkan::visibility {

// Visibility state of one face. index < 0: disabled.
struct Face {
    int32_t index = -1;
    bool increment = false;
    bool enabled() const { return index >= 0; }
    bool operator==(const Face &) const = default;
};

// The back face uses the front state when two-sided drawing is off.
inline const Face &effective_face(const Face &front, const Face &back, const bool is_back, const bool two_sided) {
    return (is_back && two_sided) ? back : front;
}

// One begun query: which guest entry it feeds and with which operation.
struct Activation {
    uint32_t entry;
    bool increment;
};

// Extra query slots on top of one per guest entry: an entry that is begun again in the same scene (draws of other entries
// in between) needs a slot of its own.
constexpr uint32_t spare_slots = 1024;

// Query slots of one scene. Slot i belongs to activations[i].
struct SceneQueries {
    std::vector<Activation> activations;

    // Returns the slot to begin, or -1 when the entry is out of range of the guest buffer or the pool is exhausted
    // (such an activation is not counted; the caller warns once).
    int32_t begin(const uint32_t entry, const bool increment, const uint32_t guest_entries, const uint32_t pool_size) {
        if (entry >= guest_entries || activations.size() >= pool_size)
            return -1;
        activations.push_back({ entry, increment });
        return static_cast<int32_t>(activations.size() - 1);
    }
    bool empty() const { return activations.empty(); }
    uint32_t slots() const { return static_cast<uint32_t>(activations.size()); }
};

struct EntryValue {
    uint32_t entry;
    uint32_t value;
};

// Entry values of one scene from the slot results (samples passed per slot, as copied from the query pool).
// - increment activations add their sample count (saturating at 32 bits)
// - set activations flag the entry: 1 when any sample passed
// Entries are returned in ascending order, each once.
inline std::vector<EntryValue> resolve(const std::vector<Activation> &activations, const uint32_t *slot_results) {
    std::vector<EntryValue> values;
    values.reserve(activations.size());
    for (size_t slot = 0; slot < activations.size(); slot++) {
        const Activation &a = activations[slot];
        const uint32_t add = a.increment ? slot_results[slot] : (slot_results[slot] != 0 ? 1u : 0u);
        auto it = std::find_if(values.begin(), values.end(), [&](const EntryValue &v) { return v.entry == a.entry; });
        if (it == values.end()) {
            values.push_back({ a.entry, add });
        } else {
            // set after increment keeps the count, set after set stays 1
            const uint64_t sum = static_cast<uint64_t>(it->value) + add;
            it->value = a.increment ? static_cast<uint32_t>(std::min<uint64_t>(sum, 0xFFFFFFFFu)) : std::max(it->value, add);
        }
    }
    std::sort(values.begin(), values.end(), [](const EntryValue &l, const EntryValue &r) { return l.entry < r.entry; });
    return values;
}

} // namespace renderer::vulkan::visibility
