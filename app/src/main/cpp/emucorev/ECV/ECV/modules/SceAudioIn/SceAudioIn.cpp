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

#include <module/module.h>

#include <SDL3/SDL_audio.h>
#include <audio/state.h>
#include <util/tracy.h>

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#define PORT_ID 0

TRACY_MODULE_NAME(SceAudioIn);

enum SceAudioInPortType {
    SCE_AUDIO_IN_PORT_TYPE_VOICE = 0,
    SCE_AUDIO_IN_PORT_TYPE_RAW = 2
};

enum SceAudioInParam {
    SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO = 0,
    SCE_AUDIO_IN_GETSTATUS_MUTE = 1
};

template <>
std::string to_debug_str<SceAudioInPortType>(const MemState &mem, SceAudioInPortType type) {
    switch (type) {
    case SceAudioInPortType::SCE_AUDIO_IN_PORT_TYPE_VOICE: return "SCE_AUDIO_IN_PORT_TYPE_VOICE"; break;
    case SceAudioInPortType::SCE_AUDIO_IN_PORT_TYPE_RAW: return "SCE_AUDIO_IN_PORT_TYPE_RAW"; break;
    default: return to_debug_str(mem, static_cast<std::underlying_type_t<SceAudioInPortType>>(type));
    }
}

template <>
std::string to_debug_str<SceAudioInParam>(const MemState &mem, SceAudioInParam type) {
    switch (type) {
    case SceAudioInParam::SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO: return "SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO"; break;
    case SceAudioInParam::SCE_AUDIO_IN_GETSTATUS_MUTE: return "SCE_AUDIO_IN_GETSTATUS_MUTE"; break;
    default: return to_debug_str(mem, static_cast<std::underlying_type_t<SceAudioInParam>>(type));
    }
}

enum SceAudioInErrorCode : uint32_t {
    //! Undefined error
    SCE_AUDIO_IN_ERROR_FATAL = 0x80260100,
    //! Bad value of port number
    SCE_AUDIO_IN_ERROR_INVALID_PORT = 0x80260101,
    //! Invalid sample length
    SCE_AUDIO_IN_ERROR_INVALID_SIZE = 0x80260102,
    //! Invalid sample frequency
    SCE_AUDIO_IN_ERROR_INVALID_SAMPLE_FREQ = 0x80260103,
    //! Invalid port type
    SCE_AUDIO_IN_ERROR_INVALID_PORT_TYPE = 0x80260104,
    //! Invalid pointer value
    SCE_AUDIO_IN_ERROR_INVALID_POINTER = 0x80260105,
    //! Invalid port param
    SCE_AUDIO_IN_ERROR_INVALID_PORT_PARAM = 0x80260106,
    //! Cannot open no ports
    SCE_AUDIO_IN_ERROR_PORT_FULL = 0x80260107,
    //! Not enough memory
    SCE_AUDIO_IN_ERROR_OUT_OF_MEMORY = 0x80260108,
    //! Port is not opened
    SCE_AUDIO_IN_ERROR_NOT_OPENED = 0x80260109,
    //! Tried to input while busy
    SCE_AUDIO_IN_ERROR_BUSY = 0x8026010A,
    //! Invalid parameter
    SCE_AUDIO_IN_ERROR_INVALID_PARAMETER = 0x8026010B
};

#ifdef __ANDROID__
// Calls a no-argument Emulator activity method (UI-thread work is posted by the Kotlin side, so this
// never blocks). Returns false when the method is missing or threw; `result` gets a boolean result.
static bool call_activity_method(const char *name, bool *result) {
    auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
    auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!env || !activity)
        return false;
    bool ok = false;
    if (jclass clazz = env->GetObjectClass(activity)) {
        if (jmethodID method = env->GetMethodID(clazz, name, result ? "()Z" : "()V")) {
            if (result)
                *result = env->CallBooleanMethod(activity, method) == JNI_TRUE;
            else
                env->CallVoidMethod(activity, method);
            ok = true;
        }
        env->DeleteLocalRef(clazz);
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        ok = false;
    }
    env->DeleteLocalRef(activity);
    return ok;
}
#endif

// Opens the capture stream if the platform allows it. On Android RECORD_AUDIO is checked first and,
// when missing, requested asynchronously (the Kotlin side asks once per session); the port then feeds
// silence until the grant re-triggers this via reopen_requested. Never blocks the guest thread on a prompt.
static void open_capture_stream(AudioInPort &in, int freq) {
#ifdef __ANDROID__
    bool granted = true;
    if (call_activity_method("hasRecordAudioPermission", &granted) && !granted) {
        call_activity_method("requestRecordAudioPermission", nullptr);
        LOG_WARN("AudioIn: RECORD_AUDIO not granted yet, feeding silence until it is");
        return;
    }
#endif
    SDL_AudioSpec desired = {
        .format = SDL_AUDIO_S16LE,
        .channels = 1,
        .freq = freq
    };
    in.id = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired, nullptr, nullptr);
    if (in.id) {
        SDL_ResumeAudioStreamDevice(static_cast<SDL_AudioStream *>(in.id));
    } else {
        LOG_WARN("AudioIn: capture device unavailable ({}), feeding silence", SDL_GetError());
    }
}

EXPORT(int, sceAudioInGetAdopt, SceAudioInPortType portType) {
    TRACY_FUNC(sceAudioInGetAdopt, portType);
    if (portType != SCE_AUDIO_IN_PORT_TYPE_VOICE && portType != SCE_AUDIO_IN_PORT_TYPE_RAW) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_TYPE);
    }
    return 1;
}

