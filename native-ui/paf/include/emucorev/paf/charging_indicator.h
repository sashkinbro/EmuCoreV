#pragma once
#include <cstdint>

namespace emucorev::paf {
// Original indicator uses sce_paf_rand() & 1 to choose between two electric
// overlays. Keep UI-owned RNG state, independent of the emulator and libc rand.
class ChargingIndicator {
public:
    explicit ChargingIndicator(std::uint32_t seed = 1) : random_(seed) {}
    void advance(bool charging, int battery_percent) {
        active_ = charging && battery_percent >= 0 && battery_percent < 100;
        if (active_) {
            random_ = random_ * 0x41c64e6du + 0x3039u;
            texture_ = ((random_ >> 16) & 1) ? 0x00ea236eu : 0x32465c57u;
        }
    }
    std::uint32_t texture() const { return active_ ? texture_ : 0; }
private:
    std::uint32_t random_;
    std::uint32_t texture_ = 0;
    bool active_ = false;
};
}
