#include <gxm/functions.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <shader/spirv_recompiler.h>
#include <renderer/shaders.h>
#include <renderer/shader_variant.h>

DECL_EXPORT(int, sceGxmColorSurfaceSetFormat, SceGxmColorSurface *surface, SceGxmColorFormat format);

TEST(GxmAlphaSurface, ConvertsAlphaOnlyBackgroundTextureWithoutChangingByteStorage) {
    SceGxmTextureFormat format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;
    ASSERT_TRUE(gxm::convert_color_format_to_texture_format(SCE_GXM_COLOR_FORMAT_U8_A, format));
    EXPECT_EQ(format, SCE_GXM_TEXTURE_FORMAT_U8_R000);
    const auto mapping = renderer::vulkan::texture::translate_swizzle(format);
    EXPECT_EQ(mapping.r, vk::ComponentSwizzle::eZero);
    EXPECT_EQ(mapping.g, vk::ComponentSwizzle::eZero);
    EXPECT_EQ(mapping.b, vk::ComponentSwizzle::eZero);
    EXPECT_EQ(mapping.a, vk::ComponentSwizzle::eR);
    EXPECT_EQ(gxm::bits_per_pixel(gxm::get_base_format(SCE_GXM_COLOR_FORMAT_U8_A)), 8);
}

TEST(GxmAlphaSurface, VulkanCacheKeepsCanonicalSingleRedStorage) {
    EXPECT_EQ(renderer::vulkan::color::translate_swizzle(SCE_GXM_COLOR_FORMAT_U8_A),
        renderer::vulkan::color::translate_swizzle(SCE_GXM_COLOR_FORMAT_U8_R));
    EXPECT_EQ(renderer::vulkan::color::translate_surface_format(SCE_GXM_COLOR_BASE_FORMAT_U8), vk::Format::eR8Unorm);
}

TEST(GxmAlphaSurface, ActualInitAndSetFormatKeepAlphaInBackgroundTexture) {
    EmuEnvState env;
    SceGxmColorSurface surface{};
    ASSERT_EQ(export_sceGxmColorSurfaceInit(env, 0, "test", &surface, SCE_GXM_COLOR_FORMAT_U8_A,
        SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
        SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, 8, 8, 8, Ptr<void>(0x1000)), 0);
    EXPECT_EQ(surface.colorFormat, SCE_GXM_COLOR_FORMAT_U8_A);
    EXPECT_EQ(gxm::get_format(surface.backgroundTex), SCE_GXM_TEXTURE_FORMAT_U8_R000);
    ASSERT_EQ(export_sceGxmColorSurfaceSetFormat(env, 0, "test", &surface, SCE_GXM_COLOR_FORMAT_U8_R), 0);
    ASSERT_EQ(export_sceGxmColorSurfaceSetFormat(env, 0, "test", &surface, SCE_GXM_COLOR_FORMAT_U8_A), 0);
    EXPECT_EQ(gxm::get_format(surface.backgroundTex), SCE_GXM_TEXTURE_FORMAT_U8_R000);
}

TEST(GxmAlphaSurface, UnsupportedFormatDoesNotCorruptExistingSurface) {
    EmuEnvState env;
    SceGxmColorSurface surface{};
    ASSERT_EQ(export_sceGxmColorSurfaceInit(env, 0, "test", &surface, SCE_GXM_COLOR_FORMAT_U8_R,
        SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
        SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, 8, 8, 8, Ptr<void>(0x1000)), 0);
    const auto previous = surface;
    const auto unsupported = static_cast<SceGxmColorFormat>(0xffffffffu);
    EXPECT_NE(export_sceGxmColorSurfaceSetFormat(env, 0, "test", &surface, unsupported), 0);
    EXPECT_EQ(std::memcmp(&previous, &surface, sizeof(surface)), 0);
    EXPECT_NE(export_sceGxmColorSurfaceInit(env, 0, "test", &surface, unsupported,
        SCE_GXM_COLOR_SURFACE_LINEAR, SCE_GXM_COLOR_SURFACE_SCALE_NONE,
        SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT, 8, 8, 8, Ptr<void>(0x1000)), 0);
    EXPECT_EQ(std::memcmp(&previous, &surface, sizeof(surface)), 0);
}

