// Render-pass external dependencies for render targets that are sampled by later passes (surface cache
// SURF_COLOR / SURF_DEPTH views, no copy in between). Kept free of the pipeline cache so a host test can
// check the stage/access masks (app/src/test/core-host/render_pass_sync_tests.cpp).
#pragma once

#include <vkutil/vkutil.h>

#include <array>

namespace renderer::vulkan {

inline constexpr vk::PipelineStageFlags rt_attachment_write_stages = vk::PipelineStageFlagBits::eColorAttachmentOutput
    | vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
inline constexpr vk::AccessFlags rt_attachment_access = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite
    | vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite;
// every shader stage that can sample a render target: Vita vertex textures are sampled in the vertex stage (B132)
inline constexpr vk::PipelineStageFlags rt_sampling_stages = vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;

// [0] incoming: writes of earlier passes (attachment stores) -> this pass (attachment access, shader reads)
// [1] WAR: reads of earlier passes (vertex/fragment sampling, compute reinterpret, transfer cast copies of the
//     target) -> this pass' attachment load/clear (early fragment tests) and writes
// [2] outgoing: this pass' writes -> later sampling (vertex + fragment), copies and attachment use
inline std::array<vk::SubpassDependency, 3> render_target_dependencies(bool shader_interlock_with_color) {
    std::array<vk::SubpassDependency, 3> d{};
    d[0] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = rt_attachment_write_stages,
        .dstStageMask = rt_attachment_write_stages | rt_sampling_stages,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
        .dstAccessMask = rt_attachment_access | vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eInputAttachmentRead
    };
    d[1] = {
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = rt_sampling_stages | vk::PipelineStageFlagBits::eComputeShader | vk::PipelineStageFlagBits::eTransfer,
        // depth load/clear and early depth writes happen in the early fragment tests stage
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eEarlyFragmentTests
            | vk::PipelineStageFlagBits::eLateFragmentTests,
        .srcAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
        .dstAccessMask = rt_attachment_access
    };
    if (shader_interlock_with_color)
        d[1].srcAccessMask |= vk::AccessFlagBits::eShaderWrite;
    d[2] = {
        .srcSubpass = 0,
        .dstSubpass = VK_SUBPASS_EXTERNAL,
        .srcStageMask = rt_attachment_write_stages | vk::PipelineStageFlagBits::eFragmentShader,
        .dstStageMask = rt_attachment_write_stages | rt_sampling_stages | vk::PipelineStageFlagBits::eTransfer,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = rt_attachment_access | vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eInputAttachmentRead
            | vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eTransferWrite
    };
    return d;
}

} // namespace renderer::vulkan
