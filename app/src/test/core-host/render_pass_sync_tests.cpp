// B132 and the Killzone depth-of-field / post-pass suspicion: a render target (colour or depth) written by one
// pass and sampled by a later pass - in the fragment OR the vertex stage - must be covered by both the
// render-pass external dependencies and the image layout transition table. Pure mask checks, no GPU.
#include <renderer/vulkan/render_pass_sync.h>
#include <vkutil/vkutil.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan;
using vk::AccessFlagBits;
using vk::PipelineStageFlagBits;

namespace {
bool has(vk::PipelineStageFlags f, PipelineStageFlagBits b) { return bool(f & b); }
bool has(vk::AccessFlags f, AccessFlagBits b) { return bool(f & b); }
} // namespace

TEST(RenderPassSync, DepthAndColourWritesAreVisibleToVertexAndFragmentSamplingOfLaterPasses) {
    const auto d = render_target_dependencies(false);
    // outgoing: DEPTH_STENCIL_ATTACHMENT_WRITE / COLOR_ATTACHMENT_WRITE -> SHADER_READ in vertex + fragment
    EXPECT_TRUE(has(d[2].srcStageMask, PipelineStageFlagBits::eLateFragmentTests));
    EXPECT_TRUE(has(d[2].srcStageMask, PipelineStageFlagBits::eColorAttachmentOutput));
    EXPECT_TRUE(has(d[2].srcAccessMask, AccessFlagBits::eDepthStencilAttachmentWrite));
    EXPECT_TRUE(has(d[2].srcAccessMask, AccessFlagBits::eColorAttachmentWrite));
    EXPECT_TRUE(has(d[2].dstStageMask, PipelineStageFlagBits::eVertexShader));
    EXPECT_TRUE(has(d[2].dstStageMask, PipelineStageFlagBits::eFragmentShader));
    EXPECT_TRUE(has(d[2].dstStageMask, PipelineStageFlagBits::eTransfer));  // cast / depth copies
    EXPECT_TRUE(has(d[2].dstAccessMask, AccessFlagBits::eShaderRead));
    EXPECT_TRUE(has(d[2].dstAccessMask, AccessFlagBits::eTransferRead));
    // incoming: the same writes reach the next pass whatever stage reads them first
    EXPECT_TRUE(has(d[0].dstStageMask, PipelineStageFlagBits::eVertexShader));
    EXPECT_TRUE(has(d[0].dstStageMask, PipelineStageFlagBits::eFragmentShader));
    EXPECT_TRUE(has(d[0].srcAccessMask, AccessFlagBits::eDepthStencilAttachmentWrite));
}

TEST(RenderPassSync, SamplingOfATargetIsOrderedBeforeTheNextPassClearsOrWritesIt) {
    // WAR: the post pass samples depth (depth of field), the next scene's pass loads/clears that depth in EARLY_FRAGMENT_TESTS
    const auto d = render_target_dependencies(false);
    EXPECT_TRUE(has(d[1].srcStageMask, PipelineStageFlagBits::eVertexShader));
    EXPECT_TRUE(has(d[1].srcStageMask, PipelineStageFlagBits::eFragmentShader));
    EXPECT_TRUE(has(d[1].srcStageMask, PipelineStageFlagBits::eComputeShader));
    EXPECT_TRUE(has(d[1].srcStageMask, PipelineStageFlagBits::eTransfer));
    EXPECT_TRUE(has(d[1].dstStageMask, PipelineStageFlagBits::eEarlyFragmentTests));
    EXPECT_TRUE(has(d[1].dstStageMask, PipelineStageFlagBits::eLateFragmentTests));
    EXPECT_TRUE(has(d[1].dstStageMask, PipelineStageFlagBits::eColorAttachmentOutput));
    EXPECT_TRUE(has(d[1].dstAccessMask, AccessFlagBits::eDepthStencilAttachmentWrite));
    EXPECT_TRUE(has(d[1].dstAccessMask, AccessFlagBits::eDepthStencilAttachmentRead));
    EXPECT_TRUE(has(d[1].srcAccessMask, AccessFlagBits::eShaderRead));
    EXPECT_TRUE(has(d[1].srcAccessMask, AccessFlagBits::eTransferRead));
}

TEST(RenderPassSync, ShaderInterlockAddsTheShaderWriteHazardOnlyWhenRequested) {
    EXPECT_FALSE(has(render_target_dependencies(false)[1].srcAccessMask, AccessFlagBits::eShaderWrite));
    EXPECT_TRUE(has(render_target_dependencies(true)[1].srcAccessMask, AccessFlagBits::eShaderWrite));
}

TEST(RenderPassSync, EveryDependencyIsExternalToTheSingleSubpass) {
    const auto d = render_target_dependencies(false);
    EXPECT_EQ(d[0].srcSubpass, VK_SUBPASS_EXTERNAL);
    EXPECT_EQ(d[0].dstSubpass, 0u);
    EXPECT_EQ(d[1].srcSubpass, VK_SUBPASS_EXTERNAL);
    EXPECT_EQ(d[2].dstSubpass, VK_SUBPASS_EXTERNAL);
}

TEST(VkutilLayoutTable, SampledStorageAndDepthReadOnlyLayoutsCoverTheVertexShader) {
    using vkutil::ImageLayout;
    for (const ImageLayout layout : { ImageLayout::SampledImage, ImageLayout::StorageImage, ImageLayout::DepthStencilReadOnly }) {
        const auto m = vkutil::get_layout_barrier_masks(layout);
        EXPECT_TRUE(has(m.stages, PipelineStageFlagBits::eVertexShader)) << static_cast<int>(layout);
        EXPECT_TRUE(has(m.stages, PipelineStageFlagBits::eFragmentShader)) << static_cast<int>(layout);
        EXPECT_TRUE(has(m.access, AccessFlagBits::eShaderRead)) << static_cast<int>(layout);
    }
    // and the layouts that existed before keep their stages
    const auto sampled = vkutil::get_layout_barrier_masks(ImageLayout::SampledImage);
    EXPECT_TRUE(has(sampled.stages, PipelineStageFlagBits::eComputeShader));
}
