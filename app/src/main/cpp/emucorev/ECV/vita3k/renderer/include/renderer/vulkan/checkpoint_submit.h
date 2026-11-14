#pragma once
#include <vkutil/vkutil.h>
#include <algorithm>
#include <string>
#include <vector>

namespace renderer::vulkan {
// These buffers are already ended renderer work. The open recording and the
// guest scene's reserved completion fence are deliberately not arguments.
inline bool submit_ended_checkpoint_segments(vk::Device device, vk::Queue queue,
    std::vector<vk::CommandBuffer> &segments, std::string &error) {
    if (segments.empty()) return true;
    if (!device || !queue || std::any_of(segments.begin(), segments.end(),
            [](vk::CommandBuffer command) { return !command; })) {
        error = "invalid ended Vulkan checkpoint submission";
        return false;
    }
    try {
        auto fence = device.createFenceUnique({});
        vk::SubmitInfo submit;
        submit.setCommandBuffers(segments);
        queue.submit(submit, *fence);
        if (device.waitForFences(*fence, true, UINT64_MAX) != vk::Result::eSuccess) {
            error = "ended Vulkan checkpoint submission did not complete";
            return false;
        }
        // Clear only after successful completion; creation/submission failures
        // leave the original physical continuation available to the caller.
        segments.clear();
        return true;
    } catch (const std::exception &exception) {
        error = std::string("ended Vulkan checkpoint submission failed: ") + exception.what();
        return false;
    }
}
}
