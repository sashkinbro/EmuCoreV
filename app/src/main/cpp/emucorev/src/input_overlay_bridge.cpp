// EmuCoreV adapter layer (Layer 2).
//
// Forwards JNI entry points expected by com.sbro.emucorev.core.vita.overlay.InputOverlay
// to the vanilla Vita3K implementations exported under org.vita3k.emulator.overlay.InputOverlay.
// The upstream input_overlay.cpp in vita3k/ stays untouched.

#include <jni.h>

extern "C" {

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_attachController(JNIEnv *env, jobject thiz);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_detachController(JNIEnv *env, jobject thiz);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_setAxis(JNIEnv *env, jobject thiz, jint axis, jshort value);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_setButton(JNIEnv *env, jobject thiz, jint button, jboolean value);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_setTouchState(JNIEnv *env, jobject thiz, jboolean is_back);

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_attachController(JNIEnv *env, jobject thiz) {
    Java_org_vita3k_emulator_overlay_InputOverlay_attachController(env, thiz);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_detachController(JNIEnv *env, jobject thiz) {
    Java_org_vita3k_emulator_overlay_InputOverlay_detachController(env, thiz);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_setAxis(JNIEnv *env, jobject thiz, jint axis, jshort value) {
    Java_org_vita3k_emulator_overlay_InputOverlay_setAxis(env, thiz, axis, value);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_setButton(JNIEnv *env, jobject thiz, jint button, jboolean value) {
    Java_org_vita3k_emulator_overlay_InputOverlay_setButton(env, thiz, button, value);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_setTouchState(JNIEnv *env, jobject thiz, jboolean is_back) {
    Java_org_vita3k_emulator_overlay_InputOverlay_setTouchState(env, thiz, is_back);
}

} // extern "C"
