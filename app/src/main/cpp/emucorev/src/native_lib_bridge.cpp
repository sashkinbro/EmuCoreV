// EmuCoreV adapter layer (Layer 2).
//
// Forwards bootstrap JNI entry points expected by com.sbro.emucorev.core.NativeLib
// to the vanilla Vita3K implementations exported under org.vita3k.emulator.NativeLib.
// The upstream native_bootstrap.cpp in vita3k/ stays untouched.

#include <jni.h>

#include <android_state.h>
#include <renderer/state.h>
#include <util/log.h>

extern "C" {

JNIEXPORT void JNICALL
ECV_NativeBridge_NativeLib_onTrimMemory(JNIEnv *env, jclass clazz, jint level);

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_NativeLib_onTrimMemory(JNIEnv *env, jobject, jint level) {
    ECV_NativeBridge_NativeLib_onTrimMemory(env, nullptr, level);
}

JNIEXPORT jboolean JNICALL
ECV_NativeBridge_NativeLib_prepareFrontend(JNIEnv *env, jclass clazz);

JNIEXPORT jboolean JNICALL
ECV_initWithPaths(JNIEnv *env, jstring runtime_path, jstring vita_path);

JNIEXPORT jboolean JNICALL
ECV_NativeBridge_NativeLib_isInitialized(JNIEnv *env, jclass clazz);

JNIEXPORT void JNICALL
ECV_NativeBridge_NativeLib_refreshAppsList(JNIEnv *env, jclass clazz);

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_NativeLib_prepareFrontend(JNIEnv *env, jobject /*thiz*/) {
    return ECV_NativeBridge_NativeLib_prepareFrontend(env, nullptr);
}

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_NativeLib_init(
    JNIEnv *env,
    jobject /*thiz*/,
    jstring runtime_path,
    jstring vita_path) {
    return ECV_initWithPaths(env, runtime_path, vita_path);
}

JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_NativeLib_isInitialized(JNIEnv *env, jobject /*thiz*/) {
    return ECV_NativeBridge_NativeLib_isInitialized(env, nullptr);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_NativeLib_refreshAppsList(JNIEnv *env, jobject /*thiz*/) {
    ECV_NativeBridge_NativeLib_refreshAppsList(env, nullptr);
}

// Release builds ship native as RelWithDebInfo (Play symbols), where upstream
// leaves spdlog verbose with flush_on(trace): every log hits logcat + file and
// starves the UI thread (Vitals ANR under condition_variable::wait). Keep only
// critical logs: steady state stays silent (no per-log flush), while the
// [CRASH]/fatal diagnostics emitted before an abort still reach vita3k.log.
JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_NativeLib_applyReleaseLogging(JNIEnv *, jobject /*thiz*/) {
    logging::set_level(spdlog::level::critical);
}

JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_NativeLib_setControlsEditorActive(JNIEnv *, jobject /*thiz*/, jboolean active) {
    auto *emuenv = get_emuenv();
    if (!emuenv || !emuenv->renderer)
        return;
    emuenv->renderer->show_pause_overlay.store(active == JNI_FALSE, std::memory_order_relaxed);
}

} // extern "C"
