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

#include <app/android_runtime.h>

#include <config/state.h>
#include <emuenv/state.h>

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

namespace app {

static EmuEnvState *g_runtime_emuenv = nullptr;

enum struct OverlayShowMask : int {
    Basic = 1,
    L2R2 = 2,
    TouchScreenSwitch = 4,
};

int get_overlay_display_mask(const Config &cfg) {
    if (!cfg.enable_gamepad_overlay)
        return 0;

    int mask = static_cast<int>(OverlayShowMask::Basic);
    if (cfg.pstv_mode)
        mask |= static_cast<int>(OverlayShowMask::L2R2);
    if (cfg.overlay_show_touch_switch)
        mask |= static_cast<int>(OverlayShowMask::TouchScreenSwitch);

    return mask;
}

void attach_runtime_emuenv(EmuEnvState *emuenv) {
    g_runtime_emuenv = emuenv;
}

void set_performance_overlay_state(bool enabled, int32_t detail, int32_t position) {
    if (!g_runtime_emuenv)
        return;

    g_runtime_emuenv->cfg.performance_overlay = enabled;
    g_runtime_emuenv->cfg.performance_overlay_detail = detail;
    g_runtime_emuenv->cfg.performance_overlay_position = position;
}

#ifdef __ANDROID__
static void call_activity_void_method(const char *method_name, const char *signature, auto... args) {
    JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = reinterpret_cast<jobject>(SDL_GetAndroidActivity());
    jclass clazz = env->GetObjectClass(activity);
    jmethodID method_id = env->GetMethodID(clazz, method_name, signature);
    env->CallVoidMethod(activity, method_id, args...);
    env->DeleteLocalRef(activity);
    env->DeleteLocalRef(clazz);
}

void set_controller_overlay_state(int overlay_mask, bool edit, bool reset) {
    call_activity_void_method("setControllerOverlayState", "(IZZ)V", overlay_mask, edit, reset);
}

void set_controller_overlay_scale(float scale) {
    call_activity_void_method("setControllerOverlayScale", "(F)V", scale);
}

void set_controller_overlay_opacity(int opacity) {
    call_activity_void_method("setControllerOverlayOpacity", "(I)V", opacity);
}
#else
void attach_runtime_emuenv(EmuEnvState *emuenv) {}
void set_controller_overlay_state(int overlay_mask, bool edit, bool reset) {}
void set_controller_overlay_scale(float scale) {}
void set_controller_overlay_opacity(int opacity) {}
#endif

} // namespace app

#ifdef __ANDROID__
extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_setPerformanceOverlayState(JNIEnv *env, jobject thiz, jboolean enabled, jint detail, jint position) {
    app::set_performance_overlay_state(enabled, detail, position);
}
#endif
