#pragma once

// Pure rear-panel contact bookkeeping (no SDL / emulator state), kept header-only so the host
// unit tests can compile it. Contacts arrive from the host UI as press/release events, but the
// guest only sees them through the per-vsync sampling in touch_vsync_update. A press+release that
// happens between two vsyncs must still produce one report, so a press latches a "pending" flag
// that keeps the contact visible for one sampling even if it was already released.

#include <touch/touch.h>

namespace touch_rear {

constexpr int SLOTS = 4;

struct Contacts {
    SceTouchReport report[SLOTS] = {};
    bool down[SLOTS] = {};
    bool pending[SLOTS] = {};

    bool any_active() const {
        for (int i = 0; i < SLOTS; ++i)
            if (down[i] || pending[i]) return true;
        return false;
    }

    bool id_in_use(int id) const {
        for (int i = 0; i < SLOTS; ++i)
            if ((down[i] || pending[i]) && report[i].id == id) return true;
        return false;
    }

    void clear() { *this = Contacts{}; }
};

// A press on a slot that is not currently down starts a new contact and takes a fresh ID from
// alloc_id; a press on a slot that is already down only moves it and keeps its ID.
template <typename AllocId>
void press(Contacts &c, int slot, SceInt16 x, SceInt16 y, AllocId &&alloc_id) {
    if (slot < 0 || slot >= SLOTS) return;
    if (!c.down[slot]) {
        c.report[slot].id = static_cast<SceUInt8>(alloc_id());
        c.down[slot] = true;
    }
    c.pending[slot] = true;
    c.report[slot].x = x;
    c.report[slot].y = y;
}

inline void release(Contacts &c, int slot) {
    if (slot < 0 || slot >= SLOTS) return;
    c.down[slot] = false; // pending stays set so a quick tap is still reported once
}

// Copies the contacts visible in this sampling into out (at most limit); returns the count.
inline unsigned sample(const Contacts &c, SceTouchReport *out, unsigned limit, SceUInt8 force) {
    unsigned n = 0;
    for (int i = 0; i < SLOTS && n < limit; ++i) {
        if (!c.down[i] && !c.pending[i]) continue;
        out[n] = c.report[i];
        out[n].force = force;
        ++n;
    }
    return n;
}

// Call once per vsync after sampling: contacts that were only latched are dropped now.
inline void end_sampling(Contacts &c) {
    for (int i = 0; i < SLOTS; ++i) c.pending[i] = false;
}

} // namespace touch_rear
