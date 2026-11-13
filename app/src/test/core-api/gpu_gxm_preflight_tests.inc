#include <emucorev/savestate/gpu_state_io.h>
#include <emucorev/savestate/gxm_state_io.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>

namespace {
std::unique_ptr<emucorev::savestate::MemoryImage> make_gpu_depth_test_memory(
    const fs::path &root, std::string &error) {
    using namespace emucorev::savestate;
    constexpr uint32_t base = 0x10000000;
    constexpr uint32_t bytes = 2u << 20;
    BufferWriter data;
    data.u32(kMemoryImageFormatVersion);
    data.u32(bytes);
    data.u32(1);
    data.u32(base);
    data.u32(bytes / kMemoryImagePageSize);
    data.u64(base);
    data.u32(bytes);
    data.u32(static_cast<uint32_t>(MemoryChunkEncoding::Zeros));
    data.u32(0);
    data.u32(0);

    const auto path = root / "gpu-depth-memory.ecvs";
    Writer writer;
    if (!writer.open(path, error)
        || !writer.write_section(SectionId::Memory, data.data().data(), data.size(), false, error)
        || !writer.finalize(error))
        return {};
    Reader reader;
    if (!reader.open(path, error) || !reader.validate_sections(error))
        return {};
    return MemoryImage::preflight(reader, root, error);
}

emucorev::savestate::GpuSnapshot make_gpu_depth_test_snapshot(bool stored) {
    using namespace emucorev::savestate;
    GpuSnapshot snapshot;
    snapshot.surfaces.colors.resize(renderer::vulkan::surface_cache_snapshot_slots);
    snapshot.surfaces.depth_stencil.resize(renderer::vulkan::surface_cache_snapshot_slots);
    for (uint8_t slot = 0; slot < renderer::vulkan::surface_cache_snapshot_slots; ++slot) {
        snapshot.surfaces.color_mru_order.push_back(slot);
        snapshot.surfaces.depth_stencil_mru_order.push_back(slot);
    }

    auto &depth = snapshot.surfaces.depth_stencil[0];
    depth.active = true;
    depth.surface.depth_data = Ptr<void>(0x10000000);
    depth.surface.set_stride(1920);
    depth.surface.set_type(SCE_GXM_DEPTH_STENCIL_SURFACE_TILED);
    depth.surface.set_format(SCE_GXM_DEPTH_STENCIL_FORMAT_D16);
    depth.memory_width = 1920;
    depth.memory_height = 1088; // Physical height for 4x MSAA; guest backing has 544 rows.
    depth.stride_samples = 1920;
    depth.multisample_mode = SCE_GXM_MULTISAMPLE_4X;
    depth.depth_content_stored = stored;
    depth.total_bytes = 1920u * 1088u * 2u;
    depth.tiling = renderer::vulkan::SurfaceTiling::Tiled;
    depth.texture.present = true;
    depth.texture.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment
        | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;
    depth.texture.image.width = depth.texture.image.height = 1;
    depth.texture.image.format = vk::Format::eD16Unorm;
    depth.texture.image.layout = vkutil::ImageLayout::DepthStencilReadOnly;
    depth.texture.image.bytes = { 0x34, 0x12 };
    return snapshot;
}
} // namespace

TEST(GpuGxmPreflight, LegacyGpuSchemaTwoCodecPreservesInlineBufferPages) {
    using namespace emucorev::savestate;
    GpuSnapshot source;
    source.surfaces.colors.resize(renderer::vulkan::surface_cache_snapshot_slots);
    source.surfaces.depth_stencil.resize(renderer::vulkan::surface_cache_snapshot_slots);
    for (uint8_t slot = 0; slot < renderer::vulkan::surface_cache_snapshot_slots; ++slot) {
        source.surfaces.color_mru_order.push_back(slot);
        source.surfaces.depth_stencil_mru_order.push_back(slot);
    }
    GpuSnapshotBuffer buffer;
    buffer.address = 0x10000000;
    buffer.size = kGpuSnapshotPageSize;
    GpuSnapshotPage page;
    page.offset = 0;
    page.bytes.resize(kGpuSnapshotPageSize);
    for (size_t i = 0; i < page.bytes.size(); ++i)
        page.bytes[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xFF);
    const auto expected_page = page.bytes;
    buffer.pages.push_back(std::move(page));
    source.buffers.push_back(std::move(buffer));

    std::vector<uint8_t> encoded;
    std::string error;
    ASSERT_TRUE(encode_gpu_state(source, encoded, error)) << error;

    GpuSnapshot decoded;
    ASSERT_TRUE(parse_gpu_state(encoded, decoded, error)) << error;
    EXPECT_EQ(decoded.schema_version, 2u);
    ASSERT_EQ(decoded.buffers.size(), 1u);
    EXPECT_EQ(decoded.buffers[0].address, 0x10000000u);
    EXPECT_EQ(decoded.buffers[0].size, kGpuSnapshotPageSize);
    ASSERT_EQ(decoded.buffers[0].pages.size(), 1u);
    EXPECT_EQ(decoded.buffers[0].pages[0].offset, 0u);
    EXPECT_EQ(decoded.buffers[0].pages[0].bytes, expected_page);
}

