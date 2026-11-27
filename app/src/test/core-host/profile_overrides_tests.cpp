// Per-game profile semantics of the native core (config/profile_overrides.h): only the fields listed in
// user-overrides leave the global settings; a legacy profile pins only the renderer and the GPU index.

#include <config/profile_overrides.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

// Same member names and types as config::Config::CurrentConfig for every table field.
struct Current {
    bool cpu_opt = true;
    int modules_mode = 0;
    std::vector<std::string> lle_modules;
    std::string audio_backend = "SDL";
    int audio_volume = 100;
    bool ngs_enable = true;
    bool pstv_mode = false;
    std::string backend_renderer = "Vulkan";
    int gpu_idx = 0;
    float resolution_multiplier = 1.0f;
    bool disable_surface_sync = false;
    std::string screen_filter = "Bilinear";
    std::string memory_mapping = "double-buffer";
    bool v_sync = true;
    int anisotropic_filtering = 1;
    bool async_pipeline_compilation = true;
    bool show_compile_shaders = false;
    bool show_shader_cache_warn = true;
    bool accurate_thread_scheduling = false;
    bool eager_surface_sync = false;
    int guest_cores = 1;
    bool import_textures = false;
    bool export_textures = false;
    bool export_as_png = false;
    bool fps_hack = false;
    bool shader_cache = true;
    bool spirv_shader = false;
    bool texture_cache = true;
    bool stretch_the_display_area = false;
    bool fullscreen_hd_res_pixel_perfect = false;
    int file_loading_delay = 0;
    bool psn_signed_in = false;
    int sys_button = 1;
    int sys_lang = 1;
    int sys_date_format = 0;
    int sys_time_format = 0;
    std::vector<uint64_t> ime_langs = { 4 };
    bool log_active_shaders = false;
    bool log_uniforms = false;
    bool color_surface_debug = false;
    bool validation_layer = false;
};

// An old profile written with stale values for options whose defaults have since changed.
constexpr const char *STALE_FIELDS = R"(<config>
  <core modules-mode="2"><lle-modules><module>libnet</module></lle-modules></core>
  <cpu cpu-opt="false"/>
  <gpu backend-renderer="OpenGL" gpu-idx="1" texture-cache="false" disable-surface-sync="true"
       guest-cores="4" fps-hack="true" accurate-thread-scheduling="true" resolution-multiplier="2"
       anisotropic-filtering="16" v-sync="false" memory-mapping="disabled" shader-cache="false"/>
  <audio audio-volume="10"/>
)";

pugi::xml_node load(pugi::xml_document &doc, const std::string &own) {
    const std::string text = std::string(STALE_FIELDS) + own + "</config>";
    EXPECT_TRUE(doc.load_string(text.c_str()));
    return doc.child("config");
}

TEST(ProfileOverrides, OnlyListedFieldsLeaveTheGlobalSettings) {
    pugi::xml_document doc;
    Current out;
    config::profile_overrides::apply(out, load(doc, R"(<emucorev user-overrides="audioVolume, guestCores,unknownName"/>)"));
    EXPECT_EQ(out.audio_volume, 10);
    EXPECT_EQ(out.guest_cores, 4);
    const Current global;
    EXPECT_EQ(out.texture_cache, global.texture_cache);
    EXPECT_EQ(out.disable_surface_sync, global.disable_surface_sync);
    EXPECT_EQ(out.fps_hack, global.fps_hack);
    EXPECT_EQ(out.accurate_thread_scheduling, global.accurate_thread_scheduling);
    EXPECT_EQ(out.memory_mapping, global.memory_mapping);
    EXPECT_EQ(out.backend_renderer, global.backend_renderer);
    EXPECT_EQ(out.gpu_idx, global.gpu_idx);
    EXPECT_EQ(out.modules_mode, global.modules_mode);
    EXPECT_TRUE(out.lle_modules.empty());
    EXPECT_EQ(out.cpu_opt, global.cpu_opt);
}

TEST(ProfileOverrides, EmptyListPinsNothing) {
    pugi::xml_document doc;
    Current out;
    config::profile_overrides::apply(out, load(doc, R"(<emucorev user-overrides=""/>)"));
    EXPECT_EQ(config::profile_overrides::changed(Current{}, out).size(), 0u);
}

TEST(ProfileOverrides, LegacyProfilePinsOnlyRendererAndGpuIndex) {
    pugi::xml_document doc;
    Current out;
    config::profile_overrides::apply(out, load(doc, R"(<emucorev gpu-driver-mode="global"/>)"));
    EXPECT_EQ(out.backend_renderer, "OpenGL");
    EXPECT_EQ(out.gpu_idx, 1);
    const auto changed = config::profile_overrides::changed(Current{}, out);
    EXPECT_EQ(changed, (std::set<std::string>{ "backendRenderer", "gpuIdx" }));

    pugi::xml_document bare;
    Current no_own_section;
    config::profile_overrides::apply(no_own_section, load(bare, ""));
    EXPECT_EQ(config::profile_overrides::changed(Current{}, no_own_section), changed);
}

TEST(ProfileOverrides, WriteThenApplyRoundTripsEveryField) {
    Current written;
    written.audio_volume = 42;
    written.resolution_multiplier = 1.5f;
    written.screen_filter = "Nearest";
    written.texture_cache = false;
    written.gpu_idx = 2;
    written.lle_modules = { "libnet", "libnetctl" };
    written.ime_langs = { 1, 2 };
    pugi::xml_document doc;
    auto root = doc.append_child("config");
    config::profile_overrides::write_fields(root, written);
    const auto names = config::profile_overrides::changed(Current{}, written);
    root.append_child("emucorev").append_attribute("user-overrides") = config::profile_overrides::encode(names).c_str();

    Current out;
    config::profile_overrides::apply(out, root);
    EXPECT_TRUE(config::profile_overrides::changed(written, out).empty());
    EXPECT_EQ(config::profile_overrides::decode(config::profile_overrides::encode(names)), names);
}

} // namespace
