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

#include <renderer/sampler_state.h>

#include <vkutil/vkutil.h>

#include <span>
#include <vector>

namespace renderer::vulkan {

// Device level capabilities of the sampler / YUV work (P4, P6), chosen from what the physical device reports.
struct SamplerDeviceSupport {
    // VK_KHR_sampler_mirror_clamp_to_edge was added to the enabled extensions
    bool mirror_clamp_to_edge = false;
    // samplerYcbcrConversion is supported and enabled (extensions + the feature struct below)
    bool ycbcr_conversion = false;
};

// Called while the logical device is being created, after the device extensions were enumerated:
//  - appends the extensions the sampler/YUV code wants (only when advertised, no duplicates) to device_extensions,
//  - fills `out`,
//  - when out.ycbcr_conversion is true, fills `ycbcr_features` (samplerYcbcrConversion = VK_TRUE) and the caller MUST
//    add it to the VkDeviceCreateInfo pNext chain.
void select_sampler_device_support(vk::PhysicalDevice physical_device, std::span<const vk::ExtensionProperties> available,
    std::vector<const char *> &device_extensions, SamplerDeviceSupport &out, vk::PhysicalDeviceSamplerYcbcrConversionFeatures &ycbcr_features);

// Limits / features that are decided at device creation, read by configure_sampler through VKState::sampler_support
renderer::SamplerSupport make_sampler_support(const vk::PhysicalDeviceProperties &properties, const vk::PhysicalDeviceFeatures &enabled_features, const SamplerDeviceSupport &device_support);

} // namespace renderer::vulkan
