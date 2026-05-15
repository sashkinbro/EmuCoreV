// EmuCoreV adapter layer (Layer 2).
//
// Runtime JNI bridge that pushes UI-driven toggles into the active vanilla
// Vita3K emulation state. Uses upstream's android_state accessor; vita3k
// itself remains untouched.

#include "interface.h"

#include <android_state.h>
#include <config/state.h>
#include <emuenv/state.h>

#include <jni.h>

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
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_setAudioVolume(
    JNIEnv * /*env*/,
    jobject /*thiz*/,
    jint volume) {
    if (auto *emuenv = get_emuenv()) {
        emuenv->cfg.audio_volume = volume;
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
