#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace emucorev::paf {

struct WaveComplex32 {
    float real = 0.f;
    float imag = 0.f;
};

// One cell from the PAF heightmap phase buffer. The two consecutive complex
// pairs are the evolving phase and its per-cell step, respectively.
struct WavePhaseCell {
    WaveComplex32 phase{};
    WaveComplex32 step{};
};
static_assert(sizeof(WaveComplex32) == 8);
static_assert(sizeof(WavePhaseCell) == 16);

inline constexpr std::size_t kWaveFftSize = 32;
inline constexpr std::size_t kWaveFftCellCount = kWaveFftSize * kWaveFftSize;

// Computes the frequency-domain values consumed by the firmware FFT. Each
// input span must contain exactly 32*32 entries.
[[nodiscard]] bool wave_phased_spectrum32(
    std::span<const WaveComplex32> initial_spectrum,
    std::span<WavePhaseCell> phases,
    std::span<WaveComplex32> output) noexcept;

// Reproduces PAF FUN_81193e32: five-bit reversal on both axes, row pass before
// column pass, positive-angle twiddles, and one final 1/32 scale. Twiddle
// coefficients are exact binary32 values captured from the initializer whose
// Thumb entry is 0x8119448f (routine body at 0x8119448e). Separate FP
// multiplies/adds preserve source ARMv7 NEON rounding; the core target disables
// FP contraction.
[[nodiscard]] bool wave_fft32(std::span<WaveComplex32> values) noexcept;

// Applies the 32x32 height output conversion from FUN_8109f8be after the
// firmware's special FFT. The source multiplies by height_scale, adds 1, then
// multiplies by 0.5 before converting each float to half precision.
[[nodiscard]] bool wave_height32_to_half(
    std::span<const WaveComplex32> transformed,
    float height_scale,
    std::span<std::uint16_t> output) noexcept;

} // namespace emucorev::paf
