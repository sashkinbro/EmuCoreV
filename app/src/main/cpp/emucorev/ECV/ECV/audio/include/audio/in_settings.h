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

// sceAudioInGetStatus(SCE_AUDIO_IN_GETSTATUS_MUTE): the built-in microphone of the Vita is never muted, so
// the answer is always 0 (not muted), also while our setting is off, RECORD_AUDIO is missing or no port is
// open. Those cases are delivered as silence by sceAudioInInput. Reporting 1 made guests poll the mute state
// in a tight loop and never reach the blocking sceAudioInInput (B146).
constexpr int guest_mute_status() { return 0; }

inline bool capture_wanted(bool enabled, bool suspended) { return enabled && !suspended; }

// Grains (256-768 samples, 16-48 ms) without any captured data after which an open stream counts as dead.
constexpr int kStallGrains = 60;
inline bool stream_stalled(int empty_grains) { return empty_grains >= kStallGrains; }

// Guest-visible selections. The SDK ranges are not recovered from firmware: provisional, any
// non-negative gain is accepted and reported back, input 0/1 (built-in / external) are selectable.
inline bool valid_guest_gain(int gain) { return gain >= 0; }
inline bool valid_guest_input(int input) { return input == 0 || input == 1; }

// Real-time pacing of the guest-visible calls. On the Vita sceAudioInInput blocks for one grain, so a
// guest mic thread costs ~0% CPU. The same must hold on the host whatever the capture state is
// (open, disabled, denied, stalled): the wait never depends on data being available.
constexpr int64_t kDefaultPollPeriodUs = 16000; // 256-sample grain at 16 kHz; fallback when no grain size is known

inline int64_t grain_period_us(int len_bytes, int freq) {
    if (len_bytes <= 0)
        return kDefaultPollPeriodUs;
    return static_cast<int64_t>(len_bytes / 2) * 1000000 / (freq > 0 ? freq : 16000);
}

// Cadence of sceAudioInInput: returns the instant (steady clock, us) this call completes and advances
// next_deadline_us by one period. First call, or a guest that was away for more than 4 periods, restarts
// from now so a late guest does not get a burst of instant calls.
inline int64_t next_grain_deadline(int64_t &next_deadline_us, int64_t now_us, int64_t period_us) {
    int64_t deadline = next_deadline_us;
    if (deadline == 0 || now_us - deadline > period_us * 4)
        deadline = now_us + period_us;
    next_deadline_us = deadline + period_us;
    return deadline;
}

// A failed sceAudioInInput (port not open, bad port argument, bad pointer) still costs one grain of guest
// time. A correct guest never sees this; a guest that keeps calling with a stale or error value as the port
// (e.g. after its sceAudioInOpenPort failed) must not busy-spin a host core (B146). Open port: its own grain.
inline int64_t failed_input_wait_us(bool port_open, int len_bytes, int freq) {
    return port_open ? grain_period_us(len_bytes, freq) : kDefaultPollPeriodUs;
}

// Guest ports. VOICE and RAW are independent ports of the Vita, each with its own id; opening one does not
// make the other PORT_FULL. Only the same type twice is PORT_FULL. Every port reads the one host capture
// stream (fan-out). The capture runs at the highest rate any open port needs (never below kCaptureFreq) and
// each port converts it down to its own rate.
constexpr int kPortSlots = 2;
constexpr int kCaptureFreq = 16000; // lowest capture rate
constexpr int kPortTypeVoice = 0;
constexpr int kPortTypeRaw = 2;

// Port id (= slot) of a port type, -1 for an unknown type. Ids: VOICE 0, RAW 1.
inline int slot_for_type(int type) {
    switch (type) {
    case kPortTypeVoice: return 0;
    case kPortTypeRaw: return 1;
    default: return -1;
    }
}

// Which guest ports are open. Not thread safe: callers hold the capture mutex.
struct SlotTable {
    static constexpr int kErrFull = -1; // that port type is already open
    static constexpr int kErrType = -2; // unknown port type
    bool open[kPortSlots] = {};

    // Reserves the port of the type and returns its id, or kErrFull / kErrType.
    int try_open(int type) {
        const int slot = slot_for_type(type);
        if (slot < 0)
            return kErrType;
        if (open[slot])
            return kErrFull;
        open[slot] = true;
        return slot;
    }
    bool is_open(int slot) const { return slot >= 0 && slot < kPortSlots && open[slot]; }
    bool release(int slot) {
        if (!is_open(slot))
            return false;
        open[slot] = false;
        return true;
    }
    int count() const {
        int n = 0;
        for (bool o : open)
            n += o ? 1 : 0;
        return n;
    }
    bool any() const { return count() > 0; }
};

// Host capture rate for the open ports: the highest port rate, kCaptureFreq when none is open.
inline int required_capture_freq(const bool (&open)[kPortSlots], const int (&freq)[kPortSlots]) {
    int rate = kCaptureFreq;
    for (int i = 0; i < kPortSlots; ++i)
        if (open[i])
            rate = std::max(rate, freq[i]);
    return rate;
}

} // namespace ecv_audio_in

#endif
