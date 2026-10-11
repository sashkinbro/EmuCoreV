#include <gxm/savestate.h>
#include <renderer/command_snapshot.h>
#include <atomic>
#include <thread>

DECL_EXPORT(int, sceGxmExecuteCommandList, SceGxmContext *context, SceGxmCommandList *list);

namespace {
// restore_contexts creates immediate renderer contexts through a synchronous
// CreateContext command. The lightweight fake renderer has no render loop, so
// service that one command exactly as a render thread would, without opening a
// graphics backend or leaving the test blocked in wait_for_status().
class GxmSnapshotContextCommandService {
public:
    explicit GxmSnapshotContextCommandService(renderer::State &state)
        : state_(state)
        , thread_([this] { run(); }) {}

    ~GxmSnapshotContextCommandService() {
        stopping_.store(true, std::memory_order_release);
        state_.command_buffer_queue.wake();
        thread_.join();
    }

private:
    void run() {
        while (!stopping_.load(std::memory_order_acquire)) {
            auto list = state_.command_buffer_queue.pop(1000);
            if (!list || !list->first)
                continue;

            auto *command = list->first;
            renderer::CommandHelper helper(command);
            int result = renderer::CommandErrorArgumentsTooLarge;
            if (command->opcode == renderer::CommandOpcode::CreateContext) {
                auto *output = helper.pop<std::unique_ptr<renderer::Context> *>();
                *output = std::make_unique<renderer::Context>();
                state_.context = output->get();
                // CreateContext handlers complete with a positive bool result;
                // renderer::create_context treats status > CommandErrorCodeNone
                // as success.
                result = 1;
            }
            {
                const std::lock_guard lock(state_.command_finish_one_mutex);
                helper.complete(result);
            }
            state_.command_finish_one.notify_all();
            renderer::generic_command_free(command);
        }
    }

    renderer::State &state_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};
} // namespace

TEST(GxmDeferredOwnership, ExecuteCopiesCommandsWithoutModifyingReusableListAndSnapshotsRoundTrip) {
    EmuEnvState env;
    env.mem.memory = Memory(new uint8_t[0x20000]{}, [](uint8_t *p) { delete[] p; });
    env.renderer = std::make_unique<SnapshotBatchTestRenderer>();
    struct Cleanup {
        EmuEnvState &env;
        ~Cleanup() { gxm::destroy_all_contexts(env, true); }
    } cleanup{env};
    GxmSnapshotContextCommandService context_command_service(*env.renderer);

    gxm::ContextSnapshot immediate;
    immediate.address = 0x1000;
    immediate.state.type = SCE_GXM_CONTEXT_TYPE_IMMEDIATE;
    immediate.state.active = true;
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
    // ExecuteCommandList reads the opaque identity token from the guest wrapper.
    // restore_contexts rebuilds the host list but deliberately leaves guest RAM
    // untouched, so seed the same token EndCommandList would have published.
    auto *guest_token = reinterpret_cast<renderer::CommandList *>(uintptr_t(9));
    std::memcpy(env.mem.memory.get() + 0x3000, &guest_token, sizeof(guest_token));
    auto *guest_context = Ptr<SceGxmContext>(immediate.address).get(env.mem);
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

    std::vector<gxm::ContextSnapshot> captured_contexts;
    std::vector<renderer::CommandSnapshot> captured_commands;
    ASSERT_TRUE(gxm::capture_contexts(env, captured_contexts, captured_commands, error)) << error;
    ASSERT_TRUE(gxm::validate_command_graph(captured_contexts, captured_commands, error)) << error;
    gxm::destroy_all_contexts(env, true);
    ASSERT_TRUE(gxm::restore_contexts(env, captured_contexts, captured_commands, error)) << error;
    ASSERT_NE(gxm::resolve_deferred_command_list(env, 9), nullptr);
}