TEST(GpuGxmPreflight, FourXDepthBackingUsesGuestHeightAndRetainsCapturedGpuImage) {
    using namespace emucorev::savestate;
    const auto root = fs::temp_directory_path() / fs::unique_path("gpu-depth-preflight-%%%%-%%%%");
    ASSERT_TRUE(fs::create_directories(root));
    std::string error;
    auto memory = make_gpu_depth_test_memory(root, error);
    ASSERT_TRUE(memory) << error;
    constexpr Address depth_address = 0x10000000;
    constexpr uint64_t guest_bytes = 1920u * 544u * 2u;
    EXPECT_TRUE(memory->contains(depth_address, guest_bytes));
    EXPECT_FALSE(memory->contains(depth_address, 1920u * 1088u * 2u));

    for (const bool stored : { false, true }) {
        auto snapshot = make_gpu_depth_test_snapshot(stored);
        ASSERT_TRUE(validate_gpu_state(snapshot, *memory, error)) << error;
        EXPECT_EQ(snapshot.surfaces.depth_stencil[0].texture.image.bytes,
            (std::vector<uint8_t>{ 0x34, 0x12 }));

        snapshot.surfaces.depth_stencil[0].texture.present = false;
        snapshot.surfaces.depth_stencil[0].texture.image = {};
        EXPECT_FALSE(validate_gpu_state(snapshot, *memory, error));
        EXPECT_NE(error.find("surface cache"), std::string::npos) << error;
    }
    memory.reset();
    fs::remove_all(root);
}

TEST(GpuGxmPreflight, RejectsScaledMacroblocksThatWouldTruncateOrOverflowToZero) {
    using namespace emucorev::savestate;
    GxmSnapshot gxm;
    gxm::RenderTargetSnapshot target;
    target.address = 0x1000;
    target.params.width = target.params.height = 128;
    target.params.flags = SCE_GXM_RENDER_TARGET_MACROTILE_SYNC | (4 << 8) | (4 << 12);
    gxm.render_targets.push_back(target);
    GpuSnapshot gpu;
    gpu.resolution = 1;
    GpuTargetSnapshot image;
    image.address = 0x1000;
    image.color.width = image.color.height = 128;
    image.color.format = vk::Format::eR8G8B8A8Unorm;
    image.depth_stencil.width = image.depth_stencil.height = 128;
    gpu.targets.push_back(image);
    std::string error;
    ASSERT_TRUE(validate_gpu_gxm_references(gpu, gxm, error)) << error;
    gpu.resolution = 0.01f;
    gpu.targets[0].color.width = gpu.targets[0].color.height = 1;
    gpu.targets[0].depth_stencil.width = gpu.targets[0].depth_stencil.height = 1;
    EXPECT_FALSE(validate_gpu_gxm_references(gpu, gxm, error));
    gpu.resolution = 16;
    gxm.render_targets[0].params.width = gxm.render_targets[0].params.height = 4096;
    gxm.render_targets[0].params.flags = SCE_GXM_RENDER_TARGET_MACROTILE_SYNC | (1 << 8) | (1 << 12);
    gpu.targets[0].color.width = gpu.targets[0].color.height = 65536;
    gpu.targets[0].depth_stencil.width = gpu.targets[0].depth_stencil.height = 65536;
    EXPECT_FALSE(validate_gpu_gxm_references(gpu, gxm, error));
}
#include <emucorev/savestate/scoped_gpu_context.h>

