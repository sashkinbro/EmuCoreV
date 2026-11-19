#pragma once

#include <cstdint>
#include <string>

namespace emucorev::paf {

enum class RadioState : int { unavailable = -2, unknown = -1, off = 0, on = 1 };

// Host data, independent of Android services and the game/emulation runtime.
// A negative battery percentage or unknown radio state must not become a
// fabricated "full"/"off" value in the view.
struct DeviceStatus {
    int battery_percent = -1;
    bool charging = false;
    int power_source = 0;
    RadioState wifi_enabled = RadioState::unknown;
    bool wifi_connected = false;
    int wifi_rssi = INT32_MIN;
    RadioState bluetooth_enabled = RadioState::unknown;
    std::int64_t epoch_minute = 0;
    std::string time_zone;
    bool use_24_hour_clock = true;
};

} // namespace emucorev::paf
