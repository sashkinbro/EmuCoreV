#include <gxm/savestate.h>
#include <gxm/functions.h>
#include <renderer/types.h>

DECL_EXPORT(int, sceGxmDestroyDeferredContext, SceGxmContext *context);
DECL_EXPORT(int, sceGxmCreateDeferredContext, SceGxmDeferredContextParams *params, Ptr<SceGxmContext> *context);
struct SceGxmCommandList;
DECL_EXPORT(int, sceGxmEndCommandList, SceGxmContext *context, SceGxmCommandList *list);

TEST(GxmContextSnapshot, DefaultConstructionInitializesReservedMaskInPoisonedStorage) {
    alignas(GxmContextState) uint8_t storage[sizeof(GxmContextState)];
    std::memset(storage, 0xf0, sizeof(storage));
    auto *state = new (storage) GxmContextState;
    uint8_t raw_mask = 0xff;
    std::memcpy(&raw_mask, storage + offsetof(GxmContextState, writing_mask), 1);
    EXPECT_EQ(raw_mask, 0);
    state->~GxmContextState();
}

TEST(GxmContextSnapshot, PoisonedGuestPlacementDoesNotLeakReservedMaskIntoCapture) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x20000]{}, [](uint8_t *p) { delete[] p; });
    SceGxmDeferredContextParams params{};
    params.hostMem = Ptr<void>(0x1000);
    params.hostMemSize = static_cast<uint32_t>(gxm::snapshot_object_sizes().context);
    std::memset(params.hostMem.get(env.mem), 0xf0, params.hostMemSize);
    Ptr<SceGxmContext> context;
    ASSERT_EQ(export_sceGxmCreateDeferredContext(env, 0, "test", &params, &context), 0);
    struct Cleanup { EmuEnvState &env; ~Cleanup() { gxm::destroy_all_contexts(env, true); } } cleanup{env};
    // SceGxmContext starts with GxmContextState. Model a legacy live context
    // after construction so this independently exercises capture normalization.
    const uint8_t legacy_mask = 0xf0;
    std::memcpy(env.mem.memory.get() + context.address() + offsetof(GxmContextState, writing_mask), &legacy_mask, 1);
    std::vector<gxm::ContextSnapshot> snapshots;
    std::string error;
    ASSERT_TRUE(gxm::capture_contexts(env, snapshots, error)) << error;
    ASSERT_EQ(snapshots.size(), 1u);
    uint8_t raw_mask = 0xff;
    std::memcpy(&raw_mask, reinterpret_cast<const uint8_t *>(&snapshots[0].state) + offsetof(GxmContextState, writing_mask), 1);
    EXPECT_EQ(raw_mask, 0);
}

TEST(GxmContextSnapshot, DeferredRoundTripPreservesRendererStateAndAllocatorWithoutGuestCallbacks) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x10000]{}, [](uint8_t *p) { delete[] p; });
    gxm::ContextSnapshot saved;
    saved.address = 0x1000;
    saved.deferred = true;
    saved.state.type = SCE_GXM_CONTEXT_TYPE_DEFERRED;
    saved.state.vdm_memory_callback = Ptr<SceGxmDeferredContextCallback>(0x99990000);
    saved.alloc_space = Ptr<void>(0x4004);
    saved.alloc_space_end = Ptr<void>(0x5000);
    saved.renderer_record.cull_mode = SCE_GXM_CULL_CW;
    saved.renderer_record.z_scale = 0.37f;
    saved.renderer_record.region_clip_min = {13, 17};
    std::string error;
    ASSERT_TRUE(gxm::restore_contexts(env, {saved}, error)) << error;
    std::vector<gxm::ContextSnapshot> captured;
    ASSERT_TRUE(gxm::capture_contexts(env, captured, error)) << error;
    ASSERT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0].alloc_space.address(), 0x4004u);
    EXPECT_EQ(captured[0].alloc_space_end.address(), 0x5000u);
    EXPECT_EQ(captured[0].renderer_record.cull_mode, SCE_GXM_CULL_CW);
    EXPECT_FLOAT_EQ(captured[0].renderer_record.z_scale, 0.37f);
    EXPECT_EQ(captured[0].renderer_record.region_clip_min.x, 13);
    EXPECT_EQ(captured[0].renderer_record.region_clip_min.y, 17);
    const auto runtime = gxm::runtime_contexts(env);
    ASSERT_EQ(runtime.size(), 1u);
    renderer::Command pending{};
    runtime[0].second->command_list.first = &pending;
    const auto retained = captured;
    EXPECT_FALSE(gxm::capture_contexts(env, captured, error));
    EXPECT_EQ(captured[0].alloc_space.address(), retained[0].alloc_space.address());
    runtime[0].second->command_list.first = nullptr;
    EXPECT_EQ(export_sceGxmDestroyDeferredContext(env, 0, "test", Ptr<SceGxmContext>(0x1000).get(env.mem)), 0);
}

