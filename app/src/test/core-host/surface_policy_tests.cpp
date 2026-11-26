// Host proofs for the surface/texture cache ownership rules (B130, B131): renderer/vulkan/surface_policy.h
// is pure logic, so each rule is checked here without a GPU. Integration is covered by the replay regress.
#include <renderer/vulkan/surface_policy.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan::surface_policy;

namespace {
constexpr uint64_t MiB = 1ull << 20;

SlotInfo occupied(uint64_t bytes, bool this_scene = false, bool this_frame = false) {
    SlotInfo s;
    s.occupied = true;
    s.bytes = bytes;
    s.used_this_scene = this_scene;
    s.used_this_frame = this_frame;
    return s;
}
} // namespace

// ---- B130 -------------------------------------------------------------------------------------

TEST(SurfacePolicyBudget, ClampedBetweenMinimumAndMaximumAndUnknownHeapGivesMinimum) {
    EXPECT_EQ(surface_budget_bytes(0), budget_min_bytes);
    EXPECT_EQ(surface_budget_bytes(1024 * MiB), budget_min_bytes);
    EXPECT_EQ(surface_budget_bytes(6144 * MiB), 768 * MiB);
    EXPECT_EQ(surface_budget_bytes(64ull * 1024 * MiB), budget_max_bytes);
}

TEST(SurfacePolicyBudget, OldWorstCaseOfTwentyFullScreenSurfacesAtOneXFitsEveryBudget) {
    const uint64_t one_x_surface = 960ull * 544 * 4;
    EXPECT_LE(20 * one_x_surface, budget_min_bytes);
    // and the cache now holds more than the old 20 slots
    EXPECT_GT(max_cache_slots, legacy_cache_slots);
}

TEST(SurfacePolicySlots, FreeSlotIsPreferredAndNothingIsEvictedWithinBudget) {
    std::vector<SlotInfo> lru(max_cache_slots);
    for (size_t i = 0; i < 30; ++i)
        lru[i] = occupied(2 * MiB);
    const SlotChoice c = choose_slot(lru, 2 * MiB, budget_min_bytes);
    EXPECT_EQ(c.slot, 30);
    EXPECT_FALSE(c.slot_occupied);
    EXPECT_TRUE(c.also_evict.empty());
    EXPECT_FALSE(c.forced);
}

TEST(SurfacePolicySlots, CyclicUseOfThirtySurfacesNeverEvictsAnything) {
    // the B130 failure: 20 slots + LRU evict exactly the surface a cycle of > 20 needs next
    std::vector<SlotInfo> lru(max_cache_slots);
    for (int i = 0; i < 30; ++i) {
        const SlotChoice c = choose_slot(lru, 2 * MiB, budget_min_bytes);
        EXPECT_FALSE(c.slot_occupied) << "surface " << i;
        EXPECT_TRUE(c.also_evict.empty()) << "surface " << i;
        EXPECT_EQ(c.slot, i);
        lru[c.slot] = occupied(2 * MiB);
    }
}

TEST(SurfacePolicySlots, WhenFullTheOldestSurfaceUntouchedThisFrameGoes) {
    std::vector<SlotInfo> lru(4);
    lru[0] = occupied(MiB, false, true);   // oldest but touched this frame
    lru[1] = occupied(MiB, false, false);  // oldest untouched
    lru[2] = occupied(MiB, false, false);
    lru[3] = occupied(MiB, true, true);    // used by the recording scene
    const SlotChoice c = choose_slot(lru, MiB, budget_min_bytes);
    EXPECT_EQ(c.slot, 1);
    EXPECT_TRUE(c.slot_occupied);
    EXPECT_FALSE(c.forced);
}

TEST(SurfacePolicySlots, SurfaceUsedByTheRecordingSceneIsNeverEvictedWhileAnotherExists) {
    std::vector<SlotInfo> lru(3);
    lru[0] = occupied(MiB, true, true);
    lru[1] = occupied(MiB, true, true);
    lru[2] = occupied(MiB, false, true);
    EXPECT_EQ(choose_slot(lru, MiB, budget_min_bytes).slot, 2);
}

