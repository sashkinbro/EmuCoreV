#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace renderer::vulkan {

template <std::size_t FrameCount>
class FrameCommandBufferCursor {
public:
    std::size_t acquire(std::size_t frame_index, uint64_t frame_epoch) {
        if (frame_epochs[frame_index] != frame_epoch) {
            frame_epochs[frame_index] = frame_epoch;
            next_indices[frame_index] = 0;
        }
        return next_indices[frame_index]++;
    }

private:
    std::array<uint64_t, FrameCount> frame_epochs{};
    std::array<std::size_t, FrameCount> next_indices{};
};

} // namespace renderer::vulkan
