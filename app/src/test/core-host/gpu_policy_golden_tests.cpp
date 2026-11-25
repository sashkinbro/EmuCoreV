// Golden snapshot of every GPU-identity decision (docs/emucorex-renderer-lessons.md, pattern 2): a table of driver identities with the
// capabilities such a driver exposes, and the checked-in decision line for each (gpu_policy_golden.inc). A change of any selection rule
// shows up here as a diff of the line, so it is reviewed on purpose. Regenerate: run this test with ECV_GOLDEN_OUT=<path to the .inc>.
#include <renderer/vulkan/gpu_caps.h>

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace renderer::vulkan;

namespace {

struct Entry {
    const char *name;
    GpuCaps caps;
    ForcedPaths forced;
};

GpuCaps modern(GpuIdentity id) {
    GpuCaps c;
    c.identity = id;
    c.sync2 = true;
    c.timeline_semaphore = true;
    c.dynamic_rendering = true;
    c.dynamic_rendering_local_read = true;
    c.depth_stencil_resolve = true;
    c.ds_resolve_depth_modes = 1 | 8;
    c.ds_resolve_stencil_modes = 1;
    c.framebuffer_color_sample_counts = c.framebuffer_depth_sample_counts = c.framebuffer_stencil_sample_counts = 1 | 2 | 4 | 8;
    c.max_per_stage_input_attachments = 8;
    c.host_visible_coherent = c.host_visible_coherent_cached = true;
    return c;
}

std::vector<Entry> table() {
    std::vector<Entry> t;
    // Qualcomm proprietary, current driver: ROAA, sync2, lazy memory, local read
    GpuCaps qcom = modern({ 0x5143, 8, 0x80300 });
    qcom.roaa_color = true;
    qcom.lazily_allocated_memory = true;
    t.push_back({ "qcom-blob-modern", qcom, {} });
    ForcedPaths dyn;
    dyn.dynamic_rendering = true;
    t.push_back({ "qcom-blob-modern+force-dynamic-rendering", qcom, dyn });
    ForcedPaths legacy;
    legacy.legacy_barriers = true;
    legacy.fences = true;
    t.push_back({ "qcom-blob-modern+force-legacy-fences", qcom, legacy });
    ForcedPaths ia;
    ia.input_attachment = true;
    t.push_back({ "qcom-blob-modern+force-input-attachment", qcom, ia });
    // Qualcomm proprietary, old driver: Vulkan 1.1 style, no sync2, no ROAA, no timeline/dynamic rendering
    GpuCaps qcom_old;
    qcom_old.identity = { 0x5143, 8, 0x40100 };
    qcom_old.max_per_stage_input_attachments = 8;
    qcom_old.lazily_allocated_memory = true;
    t.push_back({ "qcom-blob-old", qcom_old, {} });
    // Mesa Turnip
    GpuCaps turnip = modern({ 0x5143, 18, 0x6000000 });
    turnip.roaa_color = true;
    turnip.lazily_allocated_memory = true;
    t.push_back({ "turnip", turnip, {} });
    // Arm proprietary with ROAA, and without
    GpuCaps arm = modern({ 0x13B5, 9, 0x10000 });
    arm.roaa_color = true;
    arm.lazily_allocated_memory = true;
    arm.dynamic_rendering = arm.dynamic_rendering_local_read = false;
    t.push_back({ "arm-blob-roaa", arm, {} });
    GpuCaps arm_plain = arm;
    arm_plain.roaa_color = false;
    t.push_back({ "arm-blob-no-roaa", arm_plain, {} });
    ForcedPaths roaa;
    roaa.roaa = true;
    t.push_back({ "arm-blob-no-roaa+force-roaa", arm_plain, roaa });
    // desktop drivers
    GpuCaps nv = modern({ 0x10DE, 4, 0x1000 });
    nv.shader_interlock = true;
    t.push_back({ "nvidia-interlock", nv, {} });
    GpuCaps radv = modern({ 0x1002, 3, 0x1000 });
    t.push_back({ "mesa-radv", radv, {} });
    GpuCaps intel = modern({ 0x8086, 6, 0x1000 });
    intel.shader_interlock = true;
    t.push_back({ "intel-mesa-interlock", intel, {} });
    // Mesa llvmpipe (the lab replay)
    GpuCaps lvp = modern({ 0x10005, 13, 0x1000 });
    lvp.roaa_color = true;
    t.push_back({ "llvmpipe", lvp, {} });
    t.push_back({ "llvmpipe+force-dynamic-rendering", lvp, dyn });
    // PowerVR proprietary, minimal
    GpuCaps pvr;
    pvr.identity = { 0x1010, 7, 0x1000 };
    pvr.max_per_stage_input_attachments = 4;
    pvr.framebuffer_color_sample_counts = pvr.framebuffer_depth_sample_counts = 1 | 4;
    t.push_back({ "imagination-minimal", pvr, {} });
    // a driver that does not identify itself (generic fallback): everything optional stays off
    GpuCaps unknown = modern({ 0x1234, 0, 1 });
    unknown.roaa_color = true;
    unknown.lazily_allocated_memory = true;
    t.push_back({ "unknown-driver-everything-exposed", unknown, {} });
    GpuCaps unknown_min;
    unknown_min.identity = { 0x4321, 0, 1 };
    t.push_back({ "unknown-driver-minimal", unknown_min, {} });
    return t;
}

const char *const kGolden[] = {
#include "gpu_policy_golden.inc"
};

} // namespace