TEST(SurfacePolicySlots, AllInUseForcesTheLeastRecentOneAndSaysSo) {
    std::vector<SlotInfo> lru(2, occupied(MiB, true, true));
    const SlotChoice c = choose_slot(lru, MiB, budget_min_bytes);
    EXPECT_TRUE(c.forced);
    EXPECT_EQ(c.slot, 0);
    EXPECT_TRUE(c.slot_occupied);
}

TEST(SurfacePolicySlots, OverBudgetEvictsOldestIdleSurfacesUntilTheNewOneFits) {
    std::vector<SlotInfo> lru(8);
    for (size_t i = 0; i < 6; ++i)
        lru[i] = occupied(100 * MiB);       // 600 MiB live, slots 6/7 free
    lru[5].used_this_scene = true;
    lru[5].used_this_frame = true;
    const SlotChoice c = choose_slot(lru, 200 * MiB, 700 * MiB);  // 600 + 200 > 700: free 100 MiB -> evict one
    EXPECT_EQ(c.slot, 6);                    // free slot gets the surface
    ASSERT_EQ(c.also_evict.size(), 1u);
    EXPECT_EQ(c.also_evict[0], 0);           // the oldest idle one
}

TEST(SurfacePolicySlots, SurfaceLargerThanTheBudgetEvictsEveryCandidateButNotTheSceneInUse) {
    std::vector<SlotInfo> lru(4);
    lru[0] = occupied(50 * MiB);
    lru[1] = occupied(50 * MiB);
    lru[2] = occupied(50 * MiB, true, true);
    const SlotChoice c = choose_slot(lru, 900 * MiB, 400 * MiB);
    EXPECT_EQ(c.also_evict.size(), 2u);
    for (int e : c.also_evict)
        EXPECT_NE(e, 2);
}

TEST(SurfacePolicySlots, EmptyInputYieldsNoSlot) {
    EXPECT_EQ(choose_slot({}, MiB, MiB).slot, -1);
}

// ---- write-back before destroy ----------------------------------------------------------------

TEST(SurfacePolicyWriteback, OnlyAGpuNewerLinearSyncableSurfaceIsWrittenBack) {
    WritebackInput in;
    in.linear = true;
    in.sync_available = true;
    EXPECT_EQ(decide_writeback(in), Writeback::Do);
    in.kept_in_sync = true;
    EXPECT_EQ(decide_writeback(in), Writeback::SkipAlreadySynced);
    in.kept_in_sync = false;
    in.cpu_newer = true;
    EXPECT_EQ(decide_writeback(in), Writeback::SkipCpuNewer);
    in.cpu_newer = false;
    in.superseded = true;
    EXPECT_EQ(decide_writeback(in), Writeback::SkipSuperseded);
    in.superseded = false;
    in.sync_available = false;
    EXPECT_EQ(decide_writeback(in), Writeback::SkipSyncUnavailable);
    in.sync_available = true;
    in.linear = false;
    EXPECT_EQ(decide_writeback(in), Writeback::SkipUnsupportedLayout);
}

TEST(SurfacePolicyWriteback, ANewerOverlappingSurfaceSupersedesAnOlderOne) {
    EXPECT_TRUE(is_superseded(5, 9, true));
    EXPECT_FALSE(is_superseded(9, 5, true));
    EXPECT_FALSE(is_superseded(5, 9, false));
    EXPECT_FALSE(is_superseded(5, 5, true));
    EXPECT_TRUE(ranges_overlap(0x1000, 0x2000, 0x1fff, 0x3000));
    EXPECT_FALSE(ranges_overlap(0x1000, 0x2000, 0x2000, 0x3000));
}

// ---- B131: CPU-write proof --------------------------------------------------------------------

TEST(SurfacePolicyTrap, UnfiredTrapProvesNothingWritten) {
    EXPECT_EQ(judge_trap({}), TrapVerdict::Untouched);
}

TEST(SurfacePolicyTrap, WriteFaultInsideAnUnhashableSurfaceIsACpuWrite) {
    TrapObservation o;
    o.fired = true;
    o.write_inside_range = true;
    EXPECT_EQ(judge_trap(o), TrapVerdict::CpuWrote);
}

