// Host tests for the Vulkan shader module identity: every Hints field that shader::convert_gxp reads
// must change the key of the stage that reads it.
#include <gtest/gtest.h>

#include <renderer/shader_variant.h>

#include <vector>

namespace {

shader::Hints make_hints(const std::vector<SceGxmVertexAttribute> *attributes) {
    shader::Hints hints{};
    hints.attributes = attributes;
    hints.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    for (auto &format : hints.vertex_textures)
        format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;
    for (auto &format : hints.fragment_textures)
        format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR;
    return hints;
}

std::vector<SceGxmVertexAttribute> make_attributes() {
    SceGxmVertexAttribute a{};
    a.streamIndex = 0;
    a.offset = 0;
    a.format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    a.componentCount = 3;
    a.regIndex = 0;
    SceGxmVertexAttribute b = a;
    b.regIndex = 4;
    b.format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
    b.componentCount = 4;
    return { a, b };
}

std::vector<uint8_t> key(bool is_vertex, const shader::Hints &hints, uint32_t used = 0x3, bool maskupdate = false) {
    return renderer::vulkan_shader_variant_key_bytes(is_vertex, hints, used, maskupdate);
}

} // namespace

TEST(ShaderVariantKey, IdenticalInputsGiveIdenticalKeys) {
    const auto attributes = make_attributes();
    const auto hints = make_hints(&attributes);
    EXPECT_EQ(key(true, hints), key(true, hints));
    EXPECT_EQ(key(false, hints), key(false, hints));
}

TEST(ShaderVariantKey, StageAndMaskUpdateAreDistinguished) {
    const auto attributes = make_attributes();
    const auto hints = make_hints(&attributes);
    EXPECT_NE(key(true, hints), key(false, hints));
    EXPECT_NE(key(false, hints, 0x3, false), key(false, hints, 0x3, true));
    EXPECT_NE(key(true, hints, 0x3, false), key(true, hints, 0x3, true));
}

TEST(ShaderVariantKey, FragmentKeyCoversEveryFragmentHint) {
    const auto attributes = make_attributes();
    const auto base = make_hints(&attributes);
    const auto reference = key(false, base);

    auto h = base;
    h.color_format = SCE_GXM_COLOR_FORMAT_U8_A;
    EXPECT_NE(key(false, h), reference) << "color_format";

    h = base;
    h.raw_color_attachment_input = true;
    EXPECT_NE(key(false, h), reference) << "raw_color_attachment_input";

    h = base;
    h.vertex_output_mask = 0x5;
    EXPECT_NE(key(false, h), reference) << "vertex_output_mask";

    // each used texture slot contributes its own format
    for (uint32_t slot = 0; slot < 2; ++slot) {
        h = base;
        h.fragment_textures[slot] = SCE_GXM_TEXTURE_FORMAT_F32_R;
        EXPECT_NE(key(false, h), reference) << "fragment_textures[" << slot << "]";
    }
}

TEST(ShaderVariantKey, VertexKeyCoversEveryVertexHint) {
    const auto attributes = make_attributes();
    const auto base = make_hints(&attributes);
    const auto reference = key(true, base);

    for (uint32_t slot = 0; slot < 2; ++slot) {
        auto h = base;
        h.vertex_textures[slot] = SCE_GXM_TEXTURE_FORMAT_F16_R;
        EXPECT_NE(key(true, h), reference) << "vertex_textures[" << slot << "]";
    }

    auto changed = attributes;
    changed[1].regIndex = 8;
    auto h = make_hints(&changed);
    EXPECT_NE(key(true, h), reference) << "attribute regIndex";

    changed = attributes;
    changed[0].format = SCE_GXM_ATTRIBUTE_FORMAT_S16;
    h = make_hints(&changed);
    EXPECT_NE(key(true, h), reference) << "attribute format";

    changed = attributes;
    changed[0].componentCount = 4;
    h = make_hints(&changed);
    EXPECT_NE(key(true, h), reference) << "attribute componentCount";

    changed = attributes;
    changed.pop_back();
    h = make_hints(&changed);
    EXPECT_NE(key(true, h), reference) << "attribute count";

    h = make_hints(nullptr);
    EXPECT_NE(key(true, h), reference) << "attributes absent";
    const std::vector<SceGxmVertexAttribute> empty;
    EXPECT_NE(key(true, make_hints(&empty)), key(true, h)) << "empty vs absent";
}

TEST(ShaderVariantKey, UnusedStateDoesNotSplitTheCache) {
    const auto attributes = make_attributes();
    const auto base = make_hints(&attributes);

    // textures the program never samples
    auto h = base;
    h.fragment_textures[5] = SCE_GXM_TEXTURE_FORMAT_F32_R;
    h.vertex_textures[7] = SCE_GXM_TEXTURE_FORMAT_F32_R;
    EXPECT_EQ(key(false, h), key(false, base));
    EXPECT_EQ(key(true, h), key(true, base));

    // textures of the other stage
    h = base;
    h.vertex_textures[0] = SCE_GXM_TEXTURE_FORMAT_F32_R;
    EXPECT_EQ(key(false, h), key(false, base));
    h = base;
    h.fragment_textures[0] = SCE_GXM_TEXTURE_FORMAT_F32_R;
    EXPECT_EQ(key(true, h), key(true, base));

    // fragment-only hints do not affect vertex keys and vice versa
    h = base;
    h.color_format = SCE_GXM_COLOR_FORMAT_U8_A;
    h.raw_color_attachment_input = true;
    h.vertex_output_mask = 1;
    EXPECT_EQ(key(true, h), key(true, base));
    auto other = attributes;
    other[0].regIndex = 9;
    EXPECT_EQ(key(false, make_hints(&other)), key(false, base));

    // attribute fields the recompiler never reads
    other = attributes;
    other[0].streamIndex = 3;
    other[0].offset = 12;
    EXPECT_EQ(key(true, make_hints(&other)), key(true, base));
}

TEST(ShaderVariantKey, UsedTextureMaskIsPartOfTheKey) {
    const auto attributes = make_attributes();
    const auto hints = make_hints(&attributes);
    EXPECT_NE(key(false, hints, 0x1), key(false, hints, 0x3));
}
