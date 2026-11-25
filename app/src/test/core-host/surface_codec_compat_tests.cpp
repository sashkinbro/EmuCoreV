// B130: the surface cache grew from 20 to surface_cache_snapshot_slots slots. Save states written by
// builds with 20 slots must still decode (padded with empty least-recently-used slots) and the new
// layout must round-trip. Only the codec is linked; no GPU.
#include <renderer/vulkan/surface_cache_snapshot.h>

#include <gtest/gtest.h>

namespace {
using namespace renderer::vulkan;

SurfaceCacheSnapshot empty_snapshot() {
    SurfaceCacheSnapshot snapshot;
    snapshot.colors.resize(surface_cache_snapshot_slots);
    snapshot.depth_stencil.resize(surface_cache_snapshot_slots);
    for (uint32_t slot = 0; slot < surface_cache_snapshot_slots; ++slot) {
        snapshot.color_mru_order.push_back(static_cast<uint8_t>(slot));
        snapshot.depth_stencil_mru_order.push_back(static_cast<uint8_t>(slot));
    }
    return snapshot;
}

void make_active(ColorSurfaceSnapshot &color) {
    color.active = true;
    color.guest_address = 0x12000;
    color.total_bytes = 4;
    color.width = color.original_width = 1;
    color.height = color.original_height = 1;
    color.stride_bytes = 4;
    color.format = SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8;
    color.swizzle = vkutil::default_comp_mapping;
    color.texture.present = true;
    color.texture.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
        | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    color.texture.image.width = 1;
    color.texture.image.height = 1;
    color.texture.image.format = vk::Format::eR8G8B8A8Unorm;
    color.texture.image.layout = vkutil::ImageLayout::ColorAttachmentReadWrite;
    color.texture.image.bytes = { 0x11, 0x22, 0x33, 0x44 };
}
} // namespace

TEST(SurfaceCodecCompat, CurrentLayoutHasMoreSlotsThanTheLegacyOne) {
    EXPECT_EQ(surface_cache_snapshot_legacy_slots, 20u);
    EXPECT_GT(surface_cache_snapshot_slots, surface_cache_snapshot_legacy_slots);
}

TEST(SurfaceCodecCompat, CurrentLayoutRoundTrips) {
    auto snapshot = empty_snapshot();
    make_active(snapshot.colors[40]);
    snapshot.last_written_color_slot = 40;
    std::string error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(encode_surface_cache_snapshot(snapshot, bytes, error)) << error;
    SurfaceCacheSnapshot decoded;
    ASSERT_TRUE(decode_surface_cache_snapshot(bytes, decoded, error)) << error;
    EXPECT_EQ(decoded.colors.size(), surface_cache_snapshot_slots);
    EXPECT_TRUE(decoded.colors[40].active);
    EXPECT_EQ(decoded.last_written_color_slot, 40);
}

TEST(SurfaceCodecCompat, LegacyTwentySlotSaveStateDecodesAndIsPaddedWithEmptyLruSlots) {
    auto snapshot = empty_snapshot();
    make_active(snapshot.colors[3]);
    snapshot.last_written_color_slot = 3;
    // slot 5 is the most recently used, then 3, then the rest
    snapshot.color_mru_order = { 5, 3 };
    for (uint32_t slot = 0; slot < surface_cache_snapshot_slots; ++slot)
        if (slot != 5 && slot != 3)
            snapshot.color_mru_order.push_back(static_cast<uint8_t>(slot));

    std::string error;
    std::vector<uint8_t> legacy;
    ASSERT_TRUE(encode_surface_cache_snapshot_legacy_layout(snapshot, legacy, error)) << error;
    std::vector<uint8_t> current;
    ASSERT_TRUE(encode_surface_cache_snapshot(snapshot, current, error)) << error;
    EXPECT_LT(legacy.size(), current.size());

    SurfaceCacheSnapshot decoded;
    ASSERT_TRUE(decode_surface_cache_snapshot(legacy, decoded, error)) << error;
    ASSERT_EQ(decoded.colors.size(), surface_cache_snapshot_slots);
    ASSERT_EQ(decoded.depth_stencil.size(), surface_cache_snapshot_slots);
    EXPECT_TRUE(decoded.colors[3].active);
    EXPECT_EQ(decoded.colors[3].guest_address, 0x12000u);
    EXPECT_EQ(decoded.last_written_color_slot, 3);
    // the legacy order is preserved and the new slots are the least recently used ones
    ASSERT_EQ(decoded.color_mru_order.size(), surface_cache_snapshot_slots);
    EXPECT_EQ(decoded.color_mru_order[0], 5);
    EXPECT_EQ(decoded.color_mru_order[1], 3);
    for (uint32_t i = surface_cache_snapshot_legacy_slots; i < surface_cache_snapshot_slots; ++i) {
        EXPECT_EQ(decoded.color_mru_order[i], i);
        EXPECT_FALSE(decoded.colors[i].active);
    }
    EXPECT_TRUE(validate_surface_cache_snapshot(decoded, error)) << error;
}

TEST(SurfaceCodecCompat, LegacyEncoderRefusesASurfaceBeyondTheLegacySlots) {
    auto snapshot = empty_snapshot();
    make_active(snapshot.colors[30]);
    std::string error;
    std::vector<uint8_t> bytes;
    EXPECT_FALSE(encode_surface_cache_snapshot_legacy_layout(snapshot, bytes, error));
}

TEST(SurfaceCodecCompat, UnknownSlotCountIsRejected) {
    auto snapshot = empty_snapshot();
    std::string error;
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(encode_surface_cache_snapshot(snapshot, bytes, error)) << error;
    // the count is the third little-endian u32 of the header
    bytes[8] = 30;
    SurfaceCacheSnapshot decoded;
    EXPECT_FALSE(decode_surface_cache_snapshot(bytes, decoded, error));
}
