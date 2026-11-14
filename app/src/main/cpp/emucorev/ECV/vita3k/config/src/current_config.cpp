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

#include <config/functions.h>

#include <config/state.h>
#include <emuenv/state.h>
#include <util/log.h>
#include <util/string_utils.h>

#include <fmt/format.h>
#include <pugixml.hpp>

namespace config {

static Config::CurrentConfig current_config;

static bool get_custom_config(EmuEnvState &emuenv, const std::string &app_path) {
    const auto custom_config_path{ emuenv.config_path / "config" / fmt::format("config_{}.xml", app_path) };

    if (!fs::exists(custom_config_path))
        return false;

    pugi::xml_document custom_config_xml;
    if (!custom_config_xml.load_file(custom_config_path.c_str()) || custom_config_xml.child("config").empty()) {
        LOG_ERROR("Custom config XML found is corrupted or invalid in path: {}", custom_config_path);
        fs::remove(custom_config_path);
        return false;
    }

    current_config = {};
    const auto config_child = custom_config_xml.child("config");

    if (!config_child.child("core").empty()) {
        const auto core_child = config_child.child("core");
        current_config.modules_mode = core_child.attribute("modules-mode").as_int();
        for (const auto &m : core_child.child("lle-modules"))
            current_config.lle_modules.emplace_back(m.text().as_string());
    }

    if (!config_child.child("cpu").empty()) {
        const auto cpu_child = config_child.child("cpu");
        current_config.cpu_opt = cpu_child.attribute("cpu-opt").as_bool();
    }

    if (!config_child.child("gpu").empty()) {
        const auto gpu_child = config_child.child("gpu");
        current_config.backend_renderer = gpu_child.attribute("backend-renderer").as_string();
#ifdef __ANDROID__
        current_config.custom_driver_name = gpu_child.attribute("custom-driver-name").as_string();
#endif
        current_config.high_accuracy = gpu_child.attribute("high-accuracy").as_bool();
        current_config.resolution_multiplier = gpu_child.attribute("resolution-multiplier").as_float();
        current_config.disable_surface_sync = gpu_child.attribute("disable-surface-sync").as_bool();
        current_config.screen_filter = gpu_child.attribute("screen-filter").as_string();
        current_config.memory_mapping = gpu_child.attribute("memory-mapping").as_string();
        current_config.v_sync = gpu_child.attribute("v-sync").as_bool();
        current_config.anisotropic_filtering = gpu_child.attribute("anisotropic-filtering").as_int();
        current_config.async_pipeline_compilation = gpu_child.attribute("async-pipeline-compilation").as_bool();
        current_config.import_textures = gpu_child.attribute("import-textures").as_bool();
        current_config.export_textures = gpu_child.attribute("export-textures").as_bool();
        current_config.export_as_png = gpu_child.attribute("export-as-png").as_bool();
        current_config.fps_hack = gpu_child.attribute("fps-hack").as_bool();
        current_config.show_shader_cache_warn = gpu_child.attribute("show-shader-cache-warn").as_bool(true);
    }

    if (!config_child.child("audio").empty()) {
        const auto audio_child = config_child.child("audio");
        current_config.audio_volume = audio_child.attribute("audio-volume").as_int();
        current_config.ngs_enable = audio_child.attribute("enable-ngs").as_bool();
    }

    const auto system_child = config_child.child("system");
    if (!system_child.empty())
        current_config.pstv_mode = system_child.attribute("pstv-mode").as_bool();

    if (!config_child.child("emulator").empty()) {
        const auto emulator_child = config_child.child("emulator");
        current_config.show_touchpad_cursor = emulator_child.attribute("show-touchpad-cursor").as_bool();
        current_config.file_loading_delay = emulator_child.attribute("file-loading-delay").as_int();
    }

    if (!config_child.child("network").empty()) {
        const auto network_child = config_child.child("network");
        current_config.psn_signed_in = network_child.attribute("psn-signed-in").as_bool();
    }

    return true;
}

static void set_backend_renderer(EmuEnvState &emuenv, const std::string &backend_renderer) {
#ifndef __APPLE__
    if (string_utils::toupper(backend_renderer) == "OPENGL")
        emuenv.backend_renderer = static_cast<renderer::Backend>(0);
    else
        emuenv.backend_renderer = static_cast<renderer::Backend>(1);
#else
    emuenv.backend_renderer = static_cast<renderer::Backend>(1);
#endif
}

void set_current_config(EmuEnvState &emuenv, const std::string &app_path) {
    if (!app_path.empty() && get_custom_config(emuenv, app_path))
        emuenv.cfg.current_config = current_config;
    else {
        emuenv.cfg.current_config.cpu_opt = emuenv.cfg.cpu_opt;
        emuenv.cfg.current_config.modules_mode = emuenv.cfg.modules_mode;
        emuenv.cfg.current_config.lle_modules = emuenv.cfg.lle_modules;
        emuenv.cfg.current_config.backend_renderer = emuenv.cfg.backend_renderer;
#ifdef __ANDROID__
        emuenv.cfg.current_config.custom_driver_name = emuenv.cfg.custom_driver_name;
#endif
        emuenv.cfg.current_config.high_accuracy = emuenv.cfg.high_accuracy;
        emuenv.cfg.current_config.resolution_multiplier = emuenv.cfg.resolution_multiplier;
        emuenv.cfg.current_config.disable_surface_sync = emuenv.cfg.disable_surface_sync;
        emuenv.cfg.current_config.screen_filter = emuenv.cfg.screen_filter;
        emuenv.cfg.current_config.memory_mapping = emuenv.cfg.memory_mapping;
        emuenv.cfg.current_config.v_sync = emuenv.cfg.v_sync;
        emuenv.cfg.current_config.anisotropic_filtering = emuenv.cfg.anisotropic_filtering;
        emuenv.cfg.current_config.async_pipeline_compilation = emuenv.cfg.async_pipeline_compilation;
        emuenv.cfg.current_config.import_textures = emuenv.cfg.import_textures;
        emuenv.cfg.current_config.export_textures = emuenv.cfg.export_textures;
        emuenv.cfg.current_config.export_as_png = emuenv.cfg.export_as_png;
        emuenv.cfg.current_config.fps_hack = emuenv.cfg.fps_hack;
        emuenv.cfg.current_config.audio_volume = emuenv.cfg.audio_volume;
        emuenv.cfg.current_config.ngs_enable = emuenv.cfg.ngs_enable;
        emuenv.cfg.current_config.pstv_mode = emuenv.cfg.pstv_mode;
        emuenv.cfg.current_config.show_touchpad_cursor = emuenv.cfg.show_touchpad_cursor;
        emuenv.cfg.current_config.file_loading_delay = emuenv.cfg.file_loading_delay;
        emuenv.cfg.current_config.psn_signed_in = emuenv.cfg.psn_signed_in;
        emuenv.cfg.current_config.show_shader_cache_warn = emuenv.cfg.show_shader_cache_warn;
    }

    set_backend_renderer(emuenv, emuenv.cfg.current_config.backend_renderer);
}

} // namespace config
