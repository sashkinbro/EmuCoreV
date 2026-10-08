#include <emucorev/savestate/gpu_state_io.h>
#include <renderer/command_snapshot.h>
#include <renderer/vulkan/context_snapshot.h>
#include <renderer/vulkan/state.h>

#include <algorithm>
#include <limits>

namespace renderer::vulkan {
struct SurfaceCacheLifecycleTestAccess {
    static ColorSurfaceCacheInfo &color(VKSurfaceCache &cache) {
        return cache.color_surface_queue.items.front().content;
    }
    static DepthStencilSurfaceCacheInfo &depth(VKSurfaceCache &cache) {
        return cache.ds_surface_queue.items.front().content;
    }
};
} // namespace renderer::vulkan

TEST(GpuSurfaceCacheLifecycle, CleanupReleasesOptionalImagesBeforeAllocatorDestruction) {
    renderer::vulkan::VKState state(0);
    auto &cache = state.surface_cache;
    auto &color = renderer::vulkan::SurfaceCacheLifecycleTestAccess::color(cache);
    auto &depth = renderer::vulkan::SurfaceCacheLifecycleTestAccess::depth(cache);
    // These owners outlive cleanup unless it explicitly releases them. Empty
    // images exercise ownership without requiring an initialized Vulkan device.
    color.raw_image = std::make_unique<vkutil::Image>();
    depth.sample_rate_copy = std::make_unique<vkutil::Image>();
    cache.cleanup();
    EXPECT_FALSE(color.raw_image);
    EXPECT_FALSE(depth.sample_rate_copy);
    cache.cleanup();
    EXPECT_FALSE(color.raw_image);
    EXPECT_FALSE(depth.sample_rate_copy);
}

TEST(GpuSurfaceCacheLifecycle, MissingDepthSceneAndSceneEndCaptureCanonicalEmptyAssociation) {
    renderer::vulkan::VKState state(0);
    SceGxmDepthStencilSurface depth{};
    depth.depth_data = Ptr<void>(0x8000);
    auto &cache = state.surface_cache;
    const vkutil::ImageSnapshotDevice unused_device{};
    renderer::vulkan::SurfaceCacheSnapshot snapshot;
    std::string error;
    ASSERT_TRUE(cache.begin_ds_scene_depth_check(depth, true, 0x10000));
    ASSERT_TRUE(renderer::vulkan::capture_surface_cache_snapshot(cache, unused_device, snapshot, error)) << error;
    EXPECT_EQ(snapshot.pending_depth_stencil_slot, 0xFF);
    EXPECT_FALSE(snapshot.pending_depth_stencil_stores);
    cache.resolve_ds_scene_end(true);
    ASSERT_TRUE(renderer::vulkan::capture_surface_cache_snapshot(cache, unused_device, snapshot, error)) << error;
    EXPECT_EQ(snapshot.pending_depth_stencil_slot, 0xFF);
    EXPECT_FALSE(snapshot.pending_depth_stencil_stores);
    EXPECT_TRUE(renderer::vulkan::validate_surface_cache_snapshot(snapshot, error)) << error;
}

