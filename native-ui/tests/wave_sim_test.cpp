#include <emucorev/paf/wave_preset.h>
#include <emucorev/paf/wave_simulation.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::uint32_t ulp_distance(float a, float b) {
    if (a == b) {
        return 0; // Treats +0.0f and -0.0f as 0 ULP apart
    }
    const std::uint32_t ua = std::bit_cast<std::uint32_t>(a);
    const std::uint32_t ub = std::bit_cast<std::uint32_t>(b);
    const std::int32_t ia = (ua & 0x80000000u)
        ? static_cast<std::int32_t>(0x80000000u - ua)
        : static_cast<std::int32_t>(ua);
    const std::int32_t ib = (ub & 0x80000000u)
        ? static_cast<std::int32_t>(0x80000000u - ub)
        : static_cast<std::int32_t>(ub);
    const std::int64_t diff = static_cast<std::int64_t>(ia) - static_cast<std::int64_t>(ib);
    return static_cast<std::uint32_t>(diff < 0 ? -diff : diff);
}

std::uint32_t half_ulp_distance(std::uint16_t a, std::uint16_t b) {
    if ((a & 0x7fffu) == 0 && (b & 0x7fffu) == 0) {
        return 0;
    }
    const auto to_signed = [](std::uint16_t h) -> std::int32_t {
        return (h & 0x8000u) ? static_cast<std::int32_t>(0x8000u - h)
                             : static_cast<std::int32_t>(h);
    };
    const std::int32_t diff = to_signed(a) - to_signed(b);
    return static_cast<std::uint32_t>(diff < 0 ? -diff : diff);
}

