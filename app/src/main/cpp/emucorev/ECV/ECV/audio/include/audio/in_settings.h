// EmuCoreV microphone (sceAudioIn) host settings and pure decision helpers.
// Header-only and free of SDL/emulator types so it is covered by the host tests
// (app/src/test/core-host/camera_mic_tests.cpp).
#ifndef ECV_AUDIO_IN_SETTINGS_H
#define ECV_AUDIO_IN_SETTINGS_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ecv_audio_in {

constexpr int kGainMinPercent = 0;
constexpr int kGainMaxPercent = 300;
constexpr int kGainDefaultPercent = 100;

// Process-wide, pushed from the Kotlin settings (global value or the per-game override) through JNI.
// Independent of the emulator environment so a value set before the session exists is not lost.
struct Settings {
    std::atomic<bool> enabled{ true }; // user setting; off = the guest always hears silence, no prompt
    std::atomic<int> gain_percent{ kGainDefaultPercent }; // host-side input gain
    std::atomic<bool> suspended{ false }; // menu pause / background: capture must be released
    std::atomic<int> device_id{ 0 }; // Android AudioDeviceInfo id; 0 = system default
};

inline Settings &settings() {
    static Settings instance;
    return instance;
}

inline int clamp_gain(int percent) { return std::clamp(percent, kGainMinPercent, kGainMaxPercent); }

// Scales signed 16-bit mono samples in place by percent (100 = unchanged), saturating.
inline void apply_gain(int16_t *samples, size_t count, int percent) {
    percent = clamp_gain(percent);
    if (!samples || percent == kGainDefaultPercent)
        return;
    for (size_t i = 0; i < count; ++i) {
        const int32_t v = static_cast<int32_t>(samples[i]) * percent / 100;
        samples[i] = static_cast<int16_t>(std::clamp<int32_t>(v, INT16_MIN, INT16_MAX));
    }
}

// sceAudioInGetStatus(SCE_AUDIO_IN_GETSTATUS_MUTE): 1 when the guest cannot hear the microphone.
inline int mute_status(bool enabled, bool permission_granted) { return enabled && permission_granted ? 0 : 1; }

inline bool capture_wanted(bool enabled, bool suspended) { return enabled && !suspended; }

// Grains (256-768 samples, 16-48 ms) without any captured data after which an open stream counts as dead.
constexpr int kStallGrains = 60;
inline bool stream_stalled(int empty_grains) { return empty_grains >= kStallGrains; }

// Guest-visible selections. The SDK ranges are not recovered from firmware: provisional, any
// non-negative gain is accepted and reported back, input 0/1 (built-in / external) are selectable.
inline bool valid_guest_gain(int gain) { return gain >= 0; }
inline bool valid_guest_input(int input) { return input == 0 || input == 1; }

} // namespace ecv_audio_in

#endif
