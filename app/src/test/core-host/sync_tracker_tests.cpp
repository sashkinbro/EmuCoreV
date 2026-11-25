// GPU resource-state tracker (vkutil/sync_tracker.h): state transitions -> barrier masks, WAR/WAW/RAW, depth-as-texture after a
// scene, colour-to-texture, render-pass coverage, batching counters. Pure logic, no device.
#include <renderer/vulkan/render_pass_sync.h>
#include <vkutil/sync_tracker.h>

#include <gtest/gtest.h>

using namespace vkutil::sync;
using renderer::vulkan::PassAttachment;
using renderer::vulkan::note_pass_end;
using renderer::vulkan::pass_coverage;

namespace {
constexpr Stage vf = S::eVertexShader | S::eFragmentShader;
constexpr vk::ImageLayout kGeneral = vk::ImageLayout::eGeneral;
constexpr vk::ImageLayout kRead = vk::ImageLayout::eShaderReadOnlyOptimal;
constexpr vk::ImageLayout kDst = vk::ImageLayout::eTransferDstOptimal;
constexpr vk::ImageLayout kDsRo = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
constexpr vk::ImageLayout kNone = vk::ImageLayout::eUndefined;

bool has(Stage s, Stage b) { return (s & b) == b; }
bool has(Access a, Access b) { return (a & b) == b; }
} // namespace

TEST(SyncTracker, FirstUseOfAnUndefinedImageIsAnInitBarrierWithNothingToWaitFor) {
    State st;
    Barrier b;
    const UseResult r = track(st, S::eTransfer, A::eTransferWrite, kDst, true, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::Init);
    EXPECT_FALSE(any(b.src_stage));
    EXPECT_FALSE(any(b.src_access));
    EXPECT_EQ(b.old_layout, kNone);
    EXPECT_EQ(b.new_layout, kDst);
    EXPECT_EQ(st.layout, kDst);
}

TEST(SyncTracker, ReadOfNeverWrittenResourceNeedsNothing) {
    State st;
    Barrier b;
    EXPECT_FALSE(track(st, S::eFragmentShader, A::eShaderRead, kNone, false, b).barrier);
}

TEST(SyncTracker, UploadThenSampleThenReuploadOrdersAgainstVertexAndFragmentReaders) {
    // B132: vertex texture fetch of scene N must be finished before scene N+1 overwrites the texture
    State st;
    Barrier b;
    track(st, S::eTransfer, A::eTransferWrite, kDst, true, b); // first upload
    UseResult r = track(st, vf | S::eComputeShader, A::eShaderRead, kRead, false, b); // to sampled
    ASSERT_TRUE(r.barrier);
    EXPECT_TRUE(has(b.src_stage, S::eTransfer));
    EXPECT_TRUE(has(b.src_access, A::eTransferWrite));
    EXPECT_TRUE(has(b.dst_stage, S::eVertexShader));
    // sampled again in the same layout by the vertex stage: covered
    EXPECT_FALSE(track(st, S::eVertexShader, A::eShaderRead, kRead, false, b).barrier);
    // re-upload (discard): must wait for every reader
    r = track(st, S::eTransfer, A::eTransferWrite, kDst, true, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::Layout);
    EXPECT_TRUE(has(b.src_stage, S::eVertexShader));
    EXPECT_TRUE(has(b.src_stage, S::eFragmentShader));
    EXPECT_TRUE(has(b.src_stage, S::eComputeShader));
    EXPECT_FALSE(any(b.src_access)); // discard: nothing to make available
}

TEST(SyncTracker, WriteAfterReadInTheSameLayoutIsAnExecutionDependencyOnly) {
    State st;
    Barrier b;
    track(st, S::eTransfer, A::eTransferWrite, kGeneral, false, b);
    track(st, S::eTransfer, A::eTransferRead, kGeneral, false, b);
    const UseResult r = track(st, S::eTransfer, A::eTransferWrite, kGeneral, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::War);
    EXPECT_TRUE(has(b.src_stage, S::eTransfer));
    EXPECT_FALSE(any(b.src_access)); // reads need no availability
}

TEST(SyncTracker, WriteAfterWriteMakesTheFirstWriteAvailable) {
    State st;
    Barrier b;
    track(st, S::eComputeShader, A::eShaderWrite, kGeneral, false, b);
    const UseResult r = track(st, S::eTransfer, A::eTransferWrite, kGeneral, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::Waw);
    EXPECT_TRUE(has(b.src_stage, S::eComputeShader));
    EXPECT_TRUE(has(b.src_access, A::eShaderWrite));
    EXPECT_TRUE(has(b.dst_stage, S::eTransfer));
}

