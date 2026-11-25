// Pure render-pass policy (renderer/vulkan/render_pass_policy.h): depth store derivation and depth/stencil resolve planning.
#include <renderer/vulkan/render_pass_policy.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan;

TEST(RenderPassPolicy, DepthIsStoredUnlessProvablyDead) {
    EXPECT_TRUE(derive_depth_store({ true, false, false }));
    EXPECT_TRUE(derive_depth_store({ false, true, false })); // backed by a guest surface: a later reader may exist
    EXPECT_TRUE(derive_depth_store({ false, false, true })); // the pass may restart and LOAD what it stored
    EXPECT_FALSE(derive_depth_store({ false, false, false })); // unbacked, last instance of the scene, game does not ask: dead
    // the struct default is the conservative answer
    EXPECT_TRUE(derive_depth_store(DepthStoreInput{}));
}

TEST(RenderPassPolicy, ColourIsLoadedOnlyWhenBackedBySurface) {
    EXPECT_TRUE(derive_color_load(true));
    EXPECT_FALSE(derive_color_load(false));
}

namespace {
GpuCaps ms_caps() {
    GpuCaps c;
    c.framebuffer_color_sample_counts = c.framebuffer_depth_sample_counts = 1 | 2 | 4;
    c.depth_stencil_resolve = true;
    c.ds_resolve_depth_modes = resolve_mode_sample_zero | resolve_mode_max;
    c.ds_resolve_stencil_modes = resolve_mode_sample_zero;
    return c;
}
} // namespace

TEST(RenderPassPolicy, SingleSampleNeedsNoResolve) {
    EXPECT_EQ(plan_ds_resolve(ms_caps(), 1, true).kind, DsResolveKind::None);
}

TEST(RenderPassPolicy, SampleZeroAttachmentResolveWhenTheDriverOffersIt) {
    const DsResolvePlan p = plan_ds_resolve(ms_caps(), 4, true);
    EXPECT_EQ(p.kind, DsResolveKind::ResolveAttachment);
    EXPECT_EQ(p.depth_mode, resolve_mode_sample_zero);
    EXPECT_EQ(p.stencil_mode, resolve_mode_sample_zero);
    EXPECT_EQ(plan_ds_resolve(ms_caps(), 4, false).stencil_mode, 0u);
}

TEST(RenderPassPolicy, NoExactModeMeansTheShaderCopyNeverAnApproximation) {
    GpuCaps c = ms_caps();
    c.ds_resolve_depth_modes = resolve_mode_max; // MAX would change the picture
    EXPECT_EQ(plan_ds_resolve(c, 4, true).kind, DsResolveKind::ShaderCopy);
    c = ms_caps();
    c.depth_stencil_resolve = false;
    EXPECT_EQ(plan_ds_resolve(c, 4, true).kind, DsResolveKind::ShaderCopy);
    c = ms_caps();
    c.ds_resolve_stencil_modes = resolve_mode_max; // stencil lacks SAMPLE_ZERO
    EXPECT_EQ(plan_ds_resolve(c, 4, true).kind, DsResolveKind::ShaderCopy);
    EXPECT_EQ(plan_ds_resolve(c, 4, false).kind, DsResolveKind::ResolveAttachment); // depth-only is still exact
}

TEST(RenderPassPolicy, AttachmentResolveCanBeForcedOff) {
    EXPECT_EQ(plan_ds_resolve(ms_caps(), 4, true, false).kind, DsResolveKind::ShaderCopy);
}

TEST(RenderPassPolicy, UnsupportedSampleCountHasNoPlan) {
    GpuCaps c = ms_caps();
    c.framebuffer_depth_sample_counts = 1;
    EXPECT_EQ(plan_ds_resolve(c, 4, true).kind, DsResolveKind::None);
}
