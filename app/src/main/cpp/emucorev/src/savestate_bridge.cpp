// EmuCoreV save-state JNI bridge (Layer 3).
//
// Kotlin calls these entry points from a background dispatcher. The bridge
// pauses the session, drains the renderer queue and then runs the core
// save-state engine.

#include <android_state.h>
#include <app/session_controller.h>

#include <emucorev/savestate/savestate.h>

#include <display/state.h>
#include <emuenv/state.h>
#include <gxm/functions.h>
#include <gxm/state.h>
#include <io/state.h>
#include <renderer/functions.h>
#include <renderer/state.h>
#include <util/fs.h>

#include <stb_image_write.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace {

std::string to_string_or_empty(JNIEnv *env, jstring value) {
    return value ? jstring_to_string(env, value) : std::string();
}

std::string json_escape(const std::string &value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (const char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

jstring result_to_json(JNIEnv *env, const emucorev::savestate::Result &result) {
    std::string json = "{";
    json += "\"status\":\"" + std::string(emucorev::savestate::status_name(result.status)) + "\",";
    json += "\"error\":\"" + json_escape(result.error) + "\",";
    json += "\"bytes\":" + std::to_string(result.bytes) + ",";
    json += "\"title\":\"" + json_escape(result.meta.title_id) + "\",";
    json += "\"appVersion\":\"" + json_escape(result.meta.app_version) + "\",";
    json += "\"engineVersion\":" + std::to_string(result.meta.engine_version) + ",";
    json += "\"sessionMatch\":" + std::string(result.session_match ? "true" : "false") + ",";
    json += "\"timestamp\":" + std::to_string(result.meta.timestamp_ms);
    json += "}";
    return env->NewStringUTF(json.c_str());
}

jstring ok_json(JNIEnv *env) {
    emucorev::savestate::Result result;
    return result_to_json(env, result);
}

class ScopedUserPause {
public:
    explicit ScopedUserPause(EmuEnvState *emuenv, bool for_load = false)
        : emuenv_(emuenv) {
        auto *controller = get_app_session_controller();
        if (controller && controller->is_running()) {
            controller_ = controller;
            controller_->set_pause_reason(app::AppSessionPauseReason::User, true);
        }
        if (emuenv_ && emuenv_->renderer) {
            if (for_load)
                gxm::invalidate_sync_objects(emuenv_->gxm);
            renderer::finish(*emuenv_->renderer, nullptr);
        }
    }

    ~ScopedUserPause() {
        if (controller_)
            controller_->set_pause_reason(app::AppSessionPauseReason::User, false);
    }

private:
    EmuEnvState *emuenv_ = nullptr;
    app::AppSessionController *controller_ = nullptr;
};

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_SaveStateBridge_nativeSaveState(JNIEnv *env, jobject, jstring path, jstring app_version) {
    const std::string path_str = to_string_or_empty(env, path);
    const std::string app_version_str = to_string_or_empty(env, app_version);
    if (path_str.empty()) {
        emucorev::savestate::Result result;
        result.status = emucorev::savestate::Status::IoError;
        result.error = "empty path";
        return result_to_json(env, result);
    }

    auto *emuenv = get_emuenv();
    if (!emuenv) {
        emucorev::savestate::Result result;
        result.status = emucorev::savestate::Status::NotRunning;
        result.error = "no active session";
        return result_to_json(env, result);
    }

    ScopedUserPause pause(emuenv);
    const auto result = emucorev::savestate::save_state(*emuenv, fs::path(path_str), app_version_str);
    return result_to_json(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_SaveStateBridge_nativeLoadState(JNIEnv *env, jobject, jstring path, jboolean allow_cross_session) {
    const std::string path_str = to_string_or_empty(env, path);
    if (path_str.empty()) {
        emucorev::savestate::Result result;
        result.status = emucorev::savestate::Status::IoError;
        result.error = "empty path";
        return result_to_json(env, result);
    }

    auto *emuenv = get_emuenv();
    if (!emuenv) {
        emucorev::savestate::Result result;
        result.status = emucorev::savestate::Status::NotRunning;
        result.error = "no active session";
        return result_to_json(env, result);
    }

    ScopedUserPause pause(emuenv, true);
    const auto result = emucorev::savestate::load_state(*emuenv, fs::path(path_str), allow_cross_session == JNI_TRUE);
    return result_to_json(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_SaveStateBridge_nativeInspectSaveState(JNIEnv *env, jobject, jstring path) {
    const std::string path_str = to_string_or_empty(env, path);
    auto result = emucorev::savestate::inspect_state(fs::path(path_str));
    if (auto *emuenv = get_emuenv())
        result.session_match = emucorev::savestate::is_current_session(*emuenv, result.meta);
    return result_to_json(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_SaveStateBridge_nativeDeleteSaveState(JNIEnv *env, jobject, jstring path) {
    const std::string path_str = to_string_or_empty(env, path);
    const auto result = emucorev::savestate::delete_state(fs::path(path_str));
    return result_to_json(env, result);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_SaveStateBridge_nativeGetRunningTitleId(JNIEnv *env, jobject) {
    auto *emuenv = get_emuenv();
    if (!emuenv)
        return env->NewStringUTF("");
    return env->NewStringUTF(emuenv->io.title_id.c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_SaveStateBridge_nativeCaptureThumbnail(JNIEnv *env, jobject, jstring path, jint max_width) {
    const std::string path_str = to_string_or_empty(env, path);
    if (path_str.empty())
        return JNI_FALSE;

    auto *emuenv = get_emuenv();
    if (!emuenv || !emuenv->renderer)
        return JNI_FALSE;

    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> frame = emuenv->renderer->dump_frame(emuenv->display, width, height);
    if (frame.empty() || width == 0 || height == 0 || frame.size() != static_cast<size_t>(width) * height)
        return JNI_FALSE;

    for (uint32_t &pixel : frame)
        pixel |= 0xFF000000;

    const uint32_t target_width = std::min<uint32_t>(width, max_width > 0 ? static_cast<uint32_t>(max_width) : 384);
    const uint32_t target_height = std::max<uint32_t>(1, static_cast<uint32_t>(static_cast<uint64_t>(height) * target_width / width));
    std::vector<uint32_t> thumbnail(static_cast<size_t>(target_width) * target_height);

    for (uint32_t y = 0; y < target_height; y++) {
        const uint32_t source_y = static_cast<uint32_t>(static_cast<uint64_t>(y) * height / target_height);
        for (uint32_t x = 0; x < target_width; x++) {
            const uint32_t source_x = static_cast<uint32_t>(static_cast<uint64_t>(x) * width / target_width);
            thumbnail[static_cast<size_t>(y) * target_width + x] = frame[static_cast<size_t>(source_y) * width + source_x];
        }
    }

    const int written = stbi_write_jpg(path_str.c_str(), static_cast<int>(target_width), static_cast<int>(target_height), 4, thumbnail.data(), 82);
    return written == 1 ? JNI_TRUE : JNI_FALSE;
}
