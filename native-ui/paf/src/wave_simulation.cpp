#include "emucorev/paf/wave_simulation.h"

#include <emucorev/paf/plane_div.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace emucorev::paf {
namespace {

struct WaveTwiddle {
    std::uint32_t real_bits;
    std::uint32_t imag_bits;
};

// Captured from the original PAF initializer (Thumb entry 0x8119448f) by
// executing it in the reference environment. The constants are stored as
// IEEE-754 bits because several entries intentionally differ by a few ULPs.
constexpr std::array<WaveTwiddle, 4> twiddle8{{
    {0x3f800000u, 0x00000000u}, {0x3f3504f7u, 0x3f3504f7u},
    {0x80000000u, 0x3f800000u}, {0xbf3504f7u, 0x3f3504f7u},
}};
constexpr std::array<WaveTwiddle, 8> twiddle16{{
    {0x3f800000u, 0x00000000u}, {0x3f6c8366u, 0x3ec3ef07u},
    {0x3f3504f7u, 0x3f3504f7u}, {0x3ec3ef07u, 0x3f6c8366u},
    {0x80000000u, 0x3f800000u}, {0xbec3ef28u, 0x3f6c8366u},
    {0xbf3504f7u, 0x3f3504f7u}, {0xbf6c8366u, 0x3ec3ef07u},
}};
constexpr std::array<WaveTwiddle, 16> twiddle32{{
    {0x3f800000u, 0x00000000u}, {0x3f7b14bau, 0x3e47c5acu},
    {0x3f6c8366u, 0x3ec3ef07u}, {0x3f54db38u, 0x3f0e39d6u},
    {0x3f3504f7u, 0x3f3504f7u}, {0x3f0e39d6u, 0x3f54db38u},
    {0x3ec3ef07u, 0x3f6c8366u}, {0x3e47c5acu, 0x3f7b14bau},
    {0x80000000u, 0x3f800000u}, {0xbe47c5acu, 0x3f7b14bau},
    {0xbec3ef28u, 0x3f6c8366u}, {0xbf0e39d6u, 0x3f54db38u},
    {0xbf3504f7u, 0x3f3504f7u}, {0xbf54db38u, 0x3f0e39d6u},
    {0xbf6c8366u, 0x3ec3ef07u}, {0xbf7b14bau, 0x3e47c5acu},
}};

float twiddle_component(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

void transform_axis(std::span<WaveComplex32> values, std::size_t base, std::size_t stride) noexcept {
    for (std::size_t length = 2; length <= kWaveFftSize; length *= 2) {
        const std::size_t half = length / 2;
        for (std::size_t block = 0; block < kWaveFftSize; block += length) {
            for (std::size_t j = 0; j < half; ++j) {
                const std::size_t even_index = base + (block + j) * stride;
                const std::size_t odd_index = base + (block + j + half) * stride;
                const WaveComplex32 even = values[even_index];
                const WaveComplex32 odd = values[odd_index];
                float rotated_real;
                float rotated_imag;

                if (length == 2 || (length == 4 && j == 0)) {
                    rotated_real = odd.real;
                    rotated_imag = odd.imag;
                } else {
                    WaveTwiddle twiddle{};
                    if (length == 4) {
                        twiddle = {0x00000000u, 0x3f800000u}; // +i
                    } else if (length == 8) {
                        twiddle = twiddle8[j];
                    } else if (length == 16) {
                        twiddle = twiddle16[j];
                    } else {
                        twiddle = twiddle32[j];
                    }
                    const float wr = twiddle_component(twiddle.real_bits);
                    const float wi = twiddle_component(twiddle.imag_bits);
                    const float real_direct = odd.real * wr;
                    const float imag_cross = odd.imag * -wi;
                    const float real_cross = odd.real * wi;
                    const float imag_direct = odd.imag * wr;
                    rotated_real = real_direct + imag_cross;
                    rotated_imag = real_cross + imag_direct;
                }

                values[even_index] = {even.real + rotated_real, even.imag + rotated_imag};
                values[odd_index] = {even.real - rotated_real, even.imag - rotated_imag};
            }
        }
    }
}

std::size_t reverse_five_bits(std::size_t value) noexcept {
    std::size_t reversed = 0;
    for (std::size_t bit = 0; bit < 5; ++bit)
        reversed = (reversed << 1) | ((value >> bit) & 1u);
    return reversed;
}

// IEEE-754 binary32 to binary16, round-to-nearest-even. This matches the
// vector single-to-half conversion used by the firmware's 1024-sample path.
std::uint16_t float_to_half(float value) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exponent = (bits >> 23) & 0xffu;
    const std::uint32_t mantissa = bits & 0x7fffffu;

    if (exponent == 0xffu) {
        if (mantissa == 0)
            return static_cast<std::uint16_t>(sign | 0x7c00u);
        const std::uint32_t payload = (mantissa >> 13) | 0x0200u;
        return static_cast<std::uint16_t>(sign | 0x7c00u | payload);
    }

    const int half_exponent = static_cast<int>(exponent) - 127 + 15;
    if (half_exponent >= 31)
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    if (half_exponent <= 0) {
        if (half_exponent < -10)
            return static_cast<std::uint16_t>(sign);
        const std::uint32_t significand = mantissa | 0x800000u;
        const unsigned shift = static_cast<unsigned>(14 - half_exponent);
        std::uint32_t rounded = significand >> shift;
        const std::uint32_t remainder = significand & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (rounded & 1u)))
            ++rounded;
        return static_cast<std::uint16_t>(sign | rounded);
    }

    std::uint32_t rounded_mantissa = mantissa >> 13;
    const std::uint32_t remainder = mantissa & 0x1fffu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (rounded_mantissa & 1u))) {
        ++rounded_mantissa;
        if (rounded_mantissa == 0x400u) {
            rounded_mantissa = 0;
            if (half_exponent + 1 >= 31)
                return static_cast<std::uint16_t>(sign | 0x7c00u);
            return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(half_exponent + 1) << 10));
        }
    }
    return static_cast<std::uint16_t>(sign
        | (static_cast<std::uint32_t>(half_exponent) << 10)
        | rounded_mantissa);
}

} // namespace

