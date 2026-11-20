#include <emucorev/paf/wave_scene.h>

#include <algorithm>
#include <bit>
#include <cmath>

namespace emucorev::paf {

namespace {

constexpr float kWaveHalfFovTanBits = std::bit_cast<float>(0x3ecd87acu);
constexpr float kWavePitchEpsilon = std::bit_cast<float>(0x3c8efa35u); // 0.017453292f (1 deg)

WaveVec3 vec3_cross(const WaveVec3& a, const WaveVec3& b) noexcept {
    return {
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    };
}

float vec3_dot(const WaveVec3& a, const WaveVec3& b) noexcept {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

WaveVec3 vec3_normalize(const WaveVec3& v) noexcept {
    const float len2 = vec3_dot(v, v);
    if (len2 <= 1e-12f) return {0.0f, 0.0f, 0.0f};
    const float inv = 1.0f / std::sqrt(len2);
    return {v[0] * inv, v[1] * inv, v[2] * inv};
}

WaveMat4 make_translation(float x, float y, float z) noexcept {
    WaveMat4 m = wave_mat4_identity();
    m[12] = x;
    m[13] = y;
    m[14] = z;
    return m;
}

WaveMat4 make_scale(float sx, float sy, float sz) noexcept {
    WaveMat4 m{};
    m[0] = sx;
    m[5] = sy;
    m[10] = sz;
    m[15] = 1.0f;
    return m;
}

} // namespace

WaveMat4 wave_mat4_identity() noexcept {
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

WaveMat4 wave_mat4_mul(const WaveMat4& a, const WaveMat4& b) noexcept {
    WaveMat4 out{};
    for (int col = 0; col < 4; ++col) {
        const int bc = col * 4;
        for (int row = 0; row < 4; ++row) {
            out[bc + row] =
                a[row] * b[bc] +
                a[4 + row] * b[bc + 1] +
                a[8 + row] * b[bc + 2] +
                a[12 + row] * b[bc + 3];
        }
    }
    return out;
}

WaveVec4 wave_mat4_mul_vec4(const WaveMat4& m, const WaveVec4& v) noexcept {
    WaveVec4 out{};
    for (int row = 0; row < 4; ++row) {
        out[row] =
            m[row] * v[0] +
            m[4 + row] * v[1] +
            m[8 + row] * v[2] +
            m[12 + row] * v[3];
    }
    return out;
}

WaveMat4 wave_mat4_inverse_rigid(const WaveMat4& m) noexcept {
    WaveMat4 out = wave_mat4_identity();
    // Transpose the upper-left 3x3 rotation block.
    for (int col = 0; col < 3; ++col) {
        for (int row = 0; row < 3; ++row) {
            out[col * 4 + row] = m[row * 4 + col];
        }
    }
    const float tx = m[12];
    const float ty = m[13];
    const float tz = m[14];
    out[12] = -(out[0] * tx + out[4] * ty + out[8] * tz);
    out[13] = -(out[1] * tx + out[5] * ty + out[9] * tz);
    out[14] = -(out[2] * tx + out[6] * ty + out[10] * tz);
    out[15] = 1.0f;
    return out;
}

WaveMat4 wave_mat4_frustum(float left, float right, float bottom, float top,
    float near_z, float far_z) noexcept {
    const float inv_w = 1.0f / (right - left);
    const float inv_h = 1.0f / (top - bottom);
    const float inv_d = 1.0f / (far_z - near_z);
    WaveMat4 out{};
    out[0] = (2.0f * near_z) * inv_w;
    out[5] = (2.0f * near_z) * inv_h;
    out[8] = (right + left) * inv_w;
    out[9] = (top + bottom) * inv_h;
    out[10] = -(far_z + near_z) * inv_d;
    out[11] = -1.0f;
    out[14] = -(2.0f * far_z * near_z) * inv_d;
    return out;
}

WaveMat4 wave_mat4_ortho(float left, float right, float bottom, float top,
    float near_z, float far_z) noexcept {
    const float inv_w = 1.0f / (right - left);
    const float inv_h = 1.0f / (top - bottom);
    const float inv_d = 1.0f / (far_z - near_z);
    WaveMat4 out{};
    out[0] = 2.0f * inv_w;
    out[5] = 2.0f * inv_h;
    out[10] = -2.0f * inv_d;
    out[12] = -(right + left) * inv_w;
    out[13] = -(top + bottom) * inv_h;
    out[14] = -(far_z + near_z) * inv_d;
    out[15] = 1.0f;
    return out;
}

WaveMat4 wave_quat_to_matrix(const WaveVec4& q) noexcept {
    const float x = q[0];
    const float y = q[1];
    const float z = q[2];
    const float w = q[3];
    const float norm2 = x * x + y * y + z * z + w * w;
    if (norm2 <= 1e-4f) return wave_mat4_identity();

    const float s = 2.0f / norm2;
    const float xx = x * x * s;
    const float yy = y * y * s;
    const float zz = z * z * s;
    const float xy = x * y * s;
    const float xz = x * z * s;
    const float yz = y * z * s;
    const float wx = w * x * s;
    const float wy = w * y * s;
    const float wz = w * z * s;

    return {
        1.0f - (yy + zz), xy + wz,          xz - wy,          0.0f,
        xy - wz,          1.0f - (xx + zz), yz + wx,          0.0f,
        xz + wy,          yz - wx,          1.0f - (xx + yy), 0.0f,
        0.0f,             0.0f,             0.0f,             1.0f,
    };
}

WaveMat4 wave_mat4_look_at(const WaveVec3& eye, const WaveVec3& target,
    const WaveVec3& up) noexcept {
    const WaveVec3 forward{eye[0] - target[0], eye[1] - target[1], eye[2] - target[2]};
    const WaveVec3 z = vec3_normalize(forward);
    const WaveVec3 x = vec3_normalize(vec3_cross(up, z));
    const WaveVec3 y = vec3_cross(z, x);

    return {
        x[0], x[1], x[2], 0.0f,
        y[0], y[1], y[2], 0.0f,
        z[0], z[1], z[2], 0.0f,
        eye[0], eye[1], eye[2], 1.0f,
    };
}

WaveVec4 wave_pitch_roll_direction(float delta_pitch, float roll) noexcept {
    const float sr = std::sin(roll);
    const float cr = std::cos(roll);
    const float sp = std::sin(delta_pitch);
    const float cp = std::cos(delta_pitch);
    return {sr, -sp * cr, cp * cr, 0.0f};
}

WaveCameraMatrices build_wave_camera(const WaveCameraPreset& cam,
    const WaveInstancePreset& wave, float aspect, float pitch_rad, float roll_rad) noexcept {
    const float safe_aspect = aspect > 1e-4f ? aspect : (960.0f / 544.0f);
    const float tan_half = std::tan(kWaveHalfFovTanBits);
    const float right = tan_half * kWaveCameraNear;
    const float top = right / safe_aspect;

    WaveCameraMatrices result{};
    result.projection = wave_mat4_frustum(-right, right, -top, top,
        kWaveCameraNear, kWaveCameraFar);

    const float cam_z = 480.0f / tan_half;
    result.base_world = make_translation(0.0f, 0.0f, cam_z);

    // FUN_810a6bb0 defaults pitch to pitch_rest when quiescent.
    const float p_max = cam.pitch_max;
    const float p_min = std::max(cam.pitch_min, p_max);
    const float p_rest = std::clamp(cam.pitch_rest, p_max, p_min);
    const float raw_pitch = std::isnan(pitch_rad) ? p_rest : pitch_rad;
    const float p = std::clamp(raw_pitch, p_max, p_min);
    const float delta_pitch = p - p_rest;

    float pitch_up = 0.0f;
    if (p_rest + kWavePitchEpsilon < p_min) {
        pitch_up = (std::clamp(p, p_rest, p_min) - p_rest) / (p_min - p_rest);
    }
    float pitch_down_cubed = 0.0f;
    if (p_max < p_rest) {
        const float d = (std::clamp(p, p_max, p_rest) - p_rest) / (p_max - p_rest);
        pitch_down_cubed = d * d * d;
    }

    const float abs_roll = std::abs(roll_rad);
    const float roll_lo = cam.roll_max - cam.roll_max_zone;
    const float roll_hi = cam.roll_max + cam.roll_max_zone;
    float clamped_roll_mag = abs_roll;
    if (abs_roll >= roll_lo) {
        if (abs_roll > roll_hi || cam.roll_max_zone <= 1e-6f) {
            clamped_roll_mag = cam.roll_max;
        } else {
            const float t = (abs_roll - roll_lo) / (2.0f * cam.roll_max_zone);
            // Verified against FUN_810a6bb0 in Unicorn: smooth Hermite ease in [lo, hi].
            clamped_roll_mag = roll_lo + (2.0f * t - t * t) * cam.roll_max_zone;
        }
    }
    const float clamped_roll = std::copysign(clamped_roll_mag, roll_rad);

    const WaveVec4 dir = wave_pitch_roll_direction(delta_pitch, clamped_roll);
    const WaveVec3 up = vec3_normalize({0.0f, dir[2], -dir[1]});
    const WaveMat4 look = wave_mat4_look_at({0.0f, 0.0f, 0.0f}, {-dir[0], -dir[1], -dir[2]}, up);
    const WaveMat4 inv_look = wave_mat4_inverse_rigid(look);

    // FUN_810a6bb0 (0x810a704c vbif) zeroes Y so tilt orbits in the horizontal plane.
    const WaveVec3 pivot{
        wave.translation[0] * kWaveWorldScaleHack * pitch_up,
        0.0f,
        wave.translation[2] * kWaveWorldScaleHack * pitch_up,
    };
    const WaveMat4 t_pivot = make_translation(pivot[0], pivot[1], pivot[2]);
    const WaveMat4 t_inv_pivot = make_translation(-pivot[0], -pivot[1], -pivot[2]);
    const WaveMat4 t_zoom = make_translation(0.0f, 0.0f, cam_z * pitch_up * cam.zoom);

    const WaveMat4 zoom_base = wave_mat4_mul(t_zoom, result.base_world);
    const WaveMat4 pivoted = wave_mat4_mul(t_inv_pivot, zoom_base);
    const WaveMat4 rotated = wave_mat4_mul(inv_look, pivoted);
    result.world = wave_mat4_mul(t_pivot, rotated);
    result.view = wave_mat4_inverse_rigid(result.world);
    result.view_projection = wave_mat4_mul(result.projection, result.view);
    result.pitch_up = pitch_up;
    result.pitch_down_cubed = pitch_down_cubed;
    return result;
}

WaveMat4 wave_model_matrix(const WaveInstancePreset& wave) noexcept {
    WaveMat4 m = wave_quat_to_matrix(wave.rotation);
    m[12] = wave.translation[0];
    m[13] = wave.translation[1];
    m[14] = wave.translation[2];
    m[15] = 1.0f;
    return m;
}

WaveVec4 wave_cross_section_cubic(const WaveInstancePreset& wave) noexcept {
    const float p0 = wave.cross_section[0];
    const float p1 = wave.cross_section[1];
    const float m0 = wave.cross_section[2];
    const float m1 = wave.cross_section[3];
    return {
        2.0f * p0 - 2.0f * p1 + m0 + m1,
        -3.0f * p0 + 3.0f * p1 - 2.0f * m0 - m1,
        m0,
        p0,
    };
}

WaveVec4 wave_decay_uniform(const WaveInstancePreset& wave, const WaveMat4& view_model) noexcept {
    const float sy = wave.scale[1] * wave.global_scale;
    const float sz = wave.scale[2] * wave.global_scale;
    const WaveVec4 pt{0.0f, -sy, sz * (2.0f * wave.cross_section[0]), 1.0f};
    const WaveVec4 vm_pt = wave_mat4_mul_vec4(view_model, pt);
    return {kWavePerspHackX, 0.0f, -1.0f, vm_pt[2]};
}

WaveVec3 wave_spherical_position(float theta, float phi, float distance) noexcept {
    const float st = std::sin(theta);
    const float ct = std::cos(theta);
    const float sp = std::sin(phi);
    const float cp = std::cos(phi);
    return {distance * st * sp, distance * ct, distance * st * cp};
}

WaveFogPreset wave_blend_fog(const WaveFogPreset& fog0, const WaveFogPreset& fog1,
    float t) noexcept {
    const auto lerp = [t](float a, float b) noexcept { return a + (b - a) * t; };
    WaveFogPreset out{};
    for (int i = 0; i < 3; ++i) out.color[i] = lerp(fog0.color[i], fog1.color[i]);
    for (int i = 0; i < 4; ++i) {
        out.density[i] = lerp(fog0.density[i], fog1.density[i]);
        out.light_color[i] = lerp(fog0.light_color[i], fog1.light_color[i]);
    }
    out.d_scale = lerp(fog0.d_scale, fog1.d_scale);
    out.d_offset = lerp(fog0.d_offset, fog1.d_offset);
    return out;
}

std::array<float, 16> wave_pack_fog(const WaveFogPreset& fog) noexcept {
    std::array<float, 16> out{};
    out[0] = fog.color[0];
    out[1] = fog.color[1];
    out[2] = fog.color[2];
    out[3] = 1.0f;
    out[4] = fog.density[0] * fog.density[3];
    out[5] = fog.density[1] * fog.density[3];
    out[6] = fog.density[2] * fog.density[3];
    out[7] = 1.0f;
    for (int i = 0; i < 3; ++i) {
        out[8 + i] = fog.color[i] + (fog.light_color[i] - fog.color[i]) * fog.light_color[3];
    }
    out[11] = 1.0f;
    out[12] = fog.d_scale;
    out[13] = fog.d_offset;
    return out;
}

std::array<float, 16> wave_pack_material(const WaveMaterialPreset& material) noexcept {
    return {
        material.color[0], material.color[1], material.color[2], material.color[3],
        material.diffuse_coeff, material.specular_coeff, material.specular_power, 0.0f,
        material.fresnel_front, material.fresnel_control, material.fresnel_coeff, 0.0f,
        material.refraction_coeff, 0.0f, material.refraction_amount, material.second_reflection,
    };
}

std::array<float, 16> wave_pack_light2(const WaveLightPreset& light) noexcept {
    return {
        light.ambient_color[0], light.ambient_color[1], light.ambient_color[2], 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        light.diffuse_color[0], light.diffuse_color[1], light.diffuse_color[2], 1.0f,
        light.fade, light.fade, light.fade, light.fade,
    };
}

std::array<float, 16> wave_pack_light1(const WaveLightPreset& light) noexcept {
    const WaveVec3 pos = wave_spherical_position(light.theta_phi[0], light.theta_phi[1],
        light.distance);
    const bool zero_attn = (light.attn[0] == 0.0f && light.attn[1] == 0.0f && light.attn[2] == 0.0f);
    std::array<float, 16> out{};
    out[0] = pos[0];
    out[1] = pos[1];
    out[2] = pos[2];
    out[3] = 1.0f;
    out[4] = zero_attn ? 1.0f : light.attn[0];
    out[5] = zero_attn ? 0.0f : light.attn[1];
    out[6] = zero_attn ? 0.0f : light.attn[2];
    return out;
}

WaveSceneFrame build_wave_scene_frame(const WaveThemePreset& theme,
    std::uint32_t target_width, std::uint32_t target_height,
    float pitch_rad, float roll_rad) noexcept {
    const float w = target_width > 0 ? static_cast<float>(target_width) : 960.0f;
    const float h = target_height > 0 ? static_cast<float>(target_height) : 544.0f;
    const float offscreen_f = static_cast<float>(kWaveOffscreenSize);

    const std::array<float, 12> offscreen_vp = pack_wave_gxm_viewport(
        WaveGxmViewportUniforms{{1.0f, -1.0f, 1.0f, 1.0f}, 1.0f, offscreen_f, offscreen_f, 0.5f, 0.5f});
    const std::array<float, 12> offscreen_fs = pack_wave_gxm_fragment(
        WaveGxmFragmentUniforms{{{0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            1.0f / offscreen_f, 1.0f / offscreen_f, 0.0f, 16.0f}}});

    const std::array<float, 12> main_vp = pack_wave_gxm_viewport(
        WaveGxmViewportUniforms{{1.0f, 1.0f, 1.0f, 1.0f}, 1.0f, w, h, 0.5f, 0.5f});
    const std::array<float, 12> main_fs = pack_wave_gxm_fragment(
        WaveGxmFragmentUniforms{{{0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            1.0f / w, 1.0f / h, 0.0f, 16.0f}}});

    const WaveMat4 ortho01 = wave_mat4_ortho(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
    const WaveCameraMatrices cam = build_wave_camera(theme.camera, theme.wave, w / h,
        pitch_rad, roll_rad);
    const WaveMat4 model = wave_model_matrix(theme.wave);

    WaveMat4 base_world_unscaled = cam.base_world;
    base_world_unscaled[12] /= kWaveWorldScaleHack;
    base_world_unscaled[13] /= kWaveWorldScaleHack;
    base_world_unscaled[14] /= kWaveWorldScaleHack;
    const WaveMat4 view_unscaled = wave_mat4_inverse_rigid(base_world_unscaled);
    const WaveMat4 view_model = wave_mat4_mul(view_unscaled, model);

    const WaveVec3 eye_pos{
        cam.world[12] / kWaveWorldScaleHack,
        cam.world[13] / kWaveWorldScaleHack,
        cam.world[14] / kWaveWorldScaleHack,
    };

    const WaveFogPreset active_fog = wave_blend_fog(theme.fog0, theme.fog1, cam.pitch_up);

    WaveSceneFrame frame{};
    frame.clear_color = {active_fog.color[0], active_fog.color[1], active_fog.color[2], 1.0f};

    // Pass 1a: wave_pos.vp / wave_pos.fp
    std::copy(ortho01.begin(), ortho01.end(), frame.position.vertex_data.begin());
    std::copy(view_model.begin(), view_model.end(), frame.position.fragment_data.begin());
    const float distortion = theme.wave.distortion_scale * theme.wave.global_scale;
    frame.position.fragment_data[16] = distortion;
    frame.position.fragment_data[18] = theme.wave.uv_scale;
    frame.position.fragment_data[19] = theme.wave.uv_rotate * kWaveDegToRad;
    const WaveVec4 cubic = wave_cross_section_cubic(theme.wave);
    std::copy(cubic.begin(), cubic.end(), frame.position.fragment_data.begin() + 20);
    frame.position.fragment_data[24] = theme.wave.scale[0] * theme.wave.global_scale;
    frame.position.fragment_data[25] = theme.wave.scale[1] * theme.wave.global_scale;
    frame.position.fragment_data[26] = theme.wave.scale[2] * theme.wave.global_scale;
    const WaveVec4 decay = wave_decay_uniform(theme.wave, view_model);
    std::copy(decay.begin(), decay.end(), frame.position.fragment_data.begin() + 28);
    frame.position.viewport_state = offscreen_vp;
    frame.position.fragment_state = offscreen_fs;

    // Pass 1b: texops.vp / gridNormal8.fp
    std::copy(ortho01.begin(), ortho01.end(), frame.grid_normal.vertex_data.begin());
    frame.grid_normal.vertex_data[16] = 0.0f;
    frame.grid_normal.vertex_data[17] = 0.0f;
    frame.grid_normal.vertex_data[18] = 1.0f;
    frame.grid_normal.vertex_data[19] = 1.0f;
    frame.grid_normal.fragment_data = {1.0f / offscreen_f, 0.0f, 0.0f, -1.0f / offscreen_f};
    frame.grid_normal.viewport_state = offscreen_vp;
    frame.grid_normal.fragment_state = offscreen_fs;

    // Pass 2a: texops.vp / wave_lit_tex.fp
    std::copy(ortho01.begin(), ortho01.end(), frame.lighting.vertex_data.begin());
    frame.lighting.vertex_data[16] = 0.0f;
    frame.lighting.vertex_data[17] = 0.0f;
    frame.lighting.vertex_data[18] = 1.0f;
    frame.lighting.vertex_data[19] = 1.0f;
    std::copy(model.begin(), model.end(), frame.lighting.fragment_data.begin());
    const auto fog_pack = wave_pack_fog(active_fog);
    std::copy(fog_pack.begin(), fog_pack.end(), frame.lighting.fragment_data.begin() + 16);
    const auto mat_pack = wave_pack_material(theme.material);
    std::copy(mat_pack.begin(), mat_pack.end(), frame.lighting.fragment_data.begin() + 32);
    const auto light2_pack = wave_pack_light2(theme.light);
    std::copy(light2_pack.begin(), light2_pack.end(), frame.lighting.fragment_data.begin() + 48);
    frame.lighting.fragment_data[64] = 0.5f / offscreen_f;
    frame.lighting.fragment_data[65] = 0.5f / offscreen_f;
    frame.lighting.fragment_data[66] = eye_pos[0];
    frame.lighting.fragment_data[67] = eye_pos[1];
    frame.lighting.fragment_data[68] = eye_pos[2];
    const auto light1_pack = wave_pack_light1(theme.light);
    std::copy(light1_pack.begin(), light1_pack.end(), frame.lighting.fragment_data.begin() + 70);
    frame.lighting.fragment_data[86] = theme.wave.edge_color[0];
    frame.lighting.fragment_data[87] = theme.wave.edge_color[1];
    frame.lighting.fragment_data[88] = theme.wave.edge_color[2];
    frame.lighting.fragment_data[89] = theme.wave.edge_power;
    frame.lighting.fragment_data[90] = theme.wave.center_color[0];
    frame.lighting.fragment_data[91] = theme.wave.center_color[1];
    frame.lighting.fragment_data[92] = theme.wave.center_color[2];
    frame.lighting.fragment_data[93] = theme.wave.center_power;
    frame.lighting.fragment_data[94] = theme.camera.edge_fog[0];
    frame.lighting.fragment_data[95] = theme.camera.edge_fog[1];
    frame.lighting.viewport_state = offscreen_vp;
    frame.lighting.fragment_state = offscreen_fs;

    // Pass 2b: texops.vp / fliptri.fp
    std::copy(ortho01.begin(), ortho01.end(), frame.fliptri.vertex_data.begin());
    frame.fliptri.vertex_data[16] = 0.0f;
    frame.fliptri.vertex_data[17] = 0.0f;
    frame.fliptri.vertex_data[18] = 1.0f;
    frame.fliptri.vertex_data[19] = 1.0f;
    const float inv_off = 1.0f / offscreen_f;
    frame.fliptri.fragment_data = {
        -0.5f * inv_off,
        0.5f * inv_off,
        (0.5f - kWaveFliptriRadius) * inv_off,
        -0.5f * inv_off,
        0.5f * inv_off,
        (kWaveFliptriRadius - 0.5f) * inv_off,
        kWaveFlipTexSize,
        kWaveFlipTexSize,
    };
    frame.fliptri.viewport_state = offscreen_vp;
    frame.fliptri.fragment_state = offscreen_fs;

    // Pass 4: wave.vp / wave.fp
    std::copy(cam.view_projection.begin(), cam.view_projection.end(),
        frame.surface.vertex_data.begin());
    std::copy(model.begin(), model.end(), frame.surface.vertex_data.begin() + 16);
    frame.surface.vertex_data[32] = 0.5f / offscreen_f;
    frame.surface.vertex_data[33] = 0.5f / offscreen_f;
    frame.surface.vertex_data[34] = kWaveWorldScaleHack;
    frame.surface.viewport_state = main_vp;
    frame.surface.fragment_state = main_fs;
    frame.surface_raster = {0x0203u /*GL_LEQUAL*/, 0x0405u /*GL_BACK*/, true, true, true};

    // Pass 5: skydome.vp / skydome.fp (runtime overrides from 811a674e.c L960-1026)
    WaveSkydomeVertexUniforms sky{};
    apply_wave_sky_preset(sky, theme.sky);
    sky.view_projection = cam.view_projection;
    WaveMat4 sky_model = make_scale(theme.sky.radius * theme.sky.xscale, theme.sky.radius,
        theme.sky.radius);
    sky_model[12] = base_world_unscaled[12];
    sky_model[13] = base_world_unscaled[13];
    sky_model[14] = base_world_unscaled[14];
    sky.model = sky_model;
    sky.wave_model = model;
    sky.world_scale_hack = kWaveWorldScaleHack;
    sky.eye_position = eye_pos;
    sky.sun_position = wave_spherical_position(theme.sky.theta_phi[0], theme.sky.theta_phi[1],
        theme.sky.distance);
    sky.wave_fog_color = {
        frame.clear_color[0], frame.clear_color[1], frame.clear_color[2], theme.sky.wave_fog_alpha};
    sky.color_blend_range = {
        theme.sky.sky_blend_start, theme.sky.sky_blend_start + theme.sky.sky_blend_range};
    sky.distortion_scale = {
        theme.sky.pos_distortion_scale * distortion,
        theme.sky.grad_distortion_scale * 10.0f,
        theme.sky.horizon_distortion_scale * 10.0f,
    };
    frame.skydome.vertex_data = pack_wave_skydome_vertex_uniforms(sky);
    frame.skydome.viewport_state = main_vp;
    frame.skydome.fragment_state = main_fs;
    frame.skydome.blend_enabled = true;

    return frame;
}

} // namespace emucorev::paf