namespace {
emucorev::savestate::GpuSnapshot make_gpu_codec_snapshot() {
    using namespace emucorev::savestate;
    GpuSnapshot snapshot;
    snapshot.mapping_method = 0;
    snapshot.resolution = 2.0f;
    snapshot.last_scene = 17;
    snapshot.current_frame = 2;
    snapshot.surfaces.colors.resize(renderer::vulkan::surface_cache_snapshot_slots);
    snapshot.surfaces.depth_stencil.resize(renderer::vulkan::surface_cache_snapshot_slots);
    for (uint8_t slot = 0; slot < renderer::vulkan::surface_cache_snapshot_slots; ++slot) {
        snapshot.surfaces.color_mru_order.push_back(slot);
        snapshot.surfaces.depth_stencil_mru_order.push_back(slot);
    }
    GpuBufferSnapshot buffer;
    buffer.address = 0x10000;
    buffer.size = 4096;
    buffer.pages.push_back({ 0, std::vector<uint8_t>(4096, 0x5A) });
    buffer.pages.push_back({ 4096, std::vector<uint8_t>(4096, 0xA5) }); // The double-buffer guard page.
    snapshot.buffers.push_back(std::move(buffer));
    snapshot.clocks.push_back({ 0x20000, 123, 456 });
    snapshot.clocks.push_back({ 0x21000, 124, 457 });

    renderer::vulkan::VKContextSnapshot active;
    active.context_address = 0x20000;
    active.render_target_address = 0x40000;
    active.recording_open = true;
    active.scene_wrote_depth = true;
    active.scene_macroblock_flushed = true;
    active.is_first_scene_draw = false;
    active.surface_downscale = 0.5f;
    active.viewport_x = 1.25f;
    active.viewport_y = -2.5f;
    active.viewport_width = 640.0f;
    active.viewport_height = 360.0f;
    active.viewport_min_depth = 0.125f;
    active.viewport_max_depth = 0.875f;
    active.scissor_x = 3;
    active.scissor_y = 5;
    active.scissor_width = 620;
    active.scissor_height = 350;
    active.last_macroblock_x = 7;
    active.last_macroblock_y = 9;
    active.ignore_macroblock = true;
    active.rendered_rect_x0 = 10;
    active.rendered_rect_y0 = 11;
    active.rendered_rect_x1 = 120;
    active.rendered_rect_y1 = 130;
    active.draw_rect_x0 = 20;
    active.draw_rect_y0 = 21;
    active.draw_rect_x1 = 220;
    active.draw_rect_y1 = 230;
    active.visibility_buffer_address = 0x50000;
    active.visibility_stride = 64;
    active.visibility_query_index = 7;
    active.visibility_query_increment = false;
    snapshot.contexts.push_back(active);

    renderer::vulkan::VKContextSnapshot inactive;
    inactive.context_address = 0x21000;
    inactive.viewport_x = 8.0f;
    inactive.viewport_width = 320.0f;
    inactive.scissor_width = 320;
    inactive.visibility_query_index = 3;
    inactive.visibility_query_increment = false;
    snapshot.contexts.push_back(inactive);
    return snapshot;
}
} // namespace

