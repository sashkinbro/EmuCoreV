// EmuCoreV adapter layer (Layer 2).
//
// Provides the host::dialog::filesystem implementation that vanilla Vita3K
// expects on Android, and routes the file/folder dialog callback to our
// activity's JNI name (com.sbro.emucorev.core.vita.Emulator).
//
// Mirrors upstream `vita3k/android/jni/filesystem_android.cpp` so the upstream
// file can be excluded from the build without changing the vita3k tree.

#include <SDL3/SDL_system.h>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <jni.h>
#include <string>
#include <vector>

#include <util/fs.h>

namespace host::dialog::filesystem {

enum Result {
    ERROR,
    SUCCESS,
    CANCEL,
};

struct FileFilter {
    std::string display_name = "";
    std::vector<std::string> file_extensions = {};
};

Result open_file(fs::path &resulting_path, const std::vector<FileFilter> &file_filters = {}, const fs::path &default_path = "");
Result pick_folder(fs::path &resulting_path, const fs::path &default_path = "");
std::string get_error();

} // namespace host::dialog::filesystem

static std::atomic<bool> file_dialog_running = false;
static fs::path dialog_result_path{};

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_core_vita_Emulator_filedialogReturn(JNIEnv *env, jobject /*thiz*/, jstring result_path) {
    const char *result_ptr = env->GetStringUTFChars(result_path, nullptr);
    dialog_result_path = fs::path(result_ptr);
    env->ReleaseStringUTFChars(result_path, result_ptr);

    file_dialog_running.store(false, std::memory_order_release);
}

static void call_dialog_java_function(const char *name, bool need_write) {
    (void)need_write;

    JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = reinterpret_cast<jobject>(SDL_GetAndroidActivity());
    jclass clazz(env->GetObjectClass(activity));
    jmethodID method_id = env->GetMethodID(clazz, name, "()V");

    file_dialog_running = true;
    env->CallVoidMethod(activity, method_id);

    env->DeleteLocalRef(activity);
    env->DeleteLocalRef(clazz);

    while (file_dialog_running.load(std::memory_order_acquire))
        SDL_Delay(10);
}

namespace host::dialog::filesystem {

Result open_file(fs::path &resulting_path, const std::vector<FileFilter> & /*file_filters*/, const fs::path & /*default_path*/) {
    call_dialog_java_function("showFileDialog", false);

    if (dialog_result_path.empty())
        return Result::CANCEL;

    resulting_path = std::move(dialog_result_path);
    return Result::SUCCESS;
}

Result pick_folder(fs::path &resulting_path, const fs::path & /*default_path*/) {
    call_dialog_java_function("showFolderDialog", true);

    if (dialog_result_path.empty())
        return Result::CANCEL;

    resulting_path = std::move(dialog_result_path);
    return Result::SUCCESS;
}

std::string get_error() {
    return "";
}

} // namespace host::dialog::filesystem