std::uint32_t wave_paf_rand(std::uint32_t& state) noexcept {
    state = state * 1103515245u + 12345u;
    return (state >> 16) & 0x7fffu;
}

bool wave_generate_noise32(
    std::uint32_t& prng_state,
    std::span<WaveComplex32> noise) noexcept {
    if (noise.size() != kWaveFftCellCount) {
        return false;
    }
    for (std::size_t i = 0; i < kWaveFftCellCount; ++i) {
        const std::uint32_t r0 = wave_paf_rand(prng_state) % 2056u;
        const std::uint32_t r1 = wave_paf_rand(prng_state) % 2056u;
        noise[i] = {
            (static_cast<float>(r0) * 2.0f) / 2056.0f - 1.0f,
            (static_cast<float>(r1) * 2.0f) / 2056.0f - 1.0f,
        };
    }
    return true;
}

bool wave_init_phillips_spectrum32(
    const WaveSimPreset& sim,
    std::span<const WaveComplex32> noise,
    std::span<WaveComplex32> h0) noexcept {
    if (noise.size() != kWaveFftCellCount || h0.size() != kWaveFftCellCount
        || !std::isfinite(sim.patch_size) || sim.patch_size == 0.0f
        || !std::isfinite(sim.gravity) || sim.gravity == 0.0f
        || !std::isfinite(sim.wind_speed) || sim.wind_speed == 0.0f
        || !std::isfinite(sim.wind_dir) || !std::isfinite(sim.logA)) {
        return false;
    }

    const float inv_patch = arm_recip_estimate_2step(sim.patch_size);
    const float kx_scale = std::bit_cast<float>(0x40490fdbu) * inv_patch;
    const float ky_scale = std::bit_cast<float>(0x40c90fdbu) * inv_patch;
    const float wind_x = std::cos(sim.wind_dir);
    const float wind_y = std::sin(sim.wind_dir);
    const float l_wind = (sim.wind_speed * sim.wind_speed) / sim.gravity;
    const float l_wind_sq = l_wind * l_wind;
    const float norm_factor = std::bit_cast<float>(0x3f3504f3u); // 1/sqrt(2)

    for (std::size_t row = 0; row < kWaveFftSize; ++row) {
        const float ky = static_cast<float>(row) * ky_scale;
        for (std::size_t col = 0; col < kWaveFftSize; ++col) {
            const float kx = static_cast<float>(col) * kx_scale;
            const std::size_t idx = row * kWaveFftSize + col;
            const float k_sq = kx * kx + ky * ky;
            float amplitude = 0.0f;
            if (k_sq != 0.0f) {
                const float k_len = std::sqrt(k_sq);
                const float dot_kw = (kx / k_len) * wind_x + (ky / k_len) * wind_y;
                const float exponent = sim.logA - 1.0f / (k_sq * l_wind_sq);
                const float p = (std::exp(exponent) / (k_sq * k_sq)) * (dot_kw * dot_kw);
                amplitude = std::sqrt(p);
            }
            const float scale = amplitude * norm_factor;
            h0[idx] = {
                noise[idx].real * scale,
                noise[idx].imag * scale,
            };
        }
    }
    return true;
}

