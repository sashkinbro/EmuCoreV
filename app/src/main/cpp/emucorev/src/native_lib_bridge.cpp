// EmuCoreV adapter layer (Layer 2).
//
// Forwards bootstrap JNI entry points expected by com.sbro.emucorev.core.NativeLib
// to the vanilla Vita3K implementations exported under org.vita3k.emulator.NativeLib.
// The upstream native_bootstrap.cpp in vita3k/ stays untouched.

#include <jni.h>

extern "C" {

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_prepareFrontend(JNIEnv *env, jclass clazz);

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_init(JNIEnv *env, jclass clazz, jstring storage_path);

JNIEXPORT jboolean JNICALL
Java_org_vita3k_emulator_NativeLib_isInitialized(JNIEnv *env, jclass clazz);

JNIEXPORT void JNICALL
Java_org_vita3k_emulator_NativeLib_refreshAppsList(JNIEnv *env, jclass clazz);

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_NativeLib_prepareFrontend(JNIEnv *env, jobject /*thiz*/) {
    return Java_org_vita3k_emulator_NativeLib_prepareFrontend(env, nullptr);
}

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_NativeLib_init(JNIEnv *env, jobject /*thiz*/, jstring storage_path) {
    return Java_org_vita3k_emulator_NativeLib_init(env, nullptr, storage_path);
}

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_NativeLib_isInitialized(JNIEnv *env, jobject /*thiz*/) {
    return Java_org_vita3k_emulator_NativeLib_isInitialized(env, nullptr);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_NativeLib_refreshAppsList(JNIEnv *env, jobject /*thiz*/) {
    Java_org_vita3k_emulator_NativeLib_refreshAppsList(env, nullptr);
}

} // extern "C"