TEST(SurfacePolicyTrap, WriteOfIdenticalBytesDoesNotMakeTheGpuImageStale) {
    TrapObservation o;
    o.fired = true;
    o.write_inside_range = true;
    o.baseline_valid = true;
    o.hash_differs = false;
    EXPECT_EQ(judge_trap(o), TrapVerdict::Untouched);
    o.hash_differs = true;
    EXPECT_EQ(judge_trap(o), TrapVerdict::CpuWrote);
}

TEST(SurfacePolicyTrap, FaultFromNeighbouringDataIsResolvedByHashNotGuessed) {
    TrapObservation o;
    o.fired = true;
    o.baseline_valid = true;
    o.hash_differs = false;
    EXPECT_EQ(judge_trap(o), TrapVerdict::Untouched);  // unrelated write: no false positive for a small surface
    o.hash_differs = true;                             // a sweep that started outside and ran into the surface
    EXPECT_EQ(judge_trap(o), TrapVerdict::CpuWrote);
}

TEST(SurfacePolicyTrap, SyncedOrUnhashableSurfacesReportUnknownInsteadOfGuessing) {
    TrapObservation o;
    o.fired = true;
    o.baseline_valid = true;
    o.gpu_writes_memory = true;  // the GPU rewrites the bytes itself, a hash cannot tell
    EXPECT_EQ(judge_trap(o), TrapVerdict::Unknown);
    o.gpu_writes_memory = false;
    o.baseline_valid = false;    // larger than verify_hash_limit_bytes
    EXPECT_EQ(judge_trap(o), TrapVerdict::Unknown);
}

TEST(SurfacePolicyTrap, TheEmulatorsOwnWritebackIsNotACpuWrite) {
    TrapObservation o;
    o.fired = true;
    o.baseline_valid = true;
    o.hash_differs = true;       // the write-back changed the bytes ...
    o.emulator_wrote = true;     // ... but it was ours
    EXPECT_EQ(judge_trap(o), TrapVerdict::Untouched);
    o.write_inside_range = true; // a genuine guest write fault in the same window still counts
    EXPECT_EQ(judge_trap(o), TrapVerdict::CpuWrote);
}

TEST(SurfacePolicyTrap, TypicalSurfaceSizesAreAllHashable) {
    EXPECT_GE(verify_hash_limit_bytes, 960u * 544 * 4);  // a full-screen RGBA8 target
}

// ---- B131: texture ownership ------------------------------------------------------------------

TEST(SurfacePolicyTexture, DirtySurfaceHandsTheAddressToTheTextureCacheAtOnce) {
    EXPECT_EQ(texture_source(true), TextureSource::TextureCache);
    EXPECT_EQ(texture_source(false), TextureSource::Surface);
}

TEST(SurfacePolicyTexture, TextureMustLieInsideTheSurfaceWithTheFourByteGbufferSlack) {
    EXPECT_TRUE(texture_fits_surface(0x1000, 0x1000, 0x1000, 0x1000));
    EXPECT_TRUE(texture_fits_surface(0x1000, 0x1000, 0x1800, 0x800 + 4));
    EXPECT_FALSE(texture_fits_surface(0x1000, 0x1000, 0x1800, 0x800 + 5));
    EXPECT_FALSE(texture_fits_surface(0x1000, 0x1000, 0xfff, 0x10));
}

// ---- B131: framebuffer requests on an overlapping surface -------------------------------------

TEST(SurfacePolicyFramebuffer, OnlyAnIdenticalOrSmallerRequestAtTheSameBaseReusesTheSurface) {
    FramebufferRequest r;
    r.same_base = true;
    r.new_range_inside_old = true;
    EXPECT_EQ(framebuffer_action(r), FramebufferAction::Reuse);
    r.extent_or_layout_changed = true;
    EXPECT_EQ(framebuffer_action(r), FramebufferAction::ReplaceWithWriteback);
    r.extent_or_layout_changed = false;
    r.wider_or_other_format = true;
    EXPECT_EQ(framebuffer_action(r), FramebufferAction::ReplaceWithWriteback);
    r.wider_or_other_format = false;
    r.new_range_inside_old = false;
    EXPECT_EQ(framebuffer_action(r), FramebufferAction::ReplaceWithWriteback);
}

