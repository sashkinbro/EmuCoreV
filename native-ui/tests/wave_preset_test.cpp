#include "emucorev/paf/wave_preset.h"

#include <bit>
#include <cstdint>
#include <cstdio>

namespace {

std::uint32_t bits_of(float v) noexcept {
    return std::bit_cast<std::uint32_t>(v);
}

bool check_bits(const char* name, float actual, std::uint32_t expected) {
    const std::uint32_t got = bits_of(actual);
    if (got != expected) {
        std::fprintf(stderr, "FAIL %s: got 0x%08x expected 0x%08x\n", name, got, expected);
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace emucorev::paf;

    const auto presets = wave_theme_presets();
    if (presets.size() != 32) {
        std::fprintf(stderr, "FAIL count=%zu expected 32\n", presets.size());
        return 1;
    }

    const auto& def = default_wave_theme_preset();
    if (def.name != "effects_default_0512_01") {
        std::fprintf(stderr, "FAIL default name\n");
        return 1;
    }

    bool ok = true;
    const auto& sky_ref = effects_default_0512_01_sky_preset();
    ok &= (&def.sky == &sky_ref);
    ok &= check_bits("sky.theta", sky_ref.theta_phi[0], 0x401c2e8c);
    ok &= check_bits("sky.zenith.z", sky_ref.zenith_color[2], 0x3eedb6a2);
    ok &= check_bits("sim.gravity", def.sim.gravity, 0x411b5f16);
    ok &= check_bits("sim.logA", def.sim.logA, 0xc1a00000);
    ok &= check_bits("sim.wind_dir", def.sim.wind_dir, 0xc0490fd8);
    ok &= check_bits("sim.time_step", def.sim.time_step, 0x3b700ef1);
    ok &= check_bits("fog0.d_offset", def.fog0.d_offset, 0x401a5a36);
    ok &= check_bits("fog1.d_offset", def.fog1.d_offset, 0x405a9bd0);

    const auto& master = wave_master_preset();
    ok &= check_bits("master.curve", master.uv_rotate_distort_curve, 0x428b4925);
    ok &= check_bits("master.chase", master.uv_rotate_distort_control_chase, 0x3dcccccd);
    ok &= check_bits("master.ctrl", master.uv_rotate_distort_control, 0x3f800000);

    const auto* ruby = find_wave_theme_preset("effects_0406_ruby");
    ok &= (ruby != nullptr);
    if (ruby != nullptr) {
        ok &= check_bits("ruby.mat.color[0]", ruby->material.color[0], 0x3f400000);
        ok &= check_bits("ruby.mat.specular_power", ruby->material.specular_power, 0x42898000);
    }

    const auto& fallback = wave_theme_preset_or_default("nonexistent_theme");
    ok &= (fallback.name == "effects_default_0512_01");

    if (!ok) {
        return 1;
    }
    std::printf("PASS wave_preset_test (%zu themes)\n", presets.size());
    return 0;
}
