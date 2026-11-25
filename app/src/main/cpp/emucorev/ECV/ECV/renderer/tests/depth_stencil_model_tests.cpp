// Host tests of the depth/stencil model (P5): format mapping table, per-face depth bias record, two-sided split decision.
#include <renderer/vulkan/depth_stencil_model.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan::dsmodel;
using F = vk::Format;

namespace {
HostDsSupport support_of(std::initializer_list<F> formats) {
    HostDsSupport s;
    for (const F f : formats)
        s.set(f, true);
    return s;
}
const HostDsSupport all_formats = support_of({ F::eD16Unorm, F::eD32Sfloat, F::eS8Uint, F::eD16UnormS8Uint, F::eD24UnormS8Uint, F::eD32SfloatS8Uint });
// the common mobile set: D16, D32F, D24S8, D32S8 (no S8, no D16S8)
const HostDsSupport mobile = support_of({ F::eD16Unorm, F::eD32Sfloat, F::eD24UnormS8Uint, F::eD32SfloatS8Uint });
}

TEST(DsModelClass, GxmFormatsGroup) {
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_D16), DsClass::D16);
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_DF32), DsClass::DF32);
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M), DsClass::DF32);
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_S8), DsClass::S8);
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_DF32_S8), DsClass::DF32S8);
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M_S8), DsClass::DF32S8);
    EXPECT_EQ(ds_class(SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24), DsClass::S8D24);
    // a disabled/unknown format keeps both aspects (previous behaviour)
    const DsClass unknown = ds_class(static_cast<SceGxmDepthStencilFormat>(0));
    EXPECT_TRUE(gxm_has_depth(unknown));
    EXPECT_TRUE(gxm_has_stencil(unknown));
}

TEST(DsModelClass, Aspects) {
    EXPECT_TRUE(gxm_has_depth(DsClass::D16));
    EXPECT_FALSE(gxm_has_stencil(DsClass::D16));
    EXPECT_TRUE(gxm_has_depth(DsClass::DF32));
    EXPECT_FALSE(gxm_has_stencil(DsClass::DF32));
    EXPECT_FALSE(gxm_has_depth(DsClass::S8));
    EXPECT_TRUE(gxm_has_stencil(DsClass::S8));
    EXPECT_TRUE(gxm_has_depth(DsClass::DF32S8));
    EXPECT_TRUE(gxm_has_stencil(DsClass::DF32S8));
    EXPECT_TRUE(gxm_has_depth(DsClass::S8D24));
    EXPECT_TRUE(gxm_has_stencil(DsClass::S8D24));
}

TEST(DsModelFormat, EveryClassKeepsBothAspectsWhenAvailable) {
    // the tile hardware tests depth and stencil on chip for every scene (Killzone draws depth-tested scenes into an S8 surface)
    for (const HostDsSupport &s : { all_formats, mobile }) {
        for (int c = 0; c < static_cast<int>(DsClass::Count); c++) {
            const F f = select_host_format(static_cast<DsClass>(c), s);
            EXPECT_TRUE(host_has_depth(f)) << c;
            EXPECT_TRUE(host_has_stencil(f)) << c;
        }
    }
}

TEST(DsModelFormat, PrecisionOrder) {
    EXPECT_EQ(select_host_format(DsClass::D16, all_formats), F::eD16UnormS8Uint);
    EXPECT_EQ(select_host_format(DsClass::DF32, all_formats), F::eD32SfloatS8Uint);
    EXPECT_EQ(select_host_format(DsClass::DF32S8, all_formats), F::eD32SfloatS8Uint);
    EXPECT_EQ(select_host_format(DsClass::S8D24, all_formats), F::eD24UnormS8Uint);
    EXPECT_EQ(select_host_format(DsClass::S8, all_formats), F::eD24UnormS8Uint);
}

TEST(DsModelFormat, MobileSet) {
    // no D16S8 on the common mobile set: D16 takes the next smaller combined format
    EXPECT_EQ(select_host_format(DsClass::D16, mobile), F::eD24UnormS8Uint);
    EXPECT_EQ(select_host_format(DsClass::DF32, mobile), F::eD32SfloatS8Uint);
    EXPECT_EQ(select_host_format(DsClass::S8D24, mobile), F::eD24UnormS8Uint);
}

TEST(DsModelFormat, NoD32S8) {
    const HostDsSupport s = support_of({ F::eD16Unorm, F::eD32Sfloat, F::eD24UnormS8Uint });
    EXPECT_EQ(select_host_format(DsClass::DF32S8, s), F::eD24UnormS8Uint);
    EXPECT_EQ(select_host_format(DsClass::S8D24, s), F::eD24UnormS8Uint);
}

