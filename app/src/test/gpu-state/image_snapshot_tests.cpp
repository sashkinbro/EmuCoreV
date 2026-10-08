#include <vkutil/image_snapshot.h>
#include <renderer/vulkan/checkpoint_submit.h>
#include <gtest/gtest.h>
#include <cstring>

namespace {
class GpuImageSnapshot : public testing::Test {
protected:
    vk::detail::DynamicLoader loader;
    vk::UniqueInstance instance;
    vk::UniqueDevice device;
    vk::UniqueCommandPool pool;
    vkutil::ImageSnapshotDevice context;
    std::string error;

    void SetUp() override {
        const auto get_proc = loader.getProcAddress<PFN_vkGetInstanceProcAddr>("vkGetInstanceProcAddr");
        ASSERT_NE(get_proc, nullptr);
        VULKAN_HPP_DEFAULT_DISPATCHER.init(get_proc);
        const vk::ApplicationInfo application{.pApplicationName = "EmuCoreV image snapshot tests", .apiVersion = VK_API_VERSION_1_1};
        instance = vk::createInstanceUnique(vk::InstanceCreateInfo{.pApplicationInfo = &application});
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*instance);
        const auto physical_devices = instance->enumeratePhysicalDevices();
        ASSERT_FALSE(physical_devices.empty());
        context.physical_device = physical_devices.front();
        const auto queues = context.physical_device.getQueueFamilyProperties();
        uint32_t queue_family = UINT32_MAX;
        for (uint32_t i = 0; i < queues.size(); ++i) {
            if (queues[i].queueFlags & vk::QueueFlagBits::eGraphics) {
                queue_family = i;
                break;
            }
        }
        ASSERT_NE(queue_family, UINT32_MAX);
        const float priority = 1.0f;
        const vk::DeviceQueueCreateInfo queue{.queueFamilyIndex = queue_family, .queueCount = 1, .pQueuePriorities = &priority};
        device = context.physical_device.createDeviceUnique(vk::DeviceCreateInfo{.queueCreateInfoCount = 1, .pQueueCreateInfos = &queue});
        VULKAN_HPP_DEFAULT_DISPATCHER.init(*device);
        pool = device->createCommandPoolUnique(vk::CommandPoolCreateInfo{.queueFamilyIndex = queue_family});
        context.device = *device;
        context.queue = device->getQueue(queue_family, 0);
        context.command_pool = *pool;
    }

    struct OwnedImage {
        vk::UniqueDeviceMemory memory;
        vk::UniqueImage handle;
        vkutil::Image image;
        OwnedImage(uint32_t width, uint32_t height, vk::Format format) : image(width, height, format) {
            image.destroy_on_deletion = false;
        }
    };

    std::unique_ptr<OwnedImage> image(vk::Format format, vk::ImageUsageFlags extra_usage = {}) {
        auto owned = std::make_unique<OwnedImage>(13, 9, format);
        owned->handle = device->createImageUnique(vk::ImageCreateInfo{
            .imageType = vk::ImageType::e2D, .format = format,
            .extent = {.width = 13, .height = 9, .depth = 1},
            .mipLevels = 1, .arrayLayers = 1, .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | extra_usage,
            .sharingMode = vk::SharingMode::eExclusive, .initialLayout = vk::ImageLayout::eUndefined});
        const auto requirements = device->getImageMemoryRequirements(*owned->handle);
        const auto properties = context.physical_device.getMemoryProperties();
        uint32_t type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if (requirements.memoryTypeBits & (1u << i)) {
                type = i;
                break;
            }
        }
        if (type == UINT32_MAX)
            throw std::runtime_error("no image memory type");
        owned->memory = device->allocateMemoryUnique(vk::MemoryAllocateInfo{.allocationSize = requirements.size, .memoryTypeIndex = type});
        device->bindImageMemory(*owned->handle, *owned->memory, 0);
        owned->image.image = *owned->handle;
        return owned;
    }

    void round_trip(vk::Format format, std::vector<uint8_t> bytes) {
        auto owned = image(format);
        vkutil::ImageSnapshot original{13, 9, format, vkutil::ImageLayout::TransferSrc, std::move(bytes)};
        ASSERT_TRUE(vkutil::restore_image_snapshot(context, owned->image, original, error)) << error;
        vkutil::ImageSnapshot first;
        ASSERT_TRUE(vkutil::capture_image_snapshot(context, owned->image, first, error)) << error;
        EXPECT_EQ(first.bytes, original.bytes);
        EXPECT_EQ(owned->image.layout, original.layout);
        vkutil::ImageSnapshot second;
        ASSERT_TRUE(vkutil::capture_image_snapshot(context, owned->image, second, error)) << error;
        EXPECT_EQ(second.bytes, first.bytes);
        auto changed = original;
        std::fill(changed.bytes.begin(), changed.bytes.end(), 0);
        ASSERT_TRUE(vkutil::restore_image_snapshot(context, owned->image, changed, error)) << error;
        ASSERT_TRUE(vkutil::restore_image_snapshot(context, owned->image, first, error)) << error;
        vkutil::ImageSnapshot restored;
        ASSERT_TRUE(vkutil::capture_image_snapshot(context, owned->image, restored, error)) << error;
        EXPECT_EQ(restored.bytes, original.bytes);
    }
};