TEST(GxmContextSnapshot, MissingTargetOrActiveSceneRejectsBeforeConstructingGuestObjects) {
    EmuEnvState env;
    // No memory or renderer: rejection must precede any placement construction.
    gxm::ContextSnapshot saved;
    saved.render_target_address = 0x4000;
    std::string error;
    EXPECT_FALSE(gxm::restore_contexts(env, {saved}, error));
    EXPECT_TRUE(env.gxm.immediate_contexts.empty());
    saved.render_target_address = 0;
    saved.state.active = true;
    EXPECT_FALSE(gxm::restore_contexts(env, {saved}, error));
    EXPECT_TRUE(env.gxm.immediate_contexts.empty());
}

#include <gxm/functions.h>
#include <renderer/command_snapshot.h>

namespace {
renderer::CommandSnapshot gxm_test_cull_command() {
    renderer::CommandSnapshot command;
    command.opcode = renderer::CommandOpcode::SetState;
    const auto tag = renderer::GXMState::CullMode;
    std::memcpy(command.data.data(), &tag, sizeof(tag));
    return command;
}
}

TEST(GxmCommandSnapshot, CapturesDefinedPayloadAndRebuildsDeepSurfacePointers) {
    renderer::Command viewport{};
    std::memset(viewport.data, 0xcc, sizeof(viewport.data));
    viewport.opcode = renderer::CommandOpcode::SetState;
    viewport.status = nullptr;
    auto tag = renderer::GXMState::Viewport;
    const bool flat = true;
    std::memcpy(viewport.data, &tag, sizeof(tag));
    std::memcpy(viewport.data + sizeof(tag), &flat, sizeof(flat));
    renderer::CommandSnapshot saved;
    std::string error;
    ASSERT_TRUE(renderer::capture_command_snapshot(viewport, {}, saved, error)) << error;
    EXPECT_TRUE(std::all_of(saved.data.begin() + 3, saved.data.end(), [](auto v) { return v == 0; }));
    viewport.status = reinterpret_cast<int *>(uintptr_t(1));
    EXPECT_FALSE(renderer::capture_command_snapshot(viewport, {}, saved, error));
    viewport.status = nullptr;
    viewport.data[2] = 2;
    EXPECT_FALSE(renderer::capture_command_snapshot(viewport, {}, saved, error));

    SceGxmColorSurface color{}; color.width = 147; color.height = 33; color.data = Ptr<void>(0x8000);
    SceGxmDepthStencilSurface depth{}; depth.background_depth = 0.37f;
    auto *target = reinterpret_cast<renderer::RenderTarget *>(uintptr_t(0x1234));
    auto *c = &color; auto *d = &depth;
    renderer::Command context{}; context.opcode = renderer::CommandOpcode::SetContext;
    std::memcpy(context.data, &target, sizeof(target));
    std::memcpy(context.data + sizeof(target), &c, sizeof(c));
    std::memcpy(context.data + sizeof(target) + sizeof(c), &d, sizeof(d));
    ASSERT_TRUE(renderer::capture_command_snapshot(context, [&](const auto *ptr) -> Address { return ptr == target ? 0x4000 : 0; }, saved, error)) << error;
    renderer::Command rebuilt{};
    ASSERT_TRUE(renderer::restore_command_snapshot(saved, [&](Address a) { return a == 0x4000 ? target : nullptr; }, rebuilt, error)) << error;
    renderer::CommandHelper helper(&rebuilt);
    EXPECT_EQ(helper.pop<renderer::RenderTarget *>(), target);
    auto *restored_color = helper.pop<SceGxmColorSurface *>();
    auto *restored_depth = helper.pop<SceGxmDepthStencilSurface *>();
    EXPECT_NE(restored_color, &color); EXPECT_EQ(restored_color->width, 147);
    EXPECT_EQ(restored_color->data.address(), 0x8000u);
    EXPECT_NE(restored_depth, &depth); EXPECT_FLOAT_EQ(restored_depth->background_depth, 0.37f);
    delete restored_color; delete restored_depth;
}

