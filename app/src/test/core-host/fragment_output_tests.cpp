// Host tests for the register layout of the fragment colour (shader/fragment_output.h).
#include <gtest/gtest.h>

#include <shader/fragment_output.h>

#include <vector>

using shader::FragmentColorLayout;
using shader::fragment_color_layout;

TEST(FragmentOutput, NonNativeKeepsDeclaredType) {
    EXPECT_EQ(fragment_color_layout(false, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR), FragmentColorLayout::Declared);
    EXPECT_EQ(fragment_color_layout(false, SCE_GXM_COLOR_FORMAT_S8S8S8S8_ABGR), FragmentColorLayout::Declared);
}

TEST(FragmentOutput, SurfaceLayoutOnByteSurfacesIsPackedBytes) {
    // Tearaway: half4 declared, the program packs U8x4 into o0 itself
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR), FragmentColorLayout::PackedU8);
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_U8U8U8U8_ARGB), FragmentColorLayout::PackedU8);
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_U8U8U8_BGR), FragmentColorLayout::PackedU8);
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_S8S8S8S8_ABGR), FragmentColorLayout::PackedS8);
}

TEST(FragmentOutput, SurfaceLayoutOnOtherSurfacesKeepsDeclaredType) {
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_F16F16F16F16_RGBA), FragmentColorLayout::Declared);
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_F32F32_GR), FragmentColorLayout::Declared);
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_F32_R), FragmentColorLayout::Declared);
    // single-channel A8 keeps its alpha-surface remap path
    EXPECT_EQ(fragment_color_layout(true, SCE_GXM_COLOR_FORMAT_U8_A), FragmentColorLayout::Declared);
}

TEST(FragmentOutput, ProgramFlagsSelectOutputLayout) {
    std::vector<uint8_t> storage(sizeof(SceGxmProgram), 0);
    auto *program = reinterpret_cast<SceGxmProgram *>(storage.data());

    program->program_flags = SCE_GXM_PROGRAM_FLAG_FRAGMENT;
    EXPECT_FALSE(shader::fragment_output_in_surface_layout(*program));

    // Tearaway's self-packing programs
    program->program_flags = 0x183841;
    EXPECT_TRUE(shader::fragment_output_in_surface_layout(*program));
    EXPECT_TRUE(shader::fragment_input_in_surface_layout(*program));
    // vignette pass: reads the packed pixel, leaves halves
    program->program_flags = 0x182ec1;
    EXPECT_TRUE(shader::fragment_input_in_surface_layout(*program));
    EXPECT_FALSE(shader::fragment_output_in_surface_layout(*program));
    // half programs (0x182ac5: VF16ADD r0 -o0 pa0 ... o0) work on halves both ways
    program->program_flags = 0x182ac5;
    EXPECT_FALSE(shader::fragment_input_in_surface_layout(*program));
    EXPECT_FALSE(shader::fragment_output_in_surface_layout(*program));
}
