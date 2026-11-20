#include <emucorev/paf/wave_scene.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>

namespace {

using namespace emucorev::paf;

void require(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

void expect_near(float actual, float expected, float tol, const char* msg) {
    const float err = std::abs(actual - expected);
    if (!std::isfinite(actual) || err > tol) {
        std::fprintf(stderr, "FAIL %s: actual=%.8f expected=%.8f err=%.8f tol=%.8f\n",
            msg, actual, expected, err, tol);
        std::exit(1);
    }
}

void expect_finite_range(const float* data, std::size_t count, const char* label) {
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isfinite(data[i])) {
            std::fprintf(stderr, "FAIL %s[%zu] is not finite\n", label, i);
            std::exit(1);
        }
    }
}

} // namespace

int main() {
    // 1. Ortho(0, 1, 0, 1, 0, 1) matches Unicorn FUN_8100a2bc output.
    const WaveMat4 ortho = wave_mat4_ortho(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    const WaveMat4 expected_ortho{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -2.0f, 0.0f,
        -1.0f, -1.0f, -1.0f, 1.0f,
    };
    for (std::size_t i = 0; i < 16; ++i) {
        expect_near(ortho[i], expected_ortho[i], 1e-6f, "ortho01");
    }

    // 2. Pitch/roll look direction matches Unicorn FUN_81203668 output.
    const WaveVec4 d0 = wave_pitch_roll_direction(0.0f, 0.0f);
    expect_near(d0[0], 0.0f, 1e-6f, "dir(0,0).x");
    expect_near(d0[1], 0.0f, 1e-6f, "dir(0,0).y");
    expect_near(d0[2], 1.0f, 1e-6f, "dir(0,0).z");

    const WaveVec4 d1 = wave_pitch_roll_direction(-0.7f, 0.2f);
    expect_near(d1[0], 0.198669f, 1e-5f, "dir(-0.7,0.2).x");
    expect_near(d1[1], 0.631376f, 1e-5f, "dir(-0.7,0.2).y");
    expect_near(d1[2], 0.749596f, 1e-5f, "dir(-0.7,0.2).z");

    const WaveVec4 d2 = wave_pitch_roll_direction(0.5f, -0.3f);
    expect_near(d2[0], -0.295520f, 1e-5f, "dir(0.5,-0.3).x");
    expect_near(d2[1], -0.458013f, 1e-5f, "dir(0.5,-0.3).y");
    expect_near(d2[2], 0.838387f, 1e-5f, "dir(0.5,-0.3).z");

    // 3. Default preset camera resting + flat pose match Unicorn Camera::set_aspect + FUN_810a6bb0.
    const WaveThemePreset& def = default_wave_theme_preset();
    const WaveCameraMatrices cam_rest = build_wave_camera(def.camera, def.wave, 960.0f / 544.0f);
    expect_near(cam_rest.projection[0], 2.355853f, 1e-4f, "proj[0]");
    expect_near(cam_rest.projection[5], 4.157387f, 1e-4f, "proj[5]");
    expect_near(cam_rest.projection[10], -1.020202f, 1e-4f, "proj[10]");
    expect_near(cam_rest.projection[11], -1.0f, 1e-6f, "proj[11]");
    expect_near(cam_rest.projection[14], -202.020203f, 1e-4f, "proj[14]");

    expect_near(cam_rest.base_world[14], 1130.809204f, 1e-3f, "base_world.z");
    expect_near(cam_rest.pitch_up, 0.0f, 1e-6f, "rest.pitch_up");
    expect_near(cam_rest.pitch_down_cubed, 0.0f, 1e-6f, "rest.pitch_down_cubed");
    for (std::size_t i = 0; i < 16; ++i) {
        expect_near(cam_rest.world[i], cam_rest.base_world[i], 1e-5f, "rest.world==base");
    }

    const WaveCameraMatrices cam_flat = build_wave_camera(def.camera, def.wave,
        960.0f / 544.0f, 0.0f, 0.0f);
    expect_near(cam_flat.pitch_up, 1.0f, 1e-6f, "flat.pitch_up");
    expect_near(cam_flat.world[4], 0.0f, 1e-5f, "flat.col1.x");
    expect_near(cam_flat.world[5], 0.876727f, 1e-4f, "flat.col1.y");
    expect_near(cam_flat.world[6], -0.480989f, 1e-4f, "flat.col1.z");
    expect_near(cam_flat.world[8], 0.0f, 1e-5f, "flat.col2.x");
    expect_near(cam_flat.world[9], 0.480989f, 1e-4f, "flat.col2.y");
    expect_near(cam_flat.world[10], 0.876727f, 1e-4f, "flat.col2.z");
    expect_near(cam_flat.world[12], 0.0f, 1e-2f, "flat.col3.x");
    expect_near(cam_flat.world[13], 1448.726074f, 5e-2f, "flat.col3.y");
    expect_near(cam_flat.world[14], 1365.303711f, 5e-2f, "flat.col3.z");

    // 4. Roll clamping in transition zone matches Unicorn FUN_810a6bb0 output.
    const WaveCameraMatrices cam_roll = build_wave_camera(def.camera, def.wave,
        960.0f / 544.0f, def.camera.pitch_rest, def.camera.roll_max);
    expect_near(cam_roll.world[0], 0.988738f, 1e-4f, "roll_max.col0.x");
    expect_near(cam_roll.world[2], 0.149658f, 1e-4f, "roll_max.col0.z");
    expect_near(cam_roll.world[8], -0.149658f, 1e-4f, "roll_max.col2.x");
    expect_near(cam_roll.world[12], -169.235077f, 5e-2f, "roll_max.col3.x");
    expect_near(cam_roll.world[14], 1118.073730f, 5e-2f, "roll_max.col3.z");

    // 5. Spherical light / sun positions match Unicorn FUN_81203518.
    WaveLightPreset probe_light{};
    probe_light.theta_phi = {0.7f, 1.1f};
    probe_light.distance = 2.5f;
    probe_light.attn = {1.0f, 0.0f, 0.0f};
    const auto packed_l1 = wave_pack_light1(probe_light);
    expect_near(packed_l1[0], 1.435329f, 1e-5f, "light1.x");
    expect_near(packed_l1[1], 1.912106f, 1e-5f, "light1.y");
    expect_near(packed_l1[2], 0.730537f, 1e-5f, "light1.z");
    expect_near(packed_l1[3], 1.0f, 1e-6f, "light1.w");
    expect_near(packed_l1[4], 1.0f, 1e-6f, "light1.attn0");

    // 6. Full frame builds cleanly for both default and ruby presets.
    for (std::string_view theme_name : {"effects_default_0512_01", "effects_0406_ruby"}) {
        const WaveThemePreset* preset = find_wave_theme_preset(theme_name);
        require(preset != nullptr, "preset found");
        const WaveSceneFrame frame = build_wave_scene_frame(*preset, 960, 544);

        expect_finite_range(frame.clear_color.data(), frame.clear_color.size(), "clear");
        expect_finite_range(frame.position.vertex_data.data(), frame.position.vertex_data.size(), "pos.v");
        expect_finite_range(frame.position.fragment_data.data(), frame.position.fragment_data.size(), "pos.f");
        expect_finite_range(frame.grid_normal.vertex_data.data(), frame.grid_normal.vertex_data.size(), "nor.v");
        expect_finite_range(frame.grid_normal.fragment_data.data(), frame.grid_normal.fragment_data.size(), "nor.f");
        expect_finite_range(frame.lighting.vertex_data.data(), frame.lighting.vertex_data.size(), "lit.v");
        expect_finite_range(frame.lighting.fragment_data.data(), frame.lighting.fragment_data.size(), "lit.f");
        expect_finite_range(frame.fliptri.vertex_data.data(), frame.fliptri.vertex_data.size(), "flip.v");
        expect_finite_range(frame.fliptri.fragment_data.data(), frame.fliptri.fragment_data.size(), "flip.f");
        expect_finite_range(frame.surface.vertex_data.data(), frame.surface.vertex_data.size(), "surf.v");
        expect_finite_range(frame.skydome.vertex_data.data(), frame.skydome.vertex_data.size(), "sky.v");

        expect_near(frame.position.fragment_data[28], kWavePerspHackX, 1e-6f, "decay.x");
        expect_near(frame.position.fragment_data[30], -1.0f, 1e-6f, "decay.z");
        expect_near(frame.fliptri.fragment_data[6], 8.0f, 1e-6f, "fliptri.size.x");
        expect_near(frame.fliptri.fragment_data[7], 8.0f, 1e-6f, "fliptri.size.y");
        expect_near(frame.surface.vertex_data[34], kWaveWorldScaleHack, 1e-6f, "surf.scale");
        require(frame.surface_raster.depth_function == 0x0203u, "surf.depth_func");
        require(frame.surface_raster.cull_face == 0x0405u, "surf.cull_back");
        require(frame.surface_raster.depth_test && frame.surface_raster.depth_write && frame.surface_raster.cull,
            "surf.flags");

        // Verify SkyDome runtime overrides (811a674e.c L960-1026).
        expect_near(frame.skydome.vertex_data[16 + 0], preset->sky.radius * preset->sky.xscale, 1e-5f, "sky.m0");
        expect_near(frame.skydome.vertex_data[16 + 5], preset->sky.radius, 1e-5f, "sky.m5");
        expect_near(frame.skydome.vertex_data[16 + 10], preset->sky.radius, 1e-5f, "sky.m10");
        expect_near(frame.skydome.vertex_data[80], preset->sky.sky_blend_start, 1e-6f, "sky.blend0");
        expect_near(frame.skydome.vertex_data[81],
            preset->sky.sky_blend_start + preset->sky.sky_blend_range, 1e-6f, "sky.blend1");
        expect_near(frame.skydome.vertex_data[83],
            preset->sky.grad_distortion_scale * 10.0f, 1e-5f, "sky.dist.grad");
        expect_near(frame.skydome.vertex_data[84],
            preset->sky.horizon_distortion_scale * 10.0f, 1e-5f, "sky.dist.hor");
    }

    std::printf("PASS wave_scene_test (ortho, dir, cam rest/flat/roll, packs, default+ruby frames)\n");
    return 0;
}