bool wave_init_phase_steps32(
    const WaveSimPreset& sim,
    std::span<WavePhaseCell> phases) noexcept {
    if (phases.size() != kWaveFftCellCount
        || !std::isfinite(sim.patch_size) || sim.patch_size == 0.0f
        || !std::isfinite(sim.gravity) || sim.gravity < 0.0f
        || !std::isfinite(sim.time_step)) {
        return false;
    }

    // Preserve the exact axis order of PAF FUN_8109f256: outer loop advances X
    // (lane 0 * pi/L), inner loop advances Y (lane 1 * 2*pi/L).
    const float inv_patch = arm_recip_estimate_2step(sim.patch_size);
    const float kx_scale = std::bit_cast<float>(0x40490fdbu) * inv_patch;
    const float ky_scale = std::bit_cast<float>(0x40c90fdbu) * inv_patch;
    const float k_sq_min = std::bit_cast<float>(0x00800000u);
    const float step_factor = std::bit_cast<float>(0x3e19999au); // 0.15f

    for (std::size_t row = 0; row < kWaveFftSize; ++row) {
        const float kx = static_cast<float>(row) * kx_scale;
        for (std::size_t col = 0; col < kWaveFftSize; ++col) {
            const float ky = static_cast<float>(col) * ky_scale;
            const float k_sq = kx * kx + ky * ky;
            const float k_len = (k_sq > k_sq_min) ? std::sqrt(k_sq) : 0.0f;
            const float angle = std::sqrt(sim.gravity * k_len) * sim.time_step * step_factor;
            phases[row * kWaveFftSize + col] = {
                {1.0f, 0.0f},
                {std::cos(angle), std::sin(angle)},
            };
        }
    }
    return true;
}

WaveSurfaceMesh make_wave_surface_mesh(std::uint16_t columns, std::uint16_t rows) {
    if (columns < 2 || rows < 2
        || static_cast<std::uint32_t>(columns) * rows > 65536u) {
        throw std::invalid_argument("Invalid PAF wave surface grid dimensions");
    }
    const float inv_columns = arm_recip_estimate_2step(static_cast<float>(columns - 1));
    const float inv_rows = arm_recip_estimate_2step(static_cast<float>(rows - 1));
    WaveSurfaceMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(columns) * rows);
    for (std::uint32_t row = 0; row < rows; ++row) {
        const float v = static_cast<float>(row) * inv_rows;
        for (std::uint32_t col = 0; col < columns; ++col) {
            const float u = static_cast<float>(col) * inv_columns;
            mesh.vertices.push_back({{u, v, 0.0f, 1.0f}});
        }
    }

    // Identical FUN_81201278 stitched strip index generator as make_wave_skydome_mesh.
    mesh.triangle_strip_indices.reserve(
        (static_cast<std::size_t>(rows) - 1) * columns * 2
            + (static_cast<std::size_t>(rows) - 2) * 4);
    for (std::uint32_t row = 0; row + 1 < rows; ++row) {
        const auto old_start = static_cast<std::uint16_t>(row * columns);
        const auto new_start = static_cast<std::uint16_t>((row + 1) * columns);
        for (std::uint32_t col = 0; col < columns; ++col) {
            mesh.triangle_strip_indices.push_back(static_cast<std::uint16_t>(new_start + col));
            mesh.triangle_strip_indices.push_back(static_cast<std::uint16_t>(old_start + col));
        }
        if (row + 2 < rows) {
            mesh.triangle_strip_indices.push_back(static_cast<std::uint16_t>(new_start - 1));
            const auto first_next_strip = static_cast<std::uint16_t>((row + 2) * columns);
            mesh.triangle_strip_indices.push_back(first_next_strip);
            mesh.triangle_strip_indices.push_back(first_next_strip);
            mesh.triangle_strip_indices.push_back(first_next_strip);
        }
    }
    return mesh;
}

bool WaveHeightfield32::reset(const WaveSimPreset& sim, std::uint32_t seed) noexcept {
    std::uint32_t prng = seed;
    if (!wave_generate_noise32(prng, noise)) {
        return false;
    }
    if (!wave_init_phillips_spectrum32(sim, noise, h0)) {
        return false;
    }
    return wave_init_phase_steps32(sim, phases);
}

