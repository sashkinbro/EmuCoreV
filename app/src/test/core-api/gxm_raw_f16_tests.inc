// Real shader and disk-cache regression for the raw F16 subpass-input path.
namespace raw_f16_regression {
template<class H> void set_raw_hint(H &hints, bool value) {
    if constexpr (requires { hints.raw_color_attachment_input; })
        hints.raw_color_attachment_input = value;
}
inline bool has_raw_subpass(const std::vector<uint32_t> &code) {
    for (size_t word = 5; word < code.size();) {
        const auto size = code[word] >> 16;
        if (!size || size > code.size() - word) return false;
        // OpDecorate InputAttachmentIndex=43, index1 identifies the raw alias.
        if ((code[word] & 0xffff) == 71 && size == 4
            && code[word + 2] == 43 && code[word + 3] == 1) return true;
        word += size;
    }
    return false;
}
}
TEST(GxmRawF16, RealFragmentCompilerAndDiskCacheKeepRawFetchSeparateFromFloatFetch) {
    struct Fixture { SceGxmProgram program{}; SceGxmProgramVertexVaryings varyings{}; } fixture;
    auto &program = fixture.program;
    program.magic = 0x00505847;
    program.major_version = 1; program.minor_version = 4; program.size = sizeof(fixture);
    program.program_flags = SCE_GXM_PROGRAM_FLAG_FRAGMENT | SCE_GXM_PROGRAM_FLAG_NATIVECOLOR_USED | SCE_GXM_PROGRAM_FLAG_FRAGCOLOR_USED;
    program.varyings_offset = offsetof(Fixture, varyings) - offsetof(SceGxmProgram, varyings_offset);
    program.secondary_program_offset = 4;
    fixture.varyings.output_param_type = SCE_GXM_PARAMETER_TYPE_F16;
    fixture.varyings.output_comp_count = 4;
    FeatureState features;
    features.direct_fragcolor = true;
    features.support_shader_interlock = false;
    features.preserve_f16_nan_as_u16 = true;
    features.support_unknown_format = true;
    shader::Hints hints{};
    hints.color_format = SCE_GXM_COLOR_FORMAT_F16F16F16F16_ABGR;
    raw_f16_regression::set_raw_hint(hints, true);
    const auto raw = shader::convert_gxp(program, "raw-f16-regression", features, shader::Target::SpirVVulkan, hints);
    ASSERT_FALSE(raw.spirv.empty());
    EXPECT_TRUE(raw_f16_regression::has_raw_subpass(raw.spirv));
    raw_f16_regression::set_raw_hint(hints, false);
    const auto normal = shader::convert_gxp(program, "float-f16-control", features, shader::Target::SpirVVulkan, hints);
    ASSERT_FALSE(normal.spirv.empty());
    EXPECT_FALSE(raw_f16_regression::has_raw_subpass(normal.spirv));
    EXPECT_NE(raw.spirv, normal.spirv);
    const auto directory = fs::temp_directory_path() / fs::unique_path("raw-f16-shader-%%%%-%%%%");
    struct Cleanup { fs::path path; ~Cleanup() { boost::system::error_code error; fs::remove_all(path, error); } } cleanup{directory};
    for (const bool raw_first : { false, true }) {
        const auto cache = directory / (raw_first ? "raw-first" : "float-first");
        const auto load = [&](bool is_raw) {
            raw_f16_regression::set_raw_hint(hints, is_raw);
            return renderer::load_spirv_shader(program, features, true, hints, false, cache, directory / "logs", "raw-f16-variant-test", true);
        };
        const auto first = load(raw_first);
        const auto second = load(!raw_first);
        const auto reload = load(raw_first);
        EXPECT_EQ(raw_f16_regression::has_raw_subpass(first), raw_first);
        EXPECT_EQ(raw_f16_regression::has_raw_subpass(second), !raw_first);
        EXPECT_EQ(first, reload);
    }
}
