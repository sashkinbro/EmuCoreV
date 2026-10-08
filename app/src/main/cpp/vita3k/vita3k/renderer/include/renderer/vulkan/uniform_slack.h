// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

namespace renderer::vulkan {

struct UniformSlackCopyRange {
    uint64_t offset;
    uint64_t size;
};

inline std::optional<UniformSlackCopyRange> uniform_slack_copy_range(
    const uint64_t address,
    const uint64_t declared_size,
    const uint64_t mapped_remaining,
    const uint64_t surface_limit,
    const uint64_t window_size) {
    constexpr uint64_t max_value = std::numeric_limits<uint64_t>::max();
    if (declared_size == 0 || declared_size > mapped_remaining || declared_size > max_value - address)
        return std::nullopt;

    const uint64_t declared_end = address + declared_size;
    const uint64_t mapped_end = mapped_remaining > max_value - address ? max_value : address + mapped_remaining;
    const uint64_t window_end = window_size > max_value - address ? max_value : address + window_size;
    const uint64_t copy_end = std::min({ window_end, mapped_end, surface_limit });
    if (copy_end <= declared_end)
        return std::nullopt;

    return UniformSlackCopyRange{ declared_size, copy_end - declared_end };
}

} // namespace renderer::vulkan