TEST(SurfacePolicyFramebuffer, ADifferentBaseInsideTheOldRangeNeverDropsTheOldSurfaceWithoutWriteback) {
    FramebufferRequest r;
    r.same_base = false;
    r.new_range_inside_old = true;
    EXPECT_EQ(framebuffer_action(r), FramebufferAction::RetireWithWriteback);
    r.new_range_inside_old = false;
    EXPECT_EQ(framebuffer_action(r), FramebufferAction::RetireWithWriteback);
}

// ---- B131: initial contents --------------------------------------------------------------------

TEST(SurfacePolicyPreserved, RetiredRangeRestoresTheNextSurfaceThereOnceAndOnlyOnce) {
    std::vector<PreservedRange> ranges;
    remember_preserved(ranges, { 0x10000, 0x20000, 100 });
    EXPECT_FALSE(consume_preserved(ranges, 0x30000, 0x31000, 101));  // elsewhere
    EXPECT_TRUE(consume_preserved(ranges, 0x1f000, 0x25000, 101));   // overlaps its tail
    EXPECT_FALSE(consume_preserved(ranges, 0x10000, 0x20000, 102));  // consumed
}

TEST(SurfacePolicyPreserved, ExpiredRangeIsDroppedWithoutRestoring) {
    std::vector<PreservedRange> ranges;
    remember_preserved(ranges, { 0x10000, 0x20000, 100 });
    EXPECT_FALSE(consume_preserved(ranges, 0x10000, 0x20000, 100 + preserved_range_lifetime_frames + 1));
    EXPECT_TRUE(ranges.empty());
}

TEST(SurfacePolicyPreserved, CapacityIsBoundedOldestFirst) {
    std::vector<PreservedRange> ranges;
    for (uint32_t i = 0; i < preserved_range_capacity + 4; ++i)
        remember_preserved(ranges, { 0x10000 * (i + 1), 0x10000 * (i + 1) + 0x1000, 1 });
    EXPECT_EQ(ranges.size(), preserved_range_capacity);
    EXPECT_FALSE(consume_preserved(ranges, 0x10000, 0x11000, 2));  // the oldest was dropped
    EXPECT_TRUE(consume_preserved(ranges, 0x10000 * (preserved_range_capacity + 4), 0x10000 * (preserved_range_capacity + 4) + 1, 2));
}

TEST(SurfacePolicySnapshotImages, EverythingThatFitsIsKept) {
    const auto keep = select_snapshot_images({ 10, 20, 30 }, 100);
    EXPECT_EQ(keep, (std::vector<bool>{ true, true, true }));
}

TEST(SurfacePolicySnapshotImages, ImportantSurfacesWinAndSmallOnesFillTheRest) {
    // 70 + 50 do not both fit in 100: the more important one stays, the 30 still uses the room left
    const auto keep = select_snapshot_images({ 70, 50, 30, 10 }, 100);
    EXPECT_EQ(keep, (std::vector<bool>{ true, false, true, false }));
}

TEST(SurfacePolicySnapshotImages, KeptBytesNeverExceedTheBudget) {
    std::vector<uint64_t> bytes;
    for (uint64_t i = 1; i <= 64; ++i)
        bytes.push_back(i * 3'000'000);
    for (const uint64_t budget : { uint64_t{ 0 }, uint64_t{ 1 }, uint64_t{ 50'000'000 }, uint64_t{ 128ull << 20 } }) {
        const auto keep = select_snapshot_images(bytes, budget);
        uint64_t sum = 0;
        for (size_t i = 0; i < bytes.size(); ++i)
            if (keep[i])
                sum += bytes[i];
        EXPECT_LE(sum, budget);
    }
}

TEST(SurfacePolicySnapshotImages, AnUnrepresentableOrOversizedSurfaceIsSkippedNotFatal) {
    const auto keep = select_snapshot_images({ UINT64_MAX, 5 }, 100);
    EXPECT_EQ(keep, (std::vector<bool>{ false, true }));
}
