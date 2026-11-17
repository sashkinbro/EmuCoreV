#include <emucorev/savestate/np_state_io.h>
#include <np/functions.h>

extern int export_sceNpCheckCallback(EmuEnvState &, SceUID, const char *);

namespace {
using namespace emucorev::savestate;

TEST(NpSnapshotRestore, ColdRestoreKeepsInitializedCallbackPump) {
    EmuEnvState saved, cold;
    ASSERT_TRUE(init(saved.np, nullptr));
    BufferWriter bytes;
    std::string error;
    ASSERT_TRUE(capture_np(saved.np, bytes, error)) << error;
    // This is the failure seen in Jetpack's system-event callback pump.
    EXPECT_NE(export_sceNpCheckCallback(cold, 0, "test"), 0);
    NpSnapshot snapshot;
    ASSERT_TRUE(parse_np(bytes.data(), snapshot, error)) << error;
    apply_np(cold.np, std::move(snapshot));
    EXPECT_EQ(export_sceNpCheckCallback(cold, 0, "test"), 0);
}

TEST(NpSnapshotRestore, WarmRewindReplacesCallbacksAndDeliveryHistory) {
    NpState state;
    state.inited = true;
    state.cbs.emplace(7, SceNpServiceStateCallback{0x1001, 0xdeadbeef, true, 1});
    state.state_cb_id = 7;
    state.matching2.context_cb_pc = 0x2001;
    state.matching2.context_cb_arg = 0xbaadf00d;
    state.matching2.next_ctx_id = 3;
    state.matching2.pending.push_back({2, 4, 8, 0});
    BufferWriter bytes;
    std::string error;
    ASSERT_TRUE(capture_np(state, bytes, error));
    NpSnapshot snapshot;
    ASSERT_TRUE(parse_np(bytes.data(), snapshot, error));
    // Opaque callback arguments do not need to point at guest RAM.
    ASSERT_TRUE(validate_np_callbacks(snapshot, [](Address pc, size_t) {
        return pc == 0x1000 || pc == 0x2000;
    }, error));
    state.cbs.clear();
    state.cbs.emplace(9, SceNpServiceStateCallback{0x3001, 0, false, 0});
    state.matching2.pending.clear();
    const auto generation = state.generation;
    apply_np(state, std::move(snapshot));
    ASSERT_EQ(state.cbs.size(), 1u);
    EXPECT_TRUE(state.cbs.at(7).delivered);
    EXPECT_EQ(state.cbs.at(7).data, 0xdeadbeefu);
    EXPECT_EQ(state.state_cb_id, 7);
    EXPECT_GT(state.generation, generation);
    ASSERT_EQ(state.matching2.pending.size(), 1u);
    EXPECT_EQ(state.matching2.pending[0].ctx_id, 2u);
    EXPECT_EQ(state.matching2.next_ctx_id, 3u);
}

TEST(NpSnapshotRestore, UninitializedSnapshotDoesNotForceInitialization) {
    NpState state;
    BufferWriter bytes;
    std::string error;
    ASSERT_TRUE(capture_np(state, bytes, error));
    NpSnapshot snapshot;
    ASSERT_TRUE(parse_np(bytes.data(), snapshot, error));
    state.inited = true;
    apply_np(state, std::move(snapshot));
    EXPECT_FALSE(state.inited);
    EXPECT_TRUE(supports_engine_version(15));
    EXPECT_TRUE(supports_engine_version(16));
}

TEST(NpSnapshotRestore, RejectsMalformedPayloadAndUnmappedCallback) {
    NpState state;
    state.inited = true;
    state.cbs.emplace(7, SceNpServiceStateCallback{0x1001, 0, false, 0});
    state.state_cb_id = 7;
    BufferWriter bytes;
    std::string error;
    ASSERT_TRUE(capture_np(state, bytes, error));
    for (size_t size = 0; size < bytes.size(); ++size) {
        NpSnapshot snapshot;
        EXPECT_FALSE(parse_np({bytes.data().begin(), bytes.data().begin() + size}, snapshot, error));
    }
    NpSnapshot snapshot;
    ASSERT_TRUE(parse_np(bytes.data(), snapshot, error));
    EXPECT_FALSE(validate_np_callbacks(snapshot, [](Address, size_t) { return false; }, error));
    auto extra = bytes.data(); extra.push_back(0);
    EXPECT_FALSE(parse_np(extra, snapshot, error));
    state.matching2.dispatching = true;
    BufferWriter busy;
    EXPECT_FALSE(capture_np(state, busy, error));
}
} // namespace
