// Capability layer / strategy table (renderer/vulkan/gpu_caps.h): every path is picked from caps, never from a vendor, and has a
// correct fallback.
#include <renderer/vulkan/gpu_caps.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan;

namespace {
GpuCaps tiler_caps() {
    GpuCaps c;
    c.sync2 = true;
    c.timeline_semaphore = true;
    c.roaa_color = true;
    c.lazily_allocated_memory = true;
    c.dynamic_rendering_local_read = true;
    c.depth_stencil_resolve = true;
    c.ds_resolve_depth_modes = 1 | 8;
    c.framebuffer_color_sample_counts = c.framebuffer_depth_sample_counts = 1 | 2 | 4;
    c.max_per_stage_input_attachments = 8;
    return c;
}
Implemented everything() {
    Implemented i;
    i.timeline = i.lazy_memory = i.fbfetch_local_read = i.fbfetch_copy = i.ds_resolve = true;
    return i;
}
} // namespace

TEST(GpuStrategy, RoaaTilerPicksTheTopRungAndSync2) {
    const Strategy s = choose_strategy(tiler_caps(), {}, everything());
    EXPECT_EQ(s.barriers, BarrierStyle::Sync2);
    EXPECT_EQ(s.submit, SubmitTracking::TimelineSemaphore);
    EXPECT_EQ(s.transient, TransientMemory::Lazy);
    EXPECT_EQ(s.fbfetch, FbFetchPath::RasterOrderAttachmentAccess);
    EXPECT_FALSE(s.use_shader_interlock);
    EXPECT_EQ(s.msaa_resolve, MsaaResolvePath::ResolveAttachment);
}

TEST(GpuStrategy, LadderFallsThroughRoaaLocalReadInputAttachmentCopy) {
    GpuCaps c = tiler_caps();
    c.roaa_color = false;
    EXPECT_EQ(choose_strategy(c, {}, everything()).fbfetch, FbFetchPath::DynamicRenderingLocalRead);
    c.dynamic_rendering_local_read = false;
    EXPECT_EQ(choose_strategy(c, {}, everything()).fbfetch, FbFetchPath::InputAttachmentBarrier);
    c.max_per_stage_input_attachments = 0;
    EXPECT_EQ(choose_strategy(c, {}, everything()).fbfetch, FbFetchPath::CopySnapshot);
}

TEST(GpuStrategy, DefaultImplementationTableNeverSelectsUnimplementedPaths) {
    // today: the local-read and generic copy fbfetch rungs have no implementation (the renderer is VkRenderPass based, local-read needs
    // dynamic rendering); timeline submit tracking, lazy transient attachments and the ds-resolve plan do
    const Strategy s = choose_strategy(tiler_caps());
    EXPECT_EQ(s.fbfetch, FbFetchPath::RasterOrderAttachmentAccess);
    EXPECT_EQ(s.submit, SubmitTracking::TimelineSemaphore);
    EXPECT_EQ(s.transient, TransientMemory::Lazy);
    EXPECT_EQ(s.msaa_resolve, MsaaResolvePath::ResolveAttachment);
    Implemented none;
    none.timeline = none.lazy_memory = none.ds_resolve = false;
    const Strategy t = choose_strategy(tiler_caps(), {}, none);
    EXPECT_EQ(t.submit, SubmitTracking::Fences);
    EXPECT_EQ(t.transient, TransientMemory::Normal);
    EXPECT_EQ(t.msaa_resolve, MsaaResolvePath::ResolveAttachmentColorOnly);
    GpuCaps c = tiler_caps();
    c.roaa_color = false;
    EXPECT_EQ(choose_strategy(c).fbfetch, FbFetchPath::InputAttachmentBarrier); // not the unimplemented local-read
    ForcedPaths forced_timeline_off;
    forced_timeline_off.fences = true;
    EXPECT_EQ(choose_strategy(tiler_caps(), forced_timeline_off).submit, SubmitTracking::Fences);
    ForcedPaths forced_normal;
    forced_normal.normal_memory = true;
    EXPECT_EQ(choose_strategy(tiler_caps(), forced_normal).transient, TransientMemory::Normal);
}

