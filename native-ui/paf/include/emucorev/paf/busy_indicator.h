#pragma once
#include <array>
#include <cstdint>

namespace emucorev::paf {
enum class BusyState : std::uint32_t { idle, starting, running, stopping };
struct BusyDot {
    float x = 0, y = 0, size = 0;
    std::uint8_t alpha = 0;
};
struct BusySnapshot {
    BusyState state = BusyState::idle;
    float phase = 0, elapsed = 0, last_emission = 0;
    std::array<BusyDot, 9> dots{};
};

// Reconstructed PAF BusyIndicator motion. The owner supplies active animation
// time; no clocks, threads, firmware reads or emulator calls are involved.
class BusyIndicator {
public:
    explicit BusyIndicator(float width, float ball_size = 10);
    void start();
    void stop();
    void tick(float seconds);
    const BusySnapshot& snapshot() const { return snapshot_; }
private:
    float width_, ball_size_;
    BusySnapshot snapshot_;
};
}