void check_surface_mesh() {
    const auto mesh = emucorev::paf::make_wave_surface_mesh(64, 64);
    if (mesh.vertices.size() != 4096u) {
        throw std::runtime_error("Unexpected wave surface vertex count");
    }
    constexpr std::size_t kExpectedIndices = 63u * 64u * 2u + 62u * 4u; // 8312
    if (mesh.triangle_strip_indices.size() != kExpectedIndices) {
        throw std::runtime_error("Unexpected wave surface index count");
    }
    for (const auto idx : mesh.triangle_strip_indices) {
        if (idx >= mesh.vertices.size()) {
            throw std::runtime_error("Out-of-range wave surface index");
        }
    }
    const auto near_eq = [](float a, float b) { return std::abs(a - b) < 1e-6f; };
    const auto& v0 = mesh.vertices.front().uv;
    const auto& v_tr = mesh.vertices[63].uv;
    const auto& v_bl = mesh.vertices[63u * 64u].uv;
    const auto& v_br = mesh.vertices.back().uv;
    if (!near_eq(v0[0], 0.f) || !near_eq(v0[1], 0.f) || v0[2] != 0.f || v0[3] != 1.f
        || !near_eq(v_tr[0], 1.f) || !near_eq(v_tr[1], 0.f)
        || !near_eq(v_bl[0], 0.f) || !near_eq(v_bl[1], 1.f)
        || !near_eq(v_br[0], 1.f) || !near_eq(v_br[1], 1.f)) {
        throw std::runtime_error("Unexpected wave surface corner UVs");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            std::cerr << "Usage: paf_wave_sim_test REFERENCE_BIN\n";
            return 2;
        }
        check_surface_mesh();

        std::ifstream input(argv[1], std::ios::binary);
        if (!input) {
            throw std::runtime_error(std::string("Cannot open ") + argv[1]);
        }
        std::array<char, 8> magic{};
        input.read(magic.data(), magic.size());
        if (!input || std::string(magic.data(), magic.size()) != "PAFWSM01") {
            throw std::runtime_error("Invalid wave sim reference magic");
        }

        std::array<std::uint32_t, 8> sim_bits{};
        input.read(reinterpret_cast<char*>(sim_bits.data()), sizeof(sim_bits));

        const auto& preset = emucorev::paf::default_wave_theme_preset();
        if (std::bit_cast<std::uint32_t>(preset.sim.gravity) != sim_bits[0]
            || std::bit_cast<std::uint32_t>(preset.sim.logA) != sim_bits[2]
            || std::bit_cast<std::uint32_t>(preset.sim.wind_speed) != sim_bits[3]
            || std::bit_cast<std::uint32_t>(preset.sim.wind_dir) != sim_bits[4]
            || std::bit_cast<std::uint32_t>(preset.sim.patch_size) != sim_bits[5]
            || std::bit_cast<std::uint32_t>(preset.sim.time_step) != sim_bits[6]
            || std::bit_cast<std::uint32_t>(emucorev::paf::kWaveDefaultHeightScale) != sim_bits[7]) {
            throw std::runtime_error("Preset sim constants mismatch reference header");
        }

        std::array<emucorev::paf::WaveComplex32, emucorev::paf::kWaveFftCellCount> ref_noise{};
        std::array<emucorev::paf::WaveComplex32, emucorev::paf::kWaveFftCellCount> ref_h0{};
        std::array<emucorev::paf::WavePhaseCell, emucorev::paf::kWaveFftCellCount> ref_phases{};
        input.read(reinterpret_cast<char*>(ref_noise.data()), sizeof(ref_noise));
        input.read(reinterpret_cast<char*>(ref_h0.data()), sizeof(ref_h0));
        input.read(reinterpret_cast<char*>(ref_phases.data()), sizeof(ref_phases));

        std::uint32_t frame_count = 0;
        input.read(reinterpret_cast<char*>(&frame_count), sizeof(frame_count));
        if (!input || frame_count != 3u) {
            throw std::runtime_error("Unexpected wave sim frame count");
        }
        std::vector<std::array<std::uint16_t, emucorev::paf::kWaveFftCellCount>> ref_frames(frame_count);
        for (auto& frame : ref_frames) {
            input.read(reinterpret_cast<char*>(frame.data()), frame.size() * sizeof(std::uint16_t));
        }
        if (!input || input.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Corrupt or trailing wave sim reference bytes");
        }

        emucorev::paf::WaveHeightfield32 sim;
        if (!sim.reset(preset.sim)) {
            throw std::runtime_error("WaveHeightfield32::reset failed");
        }

        for (std::size_t i = 0; i < ref_noise.size(); ++i) {
            if (std::bit_cast<std::uint32_t>(sim.noise[i].real)
                    != std::bit_cast<std::uint32_t>(ref_noise[i].real)
                || std::bit_cast<std::uint32_t>(sim.noise[i].imag)
                    != std::bit_cast<std::uint32_t>(ref_noise[i].imag)) {
                std::cerr << "Noise mismatch at cell " << i << '\n';
                throw std::runtime_error("Noise is not bit-exact");
            }
        }

        std::uint32_t max_h0_ulp = 0;
        float max_h0_abs = 0.0f;
        for (std::size_t i = 0; i < ref_h0.size(); ++i) {
            const float ar = sim.h0[i].real, ai = sim.h0[i].imag;
            const float er = ref_h0[i].real, ei = ref_h0[i].imag;
            max_h0_abs = std::max(max_h0_abs, std::max(std::abs(ar - er), std::abs(ai - ei)));
            // Compare ULP only where signal isn't near trig zero-cancellation (kx=0, wind_dir=-pi).
            if (std::max(std::abs(er), std::abs(ar)) > 1e-6f) {
                max_h0_ulp = std::max(max_h0_ulp, ulp_distance(ar, er));
            }
            if (std::max(std::abs(ei), std::abs(ai)) > 1e-6f) {
                max_h0_ulp = std::max(max_h0_ulp, ulp_distance(ai, ei));
            }
        }

        std::uint32_t max_phase_ulp = 0;
        for (std::size_t i = 0; i < ref_phases.size(); ++i) {
            if (sim.phases[i].phase.real != ref_phases[i].phase.real
                || sim.phases[i].phase.imag != ref_phases[i].phase.imag) {
                throw std::runtime_error("Initial phase is not (1, 0)");
            }
            max_phase_ulp = std::max(
                max_phase_ulp,
                std::max(ulp_distance(sim.phases[i].step.real, ref_phases[i].step.real),
                    ulp_distance(sim.phases[i].step.imag, ref_phases[i].step.imag)));
        }

        std::uint32_t max_half_ulp = 0;
        for (std::uint32_t f = 0; f < frame_count; ++f) {
            std::array<std::uint16_t, emucorev::paf::kWaveFftCellCount> half{};
            if (!sim.step(half, true)) {
                throw std::runtime_error("WaveHeightfield32::step failed");
            }
            for (std::size_t i = 0; i < half.size(); ++i) {
                max_half_ulp = std::max(max_half_ulp, half_ulp_distance(half[i], ref_frames[f][i]));
            }
        }

        // Verify advance=false does not advance phase state.
        std::array<std::uint16_t, emucorev::paf::kWaveFftCellCount> hold0{}, hold1{};
        if (!sim.step(hold0, false) || !sim.step(hold1, false) || hold0 != hold1) {
            throw std::runtime_error("Paused step mutated phases");
        }

        std::cout << "Wave sim check: max_h0_abs=" << max_h0_abs
                  << " max_h0_ulp=" << max_h0_ulp
                  << " max_phase_ulp=" << max_phase_ulp
                  << " max_half_ulp=" << max_half_ulp << '\n';

        if (max_h0_abs > 1e-7f || max_h0_ulp > 32u || max_phase_ulp > 4u || max_half_ulp > 1u) {
            throw std::runtime_error("Wave simulation error exceeds tolerance");
        }
        std::cout << "PASS wave_sim_test (1024 bit-exact noise, H0/phases/3 FFT frames, 64x64 mesh)\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
