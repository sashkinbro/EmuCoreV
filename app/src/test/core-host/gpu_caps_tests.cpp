// Capability layer / strategy table (renderer/vulkan/gpu_caps.h): every path is picked from caps, never from a vendor, one path per tier,
// and the tier below exists only as the fallback for a driver without the feature.
#include <renderer/vulkan/gpu_caps.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan;

namespace {
GpuCaps tiler_caps() {
    GpuCaps c;
    c.identity = { 0x5143, 8, 0x80300 }; // a Qualcomm proprietary driver (vendor + VkDriverId, no model)
    c.sync2 = true;
    c.timeline_semaphore = true;
    c.roaa_color = true;
    c.lazily_allocated_memory = true;
    c.dynamic_rendering = true;
    c.dynamic_rendering_local_read = true;
    c.depth_stencil_resolve = true;
    c.ds_resolve_depth_modes = 1 | 8;
    c.framebuffer_color_sample_counts = c.framebuffer_depth_sample_counts = 1 | 2 | 4;
    c.max_per_stage_input_attachments = 8;
    return c;
}
GpuCaps render_pass_caps() {
    GpuCaps c = tiler_caps();
    c.dynamic_rendering = c.dynamic_rendering_local_read = false;
    return c;
}
} // namespace

TEST(GpuStrategy, EveryTierIsChosenByDefaultWhenTheDriverHasIt) {
    const Strategy s = choose_strategy(tiler_caps());
    EXPECT_EQ(s.barriers, BarrierStyle::Sync2);
    EXPECT_EQ(s.submit, SubmitTracking::TimelineSemaphore);
    EXPECT_EQ(s.transient, TransientMemory::Lazy);
    EXPECT_EQ(s.render_path, RenderPath::DynamicRendering);
    EXPECT_EQ(s.fbfetch, FbFetchPath::DynamicRenderingLocalRead);
    EXPECT_FALSE(s.use_shader_interlock);
    EXPECT_EQ(s.msaa_resolve, MsaaResolvePath::ResolveAttachment);
}

TEST(GpuStrategy, ADriverWithoutAFeatureGetsTheTierBelow) {
    GpuCaps c = tiler_caps();
    c.sync2 = false;
    c.timeline_semaphore = false;
    c.lazily_allocated_memory = false;
    const Strategy s = choose_strategy(c);
    EXPECT_EQ(s.barriers, BarrierStyle::Legacy);
    EXPECT_EQ(s.submit, SubmitTracking::Fences);
    EXPECT_EQ(s.transient, TransientMemory::Normal);
    // dynamic rendering alone is not enough: the fetch shaders need local read
    c = tiler_caps();
    c.dynamic_rendering_local_read = false;
    EXPECT_EQ(choose_strategy(c).render_path, RenderPath::RenderPass);
    c = tiler_caps();
    c.dynamic_rendering = false;
    EXPECT_EQ(choose_strategy(c).render_path, RenderPath::RenderPass);
}

TEST(GpuStrategy, VkRenderPassLadderWalksRoaaInputAttachmentCopy) {
    GpuCaps c = render_pass_caps();
    EXPECT_EQ(choose_strategy(c).fbfetch, FbFetchPath::RasterOrderAttachmentAccess);
    c.roaa_color = false;
    EXPECT_EQ(choose_strategy(c).fbfetch, FbFetchPath::InputAttachmentBarrier);
    c.input_attachment_self_dependency = false;
    EXPECT_EQ(choose_strategy(c).fbfetch, FbFetchPath::CopySnapshot);
    c.max_per_stage_input_attachments = 0; // nothing to read through: the last rung is still reported, the renderer cannot do better
    EXPECT_EQ(choose_strategy(c).fbfetch, FbFetchPath::CopySnapshot);
}

TEST(GpuStrategy, ForcingATierBelowWorksOnADeviceThatHasTheBetterOne) {
    ForcedPaths f;
    f.legacy_barriers = f.fences = f.normal_memory = f.render_pass = true;
    const Strategy s = choose_strategy(tiler_caps(), f);
    EXPECT_EQ(s.barriers, BarrierStyle::Legacy);
    EXPECT_EQ(s.submit, SubmitTracking::Fences);
    EXPECT_EQ(s.transient, TransientMemory::Normal);
    EXPECT_EQ(s.render_path, RenderPath::RenderPass);
    EXPECT_EQ(s.fbfetch, FbFetchPath::RasterOrderAttachmentAccess);
    EXPECT_FALSE(s.force_rejected);
}

