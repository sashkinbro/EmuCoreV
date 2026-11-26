// Host proofs for the texture cache validation rules: renderer/texture_policy.h is pure logic, so each rule is
// checked here without a GPU or guest memory. Integration is covered by the replay regress.
#include <renderer/texture_policy.h>

#include <gtest/gtest.h>

using namespace renderer::texture_policy;

namespace {
EntryState armed_clean() {
    EntryState s;
    s.pixels_armed = true;
    return s;
}

EntryState armed_clean_paletted() {
    EntryState s = armed_clean();
    s.paletted = true;
    s.palette_armed = true;
    return s;
}
} // namespace

// ---- lookup: when does a bind read guest memory -------------------------------------------------

TEST(TexturePolicyLookup, CleanEntryIsUsedWithoutHashing) {
    const Revalidation r = revalidation_needed(armed_clean());
    EXPECT_TRUE(r.skip);
    EXPECT_FALSE(r.hash_pixels);
    EXPECT_FALSE(r.hash_palette);
}

TEST(TexturePolicyLookup, CleanPalettedEntryHashesNeitherPixelsNorPalette) {
    const Revalidation r = revalidation_needed(armed_clean_paletted());
    EXPECT_TRUE(r.skip);
    EXPECT_FALSE(r.hash_pixels);
    EXPECT_FALSE(r.hash_palette);
}

TEST(TexturePolicyLookup, FiredPixelTrapHashesPixelsOnly) {
    EntryState s = armed_clean_paletted();
    s.pixels_fired = true;
    const Revalidation r = revalidation_needed(s);
    EXPECT_FALSE(r.skip);
    EXPECT_TRUE(r.hash_pixels);
    EXPECT_FALSE(r.hash_palette);
}

TEST(TexturePolicyLookup, FiredPaletteTrapHashesPaletteOnly) {
    EntryState s = armed_clean_paletted();
    s.palette_fired = true;
    const Revalidation r = revalidation_needed(s);
    EXPECT_FALSE(r.skip);
    EXPECT_FALSE(r.hash_pixels);
    EXPECT_TRUE(r.hash_palette);
}

TEST(TexturePolicyLookup, PaletteTrapOfAnUnpalettedTextureIsIgnored) {
    EntryState s = armed_clean();
    s.palette_fired = true;
    EXPECT_TRUE(revalidation_needed(s).skip);
}

TEST(TexturePolicyLookup, UnarmedEntryCannotBeTrustedAndIsHashed) {
    EntryState s;
    EXPECT_TRUE(revalidation_needed(s).hash_pixels);
    s.paletted = true;
    s.pixels_armed = true;
    EXPECT_TRUE(revalidation_needed(s).hash_palette);
}

TEST(TexturePolicyLookup, ReleasedMemoryIsHashedEvenWhenNoTrapFired) {
    EntryState s = armed_clean_paletted();
    s.memory_released = true;
    const Revalidation r = revalidation_needed(s);
    EXPECT_TRUE(r.hash_pixels);
    EXPECT_TRUE(r.hash_palette);
}

TEST(TexturePolicyLookup, MemoryTheGpuWritesWithoutAFaultIsHashedNotTrusted) {
    EntryState s = armed_clean_paletted();
    s.gpu_may_write = true;
    const Revalidation r = revalidation_needed(s);
    EXPECT_TRUE(r.hash_pixels);
    EXPECT_TRUE(r.hash_palette);
}

TEST(TexturePolicyLookup, OneContentVersionPerScene) {
    EntryState s = armed_clean_paletted();
    s.pixels_fired = true;
    s.palette_fired = true;
    s.validated_this_scene = true;
    const Revalidation r = revalidation_needed(s);
    EXPECT_TRUE(r.skip);
    EXPECT_FALSE(r.hash_pixels);
    EXPECT_FALSE(r.hash_palette);
}

