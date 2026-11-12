#include <renderer/vulkan/surface_capability_selection.h>
#include <gtest/gtest.h>

TEST(SurfaceCapabilitySelection, ChoosesSupportedFormatAndRejectsAnEmptyList) {
    using namespace renderer::vulkan;
    vk::SurfaceFormatKHR selected{};
    EXPECT_FALSE(select_surface_format({}, selected));

    const std::vector<vk::SurfaceFormatKHR> formats{
        { vk::Format::eA2B10G10R10UnormPack32, vk::ColorSpaceKHR::eSrgbNonlinear },
        { vk::Format::eB8G8R8A8Unorm, vk::ColorSpaceKHR::eSrgbNonlinear },
    };
    ASSERT_TRUE(select_surface_format(formats, selected));
    EXPECT_EQ(selected, formats[1]);
}

TEST(SurfaceCapabilitySelection, UsesNonOpaqueCompositeAlphaWhenOpaqueIsUnsupported) {
    using namespace renderer::vulkan;
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;
    capabilities.supportedCompositeAlpha = vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
    capabilities.supportedTransforms = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    capabilities.currentTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    SwapchainCapabilitiesSelection selected;

    ASSERT_TRUE(select_swapchain_capabilities(capabilities, false, selected));
    EXPECT_EQ(selected.composite_alpha, vk::CompositeAlphaFlagBitsKHR::ePostMultiplied);

    capabilities.supportedCompositeAlpha = vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
    ASSERT_TRUE(select_swapchain_capabilities(capabilities, false, selected));
    EXPECT_EQ(selected.composite_alpha, vk::CompositeAlphaFlagBitsKHR::ePreMultiplied);
}

TEST(SurfaceCapabilitySelection, UsesSupportedCurrentTransformWhenIdentityIsUnavailable) {
    using namespace renderer::vulkan;
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment;
    capabilities.supportedCompositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    capabilities.supportedTransforms = vk::SurfaceTransformFlagBitsKHR::eRotate90;
    capabilities.currentTransform = vk::SurfaceTransformFlagBitsKHR::eRotate90;
    SwapchainCapabilitiesSelection selected;

    ASSERT_TRUE(select_swapchain_capabilities(capabilities, false, selected));
    EXPECT_EQ(selected.pre_transform, vk::SurfaceTransformFlagBitsKHR::eRotate90);
}

TEST(SurfaceCapabilitySelection, NeverRequestsStorageOrTransferDstWhenTransferDstIsUnsupported) {
    using namespace renderer::vulkan;
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment
        | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eStorage;
    capabilities.supportedCompositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    capabilities.supportedTransforms = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    capabilities.currentTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    SwapchainCapabilitiesSelection selected;

    ASSERT_TRUE(select_swapchain_capabilities(capabilities, true, selected));
    EXPECT_FALSE(supports_fsr_surface(capabilities));
    EXPECT_FALSE(selected.transfer_dst);
    EXPECT_FALSE(selected.storage);
    EXPECT_TRUE(selected.image_usage & vk::ImageUsageFlagBits::eColorAttachment);
    EXPECT_TRUE(selected.image_usage & vk::ImageUsageFlagBits::eTransferSrc);
    EXPECT_FALSE(selected.image_usage & vk::ImageUsageFlagBits::eTransferDst);
    EXPECT_FALSE(selected.image_usage & vk::ImageUsageFlagBits::eStorage);
}

TEST(SurfaceCapabilitySelection, RejectsSurfaceWithoutColorAttachmentUsage) {
    using namespace renderer::vulkan;
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eTransferDst;
    capabilities.supportedCompositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    capabilities.supportedTransforms = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    capabilities.currentTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    SwapchainCapabilitiesSelection selected;

    EXPECT_FALSE(select_swapchain_capabilities(capabilities, true, selected));
}

TEST(SurfaceCapabilitySelection, FsrRequiresTransferDestinationEvenWhenStorageIsAvailable) {
    using namespace renderer::vulkan;
    vk::SurfaceCapabilitiesKHR capabilities{};
    capabilities.supportedUsageFlags = vk::ImageUsageFlagBits::eColorAttachment
        | vk::ImageUsageFlagBits::eStorage;
    EXPECT_FALSE(supports_fsr_surface(capabilities));

    capabilities.supportedUsageFlags |= vk::ImageUsageFlagBits::eTransferDst;
    EXPECT_TRUE(supports_fsr_surface(capabilities));
}
