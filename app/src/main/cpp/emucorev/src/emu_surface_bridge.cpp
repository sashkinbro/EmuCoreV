// EmuCoreV adapter layer (Layer 2).
//
// Forwards the JNI entry point expected by com.sbro.emucorev.core.vita.EmuSurface
// to the vanilla Vita3K implementation exported under org.vita3k.emulator.EmuSurface.
// The upstream screen_renderer.cpp in vita3k/ stays untouched.

#include <jni.h>

extern "C" {

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_EmuSurface_setSurfaceStatus(JNIEnv *env, jobject thiz, jboolean surface_present);

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_EmuSurface_setSurfaceStatus(JNIEnv *env, jobject thiz, jboolean surface_present) {
    Java_org_vita3k_emulator_EmuSurface_setSurfaceStatus(env, thiz, surface_present);
}

} // extern "C"