TEST(GpuStateCodec, RoundTripPreservesDirtyAndGuardPageBytesWithoutImageHandles) {
    using namespace emucorev::savestate;
    auto original = make_gpu_codec_snapshot();
    std::string error;
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(encode_gpu_state(original, encoded, error)) << error;
    GpuSnapshot restored;
    ASSERT_TRUE(parse_gpu_state(encoded, restored, error)) << error;
    ASSERT_EQ(restored.buffers.size(), 1u);
    ASSERT_EQ(restored.buffers[0].pages.size(), 2u);
    EXPECT_EQ(restored.buffers[0].pages[0].offset, 0u);
    EXPECT_EQ(restored.buffers[0].pages[0].bytes, original.buffers[0].pages[0].bytes);
    EXPECT_EQ(restored.buffers[0].pages[1].offset, original.buffers[0].size);
    EXPECT_EQ(restored.buffers[0].pages[1].bytes, original.buffers[0].pages[1].bytes);
    ASSERT_EQ(restored.clocks.size(), 2u);
    EXPECT_EQ(restored.clocks[0].frame, 123u);
    EXPECT_EQ(restored.clocks[0].scene, 456u);
    ASSERT_EQ(restored.contexts.size(), 2u);
    const auto &active = restored.contexts[0];
    EXPECT_EQ(active.context_address, 0x20000u);
    EXPECT_EQ(active.render_target_address, 0x40000u);
    EXPECT_TRUE(active.recording_open);
    EXPECT_TRUE(active.scene_wrote_depth);
    EXPECT_TRUE(active.scene_macroblock_flushed);
    EXPECT_FALSE(active.is_first_scene_draw);
    EXPECT_FLOAT_EQ(active.surface_downscale, 0.5f);
    EXPECT_FLOAT_EQ(active.viewport_x, 1.25f);
    EXPECT_FLOAT_EQ(active.viewport_y, -2.5f);
    EXPECT_FLOAT_EQ(active.viewport_width, 640.0f);
    EXPECT_FLOAT_EQ(active.viewport_height, 360.0f);
    EXPECT_FLOAT_EQ(active.viewport_min_depth, 0.125f);
    EXPECT_FLOAT_EQ(active.viewport_max_depth, 0.875f);
    EXPECT_EQ(active.scissor_x, 3);
    EXPECT_EQ(active.scissor_y, 5);
    EXPECT_EQ(active.scissor_width, 620u);
    EXPECT_EQ(active.scissor_height, 350u);
    EXPECT_EQ(active.last_macroblock_x, 7u);
    EXPECT_EQ(active.last_macroblock_y, 9u);
    EXPECT_TRUE(active.ignore_macroblock);
    EXPECT_EQ(active.rendered_rect_x0, 10);
    EXPECT_EQ(active.rendered_rect_y0, 11);
    EXPECT_EQ(active.rendered_rect_x1, 120);
    EXPECT_EQ(active.rendered_rect_y1, 130);
    EXPECT_EQ(active.draw_rect_x0, 20);
    EXPECT_EQ(active.draw_rect_y0, 21);
    EXPECT_EQ(active.draw_rect_x1, 220);
    EXPECT_EQ(active.draw_rect_y1, 230);
    EXPECT_EQ(active.visibility_buffer_address, 0x50000u);
    EXPECT_EQ(active.visibility_stride, 64u);
    EXPECT_EQ(active.visibility_query_index, 7);
    EXPECT_FALSE(active.visibility_query_increment);
    const auto &inactive = restored.contexts[1];
    EXPECT_FALSE(inactive.recording_open);
    EXPECT_FLOAT_EQ(inactive.viewport_x, 8.0f);
    EXPECT_FLOAT_EQ(inactive.viewport_width, 320.0f);
    EXPECT_EQ(inactive.scissor_width, 320u);
    EXPECT_EQ(inactive.visibility_query_index, 3);
    EXPECT_FALSE(inactive.visibility_query_increment);
    EXPECT_EQ(restored.surfaces.color_mru_order, original.surfaces.color_mru_order);
}

TEST(GpuStateCodec, EveryTruncationAndTrailingBytesAreRejectedWithoutPublishing) {
    using namespace emucorev::savestate;
    std::string error;
    std::vector<uint8_t> encoded;
    ASSERT_TRUE(encode_gpu_state(make_gpu_codec_snapshot(), encoded, error)) << error;
    GpuSnapshot output;
    output.last_scene = 0x123456;
    auto *const original_output = &output;
    for (size_t size = 0; size < encoded.size(); ++size) {
        std::vector<uint8_t> truncated(encoded.begin(), encoded.begin() + size);
        EXPECT_FALSE(parse_gpu_state(truncated, output, error)) << "truncation at " << size;
        EXPECT_EQ(&output, original_output);
        EXPECT_EQ(output.last_scene, 0x123456);
    }
    encoded.push_back(0);
    EXPECT_FALSE(parse_gpu_state(encoded, output, error));
    EXPECT_EQ(output.last_scene, 0x123456);
}

