#include <gtest/gtest.h>
#include <renderer/gxm_types.h>
#include <renderer/vulkan/gxm_to_vulkan.h>

#include <array>
#include <cstddef>
#include <cstring>

// Games can copy or inspect the opaque guest surface. Keeping the total size
// alone is insufficient: the width/height and clip fields share packed words.
static_assert(sizeof(SceGxmColorSurface) == 48);
static_assert(offsetof(SceGxmColorSurface, width) == 4);
static_assert(offsetof(SceGxmColorSurface, height) == 6);
static_assert(offsetof(SceGxmColorSurface, strideInPixels) == 8);
static_assert(offsetof(SceGxmColorSurface, data) == 12);
static_assert(offsetof(SceGxmColorSurface, colorFormat) == 16);
static_assert(offsetof(SceGxmColorSurface, surfaceType) == 20);
static_assert(offsetof(SceGxmColorSurface, outputRegisterSize) == 28);
static_assert(offsetof(SceGxmColorSurface, backgroundTex) == 32);

TEST(GxmColorSurface, ReadsPackedGuestSurfaceWithoutMovingDataOrClip) {
    // A little-endian guest surface: min=(17,33), max=(639,479),
    // dimensions=640x480, stride=672, data=0x12345000.
    const std::array<uint32_t, 12> guest_words = {
        0x00210110, 0x01e00280, 672, 0x12345000,
        SCE_GXM_COLOR_FORMAT_U1U5U5U5_ARGB, SCE_GXM_COLOR_SURFACE_LINEAR,
        0x001df27f, 0, 0, 0, 0, 0
    };
    SceGxmColorSurface surface;
    std::memcpy(&surface, guest_words.data(), sizeof(surface));
    EXPECT_EQ(surface.width, 640);
    EXPECT_EQ(surface.height, 480);
    EXPECT_EQ(surface.strideInPixels, 672);
    EXPECT_EQ(surface.data.address(), 0x12345000);
    EXPECT_EQ(surface.clip_x_min, 17);
    EXPECT_EQ(surface.clip_y_min, 33);
    EXPECT_EQ(surface.clip_x_max, 639);
    EXPECT_EQ(surface.clip_y_max, 479);
}

namespace {
void expect_mapping(vk::ComponentMapping actual, vk::ComponentSwizzle r,
    vk::ComponentSwizzle g, vk::ComponentSwizzle b, vk::ComponentSwizzle a) {
    EXPECT_EQ(actual.r, r);
    EXPECT_EQ(actual.g, g);
    EXPECT_EQ(actual.b, b);
    EXPECT_EQ(actual.a, a);
}
} // namespace

TEST(GxmPackedColor, A1RgbColorKeepsAlphaInTheHighBit) {
    using S = vk::ComponentSwizzle;
    EXPECT_EQ(renderer::vulkan::color::translate_format(SCE_GXM_COLOR_BASE_FORMAT_U1U5U5U5),
        vk::Format::eA1R5G5B5UnormPack16);
    expect_mapping(renderer::vulkan::color::translate_swizzle(SCE_GXM_COLOR_FORMAT_U1U5U5U5_ARGB),
        S::eR, S::eG, S::eB, S::eA);
    expect_mapping(renderer::vulkan::color::translate_swizzle(SCE_GXM_COLOR_FORMAT_U1U5U5U5_ABGR),
        S::eB, S::eG, S::eR, S::eA);
}

TEST(GxmPackedColor, A1RgbTextureKeepsAlphaOrForcesItOpaque) {
    using S = vk::ComponentSwizzle;
    EXPECT_EQ(renderer::vulkan::texture::translate_format(SCE_GXM_TEXTURE_BASE_FORMAT_U1U5U5U5),
        vk::Format::eA1R5G5B5UnormPack16);
    expect_mapping(renderer::vulkan::texture::translate_swizzle(SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ARGB),
        S::eR, S::eG, S::eB, S::eA);
    expect_mapping(renderer::vulkan::texture::translate_swizzle(SCE_GXM_TEXTURE_FORMAT_U1U5U5U5_ABGR),
        S::eB, S::eG, S::eR, S::eA);
    expect_mapping(renderer::vulkan::texture::translate_swizzle(SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1RGB),
        S::eR, S::eG, S::eB, S::eOne);
    expect_mapping(renderer::vulkan::texture::translate_swizzle(SCE_GXM_TEXTURE_FORMAT_X1U5U5U5_1BGR),
        S::eB, S::eG, S::eR, S::eOne);
}
