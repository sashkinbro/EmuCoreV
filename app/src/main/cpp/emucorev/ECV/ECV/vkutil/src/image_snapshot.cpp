#include <vkutil/image_snapshot.h>
#include <vkutil/vkutil.h>

#include <vulkan/vulkan_format_traits.hpp>

#include <cstring>
#include <limits>
#include <optional>

namespace vkutil {
namespace {
constexpr uint64_t max_image_bytes = 256ull << 20;

struct CopyLayout {
    uint64_t size = 0;
    vk::ImageAspectFlags aspects;
    std::vector<vk::BufferImageCopy> regions;
};

std::optional<CopyLayout> copy_layout(uint32_t width, uint32_t height, vk::Format format) {
    if (!width || !height || width > 65535 || height > 65535 || vk::planeCount(format) != 1)
        return {};
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    uint32_t depth_bytes = 0;
    bool stencil = false;
    switch (format) {
    case vk::Format::eD16Unorm: depth_bytes = 2; break;
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat: depth_bytes = 4; break;
    case vk::Format::eD16UnormS8Uint: depth_bytes = 2; stencil = true; break;
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint: depth_bytes = 4; stencil = true; break;
    case vk::Format::eS8Uint: stencil = true; break;
    default: break;
    }
    CopyLayout layout;
    const auto add_plane = [&](vk::ImageAspectFlagBits aspect, uint32_t bytes) {
        layout.size = (layout.size + 3) & ~uint64_t{3};
        layout.regions.push_back(vk::BufferImageCopy{
            .bufferOffset = layout.size,
            .imageSubresource = {.aspectMask = aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
            .imageExtent = {.width = width, .height = height, .depth = 1}});
        layout.aspects |= aspect;
        layout.size += pixels * bytes;
    };
    if (depth_bytes || stencil) {
        if (depth_bytes)
            add_plane(vk::ImageAspectFlagBits::eDepth, depth_bytes);
        if (stencil)
            add_plane(vk::ImageAspectFlagBits::eStencil, 1);
    } else {
        const uint32_t bytes = vk::blockSize(format);
        if (!bytes || vk::texelsPerBlock(format) != 1)
            return {};
        add_plane(vk::ImageAspectFlagBits::eColor, bytes);
    }
    if (!layout.size || layout.size > max_image_bytes)
        return {};
    return layout;
}

bool compatible_layout(ImageLayout layout, vk::ImageAspectFlags aspects) {
    switch (layout) {
    case ImageLayout::DepthStencilAttachment:
    case ImageLayout::DepthStencilReadOnly:
        return bool(aspects & (vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil));
    case ImageLayout::ColorAttachment:
    case ImageLayout::ColorAttachmentReadWrite:
        return aspects == vk::ImageAspectFlagBits::eColor;
    default:
        return static_cast<uint32_t>(layout) <= static_cast<uint32_t>(ImageLayout::DepthStencilReadOnly);
    }
}

bool transfer_snapshot(const ImageSnapshotDevice &context, Image &image,
    const CopyLayout &layout, uint8_t *readback, const uint8_t *source, ImageLayout final_layout,
    std::string &error) {
    const bool upload = source != nullptr;
    const ImageLayout original_layout = image.layout;
    try {
        // Memory precedes the buffer so reverse destruction releases the
        // bound buffer first. Coherent memory is optional on the device.
        vk::UniqueDeviceMemory memory;
        auto buffer = context.device.createBufferUnique(vk::BufferCreateInfo{
            .size = layout.size,
            .usage = upload ? vk::BufferUsageFlagBits::eTransferSrc : vk::BufferUsageFlagBits::eTransferDst,
            .sharingMode = vk::SharingMode::eExclusive});
        const auto requirements = context.device.getBufferMemoryRequirements(*buffer);
        const auto properties = context.physical_device.getMemoryProperties();
        uint32_t memory_type = UINT32_MAX;
        for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
            if ((requirements.memoryTypeBits & (1u << i))
                && (properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)) {
                memory_type = i;
                if (properties.memoryTypes[i].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent)
                    break;
            }
        }
        if (memory_type == UINT32_MAX) {
            error = "no host-visible image staging memory";
            return false;
        }
        memory = context.device.allocateMemoryUnique(vk::MemoryAllocateInfo{
            .allocationSize = requirements.size, .memoryTypeIndex = memory_type});
        context.device.bindBufferMemory(*buffer, *memory, 0);
        const bool coherent = bool(properties.memoryTypes[memory_type].propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent);
        if (upload) {
            void *mapped = context.device.mapMemory(*memory, 0, VK_WHOLE_SIZE);
            std::memcpy(mapped, source, layout.size);
            if (!coherent)
                context.device.flushMappedMemoryRanges(vk::MappedMemoryRange{.memory = *memory, .offset = 0, .size = VK_WHOLE_SIZE});
            context.device.unmapMemory(*memory);
        }
        auto commands = context.device.allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo{
            .commandPool = context.command_pool, .level = vk::CommandBufferLevel::ePrimary, .commandBufferCount = 1});
        auto &command = commands.front();
        command->begin(vk::CommandBufferBeginInfo{.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
        if (upload) {
            const vk::BufferMemoryBarrier ready{.srcAccessMask = vk::AccessFlagBits::eHostWrite,
                .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = *buffer, .offset = 0, .size = VK_WHOLE_SIZE};
            command->pipelineBarrier(vk::PipelineStageFlagBits::eHost, vk::PipelineStageFlagBits::eTransfer, {}, {}, ready, {});
        }
        const vk::ImageSubresourceRange range{.aspectMask = layout.aspects, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};
        image.transition_to(*command, upload ? ImageLayout::TransferDst : ImageLayout::TransferSrc, range);
        if (upload)
            command->copyBufferToImage(*buffer, image.image, vk::ImageLayout::eTransferDstOptimal, layout.regions);
        else
            command->copyImageToBuffer(image.image, vk::ImageLayout::eTransferSrcOptimal, *buffer, layout.regions);
        if (!upload) {
            const vk::BufferMemoryBarrier ready{.srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                .dstAccessMask = vk::AccessFlagBits::eHostRead,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .buffer = *buffer, .offset = 0, .size = VK_WHOLE_SIZE};
            command->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eHost, {}, {}, ready, {});
        }
        image.transition_to(*command, final_layout, range);
        command->end();
        auto fence = context.device.createFenceUnique({});
        const vk::SubmitInfo submit{.commandBufferCount = 1, .pCommandBuffers = &*command};
        context.queue.submit(submit, *fence);
        if (context.device.waitForFences(*fence, true, UINT64_MAX) != vk::Result::eSuccess) {
            error = "image snapshot fence did not complete";
            return false;
        }
        if (!upload) {
            const void *mapped = context.device.mapMemory(*memory, 0, VK_WHOLE_SIZE);
            if (!coherent)
                context.device.invalidateMappedMemoryRanges(vk::MappedMemoryRange{.memory = *memory, .offset = 0, .size = VK_WHOLE_SIZE});
            std::memcpy(readback, mapped, layout.size);
            context.device.unmapMemory(*memory);
        }
        return true;
    } catch (const std::exception &exception) {
        image.layout = original_layout;
        error = exception.what();
        return false;
    }
}
} // namespace

bool validate_image_snapshot(const ImageSnapshot &snapshot, std::string &error) {
    const auto layout = copy_layout(snapshot.width, snapshot.height, snapshot.format);
    if (!layout || !compatible_layout(snapshot.layout, layout->aspects)
        || (snapshot.layout == ImageLayout::Undefined
            ? !snapshot.bytes.empty() : snapshot.bytes.size() != layout->size)) {
        error = "invalid GPU image snapshot";
        return false;
    }
    return true;
}

bool capture_image_snapshot(const ImageSnapshotDevice &context, Image &image,
    ImageSnapshot &snapshot, std::string &error) {
    const auto layout = copy_layout(image.width, image.height, image.format);
    if (!image.image || !layout || !compatible_layout(image.layout, layout->aspects)) {
        error = "unsupported GPU snapshot image";
        return false;
    }
    ImageSnapshot staged{image.width, image.height, image.format, image.layout, {}};
    if (image.layout != ImageLayout::Undefined) {
        staged.bytes.resize(layout->size);
        if (!transfer_snapshot(context, image, *layout, staged.bytes.data(), nullptr, image.layout, error))
            return false;
    }
    snapshot = std::move(staged);
    return true;
}

bool restore_image_snapshot(const ImageSnapshotDevice &context, Image &image,
    const ImageSnapshot &snapshot, std::string &error) {
    if (!validate_image_snapshot(snapshot, error) || !image.image || image.width != snapshot.width
        || image.height != snapshot.height || image.format != snapshot.format) {
        if (error.empty())
            error = "GPU image does not match snapshot";
        return false;
    }
    if (snapshot.layout == ImageLayout::Undefined) {
        if (image.layout != ImageLayout::Undefined) {
            error = "undefined GPU snapshot requires a fresh undefined image";
            return false;
        }
        return true;
    }
    return transfer_snapshot(context, image, *copy_layout(snapshot.width, snapshot.height, snapshot.format),
        nullptr, snapshot.bytes.data(), snapshot.layout, error);
}
} // namespace vkutil