TEST(GpuStateBufferDiff, CapturesOnlyChangedMappedPagesAndNonzeroGuardPage) {
    using namespace emucorev::savestate;
    constexpr uint32_t mapping_size = 4096;
    std::vector<uint8_t> cpu(mapping_size + 4096, 0x11);
    std::vector<uint8_t> gpu = cpu;
    gpu[37] ^= 0xFF;
    gpu[mapping_size + 19] = 0x7B;

    std::vector<GpuBufferPage> pages;
    std::string error;
    ASSERT_TRUE(capture_gpu_buffer_pages(cpu.data(), gpu.data(), mapping_size,
                   gpu.size(), 2 * 4096, pages, error)) << error;
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_EQ(pages[0].offset, 0u);
    EXPECT_EQ(pages[0].bytes, std::vector<uint8_t>(gpu.begin(), gpu.begin() + mapping_size));
    EXPECT_EQ(pages[1].offset, mapping_size);
    EXPECT_EQ(pages[1].bytes, std::vector<uint8_t>(gpu.begin() + mapping_size, gpu.end()));
}

TEST(GpuStateBufferDiff, IdenticalMirrorAndZeroGuardProduceNoPages) {
    using namespace emucorev::savestate;
    constexpr uint32_t mapping_size = 2 * 4096;
    std::vector<uint8_t> cpu(mapping_size + 4096, 0x3C);
    std::vector<uint8_t> gpu = cpu;
    std::fill(gpu.begin() + mapping_size, gpu.end(), 0);

    std::vector<GpuBufferPage> pages;
    std::string error;
    ASSERT_TRUE(capture_gpu_buffer_pages(cpu.data(), gpu.data(), mapping_size,
                   gpu.size(), 0, pages, error)) << error;
    EXPECT_TRUE(pages.empty());
}

TEST(GpuStateBufferDiff, RejectsBadLayoutOrInsufficientBudgetWithoutPartialOutput) {
    using namespace emucorev::savestate;
    constexpr uint32_t mapping_size = 4096;
    std::vector<uint8_t> cpu(mapping_size + 4096, 0);
    std::vector<uint8_t> gpu = cpu;
    gpu[0] = 1;

    std::vector<GpuBufferPage> pages{{ 123, { 0xA5 } }};
    std::string error;
    EXPECT_FALSE(capture_gpu_buffer_pages(cpu.data(), gpu.data(), mapping_size,
        gpu.size(), mapping_size - 1, pages, error));
    ASSERT_EQ(pages.size(), 1u);
    EXPECT_EQ(pages[0].offset, 123u);
    EXPECT_EQ(pages[0].bytes, std::vector<uint8_t>{ 0xA5 });

    EXPECT_FALSE(capture_gpu_buffer_pages(cpu.data(), gpu.data(), mapping_size - 1,
        gpu.size(), gpu.size(), pages, error));
    EXPECT_FALSE(capture_gpu_buffer_pages(cpu.data(), gpu.data(), mapping_size,
        gpu.size() - 1, gpu.size(), pages, error));
}

TEST(GpuContextCheckpoint, AcceptsOnlyEmptyBetweenBatchRecordings) {
    using namespace renderer::vulkan;
    VKContextSnapshot snapshot;
    snapshot.context_address = 0x1000;
    snapshot.render_target_address = 0x2000;
    snapshot.recording_open = true;
    snapshot.visibility_buffer_address = 0x3000;
    snapshot.visibility_stride = 16;
    snapshot.visibility_query_index = 3;
    std::string error;
    EXPECT_TRUE(validate_vk_context_snapshot(snapshot, error)) << error;

    snapshot.scene_has_drawn = true;
    EXPECT_FALSE(validate_vk_context_snapshot(snapshot, error));
    snapshot.scene_has_drawn = false;
    snapshot.visibility_stride = 12;
    EXPECT_FALSE(validate_vk_context_snapshot(snapshot, error));
    snapshot.visibility_stride = 16;
    snapshot.visibility_query_index = 4;
    EXPECT_FALSE(validate_vk_context_snapshot(snapshot, error));
    snapshot.visibility_query_index = 0;
    snapshot.visibility_stride = kMaxVisibilityStrideInSnapshot + sizeof(uint32_t);
    EXPECT_FALSE(validate_vk_context_snapshot(snapshot, error));
    snapshot.visibility_stride = kMaxVisibilityStrideInSnapshot;
    EXPECT_TRUE(validate_vk_context_snapshot(snapshot, error)) << error;
}