TEST(GxmCommandSnapshot, SharedDeferredAliasHasOneOwnerAndStopsAtEachListTail) {
    auto a = gxm_test_cull_command(), b = gxm_test_cull_command(), c = gxm_test_cull_command();
    a.deferred_allocation = b.deferred_allocation = true;
    a.next = 2; b.next = 3;
    gxm::ContextSnapshot deferred;
    deferred.deferred = true;
    deferred.deferred_lists.push_back({0x5000, 1, 1, 2, false, {{0x6000, 0x6040}}});
    gxm::ContextSnapshot immediate;
    immediate.pending_first = 1; immediate.pending_last = 3;
    std::string error;
    EXPECT_TRUE(gxm::validate_command_graph({immediate, deferred}, {a,b,c}, error)) << error;
    deferred.deferred_lists.push_back({0x5020, 3, 1, 2, false, {{0x7000, 0x7040}}});
    EXPECT_FALSE(gxm::validate_command_graph({immediate, deferred}, {a,b,c}, error));
    deferred.deferred_lists.pop_back();
    a.next = 1;
    EXPECT_FALSE(gxm::validate_command_graph({immediate, deferred}, {a,b,c}, error));
    a.next = 2; c.next = 999;
    EXPECT_FALSE(gxm::validate_command_graph({immediate, deferred}, {a,b,c}, error));
}

