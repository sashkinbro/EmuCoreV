// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#pragma once

#include <cstdint>

namespace shader::usse {

// USSE allocates one four-float thread-buffer slice for each of the 16
// core/pipeline slots. Keep at least one SPIR-V element for malformed or tiny
// declarations, while tracking that element as the only safe index.
constexpr uint32_t thread_buffer_f32_count(const uint32_t byte_count) {
    const uint32_t count = byte_count / (4 * 4 * sizeof(float));
    return count == 0 ? 1 : count;
}

constexpr uint32_t thread_buffer_last_index(const uint32_t element_count) {
    return element_count == 0 ? 0 : element_count - 1;
}

} // namespace shader::usse