TEST(GpuContextCheckpoint, AcceptsNegativeViewportHeightUsedByGxm) {
    using namespace renderer::vulkan;
    VKContextSnapshot snapshot;
    snapshot.context_address = 0x1000;
    snapshot.render_target_address = 0x2000;
    snapshot.recording_open = true;
    // sync_viewport_real() maps negative GXM yScale to a negative VkViewport height;
    // VK_KHR_maintenance1 is required by the renderer for this exact convention.
    snapshot.viewport_y = 544.0f;
    snapshot.viewport_height = -544.0f;
    std::string error;
    EXPECT_TRUE(validate_vk_context_snapshot(snapshot, error)) << error;
}

TEST(GpuContextCheckpoint, PreservesPendingVisibilitySelectionWithoutPoolYet) {
    using namespace renderer::vulkan;
    VKContextSnapshot snapshot;
    snapshot.context_address = 0x1000;
    snapshot.render_target_address = 0x2000;
    snapshot.recording_open = true;
    snapshot.visibility_query_index = 7;
    snapshot.visibility_query_increment = false;
    std::string error;
    EXPECT_TRUE(validate_vk_context_snapshot(snapshot, error)) << error;

    snapshot.viewport_width = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(validate_vk_context_snapshot(snapshot, error));

    snapshot.viewport_width = 0.0f;
    snapshot.visibility_query_index = static_cast<int32_t>(kMaxVisibilityQueriesInSnapshot);
    EXPECT_FALSE(validate_vk_context_snapshot(snapshot, error));
}

TEST(RendererCommandSnapshot, TransferCopyPayloadIsDeepAndGuestRangesAreBounded) {
    renderer::Command command{};
    command.opcode = renderer::CommandOpcode::TransferCopy;
    SceGxmTransferImage images[2]{};
    images[0] = { SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, Ptr<void>(0x1000), 1, 1, 4, 2, 32 };
    images[1] = { SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR, Ptr<void>(0x2000), 0, 0, 4, 2, 32 };
    uint32_t key_value = 0, key_mask = 0;
    auto key_mode = SCE_GXM_TRANSFER_COLORKEY_NONE;
    const SceGxmTransferImage *image_ptr = images;
    auto src_type = SCE_GXM_TRANSFER_LINEAR;
    auto dst_type = SCE_GXM_TRANSFER_LINEAR;
    renderer::CommandHelper helper(&command);
    ASSERT_TRUE(helper.push(key_value));
    ASSERT_TRUE(helper.push(key_mask));
    ASSERT_TRUE(helper.push(key_mode));
    ASSERT_TRUE(helper.push(image_ptr));
    ASSERT_TRUE(helper.push(src_type));
    ASSERT_TRUE(helper.push(dst_type));

    renderer::CommandSnapshot saved;
    std::string error;
    ASSERT_TRUE(renderer::capture_command_snapshot(command, {}, {}, nullptr, saved, error)) << error;
    images[0].address = Ptr<void>(0xDEAD);
    EXPECT_EQ(saved.transfer_src.address, 0x1000u);
    EXPECT_EQ(saved.transfer_dst.address, 0x2000u);

    std::vector<renderer::CommandGuestRange> ranges;
    ASSERT_TRUE(renderer::command_snapshot_guest_ranges(saved, ranges, error)) << error;
    ASSERT_EQ(ranges.size(), 2u);
    EXPECT_EQ(ranges[0].address, 0x1000u);
    EXPECT_EQ(ranges[0].size, 84u);
    EXPECT_EQ(ranges[1].address, 0x2000u);
    EXPECT_EQ(ranges[1].size, 48u);

    renderer::Command restored{};
    ASSERT_TRUE(renderer::restore_command_snapshot(saved, {}, {}, nullptr, restored, error)) << error;
    renderer::CommandHelper restored_helper(&restored);
    EXPECT_EQ(restored_helper.pop<uint32_t>(), 0u);
    EXPECT_EQ(restored_helper.pop<uint32_t>(), 0u);
    EXPECT_EQ(restored_helper.pop<SceGxmTransferColorKeyMode>(), SCE_GXM_TRANSFER_COLORKEY_NONE);
    const auto *restored_images = restored_helper.pop<const SceGxmTransferImage *>();
    ASSERT_NE(restored_images, images);
    EXPECT_EQ(restored_images[0].address.address(), 0x1000u);
    EXPECT_EQ(restored_images[1].address.address(), 0x2000u);
    renderer::destroy_command_payload(restored);
}