TEST(GpuGxmPreflight, EachCheckpointSelectsItsOwnContextAndRestoresPreviousSelection) {
    SnapshotBatchTestRenderer state;
    renderer::Context first, second, selected;
    state.context = &selected;
    std::vector<renderer::Context *> observed;
    for (auto *checkpoint : { &first, &second }) {
        const emucorev::savestate::ScopedGpuContextSelection selection(state.context, *checkpoint);
        // Framebuffer reconstruction reads State.context rather than its caller's context argument.
        observed.push_back(state.context);
    }
    EXPECT_EQ(observed, (std::vector<renderer::Context *>{ &first, &second }));
    EXPECT_EQ(state.context, &selected);
}

TEST(GpuGxmPreflight, FailedCheckpointRestoresPreviousContextSelection) {
    SnapshotBatchTestRenderer state;
    renderer::Context checkpoint, selected;
    state.context = &selected;
    const auto restore = [&]() {
        const emucorev::savestate::ScopedGpuContextSelection selection(state.context, checkpoint);
        EXPECT_EQ(state.context, &checkpoint);
        return false; // The real checkpoint restorer can reject reconstruction.
    };
    EXPECT_FALSE(restore());
    EXPECT_EQ(state.context, &selected);
    state.context = nullptr;
    EXPECT_FALSE(restore());
    EXPECT_EQ(state.context, nullptr);
}
#include <renderer/vulkan/frame_runtime_reset.h>
#include <renderer/vulkan/snapshot_request_barrier.h>
#include <renderer/vulkan/types.h>

