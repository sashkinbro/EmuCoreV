#include <emucorev/savestate/gxm_finish_io.h>
#include <emucorev/savestate/state_io.h>

namespace {
struct FinishPreflightFixture {
    emucorev::savestate::GxmSnapshot gxm;
    std::vector<ThreadState::Snapshot> threads{1};
    std::string error;
    FinishPreflightFixture() {
        gxm.contexts.emplace_back().address = 0x4000;
        renderer::FinishSnapshot operation;
        operation.id = 0x100000002ULL;
        operation.owner = 37;
        operation.context_address = 0x4000;
        gxm.finish_operations.push_back(operation);
        renderer::CommandSnapshot command;
        command.opcode = renderer::CommandOpcode::Nop;
        command.completion_id = operation.id;
        gxm.commands.push_back(command);
        auto &thread = threads.front();
        thread.id = operation.owner;
        thread.registered = true;
        thread.waits.emplace_back();
        auto &wait = thread.waits.back();
        wait.operation = WaitOperation::renderer_finish;
        wait.args = {2, 1, 1};
        wait.target = {SCE_KERNEL_WAITTYPE_EVENT, 0};
    }
    bool valid() { return emucorev::savestate::validate_gxm_finish_continuations(gxm, threads, error); }
};
}

TEST(GxmFinishPreflight, PendingFenceRequiresItsRegisteredOwnerAndExactlyOneWait) {
    FinishPreflightFixture fixture;
    ASSERT_TRUE(fixture.valid()) << fixture.error;
    fixture.threads.front().id = 38;
    EXPECT_FALSE(fixture.valid());
    fixture.threads.front().id = 37;
    fixture.threads.front().waits.push_back(fixture.threads.front().waits.front());
    EXPECT_FALSE(fixture.valid());
    fixture.threads.front().waits.pop_back();
    fixture.threads.front().registered = false;
    EXPECT_FALSE(fixture.valid());
    fixture.threads.front().registered = true;
    fixture.threads.clear();
    EXPECT_FALSE(fixture.valid());
}

TEST(GxmFinishPreflight, DrainRetainsGuestWaitWithoutAQueuedFenceCommand) {
    FinishPreflightFixture fixture;
    auto &operation = fixture.gxm.finish_operations.front();
    operation.phase = renderer::FinishPhase::Drain;
    operation.fence_done = true;
    operation.result = 1;
    EXPECT_FALSE(fixture.valid());
    fixture.gxm.commands.clear();
    ASSERT_TRUE(fixture.valid()) << fixture.error;
    operation.drain_done = true;
    EXPECT_TRUE(fixture.valid()) << fixture.error;
    fixture.threads.front().waits.front().args[7] = 1;
    EXPECT_FALSE(fixture.valid());
    fixture.threads.front().waits.front().args[7] = 0;
    operation.context_address = 0x5000;
    EXPECT_FALSE(fixture.valid());
}

TEST(GxmFinishPreflight, DeletedOwnerCanLeaveOnlyOneUnfinishedFenceAndNoGuestWait) {
    FinishPreflightFixture fixture;
    fixture.gxm.finish_operations.front().owner = 0;
    EXPECT_FALSE(fixture.valid());
    fixture.threads.clear();
    ASSERT_TRUE(fixture.valid()) << fixture.error;
    fixture.gxm.commands.push_back(fixture.gxm.commands.front());
    EXPECT_FALSE(fixture.valid());
    fixture.gxm.commands.pop_back();
    fixture.gxm.commands.clear();
    EXPECT_FALSE(fixture.valid());
}

TEST(GxmFinishPreflight, ParserKeepsDurableCompletionFieldsAndRejectsNoncanonicalBoolean) {
    using namespace emucorev::savestate;
    BufferWriter writer;
    writer.value(SceGxmInitializeParams{});
    for (unsigned i = 0; i < 8; ++i) writer.u32(0);
    writer.u32(0); writer.u32(0); writer.u32(0); // Contexts, commands, batches.
    writer.u32(1);
    writer.u64(0x100000002ULL); writer.i32(37); writer.u32(0x4000);
    writer.u32(uint32_t(renderer::FinishPhase::Drain));
    const auto bool_offset = writer.size();
    writer.boolean(true); writer.boolean(false); writer.i32(1);
    for (unsigned i = 0; i < 8; ++i) writer.u32(0);
    GxmSnapshot output;
    std::string error;
    ASSERT_TRUE(parse_gxm_state(writer.data(), output, error)) << error;
    ASSERT_EQ(output.finish_operations.size(), 1u);
    const auto &operation = output.finish_operations.front();
    EXPECT_EQ(operation.id, 0x100000002ULL);
    EXPECT_EQ(operation.owner, 37);
    EXPECT_EQ(operation.context_address, 0x4000u);
    EXPECT_EQ(operation.phase, renderer::FinishPhase::Drain);
    EXPECT_TRUE(operation.fence_done);
    EXPECT_FALSE(operation.drain_done);
    EXPECT_EQ(operation.result, 1);
    auto malformed = writer.data();
    malformed[bool_offset] = 2;
    output.global_timestamp = 42;
    EXPECT_FALSE(parse_gxm_state(malformed, output, error));
    EXPECT_EQ(output.global_timestamp, 42u);
}
