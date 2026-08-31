#pragma once

#include <vulkan/vulkan_core.h>

namespace renderer::vulkan {

// AHardwareBuffer_lock supplies the CPU pointer; HOST_CACHED describes Vulkan's
// host mapping, not a guarantee about that gralloc pointer. Prefer cached, but
// accept coherent imports after the actual AHB pointer passes the atomic probe.
inline int native_buffer_memory_type(const VkPhysicalDeviceMemoryProperties &memory, uint32_t compatible_types) {
    int coherent_type = -1;
    for (uint32_t i = 0; i < memory.memoryTypeCount && i < VK_MAX_MEMORY_TYPES; ++i) {
        const auto flags = memory.memoryTypes[i].propertyFlags;
        if (!(compatible_types & (uint32_t{1} << i)) || !(flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) continue;
        if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) return static_cast<int>(i);
        if (coherent_type < 0) coherent_type = static_cast<int>(i);
    }
    return coherent_type;
}

inline bool native_buffer_supported(bool memory_mapping, bool android_import, bool fd_import) {
    return memory_mapping && (android_import || fd_import);
}

} // namespace renderer::vulkan