TEST(GpuGxmPreflight, FrameResetDropsAllOldFenceAliasesBeforeDestroyingAnyFrame) {
    std::array<renderer::vulkan::FrameObject, 3> frames{};
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].rendered_fences.push_back(vk::Fence(std::bit_cast<VkFence>(uint64_t(i + 1))));
        frames[i].pool_epoch = 100 + i;
        frames[i].vert_descriptors[0].descriptors_idx = 7;
        frames[i].frag_descriptors[15].descriptors_idx = 9;
        frames[i].color_descriptor.descriptors_idx = 11;
        frames[i].color_descriptor.sets.push_back(vk::DescriptorSet{});
    }
    size_t drained = 0, reset = 0;
    renderer::vulkan::reset_frame_runtime_after_idle(frames,
        [&](auto &) {
            // A different frame may retain the same target fence being destroyed here.
            for (const auto &frame : frames) EXPECT_TRUE(frame.rendered_fences.empty());
            EXPECT_EQ(reset, 0u);
            ++drained;
        },
        [&](auto &) { EXPECT_EQ(drained, frames.size()); ++reset; });
    EXPECT_EQ(drained, frames.size());
    EXPECT_EQ(reset, frames.size());
    for (const auto &frame : frames) {
        EXPECT_EQ(frame.pool_epoch, 0u);
        for (const auto &descriptor : frame.vert_descriptors) EXPECT_EQ(descriptor.descriptors_idx, 0);
        for (const auto &descriptor : frame.frag_descriptors) EXPECT_EQ(descriptor.descriptors_idx, 0);
        EXPECT_EQ(frame.color_descriptor.descriptors_idx, 0);
        EXPECT_EQ(frame.color_descriptor.sets.size(), 1u); // Pool-owned sets remain reusable.
    }
}
TEST(GpuGxmPreflight, RequestTeardownDrainsThenWakesWorkerAndResetStartsFreshGeneration) {
    using namespace renderer::vulkan;
    Queue<WaitThreadRequest> queue;
    const auto worker_body = [&]() {
        while (auto request = queue.pop()) {
            if (auto *callback = std::get_if<CallbackRequest>(request.get()); callback && callback->callback) {
                (*callback->callback)();
                delete callback->callback;
            }
        }
    };
    auto old_worker = std::async(std::launch::async, worker_body);
    std::atomic<int> completed = 0;
    stop_gpu_requests_after_idle(queue, [&]() {
        EXPECT_FALSE(queue.is_aborted());
        auto drained = std::make_shared<std::promise<void>>();
        auto ready = drained->get_future();
        queue.push(CallbackRequest{ new CallbackRequestFunction([&, drained]() { ++completed; drained->set_value(); }), true });
        EXPECT_EQ(ready.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    });
    const auto stopped = old_worker.wait_for(std::chrono::seconds(1));
    queue.abort(); // Bounded cleanup even if abort is accidentally removed from the helper.
    old_worker.get();
    EXPECT_EQ(stopped, std::future_status::ready);
    EXPECT_EQ(completed.load(), 1);
    reset_gpu_requests_after_join(queue);
    EXPECT_FALSE(queue.is_aborted());
    auto new_worker = std::async(std::launch::async, worker_body);
    queue.push(CallbackRequest{ new CallbackRequestFunction([&]() { ++completed; }), true });
    stop_gpu_requests_after_idle(queue, [&]() {
        auto drained = std::make_shared<std::promise<void>>();
        auto ready = drained->get_future();
        queue.push(CallbackRequest{ new CallbackRequestFunction([drained]() { drained->set_value(); }), true });
        EXPECT_EQ(ready.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    });
    queue.abort();
    new_worker.get();
    EXPECT_EQ(completed.load(), 2);
}

TEST(GpuGxmPreflight, RequestResetReleasesAbandonedCallbackWithoutExecutingIt) {
    using namespace renderer::vulkan;
    Queue<WaitThreadRequest> queue;
    auto owner = std::make_shared<int>(1);
    std::weak_ptr<int> lifetime = owner;
    int calls = 0;
    queue.push(CallbackRequest{ new CallbackRequestFunction([owner, &calls]() { ++calls; }), false });
    owner.reset();
    queue.abort();
    reset_gpu_requests_after_join(queue);
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(calls, 0);
    EXPECT_EQ(queue.size(), 0u);
    EXPECT_FALSE(queue.is_aborted());
}
TEST(GpuGxmPreflight, TeardownJoinsEveryRequestReaderBeforeUnmappingOldGpuMemory) {
    using namespace renderer::vulkan;
    Queue<WaitThreadRequest> queue;
    auto held_writeback = std::make_shared<std::promise<void>>();
    auto release_writeback = std::make_shared<std::promise<void>>();
    auto release = release_writeback->get_future().share();
    queue.push(CallbackRequest{ new CallbackRequestFunction([held_writeback, release]() {
        held_writeback->set_value();
        release.wait(); // Reader A is still using an old mapped buffer.
    }), false });
    const auto worker_body = [&]() {
        while (auto request = queue.pop())
            if (auto *callback = std::get_if<CallbackRequest>(request.get()); callback && callback->callback) {
                (*callback->callback)();
                delete callback->callback;
            }
    };
    std::thread reader_a(worker_body);
    auto entered = held_writeback->get_future();
    EXPECT_EQ(entered.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    std::thread reader_b(worker_body), reader_c(worker_body);
    auto marker = std::make_shared<std::promise<void>>();
    auto marked = marker->get_future();
    stop_gpu_requests_after_idle(queue, [&]() {
        queue.push(CallbackRequest{ new CallbackRequestFunction([marker]() { marker->set_value(); }), false });
        EXPECT_EQ(marked.wait_for(std::chrono::seconds(1)), std::future_status::ready);
    });
    std::array<std::thread *, 3> workers{ &reader_a, &reader_b, &reader_c };
    std::atomic<bool> unmapped = false;
    auto teardown = std::async(std::launch::async, [&]() {
        std::string error;
        const bool joined = join_gpu_request_workers_after_stop(workers, error);
        if (joined) unmapped = true;
        return joined;
    });
    const auto before_release = teardown.wait_for(std::chrono::milliseconds(20));
    EXPECT_FALSE(unmapped.load());
    release_writeback->set_value();
    EXPECT_TRUE(teardown.get());
    EXPECT_EQ(before_release, std::future_status::timeout);
    EXPECT_TRUE(unmapped.load());
    for (auto *worker : workers) {
        EXPECT_FALSE(worker->joinable());
        if (worker->joinable()) worker->join(); // Bounded cleanup on a missing-join regression.
    }
    reset_gpu_requests_after_join(queue);
}
TEST(GpuGxmPreflight, AbortedGpuRequestQueueDoesNotLeakRejectedCallbackPayload) {
    using namespace renderer::vulkan;
    Queue<WaitThreadRequest> queue;
    queue.abort();
    auto owner = std::make_shared<int>(1);
    std::weak_ptr<int> lifetime = owner;
    auto callback = std::make_unique<CallbackRequestFunction>([owner]() {});
    owner.reset();
    EXPECT_FALSE(enqueue_gpu_request_callback(queue, std::move(callback), false));
    EXPECT_TRUE(lifetime.expired());
    EXPECT_EQ(queue.size(), 0u);
}

TEST(GpuGxmPreflight, SnapshotRequestBarrierWaitsForEveryConcurrentReader) {
    using namespace renderer::vulkan;
    Queue<WaitThreadRequest> queue;
    auto earlier_started = std::make_shared<std::promise<void>>();
    auto release_earlier = std::make_shared<std::promise<void>>();
    auto earlier_release = release_earlier->get_future().share();
    queue.push(CallbackRequest{ new CallbackRequestFunction([earlier_started, earlier_release]() {
        earlier_started->set_value();
        earlier_release.wait();
    }), false });

    const auto worker_body = [&]() {
        while (auto request = queue.pop()) {
            if (auto *callback = std::get_if<CallbackRequest>(request.get()); callback && callback->callback) {
                (*callback->callback)();
                delete callback->callback;
            }
        }
    };
    std::array<std::thread, 3> workers{
        std::thread(worker_body), std::thread(worker_body), std::thread(worker_body)
    };
    auto earlier_done = earlier_started->get_future();
    const auto earlier_status = earlier_done.wait_for(std::chrono::seconds(1));
    EXPECT_EQ(earlier_status, std::future_status::ready);
    if (earlier_status != std::future_status::ready) {
        release_earlier->set_value();
        queue.abort();
        for (auto &worker : workers) if (worker.joinable()) worker.join();
        reset_gpu_requests_after_join(queue);
        return;
    }

    auto barrier = std::async(std::launch::async, [&]() {
        std::string error;
        const bool ready = wait_for_gpu_snapshot_requests(queue, workers.size(), std::chrono::seconds(2), error);
        return std::pair{ ready, error };
    });
    const auto while_reader_busy = barrier.wait_for(std::chrono::milliseconds(30));
    EXPECT_EQ(while_reader_busy, std::future_status::timeout);
    release_earlier->set_value();
    const auto result = barrier.get();

    queue.abort();
    for (auto &worker : workers) if (worker.joinable()) worker.join();
    reset_gpu_requests_after_join(queue);
    EXPECT_TRUE(result.first) << result.second;
}
#include <emucorev/savestate/physical_gpu_contexts.h>

TEST(GpuGxmPreflight, PhysicalContextTableSkipsPlainDeferredAndNullContexts) {
    struct PhysicalContext final : renderer::Context {};
    renderer::Context deferred;
    PhysicalContext immediate_a, immediate_b;
    const std::vector<std::pair<uint32_t, renderer::Context *>> runtime{
        { 0x1000, &immediate_a }, { 0x2000, &deferred }, { 0x3000, nullptr }, { 0x4000, &immediate_b }
    };
    const auto physical = emucorev::savestate::physical_gpu_contexts<PhysicalContext>(runtime);
    ASSERT_EQ(physical.size(), 2u);
    EXPECT_EQ(physical.at(0x1000), &immediate_a);
    EXPECT_EQ(physical.at(0x4000), &immediate_b);
    EXPECT_FALSE(physical.contains(0x2000));
    EXPECT_FALSE(physical.contains(0x3000));
}

TEST(GpuGxmPreflight, GpuClocksAndCheckpointsBelongOnlyToImmediateGxmContexts) {
    using namespace emucorev::savestate;
    GxmSnapshot gxm;
    gxm::ContextSnapshot immediate, deferred;
    immediate.address = 0x1000;
    deferred.address = 0x2000;
    deferred.deferred = true;
    gxm.contexts = { immediate, deferred };
    GpuSnapshot gpu;
    gpu.clocks.push_back({ 0x1000, 1, 1 });
    renderer::vulkan::VKContextSnapshot checkpoint;
    checkpoint.context_address = 0x1000;
    gpu.contexts.push_back(checkpoint);
    std::string error;
    EXPECT_TRUE(validate_gpu_gxm_references(gpu, gxm, error)) << error;
    gpu.clocks[0].address = 0x2000;
    EXPECT_FALSE(validate_gpu_gxm_references(gpu, gxm, error));
    gpu.clocks[0].address = 0x1000;
    gpu.contexts[0].context_address = 0x2000;
    EXPECT_FALSE(validate_gpu_gxm_references(gpu, gxm, error));
    gpu.contexts[0].context_address = 0x1000;
    checkpoint.context_address = 0x2000;
    gpu.contexts.push_back(checkpoint);
    EXPECT_FALSE(validate_gpu_gxm_references(gpu, gxm, error));
}