TEST(GpuStrategy, DesktopInterlockOnlyWhenNoBetterRungExists) {
    GpuCaps c;
    c.shader_interlock = true;
    c.max_per_stage_input_attachments = 4;
    Strategy s = choose_strategy(c);
    EXPECT_TRUE(s.use_shader_interlock);
    EXPECT_EQ(s.fbfetch, FbFetchPath::InputAttachmentBarrier);
    c.roaa_color = true;
    s = choose_strategy(c);
    EXPECT_FALSE(s.use_shader_interlock); // ROAA wins over interlock (existing renderer rule)
}

TEST(GpuStrategy, NoSync2MeansLegacyBarriers) {
    GpuCaps c = tiler_caps();
    c.sync2 = false;
    EXPECT_EQ(choose_strategy(c, {}, everything()).barriers, BarrierStyle::Legacy);
}

TEST(GpuStrategy, SingleSampleOnlyDriverHasNoResolvePath) {
    GpuCaps c;
    EXPECT_EQ(choose_strategy(c, {}, everything()).msaa_resolve, MsaaResolvePath::None);
}

TEST(GpuStrategy, NoDepthResolveExtensionUsesTheColourOnlyPathWithAShaderFallbackForDepth) {
    GpuCaps c = tiler_caps();
    c.depth_stencil_resolve = false;
    EXPECT_EQ(choose_strategy(c, {}, everything()).msaa_resolve, MsaaResolvePath::ResolveAttachmentColorOnly);
    c = tiler_caps();
    c.ds_resolve_depth_modes = 8; // MAX only, no SAMPLE_ZERO: the first-sample rule needs the fallback
    EXPECT_EQ(choose_strategy(c, {}, everything()).msaa_resolve, MsaaResolvePath::ResolveAttachmentColorOnly);
}

TEST(GpuStrategy, ForcedPathsAreHonouredWhenSupportedAndRejectedOtherwise) {
    GpuCaps c = tiler_caps();
    ForcedPaths f;
    f.legacy_barriers = true;
    f.input_attachment = true;
    f.fences = true;
    Strategy s = choose_strategy(c, f, everything());
    EXPECT_EQ(s.barriers, BarrierStyle::Legacy);
    EXPECT_EQ(s.fbfetch, FbFetchPath::InputAttachmentBarrier);
    EXPECT_EQ(s.submit, SubmitTracking::Fences);
    EXPECT_FALSE(s.force_rejected);

    c.dynamic_rendering_local_read = false;
    ForcedPaths lr;
    lr.local_read = true;
    s = choose_strategy(c, lr, everything());
    EXPECT_TRUE(s.force_rejected);
    EXPECT_EQ(s.fbfetch, FbFetchPath::RasterOrderAttachmentAccess); // fell back to the normal ladder

    ForcedPaths roaa;
    roaa.roaa = true;
    GpuCaps none;
    none.max_per_stage_input_attachments = 4;
    s = choose_strategy(none, roaa, everything());
    EXPECT_TRUE(s.force_rejected);
    EXPECT_EQ(s.fbfetch, FbFetchPath::InputAttachmentBarrier);
}

TEST(GpuStrategy, ForceFlagNamesRoundTrip) {
    for (const std::string_view name : force_path_names) {
        ForcedPaths f;
        EXPECT_TRUE(apply_force_name(f, name)) << name;
    }
    ForcedPaths f;
    EXPECT_FALSE(apply_force_name(f, "adreno-workaround")); // no vendor names exist
}

TEST(GpuStrategy, DescribeLineHasStableKeys) {
    const std::string d = describe(tiler_caps(), choose_strategy(tiler_caps()));
    for (const char *key : { "barriers=", "submit=", "transient=", "fbfetch=", "interlock=", "msaa_resolve=", "ds_resolve=", "load_store=", "forced_rejected=" })
        EXPECT_NE(d.find(key), std::string::npos) << key;
}