TEST(GpuStrategy, ForcedFbfetchRungsBelongToTheRenderPassPath) {
    ForcedPaths f;
    f.input_attachment = true;
    Strategy s = choose_strategy(tiler_caps(), f); // dynamic path: its single rung wins, the request is reported
    EXPECT_EQ(s.render_path, RenderPath::DynamicRendering);
    EXPECT_EQ(s.fbfetch, FbFetchPath::DynamicRenderingLocalRead);
    EXPECT_TRUE(s.force_rejected);
    f.render_pass = true;
    s = choose_strategy(tiler_caps(), f);
    EXPECT_EQ(s.fbfetch, FbFetchPath::InputAttachmentBarrier);
    EXPECT_FALSE(s.force_rejected);
    ForcedPaths copy;
    copy.render_pass = copy.copy_snapshot = true;
    EXPECT_EQ(choose_strategy(tiler_caps(), copy).fbfetch, FbFetchPath::CopySnapshot);
    ForcedPaths roaa;
    roaa.render_pass = roaa.roaa = true;
    GpuCaps none = render_pass_caps();
    none.roaa_color = false;
    const Strategy rejected = choose_strategy(none, roaa); // the driver has no ROAA: the ladder runs and the request is reported
    EXPECT_TRUE(rejected.force_rejected);
    EXPECT_EQ(rejected.fbfetch, FbFetchPath::InputAttachmentBarrier);
}

TEST(GpuStrategy, DesktopInterlockOnlyWhenNoBetterRungExists) {
    GpuCaps c = render_pass_caps();
    c.roaa_color = false;
    c.shader_interlock = true;
    Strategy s = choose_strategy(c);
    EXPECT_TRUE(s.use_shader_interlock);
    EXPECT_EQ(s.fbfetch, FbFetchPath::InputAttachmentBarrier);
    c.roaa_color = true;
    s = choose_strategy(c);
    EXPECT_FALSE(s.use_shader_interlock); // ROAA wins over interlock
    c = tiler_caps();
    c.shader_interlock = true;
    EXPECT_FALSE(choose_strategy(c).use_shader_interlock); // the dynamic path has no interlock variant
}

TEST(GpuStrategy, MsaaResolvePlanFollowsTheResolveCaps) {
    GpuCaps c;
    EXPECT_EQ(choose_strategy(c).msaa_resolve, MsaaResolvePath::None);
    c = tiler_caps();
    c.depth_stencil_resolve = false;
    EXPECT_EQ(choose_strategy(c).msaa_resolve, MsaaResolvePath::ResolveAttachmentColorOnly);
    c = tiler_caps();
    c.ds_resolve_depth_modes = 8; // MAX only, no SAMPLE_ZERO: the exact rule needs the fallback
    EXPECT_EQ(choose_strategy(c).msaa_resolve, MsaaResolvePath::ResolveAttachmentColorOnly);
}

TEST(GpuStrategy, ForceFlagNamesAreExactlyTheTierForces) {
    for (const std::string_view name : force_path_names) {
        ForcedPaths f;
        EXPECT_TRUE(apply_force_name(f, name)) << name;
    }
    ForcedPaths f;
    for (const char *gone : { "sync2-barriers", "timeline", "lazy-memory", "local-read", "dynamic-rendering", "content-load-ops", "color-only-resolve", "adreno-workaround" })
        EXPECT_FALSE(apply_force_name(f, gone)) << gone; // paths that no longer exist have no flag
}

TEST(GpuStrategy, ExtendedDynamicStateFollowsTheDeviceAndCanBeForcedOff) {
    GpuCaps c = tiler_caps();
    c.extended_dynamic_state = true;
    EXPECT_TRUE(choose_strategy(c).extended_dynamic_state);
    ForcedPaths f;
    ASSERT_TRUE(apply_force_name(f, "base-dynamic-state"));
    EXPECT_FALSE(choose_strategy(c, f).extended_dynamic_state);
    c.extended_dynamic_state = false;
    EXPECT_FALSE(choose_strategy(c).extended_dynamic_state);
}

TEST(GpuStrategy, DescribeLineHasStableKeys) {
    const std::string d = describe(tiler_caps(), choose_strategy(tiler_caps()));
    for (const char *key : { "render_path=", "dyn_state=", "barriers=", "submit=", "transient=", "fbfetch=", "interlock=", "msaa_resolve=", "ds_resolve=", "depth_range=", "forced_rejected=" })
        EXPECT_NE(d.find(key), std::string::npos) << key;
}

// The Vita depth range rule (near: clamp, far: clip) gets the best tier the device can express
TEST(GpuStrategy, DepthRangeTierFollowsDepthClampAndClipDistances) {
    GpuCaps c = tiler_caps();
    c.depth_clamp = c.shader_clip_distance = true;
    EXPECT_EQ(choose_strategy(c).depth_range, DepthRangePath::ClampClipDistance);
    c.shader_clip_distance = false;
    EXPECT_EQ(choose_strategy(c).depth_range, DepthRangePath::FragmentDiscard);
    c.depth_clamp = false;
    c.shader_clip_distance = true;
    EXPECT_EQ(choose_strategy(c).depth_range, DepthRangePath::VertexClamp);

    c.depth_clamp = c.shader_clip_distance = true;
    ForcedPaths f;
    f.fragment_discard_depth = true;
    EXPECT_EQ(choose_strategy(c, f).depth_range, DepthRangePath::FragmentDiscard);
    f.vertex_clamp_depth = true;
    EXPECT_EQ(choose_strategy(c, f).depth_range, DepthRangePath::VertexClamp);
    EXPECT_NE(describe(c, choose_strategy(c)).find("depth_range=clamp+clip-distance"), std::string::npos);
}
