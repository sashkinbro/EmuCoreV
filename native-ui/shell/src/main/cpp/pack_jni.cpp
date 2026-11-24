// JNI entry of the firmware asset extractor (FirmwarePack.kt). Standalone: no
// emulator, SDL or guest CPU; it only reads the installed firmware files.

#include <emucorev/extract/pack_extractor.h>

#include <android/log.h>
#include <jni.h>

#include <string>

namespace {
std::string utf(JNIEnv* env, jstring text) {
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string value = chars ? chars : "";
    if (chars) env->ReleaseStringUTFChars(text, chars);
    return value;
}
}  // namespace

extern "C" JNIEXPORT jstring JNICALL Java_com_sbro_emucorev_nativeui_FirmwarePack_nativeExtract(
    JNIEnv* env, jobject, jstring vita_root, jstring out_directory) {
    const auto result = emucorev::extract::extract_pack(utf(env, vita_root), utf(env, out_directory),
        [](int percent, const char* stage) { __android_log_print(ANDROID_LOG_DEBUG, "FirmwarePack", "%d%% %s", percent, stage); });
    for (const auto& warning : result.warnings)
        __android_log_print(ANDROID_LOG_WARN, "FirmwarePack", "%s", warning.c_str());
    if (result.ok) return nullptr;
    return env->NewStringUTF(result.error.empty() ? "Firmware asset extraction failed" : result.error.c_str());
}

extern "C" JNIEXPORT jint JNICALL Java_com_sbro_emucorev_nativeui_FirmwarePack_nativeRevision(JNIEnv*, jobject) {
    return emucorev::extract::kExtractorRevision;
}
