#include <renderer/vulkan/surface_cache_snapshot.h>
#include <gtest/gtest.h>

namespace {
using namespace renderer::vulkan;

SurfaceCacheSnapshot empty_snapshot() {
    SurfaceCacheSnapshot snapshot;
    snapshot.colors.resize(surface_cache_snapshot_slots);
    snapshot.depth_stencil.resize(surface_cache_snapshot_slots);
    for (uint8_t slot = 0; slot < surface_cache_snapshot_slots; ++slot) {
        snapshot.color_mru_order.push_back(slot);
        snapshot.depth_stencil_mru_order.push_back(slot);
    }
    return snapshot;
}

SurfaceCacheImageSnapshot color_image() {
    SurfaceCacheImageSnapshot image;
    image.present = true;
    image.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
        | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    image.image.width = 1;
    image.image.height = 1;
    image.image.format = vk::Format::eR8G8B8A8Unorm;
    image.image.layout = vkutil::ImageLayout::ColorAttachmentReadWrite;
    image.image.bytes = { 0x11, 0x22, 0x33, 0x44 };
    return image;
}

TEST(SurfaceCacheSnapshot, EmptyQueuesAndExactColorMetadataRoundTrip) {
    auto snapshot = empty_snapshot();
    auto &color = snapshot.colors[7];
    color.active = true;
    color.guest_address = 0x12000;
    color.total_bytes = 4;
    color.width = color.original_width = 1;
    color.height = color.original_height = 1;
    color.stride_bytes = 4;
    color.format = SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8;
    color.swizzle = vkutil::default_comp_mapping;
    color.content_is_blended = true;
    color.has_phase_view = true;
    color.need_surface_sync_object = true;
    color.need_surface_sync = true;
    color.dirty_object = true;
    color.dirty = true;
    color.written_x0 = 1;
    color.written_x1 = 2;
    color.partial_write_back = true;
    color.texture = color_image();
    snapshot.last_written_color_slot = 7;
    snapshot.cpu_surfaces_changed = { 0x44000, 0x55000 };

    std::string error;
    ASSERT_TRUE(validate_surface_cache_snapshot(snapshot, error)) << error;
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(encode_surface_cache_snapshot(snapshot, encoded, error)) << error;
    SurfaceCacheSnapshot decoded;
    ASSERT_TRUE(decode_surface_cache_snapshot(encoded, decoded, error)) << error;
    EXPECT_EQ(decoded.color_mru_order, snapshot.color_mru_order);
    EXPECT_EQ(decoded.cpu_surfaces_changed, snapshot.cpu_surfaces_changed);
    EXPECT_EQ(decoded.last_written_color_slot, 7);
    EXPECT_TRUE(decoded.colors[7].content_is_blended);
    EXPECT_TRUE(decoded.colors[7].has_phase_view);
    EXPECT_TRUE(decoded.colors[7].need_surface_sync);
    EXPECT_TRUE(decoded.colors[7].dirty);
    EXPECT_EQ(decoded.colors[7].written_x0, 1);
    EXPECT_EQ(decoded.colors[7].texture.image.bytes, snapshot.colors[7].texture.image.bytes);
}

TEST(SurfaceCacheSnapshot, DepthStencilPlaneBytesAndPendingSceneRoundTrip) {
    auto snapshot = empty_snapshot();
    auto &depth = snapshot.depth_stencil[4];
    depth.active = true;
    depth.surface.depth_data = Ptr<void>(0x8000);
    depth.surface.stencil_data = Ptr<void>(0x9000);
    depth.surface.unk1 = 1;
    depth.surface.force_load = 1;
    depth.surface.force_store = 1;
    depth.surface.set_stride(64);
    depth.surface.stencil = 0xA5;
    depth.surface.mask = 1;
    depth.surface.unk2 = 1;
    depth.memory_width = 1;
    depth.memory_height = 1;
    depth.stride_samples = 32;
    depth.multisample_mode = SCE_GXM_MULTISAMPLE_4X;
    depth.depth_content_stored = false;
    depth.total_bytes = 4;
    depth.texture.present = true;
    depth.texture.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst
        | vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
    depth.texture.image.width = 1;
    depth.texture.image.height = 1;
    depth.texture.image.format = vk::Format::eD24UnormS8Uint;
    depth.texture.image.layout = vkutil::ImageLayout::DepthStencilReadOnly;
    depth.texture.image.bytes = { 1, 2, 3, 4, 5 };
    snapshot.pending_depth_stencil_slot = 4;
    snapshot.pending_depth_stencil_stores = true;

    std::string error;
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(encode_surface_cache_snapshot(snapshot, encoded, error)) << error;
    SurfaceCacheSnapshot decoded;
    ASSERT_TRUE(decode_surface_cache_snapshot(encoded, decoded, error)) << error;
    EXPECT_EQ(decoded.pending_depth_stencil_slot, 4);
    EXPECT_TRUE(decoded.pending_depth_stencil_stores);
    EXPECT_EQ(decoded.depth_stencil[4].surface.depth_data.address(), 0x8000);
    EXPECT_EQ(decoded.depth_stencil[4].surface.stencil_data.address(), 0x9000);
    EXPECT_EQ(decoded.depth_stencil[4].surface.get_stride(), 64);
    EXPECT_EQ(decoded.depth_stencil[4].surface.stencil, 0xA5);
    EXPECT_TRUE(decoded.depth_stencil[4].surface.force_load);
    EXPECT_TRUE(decoded.depth_stencil[4].surface.force_store);
    EXPECT_TRUE(decoded.depth_stencil[4].surface.mask);
    EXPECT_EQ(decoded.depth_stencil[4].texture.image.bytes, depth.texture.image.bytes);
}

TEST(SurfaceCacheSnapshot, RejectsMalformedQueueImageAndTrailingData) {
    std::string error;
    auto snapshot = empty_snapshot();
    snapshot.color_mru_order[3] = snapshot.color_mru_order[2];
    EXPECT_FALSE(validate_surface_cache_snapshot(snapshot, error));

    snapshot = empty_snapshot();
    auto &color = snapshot.colors[0];
    color.active = true;
    color.guest_address = 0x1000;
    color.total_bytes = 4;
    color.width = color.original_width = color.height = color.original_height = 1;
    color.stride_bytes = 4;
    color.texture = color_image();
    color.texture.image.bytes.pop_back();
    EXPECT_FALSE(validate_surface_cache_snapshot(snapshot, error));

    snapshot = empty_snapshot();
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(encode_surface_cache_snapshot(snapshot, encoded, error)) << error;
    encoded.push_back(0);
    SurfaceCacheSnapshot decoded;
    EXPECT_FALSE(decode_surface_cache_snapshot(encoded, decoded, error));
}
} // namespace
