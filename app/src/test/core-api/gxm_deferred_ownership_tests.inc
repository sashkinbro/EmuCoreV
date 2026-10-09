#include <gxm/savestate.h>
#include <renderer/command_snapshot.h>

DECL_EXPORT(int, sceGxmExecuteCommandList, SceGxmContext *context, SceGxmCommandList *list);

TEST(GxmDeferredOwnership, ExecuteCopiesCommandsWithoutModifyingReusableListAndSnapshotsRoundTrip) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x20000]{}, [](uint8_t *p) { delete[] p; });
    env.renderer = std::make_unique<SnapshotBatchTestRenderer>();
    struct Cleanup {
        EmuEnvState &env;
        ~Cleanup() { gxm::destroy_all_contexts(env, true); }
    } cleanup{env};

    gxm::ContextSnapshot immediate;
    immediate.address = 0x1000;
    immediate.state.type = SCE_GXM_CONTEXT_TYPE_IMMEDIATE;
    gxm::ContextSnapshot deferred;
    deferred.address = 0x5000;
    deferred.deferred = true;
    deferred.state.type = SCE_GXM_CONTEXT_TYPE_DEFERRED;
    deferred.deferred_lists.push_back({0x3000, 9, 1, 2, false, {{0x8000, 0x8040}}});
    auto surface = gxm_test_cull_command();
    surface.deferred_allocation = true;
    surface.next = 2;
    auto state = gxm_test_cull_command();
    state.deferred_allocation = true;
    std::string error;
    ASSERT_TRUE(gxm::restore_contexts(env, {immediate, deferred}, {surface, state}, error)) << error;
    auto *source = gxm::resolve_deferred_command_list(env, 9);
    ASSERT_NE(source, nullptr);
    auto *first_source = source->first;
    auto *last_source = source->last;
    auto *guest_context = Ptr<SceGxmContext>(immediate.address).get(env.mem);
    reinterpret_cast<GxmContextState *>(guest_context)->active = true;
    renderer::Context *destination = nullptr;
    for (const auto &[address, context] : gxm::runtime_contexts(env))
        if (address == immediate.address) destination = context;
    ASSERT_NE(destination, nullptr);
    ASSERT_EQ(export_sceGxmExecuteCommandList(env, 0, "test", guest_context,
        Ptr<SceGxmCommandList>(0x3000).get(env.mem)), 0);

    auto *copy = destination->command_list.first;
    EXPECT_NE(copy, first_source);
    EXPECT_EQ(last_source->next, nullptr);
    EXPECT_EQ(copy->flags & renderer::Command::FLAG_NO_FREE, 0);
    // Detach the old implementation's alias before cleanup so a failing
    // regression reports ownership violations without double-freeing nodes.
    if (copy == first_source) {
        destination->command_list.first = last_source->next;
        last_source->next = nullptr;
        return;
    }
    const auto changed_mode = SCE_GXM_CULL_CW;
    std::memcpy(first_source->data + sizeof(renderer::GXMState), &changed_mode, sizeof(changed_mode));
    EXPECT_NE(std::memcmp(first_source->data, copy->data, sizeof(copy->data)), 0);

    auto *first_execution_tail = destination->command_list.last;
    ASSERT_EQ(export_sceGxmExecuteCommandList(env, 0, "test", guest_context,
        Ptr<SceGxmCommandList>(0x3000).get(env.mem)), 0);
    ASSERT_NE(first_execution_tail->next, nullptr);
    EXPECT_NE(first_execution_tail->next, copy);
    EXPECT_NE(first_execution_tail->next, first_source);
    EXPECT_EQ(last_source->next, nullptr);

    reinterpret_cast<GxmContextState *>(guest_context)->active = false;
    std::vector<gxm::ContextSnapshot> captured_contexts;
    std::vector<renderer::CommandSnapshot> captured_commands;
    ASSERT_TRUE(gxm::capture_contexts(env, captured_contexts, captured_commands, error)) << error;
    ASSERT_TRUE(gxm::validate_command_graph(captured_contexts, captured_commands, error)) << error;
    gxm::destroy_all_contexts(env, true);
    ASSERT_TRUE(gxm::restore_contexts(env, captured_contexts, captured_commands, error)) << error;
    ASSERT_NE(gxm::resolve_deferred_command_list(env, 9), nullptr);
}
