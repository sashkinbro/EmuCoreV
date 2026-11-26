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

#include <vkutil/sync_tracker.h>
#include <vkutil/vkutil.h>

#include <bit>

namespace vkutil {

void init(vma::Allocator vma_allocator);
void deinit();

struct Image {
    vma::Allocation allocation;
    vk::Image image{};
    vk::ImageView view{};
    vk::Sampler sampler{};

    uint32_t width;
    uint32_t height;
    vk::Format format;
    ImageLayout layout = ImageLayout::Undefined;
    // tracked synchronisation state (layout, last writer, readers, visibility), see sync_tracker.h
    sync::State sync;

    // should the existing image, view, sampler be destroyed when this image is destroyed?
    bool destroy_on_deletion = true;

    Image();
    Image(uint32_t width, uint32_t height, vk::Format format);
    ~Image();

    // allow the object to be moved
    Image(Image &&other) noexcept;
    Image &operator=(Image &&other) noexcept;

    // make sure an image is never copied
    Image(const Image &) = delete;
    Image &operator=(Image const &) = delete;

    void init_image(vk::ImageUsageFlags usage, vk::ComponentMapping mapping = default_comp_mapping, const vk::ImageCreateFlags image_create_flags = vk::ImageCreateFlags(), const void *pNext = nullptr,
        const vma::AllocationCreateInfo &alloc_info = vma_auto_alloc);
    // Attachment whose content never leaves the render pass (MSAA colour/depth that is resolved in-pass, depth that is dead after the
    // scene): TRANSIENT_ATTACHMENT usage in LAZILY_ALLOCATED memory when `lazy` (GpuCaps::lazily_allocated_memory chosen by the
    // strategy table), otherwise the same image in normal memory (storeOp must be DONT_CARE either way). `samples` is for the MSAA user.
    void init_transient_attachment(vk::ImageUsageFlags attachment_usage, bool lazy, vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1);
    // called by ~Image
    void destroy();

    void transition_to(vk::CommandBuffer buffer, ImageLayout new_layout, const vk::ImageSubresourceRange &range = color_subresource_range);
    // use this when you don't care about the former content of the image
    void transition_to_discard(vk::CommandBuffer buffer, ImageLayout new_layout, const vk::ImageSubresourceRange &range = color_subresource_range);

    // Tracked use of the whole image for a specific access (copy source, storage image, sampled in a given stage...).
    // The barrier goes into `batch`, which the caller flushes before the command that needs it.
    void use(sync::Batch &batch, sync::Stage stage, sync::Access access, vk::ImageLayout vk_layout, const vk::ImageSubresourceRange &range = color_subresource_range, bool discard = false);
    // Same, in the layout the image already is in (general-layout colour surfaces).
    void use_in_place(sync::Batch &batch, sync::Stage stage, sync::Access access, const vk::ImageSubresourceRange &range = color_subresource_range);
    // Record that a render pass wrote this attachment and left it in `final_layout` (see sync::track_pass_end).
    // Also the point where an unconditional layout field is kept in sync with the tracker.
    void sync_reset();
};

struct Buffer {
    vma::Allocation allocation;
    vk::Buffer buffer;

    vk::DeviceSize size = 0;
    sync::State sync;
    // only useful is buffer is host visible
    void *mapped_data = nullptr;

    bool destroy_on_deletion = true;

    Buffer();
    Buffer(vk::DeviceSize size);
    ~Buffer();

    // allow the object to be moved
    Buffer(Buffer &&other) noexcept;
    Buffer &operator=(Buffer &&other) noexcept;

    // make sure an image is never copied
    Buffer(const Buffer &) = delete;
    Buffer &operator=(Buffer const &) = delete;

