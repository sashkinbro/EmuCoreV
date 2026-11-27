#pragma once

// Per-game profile (config/config_<ID>.xml) field table, shared with the Android app.
//
// A profile stores the full effective config plus <emucorev user-overrides="name,..."/>: the names of the
// fields the user changed for this game. Only those fields are applied over the global settings; every other
// field keeps following the global settings. The names are the Kotlin VitaCoreConfig property names
// (ConfigOverrides in GameProfileOverrides.kt); GameProfileFieldTableTest parses this file and checks that
// every shared name exists in VitaCoreConfig and that the app writes it under the same section/attribute.
//
// A legacy profile (no user-overrides attribute, written before overrides existed) pins only the fields in
// legacy_pinned plus the driver; the app additionally keeps its own input-mapping fields.
// The GPU driver (gpu/custom-driver-name) is always taken from the profile: the app resolves it per launch.

#include <pugixml.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace config::profile_overrides {

inline constexpr const char *ATTRIBUTE = "user-overrides";
inline constexpr const char *OWN_SECTION = "emucorev";

// Names shared with VitaCoreConfig. One call per line: name, XML section, XML attribute, member(s).
template <typename Fn, typename... Outs>
void visit_shared_fields(Fn &&fn, Outs &...outs) {
    fn("modulesMode", "core", "modules-mode", outs.modules_mode...);
    fn("backendRenderer", "gpu", "backend-renderer", outs.backend_renderer...);
    fn("resolutionMultiplier", "gpu", "resolution-multiplier", outs.resolution_multiplier...);
    fn("disableSurfaceSync", "gpu", "disable-surface-sync", outs.disable_surface_sync...);
    fn("eagerSurfaceSync", "gpu", "eager-surface-sync", outs.eager_surface_sync...);
    fn("screenFilter", "gpu", "screen-filter", outs.screen_filter...);
    fn("memoryMapping", "gpu", "memory-mapping", outs.memory_mapping...);
    fn("vSync", "gpu", "v-sync", outs.v_sync...);
    fn("anisotropicFiltering", "gpu", "anisotropic-filtering", outs.anisotropic_filtering...);
    fn("asyncPipelineCompilation", "gpu", "async-pipeline-compilation", outs.async_pipeline_compilation...);
    fn("showCompileShaders", "gpu", "show-compile-shaders", outs.show_compile_shaders...);
    fn("showShaderCacheWarn", "gpu", "show-shader-cache-warn", outs.show_shader_cache_warn...);
    fn("accurateThreadScheduling", "gpu", "accurate-thread-scheduling", outs.accurate_thread_scheduling...);
    fn("guestCores", "gpu", "guest-cores", outs.guest_cores...);
    fn("importTextures", "gpu", "import-textures", outs.import_textures...);
    fn("exportTextures", "gpu", "export-textures", outs.export_textures...);
    fn("exportAsPng", "gpu", "export-as-png", outs.export_as_png...);
    fn("fpsHack", "gpu", "fps-hack", outs.fps_hack...);
    fn("shaderCache", "gpu", "shader-cache", outs.shader_cache...);
    fn("spirvShader", "gpu", "spirv-shader", outs.spirv_shader...);
    fn("textureCache", "gpu", "texture-cache", outs.texture_cache...);
    fn("audioBackend", "audio", "audio-backend", outs.audio_backend...);
    fn("audioVolume", "audio", "audio-volume", outs.audio_volume...);
    fn("ngsEnable", "audio", "enable-ngs", outs.ngs_enable...);
    fn("pstvMode", "system", "pstv-mode", outs.pstv_mode...);
    fn("sysButton", "system", "sys-button", outs.sys_button...);
    fn("sysLang", "system", "sys-lang", outs.sys_lang...);
    fn("fileLoadingDelay", "emulator", "file-loading-delay", outs.file_loading_delay...);
    fn("stretchDisplayArea", "emulator", "stretch-the-display-area", outs.stretch_the_display_area...);
    fn("fullscreenHdResPixelPerfect", "emulator", "fullscreen-hd-res-pixel-perfect", outs.fullscreen_hd_res_pixel_perfect...);
    fn("logActiveShaders", "debug", "log-active-shaders", outs.log_active_shaders...);
    fn("logUniforms", "debug", "log-uniforms", outs.log_uniforms...);
    fn("colorSurfaceDebug", "debug", "color-surface-debug", outs.color_surface_debug...);
    fn("validationLayer", "debug", "validation-layer", outs.validation_layer...);
    fn("psnSignedIn", "network", "psn-signed-in", outs.psn_signed_in...);
}

// Fields only the native settings UI edits (the app never lists them; it always writes gpu-idx="0").
template <typename Fn, typename... Outs>
void visit_native_fields(Fn &&fn, Outs &...outs) {
    fn("cpuOpt", "cpu", "cpu-opt", outs.cpu_opt...);
    fn("gpuIdx", "gpu", "gpu-idx", outs.gpu_idx...);
    fn("sysDateFormat", "system", "sys-date-format", outs.sys_date_format...);
    fn("sysTimeFormat", "system", "sys-time-format", outs.sys_time_format...);
}

