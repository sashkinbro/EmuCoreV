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
#include <SDL3/SDL_hints.h>
#include <audio/state.h>
#include <kernel/state.h>
#include <kernel/thread/thread_state.h>
#include <util/tracy.h>

#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

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

// True unless the platform reports the microphone permission as missing (no question asked here).
static bool record_permission_granted() {
#ifdef __ANDROID__
    bool granted = true;
    if (call_activity_method("hasRecordAudioPermission", &granted))
        return granted;
#endif
    return true;
}

// Opens the capture stream if the user setting, the host state and the platform allow it. On Android
// RECORD_AUDIO is checked first and, when missing, requested asynchronously (the Kotlin side asks once
// per session); the port then feeds silence until the grant re-triggers this via reopen_requested.
// Never blocks the guest thread on a prompt and never prompts while the microphone is disabled.
static void open_capture_stream(AudioInPort &in) {
    const auto &cfg = ecv_audio_in::settings();
    if (!cfg.enabled.load()) {
        LOG_INFO("AudioIn: capture disabled by setting, feeding silence");
        in.reopen_requested = true; // retried when the user enables the microphone
        return;
    }
    if (cfg.suspended.load()) {
        in.reopen_requested = true; // retried after the resume
        return;
    }
    // Chosen input device (AudioDeviceInfo id); read by the Android AAudio driver, 0 = system default.
    const int device_id = cfg.device_id.load();
#ifdef __ANDROID__
    if (!record_permission_granted()) {
        call_activity_method("requestRecordAudioPermission", nullptr);
        LOG_WARN("AudioIn: RECORD_AUDIO not granted yet, feeding silence until it is");
        return;
    }
    if (device_id > 0) {
        call_activity_method("prepareMicrophoneRouting", nullptr); // Bluetooth headset mics need their SCO route
        SDL_SetHint("ECV_AAUDIO_INPUT_DEVICE_ID", std::to_string(device_id).c_str());
    } else
        SDL_ResetHint("ECV_AAUDIO_INPUT_DEVICE_ID");
#endif
    // Capture at the highest rate an open port needs; the ports convert down from it.
    int capture_freq = ecv_audio_in::kCaptureFreq;
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        int freqs[ecv_audio_in::kPortSlots];
        for (int i = 0; i < ecv_audio_in::kPortSlots; ++i)
            freqs[i] = in.slot[i].freq;
        capture_freq = ecv_audio_in::required_capture_freq(in.ports.open, freqs);
    }
    SDL_AudioSpec desired = {
        .format = SDL_AUDIO_S16LE,
        .channels = 1,
        .freq = capture_freq
    };
    auto *stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired, nullptr, nullptr);
#ifdef __ANDROID__
    if (!stream && device_id > 0) {
        // The chosen device vanished (unplugged USB / Bluetooth): fall back to the system default.
        LOG_WARN("AudioIn: input device {} unavailable ({}), using the default", device_id, SDL_GetError());
        SDL_ResetHint("ECV_AAUDIO_INPUT_DEVICE_ID");
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &desired, nullptr, nullptr);
    }
#endif
    if (!stream) {
        LOG_WARN("AudioIn: capture device unavailable ({}), feeding silence", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(stream);
    const std::lock_guard<std::mutex> lock(in.capture_mutex);
    in.id = stream;
    in.capture_freq = capture_freq;
    in.stalled_grains = 0;
    // The ports' feeds convert from the rate the capture really runs at.
    const SDL_AudioSpec src = { SDL_AUDIO_S16LE, 1, capture_freq };
    for (auto &slot : in.slot)
        if (slot.feed)
            SDL_SetAudioStreamFormat(static_cast<SDL_AudioStream *>(slot.feed), &src, nullptr);
}

static int64_t steady_now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Blocks the calling guest thread until the steady-clock instant deadline_us (no-op when already past).
static void wait_until_us(KernelState &kernel, SceUID thread_id, int64_t deadline_us) {
    if (deadline_us <= steady_now_us())
        return;
    if (const auto thread = kernel.get_thread(thread_id))
        (void)thread->delay_until(Deadline(std::chrono::microseconds(deadline_us)), false);
}

EXPORT(int, sceAudioInGetAdopt, SceAudioInPortType portType) {
    TRACY_FUNC(sceAudioInGetAdopt, portType);
    if (portType != SCE_AUDIO_IN_PORT_TYPE_VOICE && portType != SCE_AUDIO_IN_PORT_TYPE_RAW) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_TYPE);
    }
    return 1;
}

// The SDK signatures of GetInput / SelectInput / Get/SetMicGain are not recovered from firmware:
// provisional, they store and report back the guest's own selection (see ecv_audio_in::valid_guest_*).
EXPORT(int, sceAudioInGetInput) {
    TRACY_FUNC(sceAudioInGetInput);
    return emuenv.audio.in_port.guest_input.load();
}

EXPORT(int, sceAudioInGetMicGain) {
    TRACY_FUNC(sceAudioInGetMicGain);
    return emuenv.audio.in_port.guest_mic_gain.load();
}

