// EmuCoreV adapter layer (Layer 2).
//
// Adapts JNI entry points expected by com.sbro.emucorev.core.vita.overlay.InputOverlay
// to Vita3K's Android overlay controller implementation.

#include <jni.h>

bool attach_overlay_virtual_controller();

extern "C" {

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_detachController(JNIEnv *env, jobject thiz);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_setAxis(JNIEnv *env, jobject thiz, jint axis, jshort value);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_setButton(JNIEnv *env, jobject thiz, jint button, jboolean value);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_overlay_InputOverlay_setTouchState(JNIEnv *env, jobject thiz, jint mode);

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_attachController(JNIEnv *env, jobject thiz) {
    return attach_overlay_virtual_controller() ? JNI_TRUE : JNI_FALSE;
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
Java_com_sbro_emucorev_core_vita_overlay_InputOverlay_setTouchState(JNIEnv *env, jobject thiz, jint mode) {
    Java_org_vita3k_emulator_overlay_InputOverlay_setTouchState(env, thiz, mode);
}

} // extern "C"
