// Vita3K emulator project
// Copyright (C) 2026 Vita3K team

#include "interface.h"

#include <config/state.h>
#include <emuenv/state.h>
#include <jni.h>
#include <packages/functions.h>
#include <packages/license.h>
#include <packages/pkg.h>
#include <util/string_utils.h>

namespace {

std::string from_jstring(JNIEnv *env, jstring value) {
    if (!value)
        return {};
    const char *chars = env->GetStringUTFChars(value, nullptr);
    std::string result = chars ? chars : "";
    env->ReleaseStringUTFChars(value, chars);
    return result;
}

class JavaProgressReporter {
public:
    JavaProgressReporter(JNIEnv *env, jobject bridge_object)
        : env_(env)
        , bridge_class_(env->GetObjectClass(bridge_object))
        , callback_(env->GetStaticMethodID(
              bridge_class_,
              "onNativeProgress",
              "(Ljava/lang/String;FFFLjava/lang/String;)V")) {}

    ~JavaProgressReporter() {
        if (bridge_class_) {
            env_->DeleteLocalRef(bridge_class_);
        }
    }

    void report(const char *stage, float progress, float current, float total, const std::string &detail = {}) const {
        if (!callback_)
            return;

        jstring j_stage = env_->NewStringUTF(stage);
        jstring j_detail = detail.empty() ? nullptr : env_->NewStringUTF(detail.c_str());
        env_->CallStaticVoidMethod(bridge_class_, callback_, j_stage, progress, current, total, j_detail);
        env_->DeleteLocalRef(j_stage);
        if (j_detail) {
            env_->DeleteLocalRef(j_detail);
        }
    }

private:
    JNIEnv *env_;
    jclass bridge_class_;
    jmethodID callback_;
};

void configure_install_env(EmuEnvState &emuenv, const fs::path &vita_root, const fs::path &cache_root, jint system_language) {
    emuenv.pref_path = vita_root;
    emuenv.cache_path = cache_root;
    emuenv.cfg.set_pref_path(vita_root);
    emuenv.cfg.sys_lang = system_language;
    fs::create_directories(emuenv.pref_path);
    fs::create_directories(emuenv.cache_path);
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_sbro_emucorev_core_VitaInstallBridge_nativeInstallFirmware(
    JNIEnv *env,
    jobject thiz,
    jstring vita_root_path,
    jstring firmware_path,
    jint system_language) {
    const auto vita_root = fs_utils::utf8_to_path(from_jstring(env, vita_root_path));
    const auto firmware = fs_utils::utf8_to_path(from_jstring(env, firmware_path));
    JavaProgressReporter reporter(env, thiz);

    const auto version = install_pup(vita_root, firmware, [&](uint32_t progress) {
        reporter.report("firmware", static_cast<float>(progress), 0.f, 0.f);
    });

    return version.empty() ? nullptr : env->NewStringUTF(version.c_str());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_sbro_emucorev_core_VitaInstallBridge_nativeInstallContent(
    JNIEnv *env,
    jobject thiz,
    jstring vita_root_path,
    jstring cache_root_path,
    jstring content_path,
    jint system_language) {
    EmuEnvState emuenv;
    configure_install_env(
        emuenv,
        fs_utils::utf8_to_path(from_jstring(env, vita_root_path)),
        fs_utils::utf8_to_path(from_jstring(env, cache_root_path)),
        system_language);
    const auto path = fs_utils::utf8_to_path(from_jstring(env, content_path));
    const auto extension = string_utils::tolower(path.extension().string());
    JavaProgressReporter reporter(env, thiz);

    if ((extension == ".rif") || (extension == ".bin") || (path.filename() == "work.bin")) {
        return copy_license(emuenv, path) ? 1 : 0;
    }

    if (fs::is_directory(path)) {
        reporter.report("content", 0.f, 0.f, 0.f);
        return static_cast<jint>(install_contents(emuenv, nullptr, path));
    }

    if ((extension == ".vpk") || (extension == ".zip")) {
        const auto installed = install_archive(emuenv, nullptr, path, [&](ArchiveContents contents) {
            reporter.report(
                "content",
                contents.progress.value_or(0.f),
                contents.current.value_or(0.f),
                contents.count.value_or(0.f),
                contents.detail.value_or(""));
        });
        return static_cast<jint>(std::count_if(installed.begin(), installed.end(), [](const ContentInfo &item) {
            return item.state;
        }));
    }

    return 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_sbro_emucorev_core_VitaInstallBridge_nativeInstallPkg(
    JNIEnv *env,
    jobject thiz,
    jstring vita_root_path,
    jstring cache_root_path,
    jstring pkg_path,
    jstring zrif,
    jint system_language) {
    EmuEnvState emuenv;
    configure_install_env(
        emuenv,
        fs_utils::utf8_to_path(from_jstring(env, vita_root_path)),
        fs_utils::utf8_to_path(from_jstring(env, cache_root_path)),
        system_language);
    const auto path = fs_utils::utf8_to_path(from_jstring(env, pkg_path));
    auto zrif_value = from_jstring(env, zrif);
    JavaProgressReporter reporter(env, thiz);

    const auto success = install_pkg(path, emuenv, zrif_value, [&](float progress) {
        reporter.report("pkg", progress, 0.f, 0.f);
    });
    return static_cast<jboolean>(success);
}