TEST(GpuPolicyGolden, EveryIdentityDecisionMatchesTheCheckedInSnapshot) {
    const std::vector<Entry> entries = table();
    std::vector<std::string> actual;
    for (const Entry &e : entries)
        actual.push_back(std::string(e.name) + " => " + describe(e.caps, choose_strategy(e.caps, e.forced)));

    if (const char *out = std::getenv("ECV_GOLDEN_OUT")) {
        std::ofstream f(out, std::ios::binary | std::ios::trunc);
        for (const std::string &line : actual) {
            f << "    \"";
            for (const char ch : line)
                f << ((ch == '"' || ch == '\\') ? "\\" : "") << ch;
            f << "\",\n";
        }
        GTEST_SKIP() << "golden written to " << out;
    }

    ASSERT_EQ(actual.size(), sizeof(kGolden) / sizeof(kGolden[0])) << "the identity table and gpu_policy_golden.inc have different lengths";
    for (size_t i = 0; i < actual.size(); i++)
        EXPECT_EQ(actual[i], kGolden[i]) << "decision changed for identity #" << i << " (review it, then regenerate with ECV_GOLDEN_OUT)";
}

TEST(GpuPolicyGolden, NamedRulesAreKeyedByDriverNotModelAndApplyToTheRightFamilies) {
    GpuCaps c = modern({ 0x5143, 8, 0x80300 });
    const Strategy qcom = choose_strategy(c);
    EXPECT_EQ(qcom.family, DriverFamily::QualcommProprietary);
    EXPECT_TRUE(qcom.rule_effects & (1u << static_cast<unsigned>(RuleEffect::FbfetchNeedsMeasurement)));
    EXPECT_TRUE(qcom.rule_effects & (1u << static_cast<unsigned>(RuleEffect::PipelineCacheStrict)));
    EXPECT_FALSE(qcom.rule_effects & (1u << static_cast<unsigned>(RuleEffect::RoaaDestinationReadUnverified)));
    // the same vendor with another driver id (Turnip) matches none of the Qualcomm blob rules
    c.identity = { 0x5143, 18, 0x80300 };
    EXPECT_EQ(choose_strategy(c).rule_effects, 0u);
    c.identity = { 0x13B5, 9, 1 };
    EXPECT_TRUE(choose_strategy(c).rule_effects & (1u << static_cast<unsigned>(RuleEffect::RoaaDestinationReadUnverified)));
    // an unknown driver falls back to the generic policy: legacy barriers, nothing opt-in
    c.identity = { 0x9999, 0, 1 };
    const Strategy generic = choose_strategy(c);
    EXPECT_EQ(generic.family, DriverFamily::Unknown);
    EXPECT_EQ(generic.barriers, BarrierStyle::Legacy);
    EXPECT_EQ(generic.render_path, RenderPath::RenderPass);
    // ... unless the lab forces the path
    ForcedPaths f;
    f.sync2_barriers = true;
    EXPECT_EQ(choose_strategy(c, f).barriers, BarrierStyle::Sync2);
}

TEST(GpuPolicyGolden, DriverVersionRangesBoundTheRules) {
    // the rule table has no ranged rule yet; the matcher still has to honour bounds when one is added
    const DriverRule ranged{ "test", DriverFamily::QualcommProprietary, 100, 200, RuleEffect::FbfetchNeedsMeasurement, true };
    EXPECT_FALSE(rule_matches(ranged, { 0x5143, 8, 99 }));
    EXPECT_TRUE(rule_matches(ranged, { 0x5143, 8, 100 }));
    EXPECT_TRUE(rule_matches(ranged, { 0x5143, 8, 200 }));
    EXPECT_FALSE(rule_matches(ranged, { 0x5143, 8, 201 }));
    EXPECT_FALSE(rule_matches(ranged, { 0x5143, 18, 150 }));
}