TEST(GxmCommandSnapshot, ActiveAndCompletedDeferredListsRoundTripWithoutExecutingCommands) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x10000]{}, [](uint8_t *p) { delete[] p; });
    gxm::ContextSnapshot saved;
    saved.address = 0x1000; saved.deferred = true;
    saved.state.type = SCE_GXM_CONTEXT_TYPE_DEFERRED; saved.state.active = true;
    saved.state.vdm_memory_callback = Ptr<SceGxmDeferredContextCallback>(0x99990000);
    saved.alloc_space_start = 0x4040;
    saved.alloc_space = Ptr<void>(0x4060); saved.alloc_space_end = Ptr<void>(0x5000);
    saved.pending_first = saved.pending_last = 3;
    saved.was_vert_default_uniform_reserved = true;
    saved.deferred_lists.push_back({0x3000, 5, 1, 2, false, {{0x4000,0x4040}}});
    saved.deferred_lists.push_back({0, 0, 3, 3, true, {}});
    std::vector<renderer::CommandSnapshot> commands(3);
    for (auto &cmd : commands) { cmd = gxm_test_cull_command(); cmd.deferred_allocation = true; }
    commands[0].next = 2;
    std::string error;
    const uint64_t existing_token = 5;
    std::memcpy(env.mem.memory.get() + 0x3000, &existing_token, sizeof(existing_token));
    std::memcpy(env.mem.memory.get() + 0x3020, &existing_token, sizeof(existing_token));
    ASSERT_TRUE(gxm::restore_contexts(env, {saved}, commands, error)) << error;
    struct Cleanup { EmuEnvState &env; ~Cleanup() { gxm::destroy_all_contexts(env, true); } } cleanup{env};
    // A copied opaque guest token resolves the same newly rebuilt host list.
    auto *restored_list = gxm::resolve_deferred_command_list(env, 5);
    ASSERT_NE(restored_list, nullptr);
    const uint64_t copied_token = 5;
    std::memcpy(env.mem.memory.get() + 0x3020, &copied_token, sizeof(copied_token));
    uint64_t guest_token = 0;
    std::memcpy(&guest_token, env.mem.memory.get() + 0x3020, sizeof(guest_token));
    EXPECT_EQ(gxm::resolve_deferred_command_list(env, guest_token), restored_list);
    EXPECT_EQ(gxm::resolve_deferred_command_list(env, 999), nullptr);
    std::vector<gxm::ContextSnapshot> contexts;
    std::vector<renderer::CommandSnapshot> captured;
    ASSERT_TRUE(gxm::capture_contexts(env, contexts, captured, error)) << error;
    ASSERT_EQ(contexts.size(), 1u); ASSERT_EQ(captured.size(), 3u);
    EXPECT_TRUE(contexts[0].state.active);
    EXPECT_TRUE(contexts[0].was_vert_default_uniform_reserved);
    EXPECT_EQ(contexts[0].alloc_space_start, 0x4040u);
    EXPECT_EQ(contexts[0].alloc_space.address(), 0x4060u);
    EXPECT_EQ(contexts[0].state.vdm_memory_callback.address(), 0x99990000u);
    ASSERT_EQ(contexts[0].deferred_lists.size(), 2u);
    const auto completed = std::find_if(contexts[0].deferred_lists.begin(), contexts[0].deferred_lists.end(), [](const auto &v) { return !v.current; });
    ASSERT_NE(completed, contexts[0].deferred_lists.end());
    EXPECT_EQ(completed->guest_address, 0x3000u); ASSERT_EQ(completed->ranges.size(), 1u);
    EXPECT_EQ(completed->ranges[0].first, 0x4000u); EXPECT_EQ(completed->ranges[0].second, 0x4040u);
    EXPECT_TRUE(gxm::validate_command_graph(contexts, captured, error)) << error;
    gxm::destroy_all_contexts(env, true);
    ASSERT_TRUE(gxm::restore_contexts(env, contexts, captured, error)) << error;
    EXPECT_NE(gxm::resolve_deferred_command_list(env, guest_token), nullptr);
    // Reuse the same output wrapper for the newly completed list. The copied
    // old wrapper must retain its original list, rather than follow the reuse.
    EXPECT_EQ(export_sceGxmEndCommandList(env, 0, "test", Ptr<SceGxmContext>(0x1000).get(env.mem),
        Ptr<SceGxmCommandList>(0x3000).get(env.mem)), 0);
    uint64_t new_token = 0;
    std::memcpy(&new_token, env.mem.memory.get() + 0x3000, sizeof(new_token));
    EXPECT_NE(new_token, guest_token);
    auto *old_list = gxm::resolve_deferred_command_list(env, guest_token);
    auto *new_list = gxm::resolve_deferred_command_list(env, new_token);
    ASSERT_NE(old_list, nullptr); ASSERT_NE(new_list, nullptr); EXPECT_NE(old_list, new_list);
}

TEST(GxmCommandSnapshot, UniformBlockCountIsNotAValidArrayIndex) {
    renderer::Command command{}; command.opcode = renderer::CommandOpcode::SetState;
    renderer::CommandHelper helper(&command);
    const auto tag = renderer::GXMState::UniformBuffer;
    ASSERT_TRUE(renderer::do_command_push_data(helper, tag, Ptr<uint8_t>(0x1000), true,
        int32_t(SCE_GXM_REAL_MAX_UNIFORM_BUFFER), uint32_t(4)));
    renderer::CommandSnapshot saved;
    std::string error;
    EXPECT_FALSE(renderer::capture_command_snapshot(command, {}, saved, error));
    const int32_t negative = -1;
    std::memcpy(command.data + sizeof(tag) + 5, &negative, sizeof(negative));
    EXPECT_FALSE(renderer::capture_command_snapshot(command, {}, saved, error));
    const int32_t last_valid = SCE_GXM_REAL_MAX_UNIFORM_BUFFER - 1;
    std::memcpy(command.data + sizeof(tag) + 5, &last_valid, sizeof(last_valid));
    EXPECT_TRUE(renderer::capture_command_snapshot(command, {}, saved, error)) << error;
}
