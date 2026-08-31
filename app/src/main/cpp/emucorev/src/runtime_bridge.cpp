// EmuCoreV adapter layer (Layer 2).
//
// Runtime JNI bridge that pushes UI-driven toggles into the active vanilla
// Vita3K emulation state. Uses upstream's android_state accessor; vita3k
// itself remains untouched.

#include "interface.h"

#include <app/functions.h>
#include <app/session_controller.h>
#include <android_state.h>
#include <config/state.h>
#include <emuenv/state.h>
#include <ime/keyboard.h>
#include <ime/functions.h>
#include <dialog/state.h>

#include <jni.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_setPerformanceOverlayState(
    JNIEnv * /*env*/,
    jobject /*thiz*/,
    jboolean enabled,
    jint detail,
    jint position) {
    if (auto *emuenv = get_emuenv()) {
        emuenv->cfg.performance_overlay = static_cast<bool>(enabled);
        emuenv->cfg.performance_overlay_detail = detail;
        emuenv->cfg.performance_overlay_position = position;
        app::sync_perf_overlay_config(*emuenv);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_setAudioVolume(
    JNIEnv * /*env*/,
    jobject /*thiz*/,
    jint volume) {
    if (auto *emuenv = get_emuenv()) {
        emuenv->cfg.audio_volume = volume;
        emuenv->cfg.current_config.audio_volume = volume;
        app::apply_runtime_settings(*emuenv);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_applyRuntimeCoreSettings(
    JNIEnv * /*env*/,
    jobject /*thiz*/,
    jboolean vSync,
    jboolean stretchDisplayArea,
    jboolean disableSurfaceSync,
    jboolean fpsHack,
    jint frameLimit,
    jboolean turboMode,
    jboolean showCompileShaders,
    jboolean pstvMode) {
    if (auto *emuenv = get_emuenv()) {
        auto &cfg = emuenv->cfg;
        auto &current = cfg.current_config;

        current.v_sync = vSync == JNI_TRUE;
        current.stretch_the_display_area = stretchDisplayArea == JNI_TRUE;
        current.disable_surface_sync = disableSurfaceSync == JNI_TRUE;
        current.fps_hack = fpsHack == JNI_TRUE;
        const int normalized_frame_limit = frameLimit == 30 || frameLimit == 45 || frameLimit == 60
            ? frameLimit
            : 0;
        if (emuenv->renderer)
            emuenv->renderer->frame_limit.store(normalized_frame_limit, std::memory_order_relaxed);
        current.pstv_mode = pstvMode == JNI_TRUE;
        cfg.turbo_mode = turboMode == JNI_TRUE;
        current.show_compile_shaders = showCompileShaders == JNI_TRUE;

        app::apply_runtime_settings(*emuenv);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_requestScreenshot(
    JNIEnv * /*env*/,
    jobject /*thiz*/) {
    if (auto *emuenv = get_emuenv()) {
        take_screenshot(*emuenv);
        return JNI_TRUE;
    }
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_setAppSessionMenuPaused(
    JNIEnv * /*env*/,
    jobject /*thiz*/,
    jboolean paused) {
    auto *controller = get_app_session_controller();
    if (!controller || !controller->is_running()) {
        return JNI_FALSE;
    }

    return controller->set_pause_reason(app::AppSessionPauseReason::Menu, paused == JNI_TRUE)
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_isNativeImeActive(
    JNIEnv * /*env*/,
    jobject /*thiz*/) {
    auto *emuenv = get_emuenv();
    auto *controller = get_app_session_controller();
    return emuenv && controller && controller->is_running() && is_any_ime_active(*emuenv)
        ? JNI_TRUE
        : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_dismissNativeIme(
    JNIEnv * /*env*/,
    jobject /*thiz*/) {
    auto *emuenv = get_emuenv();
    auto *controller = get_app_session_controller();
    if (!emuenv || !controller || !controller->is_running()) {
        return JNI_FALSE;
    }

    const bool dismissed = dismiss_current_ime(*emuenv);
    if (dismissed) {
        ime::notify_ime_state_changed();
    }
    return dismissed ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_submitNativeIme(JNIEnv *, jobject) {
    auto *emuenv = get_emuenv();
    auto *controller = get_app_session_controller();
    if (!emuenv || !controller || !controller->is_running() || !is_any_ime_active(*emuenv))
        return JNI_FALSE;
    // Queue Enter behind Android's pending SDL text events. Completing directly
    // on the UI thread can return the old string before the final character arrives.
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_RETURN;
    event.key.down = true;
    return SDL_PushEvent(&event) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_editNativeIme(JNIEnv *env, jobject, jstring text, jint action) {
    auto *emuenv = get_emuenv();
    auto *controller = get_app_session_controller();
    if (!emuenv || !controller || !controller->is_running() || !text || action < 0 || action > 3)
        return JNI_FALSE;
    const jchar *chars = env->GetStringChars(text, nullptr);
    if (!chars)
        return JNI_FALSE;
    std::u16string input(reinterpret_cast<const char16_t *>(chars), env->GetStringLength(text));
    env->ReleaseStringChars(text, chars);
    {
        // Same lock order as the native snapshot/finish paths. Never callback to
        // Java while holding these locks: updates are posted to the UI thread.
        std::lock_guard<std::recursive_mutex> dialog_lock(emuenv->common_dialog.mutex);
        std::lock_guard<std::mutex> ime_lock(emuenv->ime.mutex);
        if (!is_any_ime_active(*emuenv))
            return JNI_FALSE;
        const bool multiline = is_ime_dialog_active(*emuenv) ? emuenv->common_dialog.ime.multiline
            : (emuenv->ime.param.option & SCE_IME_OPTION_MULTILINE) != 0;
        std::erase_if(input, [multiline](char16_t ch) { return ch == 0 || ch == u'\r' || (!multiline && ch == u'\n'); });
        switch (action) {
        case 0: ime_commit_text(emuenv->ime, input); break;
        case 1: ime_backspace(emuenv->ime); break;
        case 2: ime_cursor_left(emuenv->ime); break;
        case 3: ime_cursor_right(emuenv->ime); break;
        }
    }
    ime::notify_ime_state_changed();
    return JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_getRunningGameTitle(
    JNIEnv *env,
    jobject /*thiz*/) {
    auto *controller = get_app_session_controller();
    auto *emuenv = get_emuenv();
    if (!controller || !controller->is_running() || !emuenv) {
        return env->NewStringUTF("");
    }

    return env->NewStringUTF(emuenv->current_app_title.c_str());
}
