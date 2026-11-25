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

#include <renderer/vulkan/sampler_support.h>

#include <util/log.h>

#include <algorithm>
#include <cstring>

namespace renderer::vulkan {

static bool has_extension(std::span<const vk::ExtensionProperties> available, const char *name) {
    return std::any_of(available.begin(), available.end(), [name](const vk::ExtensionProperties &ext) {
        return std::strcmp(ext.extensionName.data(), name) == 0;
    });
}

static bool is_enabled(const std::vector<const char *> &enabled, const char *name) {
    return std::any_of(enabled.begin(), enabled.end(), [name](const char *e) { return std::strcmp(e, name) == 0; });
}

static void enable_extension(std::vector<const char *> &enabled, const char *name) {
    if (!is_enabled(enabled, name))
        enabled.push_back(name);
}

void select_sampler_device_support(vk::PhysicalDevice physical_device, std::span<const vk::ExtensionProperties> available,
    std::vector<const char *> &device_extensions, SamplerDeviceSupport &out, vk::PhysicalDeviceSamplerYcbcrConversionFeatures &ycbcr_features) {
    out = {};
    ycbcr_features = vk::PhysicalDeviceSamplerYcbcrConversionFeatures{};

    // Vulkan 1.2 has this as a core feature, but the instance is created as 1.1, so only the extension counts
    if (has_extension(available, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME)) {
        enable_extension(device_extensions, VK_KHR_SAMPLER_MIRROR_CLAMP_TO_EDGE_EXTENSION_NAME);
        out.mirror_clamp_to_edge = true;
    }

    // YCbCr conversion: core in 1.1, otherwise the KHR extension and its dependencies
    const bool has_features2 = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFeatures2KHR != nullptr
        || VULKAN_HPP_DEFAULT_DISPATCHER.vkGetPhysicalDeviceFeatures2 != nullptr;
    const bool core_11 = physical_device.getProperties().apiVersion >= VK_API_VERSION_1_1;
    const bool ext_chain = has_extension(available, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME)
        && has_extension(available, VK_KHR_MAINTENANCE_1_EXTENSION_NAME)
        && has_extension(available, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME)
        && has_extension(available, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
    if (has_features2 && (core_11 || ext_chain)) {
        const auto queried = physical_device.getFeatures2KHR<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceSamplerYcbcrConversionFeatures>();
        if (queried.get<vk::PhysicalDeviceSamplerYcbcrConversionFeatures>().samplerYcbcrConversion) {
            if (!core_11) {
                enable_extension(device_extensions, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
                enable_extension(device_extensions, VK_KHR_MAINTENANCE_1_EXTENSION_NAME);
                enable_extension(device_extensions, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME);
                enable_extension(device_extensions, VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME);
            }
            ycbcr_features.samplerYcbcrConversion = VK_TRUE;
            out.ycbcr_conversion = true;
        }
    }

    LOG_INFO("Sampler support: mirror_clamp_to_edge={} ycbcr_conversion={}", out.mirror_clamp_to_edge, out.ycbcr_conversion);
}

renderer::SamplerSupport make_sampler_support(const vk::PhysicalDeviceProperties &properties, const vk::PhysicalDeviceFeatures &enabled_features, const SamplerDeviceSupport &device_support) {
    renderer::SamplerSupport support;
    support.mirror_clamp_to_edge = device_support.mirror_clamp_to_edge;
    support.anisotropy = static_cast<bool>(enabled_features.samplerAnisotropy);
    support.max_anisotropy = std::max(properties.limits.maxSamplerAnisotropy, 1.0f);
    support.max_lod_bias = properties.limits.maxSamplerLodBias;
    return support;
}

} // namespace renderer::vulkan
