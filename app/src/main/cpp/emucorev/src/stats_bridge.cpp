#include <android_state.h>
#include <config/version.h>
#include <display/state.h>
#include <emucorev/savestate/operation_mutex.h>
#include <renderer/state.h>

#include <jni.h>
#include <mutex>
#include <sstream>
#include <string>

namespace {
std::string quote(const std::string_view value) {
    std::string result = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { result += '\\'; result += ch; }
        else if (ch < 0x20) { result += "\\u00"; result += hex[ch >> 4]; result += hex[ch & 15]; }
        else result += ch;
    }
    return result + '"';
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_PerformanceStatsBridge_nativeSnapshot(JNIEnv *env, jobject) {
    // Same lifetime boundary as shutdown/Save/Load; no kernel or renderer gate is acquired.
    std::lock_guard lock(emucorev::savestate::save_state_operation_mutex());
    auto *controller = get_app_session_controller();
    auto *emuenv = get_emuenv();
    if (!controller || !controller->is_running() || !emuenv || !emuenv->renderer)
        return env->NewStringUTF("{}");
    auto &state = *emuenv->renderer;
    auto &telemetry = state.performance_stats;
    const auto updated = telemetry.updated_ms.load(std::memory_order_acquire);
    const auto packed = telemetry.frame_metrics.load(std::memory_order_acquire);
    std::ostringstream out;
    out << "{\"gpu\":" << quote(state.get_gpu_name())
        << ",\"backend\":" << quote(state.current_backend == renderer::Backend::Vulkan ? "Vulkan" : "OpenGL")
        << ",\"core\":" << quote(std::string("Vita3K ") + app_version)
        << ",\"paused\":" << (controller->is_paused() ? "true" : "false");
    if (updated > 0 && renderer::PerformanceStatsTelemetry::now_ms() - updated < 3000 && !controller->is_paused())
        out << ",\"fps\":" << uint32_t(packed) << ",\"frameMs\":" << uint32_t(packed >> 32);
    {
        std::lock_guard display_lock(emuenv->display.display_info_mutex);
        out << ",\"width\":" << emuenv->display.next_rendered_frame.image_size.x
            << ",\"height\":" << emuenv->display.next_rendered_frame.image_size.y;
    }
    out << '}';
    return env->NewStringUTF(out.str().c_str());
}
