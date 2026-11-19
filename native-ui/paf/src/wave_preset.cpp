#include "emucorev/paf/wave_preset.h"

#include <bit>
#include <cstdint>

namespace emucorev::paf {
namespace {

constexpr float from_bits(std::uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}

// The 0512_01 entry is registered at 0x810b0fba. This immutable snapshot
// reproduces its preceding uniform writes using the exact ARM float bits.
// In particular, zenith_color.z is loaded from [sp,#0xd0] at 0x810b0ea8;
// that slot is written with 0x3eedb6a2 at 0x810b0bd4.
constexpr WaveSkyPreset default_0512_01{
    .theta_phi = {from_bits(0x401c2e8c), from_bits(0xbeac56b4)}, // 0x810b0a6c-0a9c
    .distance = from_bits(0x4220dd93),                         // 0x810b0aa0-0ac0
    .sun_color = {from_bits(0x3f800000), from_bits(0x3f800000), from_bits(0x00000000)},
    .sun_power = from_bits(0x41ac83c9),                       // 0x810b0b08-0b28
    .sun_control = from_bits(0x3d1248d8),                     // 0x810b0b2c-0b4c
    .zenith_color = {from_bits(0x3c9248d8), from_bits(0x3d800000), from_bits(0x3eedb6a2)},
    .horizon_color = {from_bits(0x3e400000), from_bits(0x3ec49214), from_bits(0x3f69248d)},
    .wave_fog_alpha = from_bits(0x00000000),                  // s19 initialized at 0x810b08b4
    .radius = from_bits(0x4105d1df),                          // s20 initialized at 0x810b08ce
    .xscale = from_bits(0x3f8db6cc),                          // [sp,#0x104] stored at 0x810b0902
    .side_angle = from_bits(0x3f55f267),                      // movw/movt at 0x810b0f14-0f1a
    .horizon_angle = from_bits(0x3ddc358b),                   // movw/movt at 0x810b0f28-0f2e
    .horizon_blend_range = from_bits(0x3d9349bf),             // movw/movt at 0x810b0f3c-0f42
    .horizon_curvature = from_bits(0x3ea5286b),               // movw/movt at 0x810b0f50-0f56
    .sky_blend_start = from_bits(0x00000000),                 // s19 at 0x810b0f64
    .sky_blend_range = from_bits(0x3e58b32d),                 // movw/movt/vmov at 0x810b0f70-0f78
    .pos_distortion_scale = from_bits(0x3ba89fc7),            // s28
    .grad_distortion_scale = from_bits(0x3ba89fc7),            // s28
    .horizon_distortion_scale = from_bits(0x3b0a265f),         // s29
};

} // namespace

const WaveSkyPreset& effects_default_0512_01_sky_preset() noexcept {
    return default_0512_01;
}

} // namespace emucorev::paf
