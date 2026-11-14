// EmuCoreV native shader adapter
// Copyright (C) 2026 EmuCoreV contributors
//
// This program is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
// PARTICULAR PURPOSE. See the GNU General Public License for more details.
#include "emucorev/shaders/shader_preset_bridge.h"

#include "renderer/vulkan/screen_renderer.h"
#include "util/log.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#ifdef __ANDROID__
#include <jni.h>
#endif
#include <map>
#include <mutex>

#ifdef __ANDROID__
namespace {
std::string fromJava(JNIEnv *env, jstring value) {
    if (!value)
        return {};
    const jchar *chars = env->GetStringChars(value, nullptr);
    if (!chars)
        return {};
    const jsize length = env->GetStringLength(value);
    std::string result;
    result.reserve(static_cast<size_t>(length));
    auto append = [&result](uint32_t codepoint) {
        if (codepoint <= 0x7f) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    };
    for (jsize i = 0; i < length; ++i) {
        uint32_t codepoint = chars[i];
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && i + 1 < length
            && chars[i + 1] >= 0xdc00 && chars[i + 1] <= 0xdfff) {
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (chars[++i] - 0xdc00);
        } else if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
            codepoint = 0xfffd;
        }
        append(codepoint);
    }
    env->ReleaseStringChars(value, chars);
    return result;
}
} // namespace
#endif

namespace emucorev::shaders {
namespace {
std::mutex active_renderer_mutex;
renderer::vulkan::ScreenRenderer *active_renderer = nullptr;
bool desired_preset_received = false;
std::string desired_preset_path;
std::map<std::string, float> desired_parameters;
std::string bridge_error;
} // namespace

void registerScreenRenderer(renderer::vulkan::ScreenRenderer *renderer) {
    std::lock_guard<std::mutex> lock(active_renderer_mutex);
    active_renderer = renderer;
    if (!renderer)
        return;
    if (desired_preset_received)
        renderer->set_retroarch_shader_preset(desired_preset_path);
    for (const auto &[name, value] : desired_parameters)
        renderer->set_retroarch_shader_parameter(name, value);
}

void unregisterScreenRenderer(renderer::vulkan::ScreenRenderer *renderer) {
    std::lock_guard<std::mutex> lock(active_renderer_mutex);
    if (active_renderer == renderer)
        active_renderer = nullptr;
}

bool requestPreset(const std::string &pack_root, const std::string &preset_path, std::string &error) {
    if (!vulkanShaderRuntimeAvailable()) {
        error = "librashader Vulkan runtime was not included in this build";
        return false;
    }
    std::string canonical_preset;
    if (preset_path.empty()) {
        canonical_preset.clear();
    } else {
        try {
            const auto root = std::filesystem::canonical(pack_root);
            const auto preset = std::filesystem::canonical(preset_path);
            const auto relative = std::filesystem::relative(preset, root);
            std::string extension = preset.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            if (relative.empty() || relative.is_absolute() || *relative.begin() == ".." ||
                !std::filesystem::is_regular_file(preset) || extension != ".slangp") {
                error = "selected shader preset is outside the installed pack or has an unsupported extension";
                return false;
            }
            canonical_preset = preset.string();
        } catch (const std::filesystem::filesystem_error &) {
            error = "selected shader preset or shader pack is missing";
            return false;
        }
    }
    std::lock_guard<std::mutex> lock(active_renderer_mutex);
    const bool preset_changed = !desired_preset_received || desired_preset_path != canonical_preset;
    desired_preset_received = true;
    desired_preset_path = canonical_preset;
    bridge_error.clear();
    if (preset_changed) {
        desired_parameters.clear();
        if (active_renderer)
            active_renderer->clear_retroarch_shader_parameters();
    }
    if (active_renderer)
        active_renderer->set_retroarch_shader_preset(desired_preset_path);
    return true;
}

bool setParameter(const std::string &name, float value) {
    if (!vulkanShaderRuntimeAvailable())
        return false;
    std::lock_guard<std::mutex> lock(active_renderer_mutex);
    auto *renderer = active_renderer;
    if (name.empty() || name.size() > 128 || !std::isfinite(value))
        return false;
    desired_parameters[name] = value;
    if (renderer)
        renderer->set_retroarch_shader_parameter(name, value);
    return true;
}

bool resetParameters() {
    if (!vulkanShaderRuntimeAvailable())
        return false;
    std::lock_guard<std::mutex> lock(active_renderer_mutex);
    auto *renderer = active_renderer;
    desired_parameters.clear();
    if (renderer)
        renderer->clear_retroarch_shader_parameters();
    return true;
}

std::string lastPresetError() {
    std::lock_guard<std::mutex> lock(active_renderer_mutex);
    if (!active_renderer)
        return bridge_error;
    return active_renderer->retroarch_shader_error();
}

} // namespace emucorev::shaders

#ifdef __ANDROID__
extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_data_NativeRetroArchShaderBridge_setPreset(
    JNIEnv *env, jclass, jstring pack_root, jstring preset_path) {
    std::string error;
    const bool applied = emucorev::shaders::requestPreset(fromJava(env, pack_root), fromJava(env, preset_path), error);
    if (!applied && !error.empty())
        LOG_WARN("RetroArch shader selection was not applied: {}", error);
    return applied ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_data_NativeRetroArchShaderBridge_setParameter(
    JNIEnv *env, jclass, jstring name, jfloat value) {
    return emucorev::shaders::setParameter(fromJava(env, name), value) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_data_NativeRetroArchShaderBridge_resetParameters(JNIEnv *, jclass) {
    return emucorev::shaders::resetParameters() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_data_NativeRetroArchShaderBridge_isSupported(JNIEnv *, jclass) {
    return emucorev::shaders::vulkanShaderRuntimeAvailable() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_data_NativeRetroArchShaderBridge_lastError(JNIEnv *env, jclass) {
    const std::string error = emucorev::shaders::lastPresetError();
    return env->NewStringUTF(error.c_str());
}
#endif
