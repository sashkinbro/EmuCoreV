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

// Host-side implementation of the sceKernelAtomic* family. Pure header (no emulator state) so
// it can be unit tested on the host. Guest memory is shared with guest threads that run in
// other host threads, so every operation is a real host atomic on the guest address.
//
// Return conventions (vita-headers psp2kern/kernel/cpu/atomic.h): "XAndGet" returns the NEW
// value, "GetAndX" returns the OLD value, CompareAndSet returns the OLD value, AddUnless
// returns 1 when the add was performed and 0 otherwise (Linux atomic_add_unless), DecIfPositive
// returns old - 1 (Linux atomic_dec_if_positive) and only stores it when old > 0 (signed).
// Set / ClearMask are void in the headers; they return the old value here (unused by callers).
// The public types are signed; the helpers work on the unsigned twin, callers sign-extend.

#include <cstdint>
#include <mutex>
#include <type_traits>

namespace guest_atomic {

template <typename U>
struct Rmw {
    U old;
    U updated;
};

// Misaligned guest pointers would fault in ldrex on real hardware and trap on AArch64 hosts
// for atomic accesses. They are serialized through one mutex instead of crashing the host.
inline std::mutex &misaligned_mutex() {
    static std::mutex m;
    return m;
}

// Applies f(old) -> new atomically (CAS loop). A null pointer is a no-op returning 0.
template <typename U, typename F>
inline Rmw<U> rmw(U *p, F f) {
    static_assert(std::is_unsigned_v<U>);
    if (!p)
        return { 0, 0 };
    if (reinterpret_cast<uintptr_t>(p) % sizeof(U) != 0) {
        std::lock_guard lock(misaligned_mutex());
        U old;
        __builtin_memcpy(&old, p, sizeof(U));
        const U updated = f(old);
        __builtin_memcpy(p, &updated, sizeof(U));
        return { old, updated };
    }
    U old = __atomic_load_n(p, __ATOMIC_SEQ_CST);
    U updated;
    do {
        updated = f(old);
    } while (!__atomic_compare_exchange_n(p, &old, updated, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
    return { old, updated };
}

// --- new value returned ---
template <typename U> inline U add_and_get(U *p, U v) { return rmw(p, [=](U o) -> U { return static_cast<U>(o + v); }).updated; }
template <typename U> inline U sub_and_get(U *p, U v) { return rmw(p, [=](U o) -> U { return static_cast<U>(o - v); }).updated; }
template <typename U> inline U and_and_get(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o & m); }).updated; }
template <typename U> inline U or_and_get(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o | m); }).updated; }
template <typename U> inline U xor_and_get(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o ^ m); }).updated; }
template <typename U> inline U clear_and_get(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o & ~m); }).updated; }

// --- old value returned ---
template <typename U> inline U get_and_add(U *p, U v) { return rmw(p, [=](U o) -> U { return static_cast<U>(o + v); }).old; }
template <typename U> inline U get_and_sub(U *p, U v) { return rmw(p, [=](U o) -> U { return static_cast<U>(o - v); }).old; }
template <typename U> inline U get_and_and(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o & m); }).old; }
template <typename U> inline U get_and_or(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o | m); }).old; }
template <typename U> inline U get_and_xor(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o ^ m); }).old; }
template <typename U> inline U get_and_clear(U *p, U m) { return rmw(p, [=](U o) -> U { return static_cast<U>(o & ~m); }).old; }
template <typename U> inline U get_and_set(U *p, U v) { return rmw(p, [=](U) -> U { return v; }).old; }
template <typename U> inline U set(U *p, U v) { return get_and_set(p, v); }
template <typename U> inline U clear_mask(U *p, U m) { return get_and_clear(p, m); }

// Stores `set_to` only when *p == cmp. Returns the old value (== cmp on success).
template <typename U>
inline U compare_and_set(U *p, U cmp, U set_to) {
    return rmw(p, [=](U o) -> U { return o == cmp ? set_to : o; }).old;
}

// *p += v unless *p == cmp. Returns 1 if the add was performed, 0 otherwise.
template <typename U>
inline U add_unless(U *p, U v, U cmp) {
    const auto r = rmw(p, [=](U o) -> U { return o == cmp ? o : static_cast<U>(o + v); });
    return r.old != cmp ? U(1) : U(0);
}

// *p -= 1 only when *p is > 0 as a signed value. Returns old - 1 (also when not stored).
template <typename U>
inline U dec_if_positive(U *p) {
    using S = std::make_signed_t<U>;
    return static_cast<U>(rmw(p, [](U o) -> U { return static_cast<S>(o) > 0 ? static_cast<U>(o - 1) : o; }).old - 1);
}

} // namespace guest_atomic
