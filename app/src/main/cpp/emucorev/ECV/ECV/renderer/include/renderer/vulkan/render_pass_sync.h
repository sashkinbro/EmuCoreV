// Render-pass external dependencies for render targets that are sampled by later passes (surface cache
// SURF_COLOR / SURF_DEPTH views, no copy in between). Kept free of the pipeline cache so a host test can
// check the stage/access masks (app/src/test/core-host/render_pass_sync_tests.cpp).
#pragma once

#include <vkutil/sync_tracker.h>
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

// ---- tracker view of a render pass (docs/gpu-sync-tracker.md, "Render passes") ------------------------------------------------
// The pass' external dependencies above are the barriers the tracker relies on. The tracker is asked at vkCmdBeginRenderPass
// whether the tracked state of each attachment is COVERED by them (same stages, access, layout); only if it is not, an explicit
// barrier is recorded before the pass. At vkCmdEndRenderPass it records the writes and what dependency [2] made visible.
enum class PassAttachment { Color, ColorRaw, DepthStencil };

inline vkutil::sync::Stage to_stage2(vk::PipelineStageFlags f) {
    return vkutil::sync::Stage(static_cast<VkPipelineStageFlags2>(static_cast<VkPipelineStageFlags>(f)));
}
inline vkutil::sync::Access to_access2(vk::AccessFlags f) {
    return vkutil::sync::Access(static_cast<VkAccessFlags2>(static_cast<VkAccessFlags>(f)));
}

// `content_loaded`: the pass loads (does not clear / discard) the attachment, which for depth selects the pass' initialLayout
inline vkutil::sync::PassCoverage pass_coverage(PassAttachment kind, bool content_loaded) {
    const auto d = render_target_dependencies(false);
    vkutil::sync::PassCoverage c;
    c.raw_src_stages = to_stage2(d[0].srcStageMask);
    c.raw_src_access = to_access2(d[0].srcAccessMask);
    c.war_src_stages = to_stage2(d[1].srcStageMask);
    c.dst_stages = to_stage2(rt_attachment_write_stages);
    c.dst_access = to_access2(rt_attachment_access);
    c.initial_layout = !content_loaded ? vk::ImageLayout::eUndefined
        : (kind == PassAttachment::DepthStencil ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eGeneral);
    return c;
}

inline vk::ImageLayout pass_final_layout(PassAttachment kind) {
    return kind == PassAttachment::DepthStencil ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eGeneral;
}

inline void note_pass_end(vkutil::sync::State &st, PassAttachment kind) {
    const auto d = render_target_dependencies(false);
    // writers: attachment output / fragment tests, plus fragment shader storage writes (interlock / raw attachment)
    vkutil::sync::track_pass_end(st, pass_final_layout(kind), to_stage2(d[2].srcStageMask), to_access2(d[2].srcAccessMask),
        to_stage2(d[2].dstStageMask), to_access2(d[2].dstAccessMask));
}

// Where a GPU-side copy that is requested between scenes (depth transfer) must be recorded so it executes between the right scenes.
// A separate submission is only correct when nothing is open and nothing is queued behind an unsubmitted batch.
enum class GpuCopyRoute { RecordIntoOpenScene, QueueBehindPending, SeparateSubmit };
inline GpuCopyRoute gpu_copy_route(bool scene_recording, bool unsubmitted_scenes_queued) {
    if (scene_recording)
        return GpuCopyRoute::RecordIntoOpenScene;
    return unsubmitted_scenes_queued ? GpuCopyRoute::QueueBehindPending : GpuCopyRoute::SeparateSubmit;
}

} // namespace renderer::vulkan
