// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "android_state.h"

#include <dialog/state.h>
#include <ime/keyboard.h>
#include <ime/state.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_system.h>
#include <jni.h>

namespace ime {

static SDL_Window *s_window = nullptr;

namespace {

// These callbacks are an optional Android UI extension. A Java/native version
// mismatch must not leave NoSuchMethodError pending on the JNI thread because
// every subsequent JNI operation is invalid while that exception is pending.
jmethodID get_optional_method(JNIEnv *env, jclass clazz, const char *name, const char *signature) {
    const jmethodID method_id = env->GetMethodID(clazz, name, signature);
    if (!method_id && env->ExceptionCheck())
        env->ExceptionClear();
    return method_id;
}

void clear_callback_exception(JNIEnv *env) {
    if (env->ExceptionCheck()) {
        env->ExceptionDescribe();
        env->ExceptionClear();
    }
}

void clear_ime_state(JNIEnv *env, jobject activity, jclass clazz) {
    const jmethodID method_id = get_optional_method(env, clazz, "clearNativeImeState", "()V");
    if (method_id) {
        env->CallVoidMethod(activity, method_id);
        clear_callback_exception(env);
    }
}

void push_ime_state(JNIEnv *env, jobject activity, jclass clazz, EmuEnvState &emuenv) {
    const bool dialog_active = is_ime_dialog_active(emuenv);
    const bool sce_ime_active = emuenv.ime.state;

    if (!sce_ime_active && !dialog_active) {
        clear_ime_state(env, activity, clazz);
        return;
    }

    std::u16string text;
    uint32_t preedit_start = 0;
    uint32_t preedit_length = 0;
    uint32_t caret_index = 0;
    bool multiline = false;
    std::string enter_label;
    {
        std::lock_guard<std::recursive_mutex> dialog_lock(emuenv.common_dialog.mutex);
        std::lock_guard<std::mutex> ime_lock(emuenv.ime.mutex);
        text = emuenv.ime.str;
        preedit_start = emuenv.ime.edit_text.preeditIndex;
        preedit_length = emuenv.ime.edit_text.preeditLength;
        caret_index = emuenv.ime.edit_text.caretIndex;
        multiline = dialog_active
            ? emuenv.common_dialog.ime.multiline
            : ((emuenv.ime.param.option & SCE_IME_OPTION_MULTILINE) != 0);
        enter_label = emuenv.ime.enter_label;
    }

    const jmethodID method_id = get_optional_method(
        env,
        clazz,
        "updateNativeImeState",
        "(ZZLjava/lang/String;IIIZLjava/lang/String;)V");
    if (!method_id)
        return;

    jstring text_value = env->NewString(
        reinterpret_cast<const jchar *>(text.data()),
        static_cast<jsize>(text.size()));
    jstring enter_label_value = env->NewStringUTF(enter_label.c_str());
    if (!text_value || !enter_label_value) {
        if (text_value)
            env->DeleteLocalRef(text_value);
        if (enter_label_value)
            env->DeleteLocalRef(enter_label_value);
        clear_callback_exception(env);
        return;
    }

    env->CallVoidMethod(activity,
        method_id,
        static_cast<jboolean>(sce_ime_active),
        static_cast<jboolean>(dialog_active),
        text_value,
        static_cast<jint>(preedit_start),
        static_cast<jint>(preedit_length),
        static_cast<jint>(caret_index),
        static_cast<jboolean>(multiline),
        enter_label_value);
    clear_callback_exception(env);
    env->DeleteLocalRef(text_value);
    env->DeleteLocalRef(enter_label_value);
}

} // namespace

void set_sdl_window(SDL_Window *window) {
    s_window = window;
}

void set_keyboard_active(bool active) {
    if (s_window) {
        if (active)
            SDL_StartTextInput(s_window);
        else
            SDL_StopTextInput(s_window);
    }

    JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = reinterpret_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity)
        return;

    jclass clazz = env->GetObjectClass(activity);
    if (!clazz) {
        if (env->ExceptionCheck())
            env->ExceptionClear();
        env->DeleteLocalRef(activity);
        return;
    }

    jmethodID method_id = get_optional_method(env, clazz, "setKeyboardActive", "(Z)V");
    if (method_id) {
        env->CallVoidMethod(activity, method_id, static_cast<jboolean>(active));
        clear_callback_exception(env);
    }

    auto *emuenv = get_emuenv();
    if (emuenv)
        push_ime_state(env, activity, clazz, *emuenv);
    else
        clear_ime_state(env, activity, clazz);

    env->DeleteLocalRef(clazz);
    env->DeleteLocalRef(activity);
}

void notify_ime_state_changed() {
    JNIEnv *env = reinterpret_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    jobject activity = reinterpret_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity)
        return;

    jclass clazz = env->GetObjectClass(activity);
    auto *emuenv = get_emuenv();
    if (clazz) {
        if (emuenv)
            push_ime_state(env, activity, clazz, *emuenv);
        else
            clear_ime_state(env, activity, clazz);
        env->DeleteLocalRef(clazz);
    }
    env->DeleteLocalRef(activity);
}

} // namespace ime