EXPORT(int, sceAudioInGetStatus, int select) {
    TRACY_FUNC(sceAudioInGetStatus, select);
    if (select != SCE_AUDIO_IN_GETSTATUS_MUTE) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PARAMETER);
    }
    // Never muted, like the built-in microphone of the hardware (see guest_mute_status).
    return ecv_audio_in::guest_mute_status();
}

EXPORT(int, sceAudioInInput, int port, void *destPtr) {
    TRACY_FUNC(sceAudioInInput, port, destPtr);
    auto &in = emuenv.audio.in_port;
    bool any_open = false;
    bool port_open = false;
    int len = 0;
    int freq = 0;
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        any_open = in.ports.any();
        port_open = in.ports.is_open(port);
        if (port_open) {
            len = in.slot[port].len_bytes;
            freq = in.slot[port].freq > 0 ? in.slot[port].freq : 16000;
        }
    }
    // Error paths are paced as well (see failed_input_wait_us); the error codes are unchanged.
    const auto fail = [&](int error) {
        wait_until_us(emuenv.kernel, thread_id, steady_now_us() + ecv_audio_in::failed_input_wait_us(port_open, len, freq));
        return error;
    };
    if (!any_open)
        return fail(RET_ERROR(SCE_AUDIO_IN_ERROR_NOT_OPENED));
    if (port < 0 || port >= ecv_audio_in::kPortSlots)
        return fail(RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_PARAM));
    if (!port_open)
        return fail(RET_ERROR(SCE_AUDIO_IN_ERROR_NOT_OPENED));
    if (!destPtr)
        return fail(RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_POINTER));

    // Vita semantics: the call blocks until one grain is available, paced in real time
    // (grain / sample rate). If the capture device delivers nothing in that period (no
    // permission, no microphone, stalled device) the rest of the grain is zero-filled.
    // The wait is bounded by one grain period and is a guest thread wait (delay), not a host sleep, so the
    // scheduler is released like in sceKernelDelayThread.
    const int64_t period_us = ecv_audio_in::grain_period_us(len, freq);
    int64_t deadline;
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        deadline = ecv_audio_in::next_grain_deadline(in.slot[port].next_deadline_us, steady_now_us(), period_us);
    }
    wait_until_us(emuenv.kernel, thread_id, deadline);

    const auto &cfg = ecv_audio_in::settings();
    const bool want = ecv_audio_in::capture_wanted(cfg.enabled.load(), cfg.suspended.load());

    // RECORD_AUDIO was granted after the port opened, or the capture was released (resume after a pause,
    // setting turned on, input device changed, stalled stream): start capturing without a restart.
    if (want && !in.id && in.reopen_requested.exchange(false))
        open_capture_stream(in);

    int filled = 0;
    bool stalled = false;
    if (want) {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        if (auto *stream = static_cast<SDL_AudioStream *>(in.id)) {
            // Fan-out: whatever the device delivered goes to every open port (each converts to its rate), so
            // two ports reading in turn both hear the microphone.
            int16_t chunk[2048];
            bool pumped = false;
            for (;;) {
                const int got = SDL_GetAudioStreamData(stream, chunk, sizeof(chunk));
                if (got < 0) {
                    LOG_ERROR("AudioIn: Error while reading audio stream: {}", SDL_GetError());
                    break;
                }
                if (got == 0)
                    break;
                pumped = true;
                for (int i = 0; i < ecv_audio_in::kPortSlots; ++i) {
                    auto *feed = static_cast<SDL_AudioStream *>(in.slot[i].feed);
                    if (!feed || !in.ports.open[i])
                        continue;
                    if (SDL_GetAudioStreamAvailable(feed) > (1 << 17)) // port nobody reads: bound its backlog
                        SDL_ClearAudioStream(feed);
                    SDL_PutAudioStreamData(feed, chunk, got);
                }
            }
            if (auto *feed = static_cast<SDL_AudioStream *>(in.slot[port].feed)) {
                // Keep latency bounded if the device ran ahead of the guest.
                if (SDL_GetAudioStreamAvailable(feed) > len * 4)
                    SDL_ClearAudioStream(feed);
                const int got = SDL_GetAudioStreamData(feed, destPtr, len);
                filled = got > 0 ? got : 0;
            }
            // A stream that delivers nothing for a long time is dead (e.g. the OS silenced a background
            // app's capture): drop it and open a new one. Every open port counts its grains.
            in.stalled_grains = pumped ? 0 : in.stalled_grains + 1;
            stalled = ecv_audio_in::stream_stalled(in.stalled_grains / std::max(1, in.ports.count()));
        }
    }
    if (filled < len)
        std::memset(static_cast<char *>(destPtr) + filled, 0, len - filled);
    if (filled > 0)
        ecv_audio_in::apply_gain(static_cast<int16_t *>(destPtr), static_cast<size_t>(filled) / 2, cfg.gain_percent.load());
    if (stalled) {
        LOG_WARN("AudioIn: capture stalled, re-opening");
        audio_in_release_capture(in);
        in.reopen_requested = true;
    }
    return 0;
}