TEST(DsModelFormat, NoD24S8) {
    const HostDsSupport s = support_of({ F::eD16Unorm, F::eD32Sfloat, F::eD32SfloatS8Uint });
    EXPECT_EQ(select_host_format(DsClass::S8D24, s), F::eD32SfloatS8Uint);
    EXPECT_EQ(select_host_format(DsClass::D16, s), F::eD32SfloatS8Uint);
}

TEST(DsModelFormat, StencilIsOnlyLostWithoutAnyCombinedFormat) {
    const HostDsSupport s = support_of({ F::eD16Unorm, F::eD32Sfloat });
    EXPECT_EQ(select_host_format(DsClass::DF32S8, s), F::eD32Sfloat);
    EXPECT_EQ(select_host_format(DsClass::S8, s), F::eD32Sfloat);
    EXPECT_EQ(select_host_format(DsClass::D16, s), F::eD16Unorm);
    EXPECT_EQ(select_host_format(DsClass::D16, support_of({ F::eD16Unorm })), F::eD16Unorm);
}

TEST(DsModelFormat, D16S8IsTheLastCombinedFallback) {
    const HostDsSupport s = support_of({ F::eD16Unorm, F::eD16UnormS8Uint });
    EXPECT_EQ(select_host_format(DsClass::DF32S8, s), F::eD16UnormS8Uint);
    EXPECT_EQ(select_host_format(DsClass::S8, s), F::eD16UnormS8Uint);
    EXPECT_EQ(select_host_format(DsClass::D16, s), F::eD16UnormS8Uint);
}

TEST(DsModelFormat, NothingSupported) {
    const HostDsSupport none;
    for (int c = 0; c < static_cast<int>(DsClass::Count); c++)
        EXPECT_EQ(select_host_format(static_cast<DsClass>(c), none), F::eUndefined);
    EXPECT_EQ(select_combined_format(none), F::eUndefined);
}

TEST(DsModelFormat, CombinedFormat) {
    EXPECT_EQ(select_combined_format(all_formats), F::eD32SfloatS8Uint);
    EXPECT_EQ(select_combined_format(support_of({ F::eD24UnormS8Uint, F::eD32Sfloat })), F::eD24UnormS8Uint);
    EXPECT_EQ(select_combined_format(support_of({ F::eD16UnormS8Uint })), F::eD16UnormS8Uint);
}

TEST(DsModelFormat, ProbeFeatureTest) {
    using FF = vk::FormatFeatureFlagBits;
    EXPECT_TRUE(format_usable(FF::eDepthStencilAttachment | FF::eSampledImage));
    EXPECT_FALSE(format_usable(FF::eDepthStencilAttachment));
    EXPECT_FALSE(format_usable(FF::eSampledImage));
    EXPECT_FALSE(format_usable({}));
}

TEST(DsModelFormat, SupportBitsIgnoreUnknownFormats) {
    HostDsSupport s;
    s.set(F::eR8G8B8A8Unorm, true);
    EXPECT_EQ(s.bits, 0u);
    s.set(F::eD16Unorm, true);
    EXPECT_TRUE(s.has(F::eD16Unorm));
    s.set(F::eD16Unorm, false);
    EXPECT_FALSE(s.has(F::eD16Unorm));
}

