#include "emucorev/paf/wave_simulation.h"

#include <algorithm>
#include <bit>
#include <cmath>

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