TEST(SyncTracker, ColourSurfaceRenderedThenSampledNeedsNoExtraBarrierButComputeDoes) {
    State st;
    st.layout = kGeneral;
    note_pass_end(st, PassAttachment::Color); // the pass' outgoing dependency made it visible to vertex+fragment sampling and transfer
    Barrier b;
    EXPECT_FALSE(track(st, S::eFragmentShader, A::eShaderRead, kGeneral, false, b).barrier);
    EXPECT_FALSE(track(st, S::eVertexShader, A::eShaderRead, kGeneral, false, b).barrier);
    EXPECT_FALSE(track(st, S::eTransfer, A::eTransferRead, kGeneral, false, b).barrier);
    // the reinterpret compute shader is not covered by the pass dependency
    const UseResult r = track(st, S::eComputeShader, A::eShaderRead, kGeneral, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::Raw);
    EXPECT_TRUE(has(b.src_stage, S::eColorAttachmentOutput));
    EXPECT_TRUE(has(b.src_access, A::eColorAttachmentWrite));
    EXPECT_TRUE(has(b.dst_stage, S::eComputeShader));
    // widened: the earlier visibility is kept so a later vertex read is still covered
    EXPECT_TRUE(has(b.dst_stage, S::eFragmentShader));
    EXPECT_FALSE(track(st, S::eVertexShader, A::eShaderRead, kGeneral, false, b).barrier);
}

TEST(SyncTracker, DepthSampledAfterTheSceneCoversVertexAndFragment) {
    // Killzone depth of field: scene writes depth, the post pass samples it in the fragment (and vertex) stage
    State st;
    st.layout = kDsRo;
    note_pass_end(st, PassAttachment::DepthStencil);
    Barrier b;
    EXPECT_FALSE(track(st, S::eFragmentShader, A::eShaderRead, kDsRo, false, b).barrier);
    EXPECT_FALSE(track(st, S::eVertexShader, A::eShaderRead, kDsRo, false, b).barrier);
    // the next scene clears/loads that depth: WAR against the sampling readers, covered by the pass' dependency [1]
    Barrier pb;
    const UseResult r = track_pass_begin(st, pass_coverage(PassAttachment::DepthStencil, true), pb);
    EXPECT_FALSE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::None);
}

