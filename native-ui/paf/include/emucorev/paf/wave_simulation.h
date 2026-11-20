#pragma once

#include <emucorev/paf/wave_preset.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

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

struct WaveSurfaceVertex {
    std::array<float, 4> uv;
};

struct WaveSurfaceMesh {
    std::vector<WaveSurfaceVertex> vertices;
    std::vector<std::uint16_t> triangle_strip_indices;
};

inline constexpr std::size_t kWaveFftSize = 32;
inline constexpr std::size_t kWaveFftCellCount = kWaveFftSize * kWaveFftSize;
inline constexpr float kWaveDefaultHeightScale = 625.0f; // FUN_8109f42e param_1[0x19] = 0x441c4000

// Standard ANSI LCG used by sce_paf_rand (paf.elf 0x8105188c, state 0x81301184 init = 1u).
[[nodiscard]] std::uint32_t wave_paf_rand(std::uint32_t& state) noexcept;

// Generates 32x32 uniform [-1, 1) noise pairs via sce_paf_rand % 2056 (FUN_8109f552).
[[nodiscard]] bool wave_generate_noise32(
    std::uint32_t& prng_state,
    std::span<WaveComplex32> noise) noexcept;

// Initializes 32x32 Phillips spectrum H0(k) from noise and simulation preset (FUN_8109ee5c).
[[nodiscard]] bool wave_init_phillips_spectrum32(
    const WaveSimPreset& sim,
    std::span<const WaveComplex32> noise,
    std::span<WaveComplex32> h0) noexcept;

// Initializes 32x32 dispersion phases (1, 0) and per-step unit rotors (FUN_8109f256).
[[nodiscard]] bool wave_init_phase_steps32(
    const WaveSimPreset& sim,
    std::span<WavePhaseCell> phases) noexcept;

// Builds the 64x64 stitched triangle-strip wave surface grid (0x81202e28 + 0x810b802e + 0x81201278).
WaveSurfaceMesh make_wave_surface_mesh(std::uint16_t columns = 64, std::uint16_t rows = 64);

// Complete 32x32 CPU wave heightfield simulator wrapping init and per-frame step.
struct WaveHeightfield32 {
    std::array<WaveComplex32, kWaveFftCellCount> noise{};
    std::array<WaveComplex32, kWaveFftCellCount> h0{};
    std::array<WavePhaseCell, kWaveFftCellCount> phases{};
    std::array<WaveComplex32, kWaveFftCellCount> spectrum{};
    std::array<WaveComplex32, kWaveFftCellCount> fft_out{};

    bool reset(const WaveSimPreset& sim, std::uint32_t seed = 1u) noexcept;
    bool step(std::span<std::uint16_t> out_half, bool advance = true) noexcept;
};

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