TEST(DsModelFormat, HostAspects) {
    EXPECT_EQ(host_aspects(F::eD16Unorm), vk::ImageAspectFlagBits::eDepth);
    EXPECT_EQ(host_aspects(F::eD32Sfloat), vk::ImageAspectFlagBits::eDepth);
    EXPECT_EQ(host_aspects(F::eS8Uint), vk::ImageAspectFlagBits::eStencil);
    EXPECT_EQ(host_aspects(F::eD24UnormS8Uint), vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
    EXPECT_EQ(host_aspects(F::eD32SfloatS8Uint), vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil);
    EXPECT_FALSE(static_cast<bool>(host_aspects(F::eR8G8B8A8Unorm)));
}

TEST(DsModelBias, BackCallDoesNotOverwriteFront) {
    DepthBiasFaces b;
    b.set(true, 3, 5);
    b.set(false, 7, 11);
    EXPECT_EQ(b.front, (DepthBias{ 3, 5 }));
    EXPECT_EQ(b.back, (DepthBias{ 7, 11 }));
    b.set(false, 0, 0);
    EXPECT_EQ(b.front, (DepthBias{ 3, 5 }));
    b.set(true, 1, 2);
    EXPECT_EQ(b.back, (DepthBias{ 0, 0 }));
}

TEST(DsModelBias, FaceSelection) {
    DepthBiasFaces b;
    b.set(true, 3, 5);
    b.set(false, 7, 11);
    EXPECT_EQ(b.face(false), (DepthBias{ 3, 5 }));
    EXPECT_EQ(b.face(true), (DepthBias{ 7, 11 }));
}

TEST(DsModelBias, UnitsFollowFormatPrecision) {
    // same precision: unchanged
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::S8D24, F::eD24UnormS8Uint), 1.0f);
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::DF32, F::eD32Sfloat), 1.0f);
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::DF32S8, F::eD32SfloatS8Uint), 1.0f);
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::S8D24, F::eD32SfloatS8Uint), 1.0f);
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::D16, F::eD16Unorm), 1.0f);
    // a 16 bit guest step is 256 24-bit host steps
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::D16, F::eD24UnormS8Uint), 256.0f);
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::D16, F::eD32Sfloat), 256.0f);
    // a 24 bit guest step is 1/256 of a 16 bit host step
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::S8D24, F::eD16UnormS8Uint), 1.0f / 256.0f);
    EXPECT_FLOAT_EQ(depth_bias_units_scale(DsClass::DF32, F::eD16Unorm), 1.0f / 256.0f);
}

namespace {
FaceState default_face() {
    return FaceState{};
}
}

TEST(DsModelTwoSided, DisabledUsesFrontWhateverTheBackSays) {
    FaceState back = default_face();
    back.depth_func = SCE_GXM_DEPTH_FUNC_NEVER;
    EXPECT_EQ(plan_faces(false, SCE_GXM_CULL_NONE, default_face(), back), FacePlan::Front);
    EXPECT_EQ(plan_faces(false, SCE_GXM_CULL_CCW, default_face(), back), FacePlan::Front);
}

TEST(DsModelTwoSided, EqualStatesNeverSplit) {
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, default_face(), default_face()), FacePlan::Front);
}

TEST(DsModelTwoSided, EachDifferingFieldSplitsWithoutCulling) {
    {
        FaceState b = default_face();
        b.depth_func = SCE_GXM_DEPTH_FUNC_GREATER;
        EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, default_face(), b), FacePlan::Split);
    }
    {
        FaceState b = default_face();
        b.depth_write = SCE_GXM_DEPTH_WRITE_DISABLED;
        EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, default_face(), b), FacePlan::Split);
    }
    {
        FaceState b = default_face();
        b.polygon_mode = SCE_GXM_POLYGON_MODE_TRIANGLE_LINE;
        EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, default_face(), b), FacePlan::Split);
    }
    {
        FaceState b = default_face();
        b.fragment_mode = SCE_GXM_FRAGMENT_PROGRAM_DISABLED;
        EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, default_face(), b), FacePlan::Split);
    }
    {
        FaceState b = default_face();
        b.bias = DepthBias{ 1, 2 };
        EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, default_face(), b), FacePlan::Split);
    }
}

TEST(DsModelTwoSided, CullLeavesOneFace) {
    FaceState b = default_face();
    b.depth_func = SCE_GXM_DEPTH_FUNC_GREATER;
    // CW removes the back faces: only the front state matters
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_CW, default_face(), b), FacePlan::Front);
    // CCW removes the front faces: the back state is the one drawn
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_CCW, default_face(), b), FacePlan::Back);
}

TEST(DsModelTwoSided, PassList) {
    PassList p = passes_for(FacePlan::Front);
    EXPECT_EQ(p.count, 1u);
    EXPECT_EQ(p.variant[0], PassVariant::Front);
    p = passes_for(FacePlan::Back);
    EXPECT_EQ(p.count, 1u);
    EXPECT_EQ(p.variant[0], PassVariant::Back);
    p = passes_for(FacePlan::Split);
    EXPECT_EQ(p.count, 2u);
    EXPECT_EQ(p.variant[0], PassVariant::SplitFront);
    EXPECT_EQ(p.variant[1], PassVariant::SplitBack);
    EXPECT_FALSE(variant_uses_back_state(PassVariant::Front));
    EXPECT_FALSE(variant_uses_back_state(PassVariant::SplitFront));
    EXPECT_TRUE(variant_uses_back_state(PassVariant::Back));
    EXPECT_TRUE(variant_uses_back_state(PassVariant::SplitBack));
}