TEST(RendererCommandSnapshot, NewFrameCopiesDisplayValueAndResolvesContextWithoutHostPointers) {
    DisplayState display;
    renderer::Context *context = reinterpret_cast<renderer::Context *>(uintptr_t(0x1234));
    DisplayFrameInfo frame_storage{};
    auto *frame = &frame_storage;
    frame->base = Ptr<const void>(0x8000);
    frame->pitch = 640;
    frame->pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    frame->image_size = { 320, 180 };

    renderer::Command command{};
    command.opcode = renderer::CommandOpcode::NewFrame;
    renderer::CommandHelper helper(&command);
    DisplayFrameInfo *frame_arg = frame;
    DisplayState *display_arg = &display;
    renderer::Context *context_arg = context;
    ASSERT_TRUE(helper.push(frame_arg));
    ASSERT_TRUE(helper.push(display_arg));
    ASSERT_TRUE(helper.push(context_arg));

    renderer::CommandSnapshot saved;
    std::string error;
    ASSERT_TRUE(renderer::capture_command_snapshot(command, {},
        [&](const renderer::Context *ptr) -> Address { return ptr == context ? 0x30000 : 0; },
        &display, saved, error)) << error;
    frame->base = Ptr<const void>(0x9000);
    frame->pitch = 1280;
    ASSERT_TRUE(saved.has_display_frame);
    EXPECT_EQ(saved.display_frame.base, 0x8000u);
    EXPECT_EQ(saved.new_frame_context_address, 0x30000u);

    renderer::Command restored{};
    ASSERT_TRUE(renderer::restore_command_snapshot(saved, {},
        [&](Address address) -> renderer::Context * { return address == 0x30000 ? context : nullptr; },
        &display, restored, error)) << error;
    renderer::CommandHelper restored_helper(&restored);
    auto *restored_frame = restored_helper.pop<DisplayFrameInfo *>();
    EXPECT_NE(restored_frame, frame);
    EXPECT_EQ(restored_frame->base.address(), 0x8000u);
    EXPECT_EQ(restored_frame->pitch, 640u);
    EXPECT_EQ(restored_frame->image_size.x, 320);
    EXPECT_EQ(restored_frame->image_size.y, 180);
    EXPECT_EQ(restored_helper.pop<DisplayState *>(), &display);
    EXPECT_EQ(restored_helper.pop<renderer::Context *>(), context);
    std::vector<renderer::CommandGuestRange> ranges;
    ASSERT_TRUE(renderer::command_snapshot_guest_ranges(saved, ranges, error)) << error;
    ASSERT_EQ(ranges.size(), 1u);
    EXPECT_EQ(ranges[0].address, 0x8000u);
    EXPECT_EQ(ranges[0].size, 640u * 180u * 4u);
    renderer::destroy_command_payload(restored);

    EXPECT_FALSE(renderer::capture_command_snapshot(command, {}, {}, nullptr, saved, error));
}

