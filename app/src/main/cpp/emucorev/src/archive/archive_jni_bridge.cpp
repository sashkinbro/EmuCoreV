#include "emucorev/archive/archive_expander.hpp"

#include <jni.h>

#include <filesystem>
#include <limits>
#include <string>

namespace {

using emucorev::archive_expansion::Failure;
using emucorev::archive_expansion::Limits;
using emucorev::archive_expansion::Progress;

constexpr jint kOk = 0;
constexpr jint kUnsupportedFormat = 1;
constexpr jint kMalformedArchive = 2;
constexpr jint kUnsafePath = 3;
constexpr jint kDuplicateEntry = 4;
constexpr jint kLinkOrSpecialEntry = 5;
constexpr jint kEntryLimit = 6;
constexpr jint kExpandedBytesLimit = 7;
constexpr jint kDecoderMemoryLimit = 8;
constexpr jint kInsufficientSpace = 9;
constexpr jint kCancelled = 10;
constexpr jint kIoError = 11;
constexpr jint kEncryptedArchive = 12;

class UtfChars {
public:
    UtfChars(JNIEnv* env, jstring value) : env_(env), value_(value) {
        if (value_ != nullptr) chars_ = env_->GetStringUTFChars(value_, nullptr);
    }
    ~UtfChars() {
        if (chars_ != nullptr) env_->ReleaseStringUTFChars(value_, chars_);
    }
    const char* get() const { return chars_; }

private:
    JNIEnv* env_;
    jstring value_;
    const char* chars_ = nullptr;
};

jint status_for(Failure failure) {
    switch (failure) {
        case Failure::none: return kOk;
        case Failure::unsupported_format: return kUnsupportedFormat;
        case Failure::malformed_archive: return kMalformedArchive;
        case Failure::encrypted_archive: return kEncryptedArchive;
        case Failure::unsafe_path: return kUnsafePath;
        case Failure::duplicate_entry: return kDuplicateEntry;
        case Failure::link_or_special_entry: return kLinkOrSpecialEntry;
        case Failure::entry_count_limit: return kEntryLimit;
        case Failure::expanded_bytes_limit: return kExpandedBytesLimit;
        case Failure::decoder_memory_limit: return kDecoderMemoryLimit;
        case Failure::insufficient_space: return kInsufficientSpace;
        case Failure::cancelled: return kCancelled;
        case Failure::io_error: return kIoError;
    }
    return kIoError;
}

bool is_cancelled(JNIEnv* env, jobject cancellation, jmethodID method) {
    if (env->ExceptionCheck()) return true;
    const jboolean value = env->CallBooleanMethod(cancellation, method);
    return env->ExceptionCheck() || value == JNI_TRUE;
}

void report_progress(JNIEnv* env,
                     jobject progress_listener,
                     jmethodID method,
                     const Progress& progress) {
    jstring path = env->NewStringUTF(progress.entry_path.c_str());
    if (path == nullptr) return;
    env->CallVoidMethod(progress_listener,
                        method,
                        path,
                        static_cast<jlong>(progress.entry_bytes),
                        static_cast<jlong>(progress.total_bytes));
    env->DeleteLocalRef(path);
}

} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_com_sbro_emucorev_core_NativeArchiveExpander_nativeExtract(
    JNIEnv* env,
    jclass,
    jstring input_path,
    jstring staging_root,
    jint max_entries,
    jlong max_expanded_bytes,
    jlong free_space_reserve_bytes,
    jlong max_decoder_memory_bytes,
    jobject cancellation,
    jobject progress_listener) {
    if (input_path == nullptr || staging_root == nullptr || cancellation == nullptr ||
        progress_listener == nullptr || max_entries <= 0 || max_expanded_bytes <= 0 ||
        free_space_reserve_bytes < 0 || max_decoder_memory_bytes <= 0) {
        return kIoError;
    }
    const jclass cancellation_class = env->GetObjectClass(cancellation);
    const jclass progress_class = env->GetObjectClass(progress_listener);
    if (cancellation_class == nullptr || progress_class == nullptr) return kIoError;
    const jmethodID is_cancelled_method = env->GetMethodID(cancellation_class, "isCancelled", "()Z");
    const jmethodID progress_method = env->GetMethodID(progress_class, "onProgress", "(Ljava/lang/String;JJ)V");
    if (is_cancelled_method == nullptr || progress_method == nullptr) return kIoError;

    UtfChars input_chars(env, input_path);
    UtfChars root_chars(env, staging_root);
    if (input_chars.get() == nullptr || root_chars.get() == nullptr) return kIoError;

    Limits limits;
    limits.max_entries = static_cast<std::uint32_t>(max_entries);
    limits.max_expanded_bytes = static_cast<std::uint64_t>(max_expanded_bytes);
    limits.free_space_reserve_bytes = static_cast<std::uint64_t>(free_space_reserve_bytes);
    limits.max_decoder_memory_bytes = static_cast<std::uint64_t>(max_decoder_memory_bytes);
    auto result = emucorev::archive_expansion::extract_archive(
        std::filesystem::path(input_chars.get()),
        std::filesystem::path(root_chars.get()),
        limits,
        [env, cancellation, is_cancelled_method] {
            return is_cancelled(env, cancellation, is_cancelled_method);
        },
        [env, progress_listener, progress_method](const Progress& value) {
            report_progress(env, progress_listener, progress_method, value);
        });
    if (env->ExceptionCheck()) return kCancelled;
    return status_for(result.failure);
}