EXPORT(int, sceAudioInGetInput) {
    TRACY_FUNC(sceAudioInGetInput);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInGetMicGain) {
    TRACY_FUNC(sceAudioInGetMicGain);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInGetStatus, int select) {
    TRACY_FUNC(sceAudioInGetStatus, select);
    if (select != SCE_AUDIO_IN_GETSTATUS_MUTE) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PARAMETER);
    }
    return emuenv.audio.in_port.running ? 0 : 1;
}

EXPORT(int, sceAudioInInput, int port, void *destPtr) {
    TRACY_FUNC(sceAudioInInput, port, destPtr);
    if (!emuenv.audio.in_port.running) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_NOT_OPENED);
    }
    if (port != PORT_ID) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_PARAM);
    }

    if (!destPtr) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_POINTER);
    }

    // Vita semantics: the call blocks until one grain is available, paced in real time
    // (grain / sample rate). If the capture device delivers nothing in that period (no
    // permission, no microphone, stalled device) the rest of the grain is zero-filled.
    // The wait is bounded by one grain period; there is no unbounded polling loop.
    auto &in = emuenv.audio.in_port;
    const int len = in.len_bytes;
    const int freq = in.freq > 0 ? in.freq : 16000;
    const int64_t period_us = static_cast<int64_t>(len / 2) * 1000000 / freq;
    const auto now_us = [] {
        return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    };
    int64_t deadline = in.next_deadline_us;
    const int64_t start = now_us();
    // First call, or the guest was away for a long time: restart the cadence from now.
    if (deadline == 0 || start - deadline > period_us * 4)
        deadline = start + period_us;
    in.next_deadline_us = deadline + period_us;
    for (int64_t remaining = deadline - now_us(); remaining > 0; remaining = deadline - now_us())
        std::this_thread::sleep_for(std::chrono::microseconds(std::min<int64_t>(remaining, 2000)));

    // RECORD_AUDIO was granted after the port opened: start capturing without a restart.
    if (!in.id && in.reopen_requested.exchange(false))
        open_capture_stream(in, freq);

    int filled = 0;
    if (auto *stream = static_cast<SDL_AudioStream *>(in.id)) {
        // Keep latency bounded if the device ran ahead of the guest.
        if (SDL_GetAudioStreamAvailable(stream) > len * 4)
            SDL_ClearAudioStream(stream);
        const int got = SDL_GetAudioStreamData(stream, destPtr, len);
        if (got < 0) {
            LOG_ERROR("AudioIn: Error while reading audio stream: {}", SDL_GetError());
        } else {
            filled = got;
        }
    }
    if (filled < len)
        std::memset(static_cast<char *>(destPtr) + filled, 0, len - filled);
    return 0;
}

EXPORT(int, sceAudioInInputWithInputDeviceState) {
    TRACY_FUNC(sceAudioInInputWithInputDeviceState);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInOpenPort, SceAudioInPortType portType, int grain, int freq, SceAudioInParam param) {
    TRACY_FUNC(sceAudioInOpenPort, portType, grain, freq, param);
    if (emuenv.audio.in_port.running) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_PORT_FULL);
    }
    if (param != SCE_AUDIO_IN_PARAM_FORMAT_S16_MONO) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_PARAM);
    }
    if (portType != SCE_AUDIO_IN_PORT_TYPE_VOICE && portType != SCE_AUDIO_IN_PORT_TYPE_RAW) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_TYPE);
    }
    if (portType == SCE_AUDIO_IN_PORT_TYPE_VOICE) {
        if (freq != 16000) {
            return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_SAMPLE_FREQ);
        }
        if (grain != 256 && grain != 512) {
            return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PARAMETER);
        }
    }
    if (portType == SCE_AUDIO_IN_PORT_TYPE_RAW) {
        if (freq != 16000 && freq != 48000) {
            return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_SAMPLE_FREQ);
        }
        if ((grain != 256 && freq == 16000) || (grain != 768 && freq == 48000)) {
            return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PARAMETER);
        }
    }

    // Capture may be unavailable (permission pending/denied, no microphone): the port stays open and
    // sceAudioInInput delivers silence so the game keeps running.
    emuenv.audio.in_port.reopen_requested = false;
    open_capture_stream(emuenv.audio.in_port, freq);

    emuenv.audio.in_port.len_bytes = grain * 2;
    emuenv.audio.in_port.freq = freq;
    emuenv.audio.in_port.next_deadline_us = 0;
    emuenv.audio.in_port.running = true;
    return PORT_ID;
}

EXPORT(int, sceAudioInOpenPortForDiag) {
    TRACY_FUNC(sceAudioInOpenPortForDiag);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInReleasePort, int port) {
    TRACY_FUNC(sceAudioInReleasePort, port);
    if (port != PORT_ID) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_PARAM);
    }
    if (!emuenv.audio.in_port.running) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_NOT_OPENED);
    }
    emuenv.audio.in_port.running = false;
    if (emuenv.audio.in_port.id)
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream *>(emuenv.audio.in_port.id));
    emuenv.audio.in_port.id = nullptr;
    return 0;
}

EXPORT(int, sceAudioInSelectInput) {
    TRACY_FUNC(sceAudioInSelectInput);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInSetMicGain) {
    TRACY_FUNC(sceAudioInSetMicGain);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInSetMute) {
    TRACY_FUNC(sceAudioInSetMute);
    return UNIMPLEMENTED();
}