// Provisional signature: the optional device-state out parameter is not read (its layout is unproven).
EXPORT(int, sceAudioInInputWithInputDeviceState, int port, void *destPtr) {
    TRACY_FUNC(sceAudioInInputWithInputDeviceState, port, destPtr);
    return CALL_EXPORT(sceAudioInInput, port, destPtr);
}

EXPORT(int, sceAudioInOpenPort, SceAudioInPortType portType, int grain, int freq, SceAudioInParam param) {
    TRACY_FUNC(sceAudioInOpenPort, portType, grain, freq, param);
    LOG_WARN("sceAudioInOpenPort: thread {} type {} grain {} freq {} param {}", thread_id, static_cast<int>(portType), grain, freq,
        static_cast<int>(param));
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

    auto &in = emuenv.audio.in_port;
    // Feed converting the shared capture to this port's rate (null = this port hears silence).
    const SDL_AudioSpec src = { SDL_AUDIO_S16LE, 1, ecv_audio_in::kCaptureFreq };
    const SDL_AudioSpec dst = { SDL_AUDIO_S16LE, 1, freq };
    auto *feed = SDL_CreateAudioStream(&src, &dst);
    if (!feed)
        LOG_WARN("AudioIn: cannot create the port feed ({}), the port delivers silence", SDL_GetError());

    int id;
    bool first;
    bool raise = false; // an already running capture is too slow for this port
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        id = in.ports.try_open(static_cast<int>(portType));
        first = id >= 0 && in.ports.count() == 1;
        if (id >= 0) {
            in.slot[id].len_bytes = grain * 2;
            in.slot[id].freq = freq;
            in.slot[id].next_deadline_us = 0;
            in.slot[id].feed = feed;
            raise = in.id && freq > in.capture_freq;
        }
    }
    // Diagnostic for B146 (device logs are warning level and above): which guest thread opens which port.
    LOG_WARN("sceAudioInOpenPort: thread {} -> port {} ({} open)", thread_id, id, in.ports.count());
    if (id < 0) {
        if (feed)
            SDL_DestroyAudioStream(feed);
        if (id == ecv_audio_in::SlotTable::kErrFull)
            return RET_ERROR(SCE_AUDIO_IN_ERROR_PORT_FULL);
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_TYPE);
    }

    // Capture may be unavailable (permission pending/denied, no microphone): the port stays open and
    // sceAudioInInput delivers silence so the game keeps running. The first port starts the shared capture.
    if (first || !in.id) {
        in.reopen_requested = false;
        in.stalled_grains = 0;
        if (!in.id)
            open_capture_stream(in);
    } else if (raise) {
        // A higher-rate port (RAW 48 kHz) opened after a slower one: restart the capture at the new rate
        // (rare, a short gap of silence).
        LOG_WARN("AudioIn: raising the capture rate to {} Hz", freq);
        audio_in_release_capture(in);
        open_capture_stream(in);
    }
    return id;
}

EXPORT(int, sceAudioInOpenPortForDiag) {
    TRACY_FUNC(sceAudioInOpenPortForDiag);
    return UNIMPLEMENTED();
}

EXPORT(int, sceAudioInReleasePort, int port) {
    TRACY_FUNC(sceAudioInReleasePort, port);
    auto &in = emuenv.audio.in_port;
    void *feed = nullptr;
    bool last = false;
    {
        const std::lock_guard<std::mutex> lock(in.capture_mutex);
        if (port < 0 || port >= ecv_audio_in::kPortSlots)
            return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PORT_PARAM);
        if (!in.ports.release(port))
            return RET_ERROR(SCE_AUDIO_IN_ERROR_NOT_OPENED);
        feed = in.slot[port].feed;
        in.slot[port] = AudioInSlot{};
        last = !in.ports.any();
    }
    if (feed)
        SDL_DestroyAudioStream(static_cast<SDL_AudioStream *>(feed));
    if (last) {
        // The shared capture and the routing stay up while any other port is open.
        audio_in_release_capture(in);
#ifdef __ANDROID__
        call_activity_method("releaseMicrophoneRouting", nullptr);
#endif
    }
    return 0;
}

EXPORT(int, sceAudioInSelectInput, int input) {
    TRACY_FUNC(sceAudioInSelectInput, input);
    if (!ecv_audio_in::valid_guest_input(input)) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PARAMETER);
    }
    emuenv.audio.in_port.guest_input = input;
    return 0;
}

EXPORT(int, sceAudioInSetMicGain, int gain) {
    TRACY_FUNC(sceAudioInSetMicGain, gain);
    if (!ecv_audio_in::valid_guest_gain(gain)) {
        return RET_ERROR(SCE_AUDIO_IN_ERROR_INVALID_PARAMETER);
    }
    emuenv.audio.in_port.guest_mic_gain = gain;
    return 0;
}

EXPORT(int, sceAudioInSetMute) {
    TRACY_FUNC(sceAudioInSetMute);
    return UNIMPLEMENTED();
}