TEST(TexturePolicyLookup, ForcedEntryRebuildsItsOwnBaselines) {
    EntryState s = armed_clean();
    s.forced = true;
    const Revalidation r = revalidation_needed(s);
    EXPECT_FALSE(r.skip);
    EXPECT_FALSE(r.hash_pixels);
}

// ---- upload decision ----------------------------------------------------------------------------

TEST(TexturePolicyUpload, SameHashRewriteDoesNotUpload) {
    HashOutcome o;
    o.pixels_checked = true;
    o.pixels_changed = false;
    EXPECT_EQ(decide_upload(o), UploadKind::None);
    o.palette_checked = true;
    EXPECT_EQ(decide_upload(o), UploadKind::None);
}

TEST(TexturePolicyUpload, ChangedPixelsUploadWhole) {
    HashOutcome o;
    o.pixels_checked = true;
    o.pixels_changed = true;
    EXPECT_EQ(decide_upload(o), UploadKind::Full);
}

TEST(TexturePolicyUpload, PaletteOnlyChangeReconvertsWithoutPixelHashing) {
    HashOutcome o;
    o.palette_checked = true;
    o.palette_changed = true;
    // the pixel trap was clean: pixels were never hashed
    EXPECT_FALSE(o.pixels_checked);
    EXPECT_EQ(decide_upload(o), UploadKind::PaletteOnly);
    // pixels were also rewritten, byte for byte the same
    o.pixels_checked = true;
    EXPECT_EQ(decide_upload(o), UploadKind::PaletteOnly);
}

TEST(TexturePolicyUpload, PixelOnlyChangeKeepsThePalette) {
    HashOutcome o;
    o.pixels_checked = true;
    o.pixels_changed = true;
    o.palette_checked = true;
    o.palette_changed = false;
    EXPECT_EQ(decide_upload(o), UploadKind::Full);
    // a palette whose trap never fired is not even looked at
    o.palette_checked = false;
    EXPECT_EQ(decide_upload(o), UploadKind::Full);
}

TEST(TexturePolicyUpload, PaletteChangeRecoloursEveryRowSoNeverPartial) {
    HashOutcome o;
    o.pixels_checked = o.pixels_changed = true;
    o.palette_checked = o.palette_changed = true;
    o.partial_possible = true;
    o.dirty_rows = 4;
    o.total_rows = 512;
    EXPECT_EQ(decide_upload(o), UploadKind::Full);
}

TEST(TexturePolicyUpload, FewDirtyRowsOfALinearTextureUploadPartially) {
    HashOutcome o;
    o.pixels_checked = o.pixels_changed = true;
    o.partial_possible = true;
    o.dirty_rows = 32;
    o.total_rows = 512;
    EXPECT_EQ(decide_upload(o), UploadKind::Partial);
}

TEST(TexturePolicyUpload, MostlyDirtyOrNonAddressableTexturesUploadWhole) {
    HashOutcome o;
    o.pixels_checked = o.pixels_changed = true;
    o.partial_possible = true;
    o.total_rows = 512;
    o.dirty_rows = 512 * partial_max_dirty_percent / 100;
    EXPECT_EQ(decide_upload(o), UploadKind::Partial);
    o.dirty_rows += 1;
    EXPECT_EQ(decide_upload(o), UploadKind::Full);
    o.dirty_rows = 8;
    o.partial_possible = false; // tiled / swizzled / small
    EXPECT_EQ(decide_upload(o), UploadKind::Full);
}

// ---- chunks and partial runs --------------------------------------------------------------------

TEST(TexturePolicyChunks, SmallOrNonLinearTexturesAreOneChunk) {
    EXPECT_EQ(plan_chunks(true, partial_min_bytes - 1, 1024, 63, 1).rows_per_chunk, 0u);
    EXPECT_EQ(plan_chunks(false, 1u << 20, 4096, 256, 1).rows_per_chunk, 0u);
    EXPECT_EQ(plan_chunks(false, 1u << 20, 4096, 256, 1).chunk_count, 1u);
}

