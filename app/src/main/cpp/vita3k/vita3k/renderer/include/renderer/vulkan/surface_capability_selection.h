// Vita3K emulator project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <vulkan/vulkan.hpp>

#include <array>
#include <vector>

namespace renderer::vulkan {

struct SwapchainCapabilitiesSelection {
    vk::ImageUsageFlags image_usage{};
    vk::CompositeAlphaFlagBitsKHR composite_alpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    vk::SurfaceTransformFlagBitsKHR pre_transform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    bool transfer_dst = false;
    bool storage = false;
};

inline bool select_surface_format(const std::vector<vk::SurfaceFormatKHR> &formats, vk::SurfaceFormatKHR &selected) {
    if (formats.empty())
        return false;

    for (const auto &format : formats) {
        if ((format.format == vk::Format::eB8G8R8A8Unorm || format.format == vk::Format::eR8G8B8A8Unorm)
            && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            selected = format;
            return true;
        }
    }

    selected = formats.front();
    return true;
}

inline bool supports_fsr_surface(const vk::SurfaceCapabilitiesKHR &capabilities) {
    // Both FSR output paths transfer the final image into the swapchain. Storage
    // usage is an optimization; transfer-dst usage is required by the filter.
    return static_cast<bool>(capabilities.supportedUsageFlags & vk::ImageUsageFlagBits::eTransferDst);
}

inline bool select_swapchain_capabilities(const vk::SurfaceCapabilitiesKHR &capabilities,
    bool allow_storage, SwapchainCapabilitiesSelection &selected) {
    const auto supported_usage = capabilities.supportedUsageFlags;
    const auto color_attachment = vk::ImageUsageFlagBits::eColorAttachment;
    if (!(supported_usage & color_attachment))
        return false;

    SwapchainCapabilitiesSelection result;
    result.image_usage = color_attachment;
    result.transfer_dst = static_cast<bool>(supported_usage & vk::ImageUsageFlagBits::eTransferDst);
    if (result.transfer_dst)
        result.image_usage |= vk::ImageUsageFlagBits::eTransferDst;
    if (supported_usage & vk::ImageUsageFlagBits::eTransferSrc)
        result.image_usage |= vk::ImageUsageFlagBits::eTransferSrc;

    // FSR writes to the swapchain as a storage image and requires transfer-dst
    // support for the same path. Never request either capability unsupported by
    // the surface.
    if (allow_storage && result.transfer_dst && (supported_usage & vk::ImageUsageFlagBits::eStorage)) {
        result.image_usage |= vk::ImageUsageFlagBits::eStorage;
        result.storage = true;
    }

    const auto supported_alpha = capabilities.supportedCompositeAlpha;
    constexpr std::array alpha_preference{
        vk::CompositeAlphaFlagBitsKHR::eOpaque,
        vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
        vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
        vk::CompositeAlphaFlagBitsKHR::eInherit,
    };
    bool alpha_found = false;
    for (const auto alpha : alpha_preference) {
        if (supported_alpha & alpha) {
            result.composite_alpha = alpha;
            alpha_found = true;
            break;
        }
    }
    if (!alpha_found)
        return false;

    const auto supported_transforms = capabilities.supportedTransforms;
    const auto identity = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    if (supported_transforms & identity) {
        result.pre_transform = identity;
    } else if (supported_transforms & capabilities.currentTransform) {
        result.pre_transform = capabilities.currentTransform;
    } else {
        constexpr std::array transform_preference{
            vk::SurfaceTransformFlagBitsKHR::eRotate90,
            vk::SurfaceTransformFlagBitsKHR::eRotate180,
            vk::SurfaceTransformFlagBitsKHR::eRotate270,
            vk::SurfaceTransformFlagBitsKHR::eHorizontalMirror,
            vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate90,
            vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate180,
            vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate270,
            vk::SurfaceTransformFlagBitsKHR::eInherit,
        };
        bool transform_found = false;
        for (const auto transform : transform_preference) {
            if (supported_transforms & transform) {
                result.pre_transform = transform;
                transform_found = true;
                break;
            }
        }
        if (!transform_found)
            return false;
    }

    selected = result;
    return true;
}

} // namespace renderer::vulkan
