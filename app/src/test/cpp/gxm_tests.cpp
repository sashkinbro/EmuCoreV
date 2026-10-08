#include <gtest/gtest.h>
#include <renderer/gxm_types.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/surface_sync.h>
#include <renderer/vulkan/uniform_slack.h>
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

TEST(GxmSurfaceSync, PartialWritebackIntersectsCurrentSceneWithBoundedSyncRect) {
    using renderer::vulkan::SurfaceRect;
    const auto rect = renderer::vulkan::intersect_surface_rects(
        SurfaceRect{ 5, 6, 40, 32 }, SurfaceRect{ 20, -4, 50, 18 }, 32, 24);

    ASSERT_TRUE(rect.has_value());
    EXPECT_EQ(*rect, (SurfaceRect{ 20, 6, 32, 18 }));
}

TEST(GxmSurfaceSync, EmptyOrNonlinearIntersectionDoesNotRequestWriteback) {
    using renderer::vulkan::SurfaceRect;
    EXPECT_TRUE(renderer::vulkan::surface_partial_writeback_eligible(true, false, false, true));
    EXPECT_FALSE(renderer::vulkan::surface_partial_writeback_eligible(false, false, false, true));
    EXPECT_FALSE(renderer::vulkan::surface_partial_writeback_eligible(true, true, false, true));
    EXPECT_FALSE(renderer::vulkan::surface_partial_writeback_eligible(true, false, true, true));
    // Post-sync swizzling still processes the full guest surface, so partial writes
    // must remain disabled until that operation accepts the same rectangle.
    EXPECT_FALSE(renderer::vulkan::surface_partial_writeback_eligible(true, false, false, false));
    EXPECT_FALSE(renderer::vulkan::intersect_surface_rects(
        SurfaceRect{ 0, 0, 4, 4 }, SurfaceRect{ 4, 0, 8, 4 }, 16, 16).has_value());
    EXPECT_FALSE(renderer::vulkan::intersect_surface_rects(
        SurfaceRect{ -8, -8, -1, -1 }, SurfaceRect{ 0, 0, 8, 8 }, 8, 8).has_value());
}

TEST(GxmSurfaceSync, MatchingBufferSyncCopiesOnlyFreshRowsAndPreservesCpuNewerBytes) {
    using renderer::vulkan::copy_surface_sync_rows;
    std::array<uint8_t, 22> mapped{};
    for (size_t i = 0; i < mapped.size(); i++)
        mapped[i] = static_cast<uint8_t>(i + 1);
    std::array<uint8_t, 24> guest{};
    guest.fill(0xCC);

    ASSERT_TRUE(copy_surface_sync_rows(guest.data() + 4, 12, mapped.data() + 3, 12, 8, 3, 2));
    EXPECT_EQ(guest[4], mapped[3]);
    EXPECT_EQ(guest[5], mapped[4]);
    EXPECT_EQ(guest[6], mapped[5]);
    EXPECT_EQ(guest[12], mapped[11]);
    EXPECT_EQ(guest[13], mapped[12]);
    EXPECT_EQ(guest[14], mapped[13]);
    EXPECT_EQ(guest[3], 0xCC);
    EXPECT_EQ(guest[7], 0xCC);
    EXPECT_EQ(guest[11], 0xCC);
    EXPECT_EQ(guest[15], 0xCC);
}

TEST(GxmSurfaceSync, InvalidRowCopiesCannotExceedRequestOrMappedRanges) {
    using renderer::vulkan::copy_surface_sync_rows;
    std::array<uint8_t, 16> source{};
    std::array<uint8_t, 16> destination{};
    EXPECT_FALSE(copy_surface_sync_rows(destination.data(), destination.size(), source.data(), source.size(), 8, 9, 2));
    EXPECT_FALSE(copy_surface_sync_rows(destination.data(), 10, source.data(), source.size(), 8, 3, 2));
    EXPECT_FALSE(copy_surface_sync_rows(destination.data(), destination.size(), source.data(), 10, 8, 3, 2));
    EXPECT_TRUE(copy_surface_sync_rows(destination.data(), destination.size(), source.data(), source.size(), 8, 3, 0));
}

TEST(GxmUniformSlack, CopyRangeStopsAtWindowMappingAndColorSurfaceBoundary) {
    using renderer::vulkan::uniform_slack_copy_range;
    const auto full = uniform_slack_copy_range(0x1000, 256, 64 * 1024, 0x100000, 16 * 1024);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(full->offset, 256u);
    EXPECT_EQ(full->size, 16 * 1024u - 256u);

    const auto mapping_limited = uniform_slack_copy_range(0x1000, 256, 1024, 0x100000, 16 * 1024);
    ASSERT_TRUE(mapping_limited.has_value());
    EXPECT_EQ(mapping_limited->size, 1024u - 256u);

    const auto surface_limited = uniform_slack_copy_range(0x1000, 256, 64 * 1024, 0x1400, 16 * 1024);
    ASSERT_TRUE(surface_limited.has_value());
    EXPECT_EQ(surface_limited->size, 0x400u - 256u);

    EXPECT_FALSE(uniform_slack_copy_range(0x1000, 256, 64 * 1024, 0x1100, 16 * 1024).has_value());
    EXPECT_FALSE(uniform_slack_copy_range(0x1000, 1025, 1024, 0x100000, 16 * 1024).has_value());
    EXPECT_FALSE(uniform_slack_copy_range(0x1000, 0, 1024, 0x100000, 16 * 1024).has_value());
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