    void init_buffer(vk::BufferUsageFlags usage_flags, const vma::AllocationCreateInfo &alloc_create_info = vma_auto_alloc);
    // tracked use of the whole buffer
    void use(sync::Batch &batch, sync::Stage stage, sync::Access access);
    // called by ~Image
    void destroy();
};

// structure that holds contiguous data
// if the end is reached, it starts back at the beginning
class RingBuffer {
protected:
    vkutil::Buffer buffer;
    vk::BufferUsageFlags usage;

    uint32_t cursor = ~0;
    uint32_t capacity;
    // number of times the cursor went back to the start
    uint32_t wraps = 0;

public:
    // any buffer alignment on vulkan is at most 256 on 99% of instances
    uint32_t alignment = 256;
    uint32_t data_offset = 0;

    explicit RingBuffer(vk::BufferUsageFlags usage, const size_t capacity);
    virtual ~RingBuffer() = default;
    virtual void create() = 0;

    // Allocate new data from ring buffer
    void allocate(const uint32_t data_size);
    // copy the content to the framebuffer
    // cmd_buffer may not be used
    virtual void copy(vk::CommandBuffer cmd_buffer, const uint32_t size, const void *data, const uint32_t offset = 0) = 0;

    // allocate then copy
    void allocate(vk::CommandBuffer cmd_buffer, const uint32_t data_size, const void *data) {
        allocate(data_size);
        copy(cmd_buffer, data_size, data);
    }

    vk::Buffer handle() const {
        return buffer.buffer;
    }

    uint32_t capacity_bytes() const {
        return capacity;
    }

    uint32_t cursor_bytes() const {
        return cursor;
    }

    uint32_t wrap_count() const {
        return wraps;
    }

    vk::DeviceSize buffer_bytes() const {
        return buffer.size;
    }
};

// RingBuffer allocated in the GPU memory, may not be accessible from the host
// updates are done with updateBuffer, so each data chunk should be small
// this is used for our uniform buffers
class LocalRingBuffer : public RingBuffer {
public:
    explicit LocalRingBuffer(vk::BufferUsageFlags usage, const size_t capacity)
        : RingBuffer(usage, capacity) {
    }
    void create() override;

    void copy(vk::CommandBuffer cmd_buffer, const uint32_t size, const void *data, const uint32_t offset = 0) override;
};

// RingBuffer using host visible memory
// Should be used for content that is only read a few times and not worth
// transferring to the GPU memory
class HostRingBuffer : public RingBuffer {
protected:
    bool is_coherent;

public:
    explicit HostRingBuffer(vk::BufferUsageFlags usage, const size_t capacity)
        : RingBuffer(usage, capacity) {
    }
    void create() override;

    void copy(vk::CommandBuffer cmd_buffer, const uint32_t size, const void *data, const uint32_t offset = 0) override;

    // reads `size` bytes at an absolute buffer offset (what a shader wrote there); only valid once the GPU work that wrote them has completed
    void read_back(const uint32_t absolute_offset, const uint32_t size, void *dst);

    // write straight into the last allocation (no intermediate copy), then call flush_written
    uint8_t *write_ptr() const {
        return static_cast<uint8_t *>(buffer.mapped_data) + data_offset;
    }
    void flush_written(const uint32_t size);
};

// Queue that contains GPU objects that are planned to be destroyed (deferred destruction)
class DestroyQueue {
private:
    vk::Device device;
    std::vector<uint64_t> destroy_list;

public:
    void init(vk::Device device);

    template <typename T>
    std::enable_if_t<vk::isVulkanHandleType<T>::value> add(T &vk_object) {
        if (!vk_object)
            return;

        destroy_list.push_back(static_cast<uint64_t>(T::objectType));
        destroy_list.push_back(std::bit_cast<uint64_t>(vk_object));
        vk_object = nullptr;
    }

    void add_image(Image &image);
    void add_buffer(Buffer &buffer);
    void add_cmd_buffer(vk::CommandBuffer cmd_buffer, vk::CommandPool cmd_pool);

    void destroy_objects();
};

} // namespace vkutil