TEST(TexturePolicyChunks, BigLinearTextureSplitsIntoWholeBlockRows) {
    // 1024 x 1024 RGBA8: 4096 bytes per row, 16 KiB chunks = 4 rows
    ChunkPlan p = plan_chunks(true, 4u << 20, 4096, 1024, 1);
    EXPECT_EQ(p.rows_per_chunk, 4u);
    EXPECT_EQ(p.chunk_count, 256u);
    // BC1 style: block rows of 4 pixel rows, 2048 bytes per block row => 8 block rows per chunk
    p = plan_chunks(true, 1u << 20, 2048, 1024, 4);
    EXPECT_EQ(p.rows_per_chunk, 32u);
    EXPECT_EQ(p.rows_per_chunk % 4, 0u);
    EXPECT_EQ(p.chunk_count, 32u);
    // a row wider than a chunk still makes one-row chunks
    p = plan_chunks(true, 8u << 20, 32768, 256, 1);
    EXPECT_EQ(p.rows_per_chunk, 1u);
    EXPECT_EQ(p.chunk_count, 256u);
}

TEST(TexturePolicyChunks, ChunkCountCoversAnUnevenTail) {
    // 100 rows of 4096 bytes in 4 row chunks
    const ChunkPlan p = plan_chunks(true, 100u * 1024 * 4, 4096, 100, 1);
    ASSERT_GT(p.rows_per_chunk, 0u);
    EXPECT_EQ(p.chunk_count, (100 + p.rows_per_chunk - 1) / p.rows_per_chunk);
}

TEST(TexturePolicyRuns, OnlyChangedChunksProduceRowRuns) {
    ChunkPlan plan;
    plan.rows_per_chunk = 16;
    plan.chunk_count = 8;
    const std::vector<uint64_t> before{ 1, 2, 3, 4, 5, 6, 7, 8 };
    std::vector<uint64_t> after = before;
    after[1] = 20;
    after[2] = 30;
    after[6] = 70;
    const std::vector<RowRun> runs = changed_runs(before, after, plan, 128);
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0], (RowRun{ 16, 48 }));
    EXPECT_EQ(runs[1], (RowRun{ 96, 112 }));
    EXPECT_EQ(rows_in_runs(runs), 48u);
}

TEST(TexturePolicyRuns, IdenticalHashesGiveNoRunsAndTheLastRunIsClippedToTheTexture) {
    ChunkPlan plan;
    plan.rows_per_chunk = 16;
    plan.chunk_count = 7;
    const std::vector<uint64_t> a{ 1, 2, 3, 4, 5, 6, 7 };
    EXPECT_TRUE(changed_runs(a, a, plan, 100).empty());
    std::vector<uint64_t> b = a;
    b[6] = 0;
    const std::vector<RowRun> runs = changed_runs(a, b, plan, 100);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0], (RowRun{ 96, 100 }));
}

TEST(TexturePolicyRuns, ALayoutChangeMeansEverythingChanged) {
    ChunkPlan plan;
    plan.rows_per_chunk = 16;
    plan.chunk_count = 4;
    const std::vector<RowRun> runs = changed_runs({ 1, 2 }, { 1, 2, 3, 4 }, plan, 64);
    ASSERT_EQ(runs.size(), 1u);
    EXPECT_EQ(runs[0], (RowRun{ 0, 64 }));
}

TEST(TexturePolicyTrap, SpanCoversEveryPageTheBytesTouch) {
    EXPECT_EQ(trap_span(0x1000, 0x1000, 0x1000).begin, 0x1000u);
    EXPECT_EQ(trap_span(0x1000, 0x1000, 0x1000).end, 0x2000u);
    EXPECT_EQ(trap_span(0x1ff0, 0x20, 0x1000).begin, 0x1000u);
    EXPECT_EQ(trap_span(0x1ff0, 0x20, 0x1000).end, 0x3000u);
    // a 64 byte palette inside a page
    EXPECT_EQ(trap_span(0x40040, 64, 0x1000).begin, 0x40000u);
    EXPECT_EQ(trap_span(0x40040, 64, 0x1000).end, 0x41000u);
}
