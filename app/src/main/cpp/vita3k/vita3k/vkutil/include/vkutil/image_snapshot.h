#pragma once

#include <vkutil/objects.h>

#include <cstdint>
#include <string>
#include <vector>

namespace vkutil {

// GPU-native image bytes are independent of the guest surface's tiling,
// swizzle, CPU dirty state and mapped memory. Capturing them must not perform
// a surface writeback into the running guest.
struct ImageSnapshot {
    uint32_t width = 0;
    uint32_t height = 0;
    vk::Format format = vk::Format::eUndefined;
    ImageLayout layout = ImageLayout::Undefined;
    std::vector<uint8_t> bytes;
};

struct ImageSnapshotDevice {
    vk::PhysicalDevice physical_device;
    vk::Device device;
    vk::Queue queue;
    vk::CommandPool command_pool;
};

// The caller owns queue/pool synchronization and must submit prior render
// recording before capture. These operations wait for their own copy fence.
// Images must support the required transfer usage on this queue family.
// Undefined snapshots have no bytes and can only restore onto fresh undefined
// images; recreate initialized images before restoring such a snapshot.
bool capture_image_snapshot(const ImageSnapshotDevice &device, Image &image,
    ImageSnapshot &snapshot, std::string &error);
bool restore_image_snapshot(const ImageSnapshotDevice &device, Image &image,
    const ImageSnapshot &snapshot, std::string &error);
bool validate_image_snapshot(const ImageSnapshot &snapshot, std::string &error);

} // namespace vkutil
