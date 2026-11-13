// Local trophy context lifecycle regressions for ECV / EmuCoreV.
// SPDX-License-Identifier: GPL-2.0-or-later

#include <emuenv/state.h>
#include <np/functions.h>
#include <np/state.h>

#include <algorithm>
#include <array>
#include <limits>

DECL_EXPORT(int, sceNpTrophyGetTrophyUnlockState, np::trophy::ContextHandle context_handle,
    int32_t api_handle, np::trophy::TrophyFlagArray *flag_array, uint32_t *count);
DECL_EXPORT(int, sceNpTrophyDestroyContext, np::trophy::ContextHandle context_handle);

namespace {
constexpr uint32_t trophy_invalid_context_error = 0x80551609;

void add_trophy_lifecycle_context(NpTrophyState &state, bool valid = true) {
    auto &context = state.contexts.emplace_back();
    context.valid = valid;
    context.id = static_cast<np::trophy::ContextHandle>(state.contexts.size());
    context.io = nullptr; // These tests never destroy a valid disk-backed context.
    context.trophy_file_stream = -1;
    context.trophy_count = 3;
    std::fill_n(context.trophy_progress, np::trophy::MAX_TROPHIES >> 5, 0);
    context.trophy_progress[0] = 5;
}
} // namespace

TEST(TrophyContextLifecycle, ZeroLookupIsInvalidOnEmptyAndPopulatedTables) {
    NpTrophyState state;
    EXPECT_EQ(get_trophy_context(state, 0), nullptr);
    add_trophy_lifecycle_context(state);
    EXPECT_EQ(get_trophy_context(state, 0), nullptr);
    EXPECT_EQ(get_trophy_context(state, 1), &state.contexts[0]);
}

TEST(TrophyContextLifecycle, ZeroDestroyCannotIndexOrMutateAnotherContext) {
    NpTrophyState state;
    EXPECT_FALSE(destroy_trophy_context(state, 0));
    add_trophy_lifecycle_context(state);
    EXPECT_FALSE(destroy_trophy_context(state, 0));
    EXPECT_TRUE(state.contexts[0].valid);
    EXPECT_EQ(state.contexts[0].trophy_progress[0], 5u);
}

TEST(TrophyContextLifecycle, NegativeAndOutOfRangeHandlesRemainRejected) {
    NpTrophyState state;
    add_trophy_lifecycle_context(state);
    for (auto handle : {np::trophy::ContextHandle(-1), std::numeric_limits<np::trophy::ContextHandle>::min(),
             np::trophy::ContextHandle(2), std::numeric_limits<np::trophy::ContextHandle>::max()}) {
        SCOPED_TRACE(handle);
        EXPECT_EQ(get_trophy_context(state, handle), nullptr);
        EXPECT_FALSE(destroy_trophy_context(state, handle));
    }
    EXPECT_TRUE(state.contexts[0].valid);
}

TEST(TrophyContextLifecycle, DeadSlotIsInvisibleWithoutHidingOtherLiveSlots) {
    NpTrophyState state;
    add_trophy_lifecycle_context(state, false);
    add_trophy_lifecycle_context(state);
    EXPECT_EQ(get_trophy_context(state, 1), nullptr);
    EXPECT_FALSE(destroy_trophy_context(state, 1));
    EXPECT_EQ(get_trophy_context(state, 2), &state.contexts[1]);
    EXPECT_TRUE(state.contexts[1].valid);
}

TEST(TrophyContextLifecycle, ActualUnlockStateExportStillReturnsValidLocalProgress) {
    EmuEnvState env;
    env.np.trophy_state.inited = true;
    add_trophy_lifecycle_context(env.np.trophy_state);
    np::trophy::TrophyFlagArray flags{};
    uint32_t count = 0;
    EXPECT_EQ(export_sceNpTrophyGetTrophyUnlockState(env, 0, "test", 1, 1, &flags, &count), 0);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(flags[0], 5u);
}

TEST(TrophyContextLifecycle, ActualUnlockStateExportRejectsZeroAndDeadHandlesWithoutWritingOutputs) {
    EmuEnvState env;
    env.np.trophy_state.inited = true;
    add_trophy_lifecycle_context(env.np.trophy_state, false);
    for (np::trophy::ContextHandle handle : {0, 1}) {
        np::trophy::TrophyFlagArray flags;
        std::fill_n(flags, np::trophy::MAX_TROPHIES >> 5, 0xCCCCCCCC);
        uint32_t count = 0xEEEEEEEE;
        const int result = export_sceNpTrophyGetTrophyUnlockState(env, 0, "test", handle, 1, &flags, &count);
        EXPECT_EQ(static_cast<uint32_t>(result), trophy_invalid_context_error);
        EXPECT_EQ(count, 0xEEEEEEEEu);
        EXPECT_TRUE(std::all_of(std::begin(flags), std::end(flags), [](uint32_t value) { return value == 0xCCCCCCCC; }));
    }
}

TEST(TrophyContextLifecycle, ActualDestroyExportUsesExistingInvalidContextErrorForZeroAndDeadSlots) {
    EmuEnvState env;
    add_trophy_lifecycle_context(env.np.trophy_state, false);
    for (np::trophy::ContextHandle handle : {0, 1}) {
        const int result = export_sceNpTrophyDestroyContext(env, 0, "test", handle);
        EXPECT_EQ(static_cast<uint32_t>(result), trophy_invalid_context_error);
    }
}
