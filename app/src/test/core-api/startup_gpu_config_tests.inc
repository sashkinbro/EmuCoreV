#include <emucorev/savestate/savestate.h>
#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/gpu_state_io.h>
#include <fstream>

namespace {
class StartupGpuConfig : public testing::Test {
protected:
    fs::path directory;
    std::unique_ptr<EmuEnvState> env;
    uint64_t memory_offset = 0;
    void SetUp() override {
        directory = fs::temp_directory_path() / fs::unique_path("startup-gpu-%%%%-%%%%");
        fs::create_directories(directory);
        env = std::make_unique<EmuEnvState>();
        env->io.title_id = "PCSE00333";
        env->cfg.backend_renderer = "OpenGL";
        env->cfg.memory_mapping = "disabled";
        env->cfg.resolution_multiplier = 2;
        env->cfg.current_config.backend_renderer = "OpenGL";
        env->cfg.current_config.memory_mapping = "page-table";
        env->cfg.current_config.resolution_multiplier = 3;
        env->cfg.current_config.high_accuracy = true;
        env->backend_renderer = renderer::Backend::OpenGL;
    }
    void TearDown() override { env.reset(); fs::remove_all(directory); }
    fs::path archive(uint32_t mapping = 1, float resolution = 1,
        const std::string &title = "PCSE00333", uint32_t engine = emucorev::savestate::kEngineVersion) {
        using namespace emucorev::savestate;
        BufferWriter meta;
        meta.u32(engine); meta.u32(0); meta.u64(0);
        meta.u32(static_cast<uint32_t>(renderer::Backend::Vulkan)); meta.u32(0);
        meta.str(title); meta.str("1.00"); meta.str("test");
        auto gpu = make_gpu_codec_snapshot();
        gpu.mapping_method = mapping;
        gpu.resolution = resolution;
        gpu.buffers.clear(); gpu.clocks.clear(); gpu.contexts.clear();
        std::vector<uint8_t> encoded;
        std::string error;
        EXPECT_TRUE(encode_gpu_state(gpu, encoded, error)) << error;
        const auto path = directory / fs::unique_path("state-%%%%.ecvs");
        Writer writer;
        EXPECT_TRUE(writer.open(path, error)) << error;
        EXPECT_TRUE(writer.write_section(SectionId::Meta, meta.data().data(), meta.size(), false, error));
        memory_offset = writer.bytes_written();
        const uint8_t integrity[] = {1,2,3,4};
        EXPECT_TRUE(writer.write_section(SectionId::Memory, integrity, sizeof(integrity), false, error));
        EXPECT_TRUE(writer.write_section(SectionId::Gpu, encoded.data(), encoded.size(), true, error));
        EXPECT_TRUE(writer.finalize(error)) << error;
        return path;
    }
    void unchanged() {
        EXPECT_EQ(env->cfg.current_config.backend_renderer, "OpenGL");
        EXPECT_EQ(env->cfg.current_config.memory_mapping, "page-table");
        EXPECT_EQ(env->cfg.current_config.resolution_multiplier, 3);
        EXPECT_EQ(env->backend_renderer, renderer::Backend::OpenGL);
    }
};
}

TEST_F(StartupGpuConfig, SavedGpuModesOverrideOnlyEffectiveLaunchConfig) {
    using namespace emucorev::savestate;
    const char *names[] = {"disabled","double-buffer","external-host","page-table","native-buffer"};
    for (uint32_t method = 0; method < 5; ++method) {
        const auto result = prepare_launch_gpu_config(*env, archive(method, 1));
        ASSERT_TRUE(result.ok()) << result.error;
        EXPECT_EQ(env->cfg.current_config.memory_mapping, names[method]);
        EXPECT_EQ(env->cfg.current_config.resolution_multiplier, 1);
        EXPECT_EQ(env->backend_renderer, renderer::Backend::Vulkan);
        EXPECT_EQ(env->cfg.current_config.backend_renderer, "Vulkan");
        EXPECT_TRUE(env->cfg.current_config.high_accuracy);
        EXPECT_EQ(env->cfg.backend_renderer, "OpenGL");
        EXPECT_EQ(env->cfg.memory_mapping, "disabled");
        EXPECT_EQ(env->cfg.resolution_multiplier, 2);
    }
    EXPECT_STREQ(status_name(Status::ConfigurationMismatch), "configuration-mismatch");
}

TEST_F(StartupGpuConfig, WrongTitleAndEngineDoNotChangeLaunchConfig) {
    using namespace emucorev::savestate;
    EXPECT_EQ(prepare_launch_gpu_config(*env, archive(1,1,"PCSA00027")).status, Status::TitleMismatch);
    unchanged();
    EXPECT_EQ(prepare_launch_gpu_config(*env, archive(1,1,"PCSE00333",UINT32_MAX)).status, Status::UnsupportedVersion);
    unchanged();
}

TEST_F(StartupGpuConfig, MalformedGpuValuesDoNotChangeLaunchConfig) {
    using namespace emucorev::savestate;
    EXPECT_EQ(prepare_launch_gpu_config(*env, archive(UINT32_MAX,1)).status, Status::InvalidFile);
    unchanged();
    for (float value : {0.f,-1.f,17.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        EXPECT_EQ(prepare_launch_gpu_config(*env, archive(1,value)).status, Status::InvalidFile);
        unchanged();
    }
}

TEST_F(StartupGpuConfig, CorruptionOutsideGpuSectionCannotApplySettings) {
    using namespace emucorev::savestate;
    const auto path = archive();
    std::fstream file(path.string(),std::ios::in|std::ios::out|std::ios::binary);
    ASSERT_TRUE(file);
    file.seekp(memory_offset);
    const char corrupt = 0;
    file.write(&corrupt,1); file.close();
    const auto result = prepare_launch_gpu_config(*env,path);
    EXPECT_EQ(result.status, Status::InvalidFile) << result.error;
    unchanged();
}
