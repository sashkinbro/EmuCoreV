// P8: visibility (occlusion) buffer model: slot allocation, per-entry accumulation, face selection and the two-sided split.
#include <renderer/vulkan/depth_stencil_model.h>
#include <renderer/vulkan/visibility_model.h>

#include <gtest/gtest.h>

using namespace renderer::vulkan;
using namespace renderer::vulkan::visibility;

TEST(Visibility, EveryActivationGetsItsOwnSlot) {
    SceneQueries scene;
    EXPECT_EQ(scene.begin(3, true, 16, 64), 0);
    EXPECT_EQ(scene.begin(5, false, 16, 64), 1);
    EXPECT_EQ(scene.begin(3, true, 16, 64), 2); // the same entry again later in the scene: a new slot, never a second begin of slot 0
    EXPECT_EQ(scene.slots(), 3u);
}

TEST(Visibility, IndexOutsideTheBufferIsDroppedNotWrappedToZero) {
    SceneQueries scene;
    EXPECT_EQ(scene.begin(16, true, 16, 64), -1);
    EXPECT_EQ(scene.begin(0xFFFFFFFFu, true, 16, 64), -1);
    EXPECT_TRUE(scene.empty());
    EXPECT_EQ(scene.begin(15, true, 16, 64), 0);
}

TEST(Visibility, ExhaustedPoolStopsCounting) {
    SceneQueries scene;
    for (uint32_t i = 0; i < 4; i++)
        EXPECT_EQ(scene.begin(0, true, 4, 4), static_cast<int32_t>(i));
    EXPECT_EQ(scene.begin(0, true, 4, 4), -1);
}

TEST(Visibility, IncrementEntriesAddTheirSlotsUp) {
    const std::vector<Activation> acts = { { 2, true }, { 7, true }, { 2, true } };
    const uint32_t results[] = { 10, 4, 5 };
    const auto v = resolve(acts, results);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].entry, 2u);
    EXPECT_EQ(v[0].value, 15u);
    EXPECT_EQ(v[1].entry, 7u);
    EXPECT_EQ(v[1].value, 4u);
}

TEST(Visibility, IncrementSaturates) {
    const std::vector<Activation> acts = { { 1, true }, { 1, true } };
    const uint32_t results[] = { 0xFFFFFFF0u, 0x100u };
    EXPECT_EQ(resolve(acts, results)[0].value, 0xFFFFFFFFu);
}

TEST(Visibility, SetEntriesAreFlagsWhateverTheSampleCount) {
    const std::vector<Activation> acts = { { 0, false }, { 1, false }, { 0, false } };
    const uint32_t results[] = { 0, 12345, 77 };
    const auto v = resolve(acts, results);
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0].value, 1u); // visible in the second draw of entry 0
    EXPECT_EQ(v[1].value, 1u);
    const uint32_t none[] = { 0, 0, 0 };
    EXPECT_EQ(resolve(acts, none)[0].value, 0u);
}

TEST(Visibility, EntriesComeOutSortedOnce) {
    const std::vector<Activation> acts = { { 9, true }, { 1, true }, { 5, true }, { 1, true } };
    const uint32_t results[] = { 1, 2, 3, 4 };
    const auto v = resolve(acts, results);
    ASSERT_EQ(v.size(), 3u);
    EXPECT_EQ(v[0].entry, 1u);
    EXPECT_EQ(v[1].entry, 5u);
    EXPECT_EQ(v[2].entry, 9u);
}

TEST(Visibility, BackFaceUsesFrontStateWhenOneSided) {
    const Face front{ 4, true }, back{ 9, false };
    EXPECT_EQ(effective_face(front, back, true, false).index, 4);
    EXPECT_EQ(effective_face(front, back, true, true).index, 9);
    EXPECT_EQ(effective_face(front, back, false, true).index, 4);
}

TEST(Visibility, DifferentFaceEntriesSplitATwoSidedDrawOnlyWhenBothFacesSurvive) {
    using namespace renderer::vulkan::dsmodel;
    FaceState front, back;
    front.visibility = { 1, true };
    back.visibility = { 2, true };
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, front, back), FacePlan::Split);
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_CW, front, back), FacePlan::Front); // back faces culled
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_CCW, front, back), FacePlan::Back); // front faces culled
    EXPECT_EQ(plan_faces(false, SCE_GXM_CULL_NONE, front, back), FacePlan::Front); // one sided: back state unused
    back.visibility = front.visibility;
    EXPECT_EQ(plan_faces(true, SCE_GXM_CULL_NONE, front, back), FacePlan::Front); // equal state: one draw
}