TEST_F(GpuImageSnapshot, RepeatedCaptureAndRestorePreserveRgbaBytesWithoutGuestRam) {
    std::vector<uint8_t> bytes(13 * 9 * 4);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(i * 37 + 11);
    round_trip(vk::Format::eR8G8B8A8Uint, std::move(bytes));
}

TEST_F(GpuImageSnapshot, EndedSceneCheckpointExecutesQueriesWithoutConsumingOpenRecordingOrSceneFence) {
    auto owned = image(vk::Format::eR8G8B8A8Uint, vk::ImageUsageFlagBits::eColorAttachment);
    auto view = device->createImageViewUnique(vk::ImageViewCreateInfo{
        .image = *owned->handle, .viewType = vk::ImageViewType::e2D,
        .format = owned->image.format, .subresourceRange = vkutil::color_subresource_range});
    const vk::AttachmentDescription attachment{.format = owned->image.format,
        .samples = vk::SampleCountFlagBits::e1, .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore, .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eColorAttachmentOptimal, .finalLayout = vk::ImageLayout::eColorAttachmentOptimal};
    const vk::AttachmentReference reference{0, vk::ImageLayout::eColorAttachmentOptimal};
    const vk::SubpassDescription subpass{.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .colorAttachmentCount = 1, .pColorAttachments = &reference};
    auto pass = device->createRenderPassUnique(vk::RenderPassCreateInfo{
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass});
    const vk::ImageView raw_view = *view;
    auto framebuffer = device->createFramebufferUnique(vk::FramebufferCreateInfo{
        .renderPass = *pass, .attachmentCount = 1, .pAttachments = &raw_view, .width = 13, .height = 9, .layers = 1});
    auto queries = device->createQueryPoolUnique(vk::QueryPoolCreateInfo{.queryType = vk::QueryType::eOcclusion, .queryCount = 1});
    auto result = device->createBufferUnique(vk::BufferCreateInfo{.size = 4, .usage = vk::BufferUsageFlagBits::eTransferDst});
    const auto requirements = device->getBufferMemoryRequirements(*result);
    const auto properties = context.physical_device.getMemoryProperties();
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i))
            && (properties.memoryTypes[i].propertyFlags & (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent))
                == (vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)) { type = i; break; }
    ASSERT_NE(type, UINT32_MAX);
    auto memory = device->allocateMemoryUnique(vk::MemoryAllocateInfo{.allocationSize = requirements.size, .memoryTypeIndex = type});
    device->bindBufferMemory(*result, *memory, 0);
    auto *mapped = static_cast<uint32_t *>(device->mapMemory(*memory, 0, requirements.size));
    *mapped = 0xa5a5a5a5;
    auto commands = device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{
        .commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 3});
    const vk::CommandBuffer pre = *commands[0], draw = *commands[1], fresh = *commands[2];
    pre.begin(vk::CommandBufferBeginInfo{});
    owned->image.transition_to_discard(pre, vkutil::ImageLayout::ColorAttachment);
    pre.end();
    draw.begin(vk::CommandBufferBeginInfo{});
    draw.resetQueryPool(*queries, 0, 1);
    const vk::ClearValue clear{.color = {.uint32 = std::array<uint32_t, 4>{13, 29, 61, 255}}};
    draw.beginRenderPass(vk::RenderPassBeginInfo{.renderPass = *pass, .framebuffer = *framebuffer,
        .renderArea = {{0, 0}, {13, 9}}, .clearValueCount = 1, .pClearValues = &clear}, vk::SubpassContents::eInline);
    draw.beginQuery(*queries, 0, {});
    draw.endQuery(*queries, 0);
    draw.endRenderPass();
    draw.copyQueryPoolResults(*queries, 0, 1, *result, 0, 4, vk::QueryResultFlagBits::eWait);
    owned->image.transition_to(draw, vkutil::ImageLayout::TransferSrc);
    draw.end();
    fresh.begin(vk::CommandBufferBeginInfo{}); // Empty segment stays recording across checkpoint capture.
    auto scene_fence = device->createFenceUnique({});
    std::vector<vk::CommandBuffer> pending{pre, draw};
    ASSERT_TRUE(renderer::vulkan::submit_ended_checkpoint_segments(*device, context.queue, pending, error)) << error;
    EXPECT_TRUE(pending.empty());
    EXPECT_EQ(*mapped, 0u); // Actual occlusion result overwrote the sentinel.
    EXPECT_EQ(device->getFenceStatus(*scene_fence), vk::Result::eNotReady);
    vkutil::ImageSnapshot saved;
    ASSERT_TRUE(vkutil::capture_image_snapshot(context, owned->image, saved, error)) << error;
    ASSERT_EQ(saved.bytes.size(), 13u * 9u * 4u);
    for (size_t i = 0; i < saved.bytes.size(); i += 4) {
        EXPECT_EQ(saved.bytes[i], 13); EXPECT_EQ(saved.bytes[i+1], 29);
        EXPECT_EQ(saved.bytes[i+2], 61); EXPECT_EQ(saved.bytes[i+3], 255);
    }
    ASSERT_TRUE(renderer::vulkan::submit_ended_checkpoint_segments(*device, context.queue, pending, error));
    EXPECT_EQ(device->getFenceStatus(*scene_fence), vk::Result::eNotReady);
    fresh.end(); // Checkpoint never ended/reset/submitted the open segment.
    context.queue.submit(vk::SubmitInfo{.commandBufferCount = 1, .pCommandBuffers = &fresh}, *scene_fence);
    EXPECT_EQ(device->waitForFences(*scene_fence, true, UINT64_MAX), vk::Result::eSuccess);
    device->unmapMemory(*memory);
    result.reset(); // Destroy the bound buffer before its backing memory.
    pending = {pre, vk::CommandBuffer{}};
    const auto original = pending;
    EXPECT_FALSE(renderer::vulkan::submit_ended_checkpoint_segments(*device, context.queue, pending, error));
    EXPECT_EQ(pending, original);
}

