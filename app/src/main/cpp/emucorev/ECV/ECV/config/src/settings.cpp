// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <config/settings.h>

#include <config/functions.h>
#include <config/profile_overrides.h>

#include <util/log.h>
#include <util/fs.h>
#include <util/vector_utils.h>

#include <pugixml.hpp>

#include <algorithm>
#include <set>

namespace config {

namespace {

void copy_global_to_current(Config::CurrentConfig &current, const Config &cfg) {
    current.cpu_opt = cfg.cpu_opt;
    current.modules_mode = cfg.modules_mode;
    current.lle_modules = cfg.lle_modules;
    current.backend_renderer = cfg.backend_renderer;
    current.gpu_idx = cfg.gpu_idx;
#ifdef __ANDROID__
    current.custom_driver_name = cfg.custom_driver_name;
#endif
    current.resolution_multiplier = cfg.resolution_multiplier;
    current.disable_surface_sync = cfg.disable_surface_sync;
    current.screen_filter = cfg.screen_filter;
    current.memory_mapping = cfg.memory_mapping;
    current.v_sync = cfg.v_sync;
    current.anisotropic_filtering = cfg.anisotropic_filtering;
    current.async_pipeline_compilation = cfg.async_pipeline_compilation;
    current.show_compile_shaders = cfg.show_compile_shaders;
    current.show_shader_cache_warn = cfg.show_shader_cache_warn;
    current.accurate_thread_scheduling = cfg.accurate_thread_scheduling;
    current.eager_surface_sync = cfg.eager_surface_sync;
    current.guest_cores = cfg.guest_cores;
    current.import_textures = cfg.import_textures;
    current.export_textures = cfg.export_textures;
    current.export_as_png = cfg.export_as_png;
    current.fps_hack = cfg.fps_hack;
    current.shader_cache = cfg.shader_cache;
    current.spirv_shader = cfg.spirv_shader;
    current.texture_cache = cfg.texture_cache;
    current.audio_backend = cfg.audio_backend;
    current.audio_volume = cfg.audio_volume;
    current.ngs_enable = cfg.ngs_enable;
    current.pstv_mode = cfg.pstv_mode;
    current.stretch_the_display_area = cfg.stretch_the_display_area;
    current.fullscreen_hd_res_pixel_perfect = cfg.fullscreen_hd_res_pixel_perfect;
    current.file_loading_delay = cfg.file_loading_delay;
    current.psn_signed_in = cfg.psn_signed_in;
    current.sys_button = cfg.sys_button;
    current.sys_lang = cfg.sys_lang;
    current.sys_date_format = cfg.sys_date_format;
    current.sys_time_format = cfg.sys_time_format;
    current.ime_langs = cfg.ime_langs;
    current.log_active_shaders = cfg.log_active_shaders;
    current.log_uniforms = cfg.log_uniforms;
    current.color_surface_debug = cfg.color_surface_debug;
    current.validation_layer = cfg.validation_layer;
    current.tracy_primitive_impl = cfg.tracy_primitive_impl;
    current.tracy_advanced_profiling_modules = cfg.tracy_advanced_profiling_modules;
}

void copy_current_to_global(Config &cfg, const Config::CurrentConfig &current) {
    cfg.cpu_opt = current.cpu_opt;
    cfg.modules_mode = current.modules_mode;
    cfg.lle_modules = current.lle_modules;
    cfg.backend_renderer = current.backend_renderer;
    cfg.gpu_idx = current.gpu_idx;
#ifdef __ANDROID__
    cfg.custom_driver_name = current.custom_driver_name;
#endif
    cfg.resolution_multiplier = current.resolution_multiplier;
    cfg.disable_surface_sync = current.disable_surface_sync;
    cfg.screen_filter = current.screen_filter;
    cfg.memory_mapping = current.memory_mapping;
    cfg.v_sync = current.v_sync;
    cfg.anisotropic_filtering = current.anisotropic_filtering;
    cfg.async_pipeline_compilation = current.async_pipeline_compilation;
    cfg.show_compile_shaders = current.show_compile_shaders;
    cfg.show_shader_cache_warn = current.show_shader_cache_warn;
    cfg.accurate_thread_scheduling = current.accurate_thread_scheduling;
    cfg.eager_surface_sync = current.eager_surface_sync;
    cfg.guest_cores = current.guest_cores;
    cfg.import_textures = current.import_textures;
    cfg.export_textures = current.export_textures;
    cfg.export_as_png = current.export_as_png;
    cfg.fps_hack = current.fps_hack;
    cfg.shader_cache = current.shader_cache;
    cfg.spirv_shader = current.spirv_shader;
    cfg.texture_cache = current.texture_cache;
    cfg.audio_backend = current.audio_backend;
    cfg.audio_volume = current.audio_volume;
    cfg.ngs_enable = current.ngs_enable;
    cfg.pstv_mode = current.pstv_mode;
    cfg.stretch_the_display_area = current.stretch_the_display_area;
    cfg.fullscreen_hd_res_pixel_perfect = current.fullscreen_hd_res_pixel_perfect;
    cfg.file_loading_delay = current.file_loading_delay;
    cfg.psn_signed_in = current.psn_signed_in;
    cfg.sys_button = current.sys_button;
    cfg.sys_lang = current.sys_lang;
    cfg.sys_date_format = current.sys_date_format;
    cfg.sys_time_format = current.sys_time_format;
    cfg.ime_langs = current.ime_langs;
    cfg.log_active_shaders = current.log_active_shaders;
    cfg.log_uniforms = current.log_uniforms;
    cfg.color_surface_debug = current.color_surface_debug;
    cfg.validation_layer = current.validation_layer;
    cfg.tracy_primitive_impl = current.tracy_primitive_impl;
    cfg.tracy_advanced_profiling_modules = current.tracy_advanced_profiling_modules;
}

} // namespace

std::vector<RestartRequiredSetting> get_restart_required_settings(
    const Config::CurrentConfig &before,
    const Config::CurrentConfig &after) {
    std::vector<RestartRequiredSetting> changed;

    const auto append_if_changed = [&](bool has_changed, RestartRequiredSetting setting) {
        if (has_changed)
            changed.emplace_back(setting);
    };

    append_if_changed(before.cpu_opt != after.cpu_opt, RestartRequiredSetting::CpuOpt);
    append_if_changed(before.backend_renderer != after.backend_renderer, RestartRequiredSetting::BackendRenderer);
    append_if_changed(before.gpu_idx != after.gpu_idx, RestartRequiredSetting::GraphicsDevice);
#ifdef __ANDROID__
    append_if_changed(before.custom_driver_name != after.custom_driver_name, RestartRequiredSetting::CustomDriver);
#endif
    append_if_changed(before.resolution_multiplier != after.resolution_multiplier, RestartRequiredSetting::ResolutionMultiplier);
    append_if_changed(before.memory_mapping != after.memory_mapping, RestartRequiredSetting::MemoryMapping);
    append_if_changed(before.audio_backend != after.audio_backend, RestartRequiredSetting::AudioBackend);
    append_if_changed(before.validation_layer != after.validation_layer, RestartRequiredSetting::ValidationLayer);
    append_if_changed(before.accurate_thread_scheduling != after.accurate_thread_scheduling, RestartRequiredSetting::AccurateThreadScheduling);
    append_if_changed(before.eager_surface_sync != after.eager_surface_sync, RestartRequiredSetting::EagerSurfaceSync);

    return changed;
}

static fs::path get_custom_config_path(const fs::path &config_path, const std::string &app_path) {
    return config_path / "config" / fmt::format("config_{}.xml", app_path);
}

bool load_custom_config(Config::CurrentConfig &out, const fs::path &config_path, const std::string &app_path) {
    if (app_path.empty())
        return false;

    const auto custom_cfg_path = get_custom_config_path(config_path, app_path);
    if (!fs::exists(custom_cfg_path))
        return false;

    pugi::xml_document doc;
    if (!doc.load_file(custom_cfg_path.c_str()) || doc.child("config").empty()) {
        LOG_ERROR("Custom config at {} is corrupted or invalid.", custom_cfg_path);
        fs::remove(custom_cfg_path);
        return false;
    }

    const auto config_child = doc.child("config");
    profile_overrides::apply(out, config_child);
#ifdef __ANDROID__
    // The app resolves the driver for every launch and projects it here, profile or not.
    if (const auto driver = config_child.child("gpu").attribute("custom-driver-name"))
        out.custom_driver_name = driver.as_string();
#endif

    return true;
}

bool save_custom_config(const Config::CurrentConfig &cc, const Config::CurrentConfig &global, const fs::path &config_path, const std::string &app_path) {
    if (app_path.empty())
        return false;

    const auto dir = config_path / "config";
    fs::create_directories(dir);
    const auto custom_cfg_path = get_custom_config_path(config_path, app_path);

    // Fields already pinned stay pinned; every field that now differs from the global settings is pinned too.
    std::set<std::string> overrides = profile_overrides::changed(global, cc);
    pugi::xml_document previous;
    if (fs::exists(custom_cfg_path) && previous.load_file(custom_cfg_path.c_str()) && previous.child("config")) {
        const auto pinned = profile_overrides::pinned_names(previous.child("config"));
        overrides.insert(pinned.begin(), pinned.end());
    }

    pugi::xml_document doc;
    auto decl = doc.append_child(pugi::node_declaration);
    decl.append_attribute("version") = "1.0";
    decl.append_attribute("encoding") = "utf-8";

    auto config_child = doc.append_child("config");
    profile_overrides::write_fields(config_child, cc);
#ifdef __ANDROID__
    config_child.child("gpu").append_attribute("custom-driver-name") = cc.custom_driver_name.c_str();
#endif

    // The app keeps its own settings (input mapping, driver mode) in this element: carry it over.
    auto own = previous.child("config").child(profile_overrides::OWN_SECTION);
    auto own_child = own ? config_child.append_copy(own) : config_child.append_child(profile_overrides::OWN_SECTION);
    own_child.remove_attribute(profile_overrides::ATTRIBUTE);
    own_child.append_attribute(profile_overrides::ATTRIBUTE) = profile_overrides::encode(overrides).c_str();

    if (!doc.save_file(custom_cfg_path.c_str())) {
        LOG_ERROR("Failed to save custom config xml for app path: {}", app_path);
        return false;
    }

    return true;
}

bool delete_custom_config(const fs::path &config_path, const std::string &app_path) {
    if (app_path.empty())
        return false;

    const auto custom_cfg_path = get_custom_config_path(config_path, app_path);
    if (fs::exists(custom_cfg_path)) {
        fs::remove(custom_cfg_path);
        return true;
    }
    return false;
}

int delete_all_custom_configs(const fs::path &config_path) {
    const auto config_dir = config_path / "config";
    if (!fs::exists(config_dir) || !fs::is_directory(config_dir))
        return 0;

    int removed = 0;
    for (const auto &entry : fs::directory_iterator(config_dir)) {
        if (!entry.is_regular_file())
            continue;

        const auto file_name = entry.path().filename().string();
        if (!entry.path().has_extension() || entry.path().extension() != ".xml")
            continue;
        if (file_name.rfind("config_", 0) != 0)
            continue;

        boost::system::error_code error;
        if (fs::remove(entry.path(), error) && !error)
            ++removed;
    }

    return removed;
}

bool has_custom_config(const fs::path &config_path, const std::string &app_path) {
    if (app_path.empty())
        return false;
    return fs::exists(get_custom_config_path(config_path, app_path));
}

void set_current_config(Config &cfg, const fs::path &config_path, const std::string &app_path) {
    copy_global_to_current(cfg.current_config, cfg);
    if (!app_path.empty())
        load_custom_config(cfg.current_config, config_path, app_path);
}

void copy_current_config_to_global(Config &cfg) {
    copy_current_to_global(cfg, cfg.current_config);
}

void save_current_config(Config &cfg, const fs::path &config_path, const std::string &app_path, bool create_custom_if_missing) {
    if (!app_path.empty() && (create_custom_if_missing || has_custom_config(config_path, app_path))) {
        Config::CurrentConfig global;
        copy_global_to_current(global, cfg);
        save_custom_config(cfg.current_config, global, config_path, app_path);
    } else {
        copy_current_config_to_global(cfg);
    }
    serialize_config(cfg, config_path);
}

std::vector<std::pair<std::string, bool>> get_modules_list(
    const fs::path &vita_fs_path,
    const std::vector<std::string> &lle_modules) {
    std::vector<std::pair<std::string, bool>> modules;

    const auto modules_path = vita_fs_path / "vs0/sys/external/";
    if (fs::exists(modules_path) && !fs::is_empty(modules_path)) {
        for (const auto &entry : fs::directory_iterator(modules_path)) {
            if (entry.path().extension() == ".suprx")
                modules.emplace_back(entry.path().filename().replace_extension().string(), false);
        }

        for (auto &m : modules)
            m.second = std::ranges::contains(lle_modules, m.first);

        std::sort(modules.begin(), modules.end(), [](const auto &a, const auto &b) {
            if (a.second == b.second)
                return a.first < b.first;
            return a.second;
        });
    }

    return modules;
}

} // namespace config