TEST(RendererCommandSnapshot, DownscaleAndFillRestoreTheirOwnedImagePayloads) {
    std::string error;
    renderer::CommandSnapshot saved;
    auto *src = new SceGxmTransferImage{ SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
        Ptr<void>(0x10000), 0, 0, 8, 4, 32 };
    auto *dst = new SceGxmTransferImage{ SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
        Ptr<void>(0x20000), 0, 0, 4, 2, 16 };
    renderer::Command downscale{};
    downscale.opcode = renderer::CommandOpcode::TransferDownscale;
    renderer::CommandHelper downscale_args(&downscale);
    ASSERT_TRUE(downscale_args.push(src));
    ASSERT_TRUE(downscale_args.push(dst));
    ASSERT_TRUE(renderer::capture_command_snapshot(downscale, {}, {}, nullptr, saved, error)) << error;
    delete src;
    delete dst;
    renderer::Command restored_downscale{};
    ASSERT_TRUE(renderer::restore_command_snapshot(saved, {}, {}, nullptr, restored_downscale, error)) << error;
    renderer::CommandHelper restored_downscale_args(&restored_downscale);
    auto *restored_src = restored_downscale_args.pop<SceGxmTransferImage *>();
    auto *restored_dst = restored_downscale_args.pop<SceGxmTransferImage *>();
    EXPECT_EQ(restored_src->address.address(), 0x10000u);
    EXPECT_EQ(restored_dst->width, 4u);
    renderer::destroy_command_payload(restored_downscale);

    auto *fill_dst = new SceGxmTransferImage{ SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR,
        Ptr<void>(0x30000), 0, 0, 5, 3, 32 };
    renderer::Command fill{};
    fill.opcode = renderer::CommandOpcode::TransferFill;
    renderer::CommandHelper fill_args(&fill);
    uint32_t fill_color = 0xAABBCCDD;
    ASSERT_TRUE(fill_args.push(fill_color));
    const SceGxmTransferImage *fill_dst_arg = fill_dst;
    ASSERT_TRUE(fill_args.push(fill_dst_arg));
    ASSERT_TRUE(renderer::capture_command_snapshot(fill, {}, {}, nullptr, saved, error)) << error;
    delete fill_dst;
    EXPECT_EQ(saved.transfer_fill_color, 0xAABBCCDDu);
    renderer::Command restored_fill{};
    ASSERT_TRUE(renderer::restore_command_snapshot(saved, {}, {}, nullptr, restored_fill, error)) << error;
    renderer::CommandHelper restored_fill_args(&restored_fill);
    EXPECT_EQ(restored_fill_args.pop<uint32_t>(), 0xAABBCCDDu);
    EXPECT_EQ(restored_fill_args.pop<const SceGxmTransferImage *>()->address.address(), 0x30000u);
    renderer::destroy_command_payload(restored_fill);
}

TEST(RendererCommandSnapshot, ScreenFilterIsDeepCopiedAndSizeLimited) {
    std::string filter_storage("nearest");
    auto *filter = &filter_storage;
    renderer::Command command{};
    command.opcode = renderer::CommandOpcode::SetScreenFilter;
    renderer::CommandHelper helper(&command);
    std::string *filter_arg = filter;
    ASSERT_TRUE(helper.push(filter_arg));
    renderer::CommandSnapshot saved;
    std::string error;
    ASSERT_TRUE(renderer::capture_command_snapshot(command, {}, {}, nullptr, saved, error)) << error;
    *filter = "linear";
    EXPECT_EQ(saved.screen_filter, "nearest");

    renderer::Command restored{};
    ASSERT_TRUE(renderer::restore_command_snapshot(saved, {}, {}, nullptr, restored, error)) << error;
    renderer::CommandHelper restored_helper(&restored);
    auto *restored_filter = restored_helper.pop<std::string *>();
    ASSERT_NE(restored_filter, filter);
    EXPECT_EQ(*restored_filter, "nearest");
    renderer::destroy_command_payload(restored);

    saved.screen_filter.assign(4097, 'x');
    EXPECT_FALSE(renderer::validate_command_snapshot(saved, error));
}