TEST(GxmAlphaSurface, RealFragmentCompilerRemapsReadAndWriteOnlyForAlphaSurface) {
    struct Fixture {
        SceGxmProgram program{};
        SceGxmProgramVertexVaryings varyings{};
    } fixture;
    auto &program = fixture.program;
    program.magic = 0x00505847;
    program.major_version = 1;
    program.minor_version = 4;
    program.size = sizeof(fixture);
    program.program_flags = SCE_GXM_PROGRAM_FLAG_FRAGMENT | SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED | SCE_GXM_PROGRAM_FLAG_FRAGCOLOR_USED;
    program.varyings_offset = offsetof(Fixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
    // Empty instruction ranges; this exercises real fragment setup/finalize.
    program.secondary_program_offset = 4;
    fixture.varyings.output_param_type = SCE_GXM_PARAMETER_TYPE_F32;
    fixture.varyings.output_comp_count = 4;
    FeatureState features;
    features.direct_fragcolor = true;
    features.support_unknown_format = true;
    const auto has_shuffle = [](const std::vector<uint32_t> &code, std::array<uint32_t, 4> indices) {
        for (size_t word = 5; word < code.size();) {
            const auto size = code[word] >> 16;
            if (!size || size > code.size() - word)
                return false;
            if ((code[word] & 0xffff) == 79 && size == 9
                && std::equal(indices.begin(), indices.end(), code.begin() + word + 5))
                return true;
            word += size;
        }
        return false;
    };
    shader::Hints hints{};
    hints.color_format = SCE_GXM_COLOR_FORMAT_U8_A;
    const auto alpha = shader::convert_gxp(program, "alpha-regression", features, shader::Target::SpirVVulkan, hints);
    ASSERT_FALSE(alpha.spirv.empty());
    EXPECT_TRUE(has_shuffle(alpha.spirv, { 1, 2, 1, 0 }));
    EXPECT_TRUE(has_shuffle(alpha.spirv, { 3, 1, 2, 3 }));
    hints.color_format = SCE_GXM_COLOR_FORMAT_U8_R;
    const auto red = shader::convert_gxp(program, "red-control", features, shader::Target::SpirVVulkan, hints);
    ASSERT_FALSE(red.spirv.empty());
    EXPECT_FALSE(has_shuffle(red.spirv, { 1, 2, 1, 0 }));
    EXPECT_FALSE(has_shuffle(red.spirv, { 3, 1, 2, 3 }));

    const auto directory = fs::temp_directory_path() / fs::unique_path("alpha-shader-%%%%-%%%%");
    struct Cleanup { fs::path path; ~Cleanup() { boost::system::error_code error; fs::remove_all(path, error); } } cleanup{directory};
    for (const bool alpha_first : { false, true }) {
        const auto cache = directory / (alpha_first ? "alpha-first" : "red-first");
        const auto load = [&](bool is_alpha) {
            hints.color_format = is_alpha ? SCE_GXM_COLOR_FORMAT_U8_A : SCE_GXM_COLOR_FORMAT_U8_R;
            return renderer::load_spirv_shader(program, features, true, hints, false, cache, directory / "logs", "variant-test", true);
        };
        const auto first = load(alpha_first);
        const auto second = load(!alpha_first);
        const auto reload = load(alpha_first);
        EXPECT_EQ(has_shuffle(first, { 3, 1, 2, 3 }), alpha_first);
        EXPECT_EQ(has_shuffle(second, { 3, 1, 2, 3 }), !alpha_first);
        EXPECT_EQ(first, reload);
    }
}

TEST(GxmAlphaSurface, SameGxpHasDistinctShaderAndPipelineCacheIdentityForAlphaOnly) {
    Sha256Hash hash{};
    hash[0] = 0x42;
    const auto red = renderer::fragment_shader_variant_hash(hash, SCE_GXM_COLOR_FORMAT_U8_R);
    const auto alpha = renderer::fragment_shader_variant_hash(hash, SCE_GXM_COLOR_FORMAT_U8_A);
    EXPECT_EQ(red, hash);
    EXPECT_NE(alpha, red);
    EXPECT_EQ(alpha, renderer::fragment_shader_variant_hash(hash, SCE_GXM_COLOR_FORMAT_U8_A));
    EXPECT_EQ(renderer::fragment_shader_variant_hash(hash, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR), hash);
    const auto red_pipeline = renderer::alpha_surface_pipeline_key(0x1234, SCE_GXM_COLOR_FORMAT_U8_R);
    const auto alpha_pipeline = renderer::alpha_surface_pipeline_key(0x1234, SCE_GXM_COLOR_FORMAT_U8_A);
    EXPECT_EQ(red_pipeline, 0x1234);
    EXPECT_NE(red_pipeline, alpha_pipeline);
}

TEST(GxmAlphaSurface, FixedFunctionBlendUsesGuestAlphaEquationAndWriteMaskInStoredRed) {
    vk::PipelineColorBlendAttachmentState guest{
        .blendEnable = VK_TRUE,
        .srcColorBlendFactor = vk::BlendFactor::eZero,
        .dstColorBlendFactor = vk::BlendFactor::eOne,
        .colorBlendOp = vk::BlendOp::eSubtract,
        .srcAlphaBlendFactor = vk::BlendFactor::eSrcColor,
        .dstAlphaBlendFactor = vk::BlendFactor::eOneMinusDstAlpha,
        .alphaBlendOp = vk::BlendOp::eAdd,
        .colorWriteMask = vk::ColorComponentFlagBits::eA
    };
    const auto host = renderer::vulkan::translate_alpha_surface_blend(guest);
    EXPECT_EQ(host.colorWriteMask, vk::ColorComponentFlagBits::eR);
    EXPECT_EQ(host.colorBlendOp, guest.alphaBlendOp);
    EXPECT_EQ(host.srcColorBlendFactor, vk::BlendFactor::eSrcAlpha);
    EXPECT_EQ(host.dstColorBlendFactor, vk::BlendFactor::eOneMinusDstColor);
    // Guest A=0.5 over stored A=0.25: A*A + oldA*(1-oldA).
    // Source red and alpha both carry guest A; destination red carries old A.
    const float alpha = 0.5f, old_alpha = 0.25f;
    const auto factor = [&](vk::BlendFactor value) {
        if (value == vk::BlendFactor::eSrcAlpha) return alpha;
        if (value == vk::BlendFactor::eOneMinusDstColor) return 1.0f - old_alpha;
        ADD_FAILURE() << "wrong channel in alpha blend factor";
        return 0.0f;
    };
    EXPECT_FLOAT_EQ(alpha * factor(host.srcColorBlendFactor) + old_alpha * factor(host.dstColorBlendFactor), 0.4375f);
    guest.colorWriteMask = vk::ColorComponentFlagBits::eR;
    EXPECT_FALSE(renderer::vulkan::translate_alpha_surface_blend(guest).colorWriteMask);
    guest.srcAlphaBlendFactor = vk::BlendFactor::eSrcAlphaSaturate;
    guest.dstAlphaBlendFactor = vk::BlendFactor::eDstAlpha;
    const auto saturated = renderer::vulkan::translate_alpha_surface_blend(guest);
    EXPECT_EQ(saturated.srcColorBlendFactor, vk::BlendFactor::eOne);
    EXPECT_EQ(saturated.dstColorBlendFactor, vk::BlendFactor::eDstColor);
}