// List-valued native fields, stored as child elements.
inline constexpr const char *LLE_MODULES = "lleModules"; // core/lle-modules/module
inline constexpr const char *IME_LANGS = "imeLangs"; // system/ime-langs/lang

// What a profile without the user-overrides attribute still pins (besides the driver).
inline constexpr std::string_view legacy_pinned[] = { "backendRenderer", "gpuIdx" };

inline std::set<std::string> decode(std::string_view raw) {
    std::set<std::string> names;
    while (!raw.empty()) {
        const auto comma = raw.find(',');
        auto item = raw.substr(0, comma);
        raw = comma == std::string_view::npos ? std::string_view{} : raw.substr(comma + 1);
        const auto start = item.find_first_not_of(" \t\r\n");
        if (start == std::string_view::npos)
            continue;
        item = item.substr(start, item.find_last_not_of(" \t\r\n") - start + 1);
        names.emplace(item);
    }
    return names;
}

inline std::string encode(const std::set<std::string> &names) {
    std::string out;
    for (const auto &name : names) {
        if (!out.empty())
            out += ',';
        out += name;
    }
    return out;
}

// The names a profile document pins: its user-overrides list, or legacy_pinned for a legacy profile.
inline std::set<std::string> pinned_names(pugi::xml_node config_root) {
    const auto attribute = config_root.child(OWN_SECTION).attribute(ATTRIBUTE);
    if (!attribute)
        return { std::begin(legacy_pinned), std::end(legacy_pinned) };
    return decode(attribute.value());
}

namespace detail {
inline void read(pugi::xml_attribute a, bool &v) { v = a.as_bool(v); }
inline void read(pugi::xml_attribute a, int &v) { v = a.as_int(v); }
inline void read(pugi::xml_attribute a, float &v) { v = a.as_float(v); }
inline void read(pugi::xml_attribute a, std::string &v) { v = a.as_string(v.c_str()); }
inline void write(pugi::xml_node n, const char *attr, bool v) { n.append_attribute(attr) = v; }
inline void write(pugi::xml_node n, const char *attr, int v) { n.append_attribute(attr) = v; }
inline void write(pugi::xml_node n, const char *attr, float v) { n.append_attribute(attr) = v; }
inline void write(pugi::xml_node n, const char *attr, const std::string &v) { n.append_attribute(attr) = v.c_str(); }
} // namespace detail

// Applies the pinned fields of a profile document over out (which holds the global settings).
template <typename Out>
void apply(Out &out, pugi::xml_node config_root) {
    const auto names = pinned_names(config_root);
    const auto pinned = [&](const char *name) { return names.contains(name); };
    const auto apply_field = [&](const char *name, const char *section, const char *attr, auto &member) {
        if (!pinned(name))
            return;
        if (const auto a = config_root.child(section).attribute(attr))
            detail::read(a, member);
    };
    visit_shared_fields(apply_field, out);
    visit_native_fields(apply_field, out);

    if (pinned(LLE_MODULES)) {
        out.lle_modules.clear();
        for (const auto &m : config_root.child("core").child("lle-modules"))
            out.lle_modules.emplace_back(m.text().as_string());
    }
    if (pinned(IME_LANGS)) {
        std::vector<uint64_t> langs;
        for (const auto &lang : config_root.child("system").child("ime-langs"))
            langs.push_back(lang.text().as_ullong());
        if (!langs.empty())
            out.ime_langs = std::move(langs);
    }
}

// Names of the fields whose values differ between before and after.
template <typename Out>
std::set<std::string> changed(const Out &before, const Out &after) {
    std::set<std::string> names;
    const auto compare = [&](const char *name, const char *, const char *, const auto &a, const auto &b) {
        if (!(a == b))
            names.emplace(name);
    };
    visit_shared_fields(compare, before, after);
    visit_native_fields(compare, before, after);
    if (before.lle_modules != after.lle_modules)
        names.emplace(LLE_MODULES);
    if (before.ime_langs != after.ime_langs)
        names.emplace(IME_LANGS);
    return names;
}

// Writes every table field of cc into its section of config_root (sections are created on demand).
template <typename Out>
void write_fields(pugi::xml_node config_root, const Out &cc) {
    const auto section = [&](const char *name) {
        auto node = config_root.child(name);
        return node ? node : config_root.append_child(name);
    };
    const auto write_field = [&](const char *, const char *sec, const char *attr, const auto &member) {
        detail::write(section(sec), attr, member);
    };
    visit_shared_fields(write_field, cc);
    visit_native_fields(write_field, cc);
    auto lle = section("core").append_child("lle-modules");
    for (const auto &m : cc.lle_modules)
        lle.append_child("module").append_child(pugi::node_pcdata).set_value(m.c_str());
    auto ime = section("system").append_child("ime-langs");
    for (const auto &lang : cc.ime_langs)
        ime.append_child("lang").append_child(pugi::node_pcdata).set_value(std::to_string(lang).c_str());
}

} // namespace config::profile_overrides
