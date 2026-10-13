// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace renderer::vulkan {
// This renderer does not enable VK_EXT_depth_range_unrestricted. Normalize
// guest values, including NaN, before they become Vulkan clear commands.
constexpr float normalized_depth_clear(float depth) {
    if (!(depth >= 0.0f)) return 0.0f;
    return depth > 1.0f ? 1.0f : depth;
}
} // namespace renderer::vulkan