TEST_F(GpuImageSnapshot, RawSixteenBitImagePreservesEveryBitPattern) {
    std::vector<uint8_t> bytes(13 * 9 * 8);
    for (size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(i * 53 + 0xFF);
    round_trip(vk::Format::eR16G16B16A16Uint, std::move(bytes));
}

TEST_F(GpuImageSnapshot, DepthAndStencilAreCapturedAsSeparatePlanes) {
    std::vector<uint8_t> bytes(13 * 9 * 5);
    for (size_t i = 0; i < 13 * 9; ++i) {
        const float depth = float(i) / float(13 * 9);
        std::memcpy(bytes.data() + i * 4, &depth, 4);
        bytes[13 * 9 * 4 + i] = static_cast<uint8_t>(i * 7);
    }
    round_trip(vk::Format::eD32SfloatS8Uint, std::move(bytes));
}

TEST_F(GpuImageSnapshot, InvalidImageMetadataIsRejectedBeforeGpuAccess) {
    vkutil::ImageSnapshot snapshot{13, 9, vk::Format::eR8G8B8A8Uint, vkutil::ImageLayout::TransferSrc, {}};
    EXPECT_FALSE(vkutil::validate_image_snapshot(snapshot, error));
    snapshot.bytes.resize(13 * 9 * 4);
    EXPECT_TRUE(vkutil::validate_image_snapshot(snapshot, error));
    snapshot.layout = static_cast<vkutil::ImageLayout>(999);
    EXPECT_FALSE(vkutil::validate_image_snapshot(snapshot, error));
    snapshot.layout = vkutil::ImageLayout::TransferSrc;
    snapshot.width = UINT32_MAX;
    EXPECT_FALSE(vkutil::validate_image_snapshot(snapshot, error));
}

TEST(ImageSnapshotMetadata, RejectsIncompatibleAspectLayoutsAndMultiplanarCopies) {
    std::string error;
    vkutil::ImageSnapshot color{13, 9, vk::Format::eR8G8B8A8Uint,
        vkutil::ImageLayout::DepthStencilAttachment, std::vector<uint8_t>(13 * 9 * 4)};
    EXPECT_FALSE(vkutil::validate_image_snapshot(color, error));
    color.layout = vkutil::ImageLayout::DepthStencilReadOnly;
    EXPECT_FALSE(vkutil::validate_image_snapshot(color, error));
    vkutil::ImageSnapshot depth{13, 9, vk::Format::eD32Sfloat,
        vkutil::ImageLayout::ColorAttachment, std::vector<uint8_t>(13 * 9 * 4)};
    EXPECT_FALSE(vkutil::validate_image_snapshot(depth, error));
    depth.layout = vkutil::ImageLayout::ColorAttachmentReadWrite;
    EXPECT_FALSE(vkutil::validate_image_snapshot(depth, error));
    vkutil::ImageSnapshot planar{13, 9, vk::Format::eG8B8R83Plane444Unorm,
        vkutil::ImageLayout::TransferSrc, std::vector<uint8_t>(13 * 9 * 3)};
    EXPECT_FALSE(vkutil::validate_image_snapshot(planar, error));
}

TEST_F(GpuImageSnapshot, UndefinedRestoreRequiresFreshDestination) {
    auto owned = image(vk::Format::eR8G8B8A8Uint);
    vkutil::ImageSnapshot undefined{13, 9, owned->image.format, vkutil::ImageLayout::Undefined, {}};
    ASSERT_TRUE(vkutil::restore_image_snapshot(context, owned->image, undefined, error)) << error;
    vkutil::ImageSnapshot defined{13, 9, owned->image.format, vkutil::ImageLayout::TransferSrc,
        std::vector<uint8_t>(13 * 9 * 4, 0x37)};
    ASSERT_TRUE(vkutil::restore_image_snapshot(context, owned->image, defined, error)) << error;
    EXPECT_FALSE(vkutil::restore_image_snapshot(context, owned->image, undefined, error));
    vkutil::ImageSnapshot after;
    ASSERT_TRUE(vkutil::capture_image_snapshot(context, owned->image, after, error)) << error;
    EXPECT_EQ(after.layout, defined.layout);
    EXPECT_EQ(after.bytes, defined.bytes);
}

TEST_F(GpuImageSnapshot, CapturesDepthStencilAttachmentStoresAndRestoresAttachmentLayout) {
    auto owned = image(vk::Format::eD32SfloatS8Uint, vk::ImageUsageFlagBits::eDepthStencilAttachment);
    const vk::ImageSubresourceRange range{.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil,
        .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
    auto view = device->createImageViewUnique(vk::ImageViewCreateInfo{.image = owned->image.image,
        .viewType = vk::ImageViewType::e2D, .format = owned->image.format, .subresourceRange = range});
    const vk::AttachmentDescription attachment{.format = owned->image.format, .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear, .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eClear, .stencilStoreOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal};
    const vk::AttachmentReference reference{.attachment = 0, .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal};
    const vk::SubpassDescription subpass{.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
        .pDepthStencilAttachment = &reference};
    auto render_pass = device->createRenderPassUnique(vk::RenderPassCreateInfo{
        .attachmentCount = 1, .pAttachments = &attachment, .subpassCount = 1, .pSubpasses = &subpass});
    auto framebuffer = device->createFramebufferUnique(vk::FramebufferCreateInfo{.renderPass = *render_pass,
        .attachmentCount = 1, .pAttachments = &*view, .width = 13, .height = 9, .layers = 1});
    auto commands = device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{
        .commandPool = *pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
    auto &command = commands.front();
    command->begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    owned->image.transition_to(*command, vkutil::ImageLayout::DepthStencilAttachment, range);
    const vk::ClearValue clear{.depthStencil = {.depth = 0.375f, .stencil = 0x6d}};
    command->beginRenderPass(vk::RenderPassBeginInfo{.renderPass = *render_pass, .framebuffer = *framebuffer,
        .renderArea = {.offset = {0, 0}, .extent = {13, 9}}, .clearValueCount = 1, .pClearValues = &clear},
        vk::SubpassContents::eInline);
    command->endRenderPass();
    command->end();
    // Capture follows this submission without waiting for it on the host. Its
    // barrier must cover render-pass attachment stores, not just transfer writes.
    const vk::SubmitInfo submit{.commandBufferCount = 1, .pCommandBuffers = &*command};
    context.queue.submit(submit, {});
    vkutil::ImageSnapshot snapshot;
    ASSERT_TRUE(vkutil::capture_image_snapshot(context, owned->image, snapshot, error)) << error;
    EXPECT_EQ(owned->image.layout, vkutil::ImageLayout::DepthStencilAttachment);
    ASSERT_EQ(snapshot.bytes.size(), 13 * 9 * 5);
    for (size_t i = 0; i < 13 * 9; ++i) {
        float depth = 0;
        std::memcpy(&depth, snapshot.bytes.data() + i * 4, 4);
        EXPECT_EQ(depth, 0.375f);
        EXPECT_EQ(snapshot.bytes[13 * 9 * 4 + i], 0x6d);
    }
    auto restored = image(owned->image.format, vk::ImageUsageFlagBits::eDepthStencilAttachment);
    ASSERT_TRUE(vkutil::restore_image_snapshot(context, restored->image, snapshot, error)) << error;
    EXPECT_EQ(restored->image.layout, vkutil::ImageLayout::DepthStencilAttachment);
    vkutil::ImageSnapshot after;
    ASSERT_TRUE(vkutil::capture_image_snapshot(context, restored->image, after, error)) << error;
    EXPECT_EQ(after.bytes, snapshot.bytes);
}
} // namespace