TEST(SyncTracker, DepthCopiedToAnotherSurfaceThenBackToReadOnly) {
    State st;
    st.layout = kDsRo;
    note_pass_end(st, PassAttachment::DepthStencil);
    Barrier b;
    UseResult r = track(st, S::eTransfer, A::eTransferRead, vk::ImageLayout::eTransferSrcOptimal, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::Layout);
    EXPECT_TRUE(has(b.src_stage, S::eLateFragmentTests));
    EXPECT_TRUE(has(b.src_access, A::eDepthStencilAttachmentWrite));
    EXPECT_EQ(b.old_layout, kDsRo);
    r = track(st, vf, A::eShaderRead, kDsRo, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_TRUE(has(b.src_stage, S::eTransfer));
}

TEST(SyncTracker, RenderPassBeginTrustsItsDependenciesOnlyWhenTheStateIsCovered) {
    // written by an earlier pass: covered
    State st;
    st.layout = kGeneral;
    note_pass_end(st, PassAttachment::Color);
    Barrier b;
    EXPECT_FALSE(track_pass_begin(st, pass_coverage(PassAttachment::Color, true), b).barrier);

    // cleared by a transfer (not an attachment stage): explicit barrier needed
    State cleared;
    Barrier tb;
    track(cleared, S::eTransfer, A::eTransferWrite, kDst, true, tb);
    const UseResult r = track_pass_begin(cleared, pass_coverage(PassAttachment::Color, true), b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(b.old_layout, kDst);
    EXPECT_EQ(b.new_layout, kGeneral);
    EXPECT_TRUE(has(b.src_stage, S::eTransfer));

    // layout mismatch (depth left in attachment-optimal but the pass loads it as read-only)
    State ds;
    Barrier db;
    track(ds, S::eEarlyFragmentTests | S::eLateFragmentTests, A::eDepthStencilAttachmentRead | A::eDepthStencilAttachmentWrite,
        vk::ImageLayout::eDepthStencilAttachmentOptimal, true, db);
    const UseResult dr = track_pass_begin(ds, pass_coverage(PassAttachment::DepthStencil, true), db);
    ASSERT_TRUE(dr.barrier);
    EXPECT_EQ(db.new_layout, kDsRo);
}

TEST(SyncTracker, RenderPassThatDiscardsTheAttachmentNeverForcesALayout) {
    State st;
    Barrier b;
    EXPECT_FALSE(track_pass_begin(st, pass_coverage(PassAttachment::DepthStencil, false), b).barrier);
    // an image that was a transfer destination and is cleared by the pass: only an execution dependency, no new layout
    State dst;
    Barrier tb;
    track(dst, S::eTransfer, A::eTransferWrite, kDst, true, tb);
    const UseResult r = track_pass_begin(dst, pass_coverage(PassAttachment::DepthStencil, false), b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(b.new_layout, kDst); // unchanged, keeps the layout it has
}

TEST(SyncTracker, BufferHostWriteThenTransferRead) {
    State st;
    Barrier b;
    track(st, S::eHost, A::eHostWrite, kNone, false, b);
    const UseResult r = track(st, S::eTransfer, A::eTransferRead, kNone, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::Raw);
    EXPECT_TRUE(has(b.src_stage, S::eHost));
    EXPECT_TRUE(has(b.src_access, A::eHostWrite));
    EXPECT_EQ(b.new_layout, kNone);
}

TEST(SyncTracker, ReadersAccumulateAndAreAllWaitedFor) {
    State st;
    Barrier b;
    track(st, S::eTransfer, A::eTransferWrite, kNone, false, b);
    track(st, S::eFragmentShader, A::eShaderRead, kNone, false, b);
    track(st, S::eComputeShader, A::eShaderRead, kNone, false, b);
    const UseResult r = track(st, S::eTransfer, A::eTransferWrite, kNone, false, b);
    ASSERT_TRUE(r.barrier);
    EXPECT_EQ(r.hazard, Hazard::War);
    EXPECT_TRUE(has(b.src_stage, S::eFragmentShader));
    EXPECT_TRUE(has(b.src_stage, S::eComputeShader));
}

TEST(SyncTracker, ObserveInsideARenderPassCountsAnUnsyncedUseAndKeepsTheStateUntouched) {
    State st;
    Barrier b;
    track(st, S::eComputeShader, A::eShaderWrite, kGeneral, false, b);
    const uint64_t before = stats().unsynced_in_pass.load();
    EXPECT_NE(Batch::observe(st, S::eFragmentShader, A::eShaderRead, kGeneral), Hazard::None);
    EXPECT_EQ(stats().unsynced_in_pass.load(), before + 1);
    // still flagged on the next use: the state did not pretend the barrier happened
    EXPECT_NE(Batch::observe(st, S::eFragmentShader, A::eShaderRead, kGeneral), Hazard::None);
}

TEST(SyncTracker, TwoUsesOfOneImageInABatchMergeIntoOneBarrier) {
    State st;
    Batch batch;
    const vk::Image img{};
    const vk::ImageSubresourceRange range{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
    const uint64_t merged = stats().merged.load();
    batch.use_image(st, img, range, S::eTransfer, A::eTransferWrite, kDst, true);
    batch.use_image(st, img, range, S::eFragmentShader, A::eShaderRead, kRead, false);
    EXPECT_EQ(stats().merged.load(), merged + 1);
    EXPECT_FALSE(batch.empty());
    EXPECT_EQ(st.layout, kRead);
}

TEST(SyncTracker, LegacyConversionNeverProducesAnEmptyStageMask) {
    EXPECT_EQ(to_legacy(Stage{}, false), vk::PipelineStageFlagBits::eTopOfPipe);
    EXPECT_EQ(to_legacy(Stage{}, true), vk::PipelineStageFlagBits::eBottomOfPipe);
    EXPECT_TRUE(to_legacy(S::eCopy, true) & vk::PipelineStageFlagBits::eTransfer);
    EXPECT_TRUE(to_legacy(S::eVertexAttributeInput, true) & vk::PipelineStageFlagBits::eVertexInput);
    EXPECT_TRUE(to_legacy(A::eShaderSampledRead) & vk::AccessFlagBits::eShaderRead);
}

TEST(GpuCopyRoute, DepthTransferIsRecordedWhereTheSceneStreamIs) {
    using renderer::vulkan::GpuCopyRoute;
    using renderer::vulkan::gpu_copy_route;
    // an open scene: its commands are not submitted yet, a separate submission would run before its draws
    EXPECT_EQ(gpu_copy_route(true, false), GpuCopyRoute::RecordIntoOpenScene);
    EXPECT_EQ(gpu_copy_route(true, true), GpuCopyRoute::RecordIntoOpenScene);
    // closed but unsubmitted scenes: join the queue behind them
    EXPECT_EQ(gpu_copy_route(false, true), GpuCopyRoute::QueueBehindPending);
    // nothing pending: queue order alone is enough
    EXPECT_EQ(gpu_copy_route(false, false), GpuCopyRoute::SeparateSubmit);
}
