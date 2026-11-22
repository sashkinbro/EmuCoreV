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

// Pure arithmetic shared by the thread manager exports (no kernel state): timer clock handling,
// event schedule queries, thread stack accounting and message pipe vector transfers.

#include <cstdint>

namespace kernel_arith {

// ---- Timer clock. A timer keeps `time` (the host clock value its counter started from) and a
// `bias` used when restoring snapshots; the counter reads `now - bias - time` (wrapping).
constexpr std::uint64_t timer_elapsed(std::uint64_t now, std::uint64_t time, std::uint64_t bias) { return now - bias - time; }

// Makes the counter read `elapsed` at `now` (sceKernelSetTimerTime). The counter keeps running
// from that value.
constexpr void timer_set_elapsed(std::uint64_t now, std::uint64_t elapsed, std::uint64_t &time, std::uint64_t &bias) {
    time = now - elapsed;
    bias = 0;
}

// Time left until the armed event, 0 when it is already due. `next_event` equal to UINT64_MAX
// means that no event is armed.
constexpr std::uint64_t timer_event_remaining(std::uint64_t now, std::uint64_t next_event) {
    if (next_event == UINT64_MAX)
        return 0;
    return next_event > now ? next_event - now : 0;
}

constexpr bool timer_event_armed(std::uint64_t next_event) { return next_event != UINT64_MAX; }

// ---- Thread stack. Free bytes between the stack base and the stack pointer (the stack grows
// downwards); a pointer outside the stack is clamped.
constexpr std::uint32_t stack_free_size(std::uint32_t stack_base, std::uint32_t stack_size, std::uint32_t sp) {
    const std::uint64_t top = static_cast<std::uint64_t>(stack_base) + stack_size;
    if (sp <= stack_base)
        return 0;
    if (sp >= top)
        return stack_size;
    return sp - stack_base;
}

// ---- Message pipe vectors. A vector transfer is executed segment by segment (each segment
// stays a guest buffer so that blocking and save states keep working). `step(seg)` returns the
// number of bytes transferred as an unsigned value, where a negative signed value is an error.
// Stops at the first short transfer. Returns the SceInt32 status and accumulates `total`.
constexpr std::uint32_t MAX_VECTOR_SEGMENTS = 0x1000;

template <typename Seg, typename Step>
std::int32_t run_vector(const Seg *segs, std::uint32_t count, Step step, std::uint32_t &total) {
    total = 0;
    for (std::uint32_t i = 0; i < count; i++) {
        if (segs[i].size == 0)
            continue;
        const std::uint32_t done = step(segs[i]);
        if (static_cast<std::int32_t>(done) < 0)
            return total == 0 ? static_cast<std::int32_t>(done) : 0;
        total += done;
        if (done < segs[i].size)
            break;
    }
    return 0;
}

} // namespace kernel_arith