bool WaveHeightfield32::step(std::span<std::uint16_t> out_half, bool advance) noexcept {
    if (out_half.size() != kWaveFftCellCount) {
        return false;
    }
    if (advance) {
        if (!wave_phased_spectrum32(h0, phases, spectrum)) {
            return false;
        }
    } else {
        auto snapshot = phases;
        if (!wave_phased_spectrum32(h0, snapshot, spectrum)) {
            return false;
        }
    }
    fft_out = spectrum;
    if (!wave_fft32(fft_out)) {
        return false;
    }
    return wave_height32_to_half(fft_out, kWaveDefaultHeightScale, out_half);
}

bool wave_phased_spectrum32(
    std::span<const WaveComplex32> initial_spectrum,
    std::span<WavePhaseCell> phases,
    std::span<WaveComplex32> output) noexcept {
    if (initial_spectrum.size() != kWaveFftCellCount
        || phases.size() != kWaveFftCellCount
        || output.size() != kWaveFftCellCount)
        return false;

    // FUN_8109f17e indexes the mirror by (height-row-1, column), then forms
    // H0(k) - conjugate(H0(-k)) and multiplies by the evolving phase.
    for (std::size_t row = 0; row < kWaveFftSize; ++row) {
        const std::size_t mirror_row = kWaveFftSize - row - 1;
        for (std::size_t column = 0; column < kWaveFftSize; ++column) {
            const std::size_t index = row * kWaveFftSize + column;
            const std::size_t mirror_index = mirror_row * kWaveFftSize + column;
            const WaveComplex32 h0 = initial_spectrum[index];
            const WaveComplex32 mirror = initial_spectrum[mirror_index];
            const WaveComplex32 phase = phases[index].phase;
            const WaveComplex32 step = phases[index].step;

            // The native target is built with FP contraction disabled, like
            // the ARMv7 NEON VMUL/VADD/VSUB sequence in the PAF routine.
            const float next_phase_real = phase.real * step.real - phase.imag * step.imag;
            const float next_phase_imag = phase.real * step.imag + phase.imag * step.real;
            phases[index].phase = {next_phase_real, next_phase_imag};

            const float difference_real = h0.real - mirror.real;
            const float difference_imag = h0.imag + mirror.imag;
            output[index] = {
                phase.real * difference_real - phase.imag * difference_imag,
                phase.real * difference_imag + phase.imag * difference_real,
            };
        }
    }
    return true;
}

bool wave_fft32(std::span<WaveComplex32> values) noexcept {
    if (values.size() != kWaveFftCellCount)
        return false;

    // 0x81193e32 first copies rows in five-bit reversed order, then swaps
    // columns into that same order before running the separable butterflies.
    for (std::size_t row = 0; row < kWaveFftSize; ++row) {
        const std::size_t reversed_row = reverse_five_bits(row);
        for (std::size_t column = 0; column < kWaveFftSize; ++column) {
            const std::size_t reversed_column = reverse_five_bits(column);
            const std::size_t index = row * kWaveFftSize + column;
            const std::size_t reverse_index = reversed_row * kWaveFftSize + reversed_column;
            if (index < reverse_index)
                std::swap(values[index], values[reverse_index]);
        }
    }

    // First transform contiguous columns within each row, then the row axis.
    for (std::size_t row = 0; row < kWaveFftSize; ++row)
        transform_axis(values, row * kWaveFftSize, 1);
    for (std::size_t column = 0; column < kWaveFftSize; ++column)
        transform_axis(values, column, kWaveFftSize);

    constexpr float scale = std::bit_cast<float>(0x3d000000u); // original 1/32
    for (WaveComplex32& value : values) {
        value.real *= scale;
        value.imag *= scale;
    }
    return true;
}

bool wave_height32_to_half(
    std::span<const WaveComplex32> transformed,
    float height_scale,
    std::span<std::uint16_t> output) noexcept {
    if (transformed.size() != kWaveFftCellCount || output.size() != kWaveFftCellCount
        || !std::isfinite(height_scale))
        return false;

    for (std::size_t index = 0; index < kWaveFftCellCount; ++index) {
        const float scaled = transformed[index].real * height_scale;
        const float shifted = scaled + 1.f;
        const float height = shifted * 0.5f;
        output[index] = float_to_half(height);
    }
    return true;
}

} // namespace emucorev::paf
