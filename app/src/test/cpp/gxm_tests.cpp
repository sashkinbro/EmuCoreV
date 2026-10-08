#include <gtest/gtest.h>
#include <renderer/gxm_types.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/vertex_stream.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

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

TEST(GxmVertexStream, RepackingAlignsStrideAndPreservesVertexBytes) {
    const std::array<uint8_t, 12> packed = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
    const std::vector<uint8_t> repacked = renderer::vulkan::restride_vertex_stream(packed, 6);

    EXPECT_EQ(repacked, (std::vector<uint8_t>{
                            0, 1, 2, 3, 4, 5, 0, 0,
                            6, 7, 8, 9, 10, 11, 0, 0,
                        }));
}

TEST(GxmVertexStream, RepackingLeavesAlignedStreamsUnchanged) {
    const std::array<uint8_t, 8> packed = { 0, 1, 2, 3, 4, 5, 6, 7 };

    EXPECT_EQ(renderer::vulkan::restride_vertex_stream(packed, 8),
        (std::vector<uint8_t>{ 0, 1, 2, 3, 4, 5, 6, 7 }));
}

TEST(GxmVertexStream, RepackingPadsPartialFinalVertexWithoutReadingPastInput) {
    const std::array<uint8_t, 7> packed = { 1, 2, 3, 4, 5, 6, 7 };

    EXPECT_EQ(renderer::vulkan::restride_vertex_stream(packed, 3),
        (std::vector<uint8_t>{ 1, 2, 3, 0, 4, 5, 6, 0, 7, 0, 0, 0 }));
}

TEST(GxmVertexStream, RepackingRequiresEveryAttributeToStayInsideItsRecord) {
    using renderer::vulkan::VertexAttributeRange;
    const std::array<VertexAttributeRange, 2> safe = { VertexAttributeRange{ 0, 4 }, VertexAttributeRange{ 4, 2 } };
    const std::array<VertexAttributeRange, 1> crossing = { VertexAttributeRange{ 4, 4 } };
    const std::array<VertexAttributeRange, 1> beyond_stride = { VertexAttributeRange{ 8, 2 } };

    EXPECT_TRUE(renderer::vulkan::should_restride_vertex_stream(false, 6, safe));
    EXPECT_FALSE(renderer::vulkan::should_restride_vertex_stream(false, 6, crossing));
    EXPECT_FALSE(renderer::vulkan::should_restride_vertex_stream(false, 6, beyond_stride));
    EXPECT_FALSE(renderer::vulkan::should_restride_vertex_stream(false, 8, safe));
    EXPECT_FALSE(renderer::vulkan::should_restride_vertex_stream(false, 0, safe));
    EXPECT_FALSE(renderer::vulkan::should_restride_vertex_stream(true, 6, safe));
}

TEST(GxmVertexStream, RepackedStreamMustFitTheVertexRingCapacity) {
    constexpr size_t ring_capacity = 64u * 1024u * 1024u;
    EXPECT_TRUE(renderer::vulkan::restrided_vertex_stream_fits(48, 3, ring_capacity));
    EXPECT_FALSE(renderer::vulkan::restrided_vertex_stream_fits(49u * 1024u * 1024u, 3, ring_capacity));
    EXPECT_TRUE(renderer::vulkan::restrided_vertex_stream_fits(ring_capacity, 4, ring_capacity));
}

TEST(GxmVertexStream, AllStreamsInOneDrawMustNotWrapOverEarlierStreams) {
    constexpr size_t ring_capacity = 64u * 1024u * 1024u;
    constexpr size_t alignment = 16;
    const std::array<size_t, 2> two_40_mib_streams = { 40u * 1024u * 1024u, 40u * 1024u * 1024u };
    const std::array<size_t, 3> wrap_and_overlap = {
        32u * 1024u * 1024u,
        24u * 1024u * 1024u,
        20u * 1024u * 1024u,
    };

    EXPECT_FALSE(renderer::vulkan::vertex_stream_batch_fits(0, ring_capacity, alignment, two_40_mib_streams));
    EXPECT_FALSE(renderer::vulkan::vertex_stream_batch_fits(ring_capacity - 16, ring_capacity, alignment, wrap_and_overlap));
    EXPECT_TRUE(renderer::vulkan::vertex_stream_batch_fits(0, ring_capacity, alignment, std::span<const size_t>(two_40_mib_streams).first(1)));
}

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
