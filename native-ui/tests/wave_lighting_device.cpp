#include <emucorev/paf/wave_lighting_renderer.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

struct EglContext {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;

    EglContext() {
        EGLint major = 0, minor = 0, count = 0;
        if (!eglInitialize(display, &major, &minor)) {
            throw std::runtime_error("EGL initialization failed");
        }
        constexpr EGLint cfg_attrs[]{
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE};
        EGLConfig cfg{};
        if (!eglChooseConfig(display, cfg_attrs, &cfg, 1, &count) || !count) {
            throw std::runtime_error("EGL config selection failed");
        }
        constexpr EGLint pbuf_attrs[]{EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
        surface = eglCreatePbufferSurface(display, cfg, pbuf_attrs);
        constexpr EGLint ctx_attrs[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, cfg, EGL_NO_CONTEXT, ctx_attrs);
        if (!eglMakeCurrent(display, surface, surface, context)) {
            throw std::runtime_error("EGL make current failed");
        }
    }
    ~EglContext() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (display != EGL_NO_DISPLAY) eglTerminate(display);
    }
};

struct Vec3 {
    float x, y, z;
};

Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalize(Vec3 v) {
    const float inv = 1.0f / std::sqrt(dot(v, v));
    return v * inv;
}
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }
float safe_pow(float base, float exp_v) {
    if (exp_v == 0.0f) return 1.0f;
    return (base > 0.0f) ? std::exp2(std::log2(base) * exp_v) : 0.0f;
}

std::uint16_t float_to_half(float v) {
    const std::uint32_t bits = *reinterpret_cast<const std::uint32_t*>(&v);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const int exp_v = static_cast<int>((bits >> 23) & 0xffu) - 127 + 15;
    const std::uint32_t mant = (bits >> 13) & 0x3ffu;
    if (exp_v <= 0) return static_cast<std::uint16_t>(sign);
    if (exp_v >= 31) return static_cast<std::uint16_t>(sign | 0x7c00u);
    return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(exp_v) << 10) | mant);
}

std::array<float, 20> ortho_quad_vertex_data() {
    // Ortho(0, 1, 0, 1, -1, 1) column-major followed by UV scale/bias (0,0,1,1)
    return {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 1.0f,
    };
}

Vec3 sample_bilinear_rgba(const std::vector<float>& img, int w, int h, float u, float v) {
    const float fx = std::clamp(u, 0.0f, 1.0f) * w - 0.5f;
    const float fy = std::clamp(v, 0.0f, 1.0f) * h - 0.5f;
    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, h - 1);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = std::clamp(fx - x0, 0.0f, 1.0f);
    const float ty = std::clamp(fy - y0, 0.0f, 1.0f);
    auto at = [&](int px, int py) -> Vec3 {
        const std::size_t off = static_cast<std::size_t>((py * w + px) * 4);
        return {img[off], img[off + 1], img[off + 2]};
    };
    const Vec3 a = at(x0, y0) * (1.0f - tx) + at(x1, y0) * tx;
    const Vec3 b = at(x0, y1) * (1.0f - tx) + at(x1, y1) * tx;
    return a * (1.0f - ty) + b * ty;
}

} // namespace

int main() {
    try {
        EglContext egl;
        constexpr int W = 64, H = 64;

        // Populate synthetic PosTex (RGBA16F) and NorTex (RGBA8).
        std::vector<float> pos_f32(W * H * 4);
        std::vector<std::uint16_t> pos_f16(W * H * 4);
        std::vector<float> nor_f32(W * H * 4);
        std::vector<std::uint8_t> nor_u8(W * H * 4);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float u = static_cast<float>(x) / 63.0f;
                const float v = static_cast<float>(y) / 63.0f;
                const std::size_t o = static_cast<std::size_t>((y * W + x) * 4);
                pos_f32[o + 0] = (u - 0.5f) * 3.0f;
                pos_f32[o + 1] = (v - 0.5f) * 2.0f;
                pos_f32[o + 2] = 0.25f * std::sin(u * 4.0f) + 0.15f * std::cos(v * 3.0f);
                pos_f32[o + 3] = 1.0f;
                for (int c = 0; c < 4; ++c) pos_f16[o + c] = float_to_half(pos_f32[o + c]);

                const Vec3 n = normalize({0.35f * std::cos(u * 5.0f), 0.3f * std::sin(v * 4.0f), 1.0f});
                nor_u8[o + 0] = static_cast<std::uint8_t>(std::lround((n.x * 0.5f + 0.5f) * 255.0f));
                nor_u8[o + 1] = static_cast<std::uint8_t>(std::lround((n.y * 0.5f + 0.5f) * 255.0f));
                nor_u8[o + 2] = static_cast<std::uint8_t>(std::lround((n.z * 0.5f + 0.5f) * 255.0f));
                nor_u8[o + 3] = 255u;
                for (int c = 0; c < 4; ++c) nor_f32[o + c] = nor_u8[o + c] / 255.0f;
            }
        }

        GLuint pos_tex = 0, nor_tex = 0, lit_tex = 0, flip_out_tex = 0;
        GLuint lit_fbo = 0, flip_fbo = 0;
        glGenTextures(1, &pos_tex);
        glBindTexture(GL_TEXTURE_2D, pos_tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, W, H);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_HALF_FLOAT, pos_f16.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenTextures(1, &nor_tex);
        glBindTexture(GL_TEXTURE_2D, nor_tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, nor_u8.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenTextures(1, &lit_tex);
        glBindTexture(GL_TEXTURE_2D, lit_tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &lit_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, lit_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, lit_tex, 0);

        glGenTextures(1, &flip_out_tex);
        glBindTexture(GL_TEXTURE_2D, flip_out_tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, W, H);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &flip_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, flip_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, flip_out_tex, 0);

        emucorev::paf::WaveLightingParameters lp{};
        lp.vertex_data = ortho_quad_vertex_data();
        lp.viewport_state = {1.f, -1.f, 1.f, 1.f,  1.f, float(W), float(H), 0.5f, 0.5f, 0.f, 0.f, 0.f};
        lp.fragment_state = {0.f, 0.f, 0.f, 0.f,  1.f, 0.f, 0.f, 1.f / W, 1.f / H, 0.f, 16.f, 0.f};
        auto& f = lp.fragment_data;
        // ModelMatrix (rotation around X + translation)
        const float ca = std::cos(0.4f), sa = std::sin(0.4f);
        f[0] = 1.f;  f[1] = 0.f;  f[2] = 0.f;  f[3] = 0.f;
        f[4] = 0.f;  f[5] = ca;   f[6] = sa;   f[7] = 0.f;
        f[8] = 0.f;  f[9] = -sa;  f[10] = ca;  f[11] = 0.f;
        f[12] = 0.f; f[13] = -0.2f; f[14] = -2.7f; f[15] = 1.f;
        // FogPack
        f[16] = 0.14f; f[17] = 0.61f; f[18] = 0.88f; f[19] = 1.0f;
        f[20] = 1.2f;  f[21] = 1.35f; f[22] = 2.6f;  f[23] = 1.0f;
        f[24] = 0.10f; f[25] = 0.43f; f[26] = 0.72f; f[27] = 1.0f;
        f[28] = 2.51f; f[29] = 2.41f;
        // MaterialPack
        f[32] = 0.15f; f[33] = 0.16f; f[34] = 0.46f; f[35] = 1.0f;
        f[36] = 0.60f;
        // PointLightPack2 (offset 48)
        f[48] = 0.78f; f[49] = 0.79f; f[50] = 0.00f; f[51] = 1.0f;
        f[56] = 0.57f; f[57] = 0.125f; f[58] = 0.69f; f[59] = 1.0f;
        f[60] = 0.92f; f[61] = 0.92f; f[62] = 0.92f; f[63] = 0.92f;
        // HalfPixel + EyePos
        f[64] = 0.5f / float(W); f[65] = 0.5f / float(H);
        f[66] = 0.0f; f[67] = 0.0f; f[68] = 2.26f;
        // PointLightPack1 (offset 70)
        f[70] = -0.9f; f[71] = 0.01f; f[72] = 0.65f; f[73] = 1.0f;
        // EdgeColor, CenterColor, EdgeFade
        f[86] = 0.8f;  f[87] = 0.9f;  f[88] = 1.0f;  f[89] = 2.2f;
        f[90] = 0.25f; f[91] = 0.55f; f[92] = 0.85f; f[93] = 1.5f;
        f[94] = 0.15f; f[95] = 0.20f;

        emucorev::paf::WaveLightingRenderer lighting;
        lighting.draw(pos_tex, nor_tex, lit_fbo, W, H, emucorev::paf::kPafTextureOpsQuadVertices, lp);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GL error in WaveLightingRenderer");

        std::vector<std::uint8_t> gpu_lit(W * H * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, lit_fbo);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, gpu_lit.data());

        // CPU oracle of wave_lit_tex.fp (0x812e4f4c)
        int max_lit_err = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float tc_x = (x + 0.5f) / float(W);
                const float v_tc_y = 1.0f - (y + 0.5f) / float(H);
                const float tc_y = 1.0f - v_tc_y;
                const float uv_x = tc_x - f[64];
                const float uv_y = tc_y - f[65];
                const Vec3 pos_local = sample_bilinear_rgba(pos_f32, W, H, uv_x, uv_y);
                const Vec3 nor_raw = sample_bilinear_rgba(nor_f32, W, H, uv_x, uv_y);
                const Vec3 nor_local = normalize(nor_raw * 2.0f - Vec3{1.f, 1.f, 1.f});
                const Vec3 m0{f[0], f[1], f[2]}, m1{f[4], f[5], f[6]}, m2{f[8], f[9], f[10]};
                const Vec3 m3{f[12], f[13], f[14]};
                auto mul_m = [&](Vec3 v) -> Vec3 {
                    return m0 * v.x + m1 * v.y + m2 * v.z;
                };
                const Vec3 N = normalize(mul_m(nor_local));
                const Vec3 P = mul_m(pos_local) + m3;
                const Vec3 V_vec = Vec3{f[66], f[67], f[68]} - P;
                const float dist = std::sqrt(dot(V_vec, V_vec));
                const Vec3 V = V_vec * (1.0f / dist);
                const float NdotV = clamp01(dot(N, V));
                const Vec3 L = normalize(Vec3{f[70], f[71], f[72]} - P);

                const float ex = smooth01(clamp01((std::abs(tc_x * 2.f - 1.f) - f[94]) / (1.f - f[94])));
                const float ey = smooth01(clamp01((std::abs(tc_y * 2.f - 1.f) - f[95]) / (1.f - f[95])));
                const float edge_vis = (1.f - ey) * (1.f - ex);

                const Vec3 fog_col0{f[16], f[17], f[18]}, fog_dens{f[20], f[21], f[22]}, fog_sun{f[24], f[25], f[26]};
                const Vec3 fog_s{
                    smooth01(clamp01((dist - f[29]) / (fog_dens.x * f[28]))),
                    smooth01(clamp01((dist - f[29]) / (fog_dens.y * f[28]))),
                    smooth01(clamp01((dist - f[29]) / (fog_dens.z * f[28]))),
                };
                const float ldv = std::max(dot(L, V), 0.0f);
                const float sun_f = (ldv > 0.0f) ? std::exp2(std::log2(ldv) * 8.0f) : 0.0f;
                const Vec3 fog_col = fog_col0 * (1.0f - sun_f) + fog_sun * sun_f;

                const float wrap = dot(N, L) * 0.5f + 0.5f;
                const Vec3 lit = Vec3{f[56], f[57], f[58]} * (f[36] * wrap * wrap) + Vec3{f[48], f[49], f[50]};
                const float cf = safe_pow(NdotV, f[93]);
                const float ef = safe_pow(1.0f - NdotV, f[89]);
                const Vec3 surf = lit * Vec3{f[32], f[33], f[34]} + Vec3{f[86], f[87], f[88]} * ef + Vec3{f[90], f[91], f[92]} * cf;
                const Vec3 fogged{
                    surf.x + (fog_col.x - surf.x) * fog_s.x,
                    surf.y + (fog_col.y - surf.y) * fog_s.y,
                    surf.z + (fog_col.z - surf.z) * fog_s.z,
                };
                const Vec3 out_rgb = fog_col0 * (1.0f - edge_vis) + fogged * edge_vis;
                const std::array<int, 4> ref_u8{
                    static_cast<int>(std::lround(clamp01(out_rgb.x) * 255.0f)),
                    static_cast<int>(std::lround(clamp01(out_rgb.y) * 255.0f)),
                    static_cast<int>(std::lround(clamp01(out_rgb.z) * 255.0f)),
                    255};
                const std::size_t o = static_cast<std::size_t>((y * W + x) * 4);
                for (int c = 0; c < 4; ++c) {
                    const int err = std::abs(static_cast<int>(gpu_lit[o + c]) - ref_u8[c]);
                    if (err > max_lit_err) {
                        max_lit_err = err;
                        if (err > 2) {
                            std::cout << "  diff at (" << x << "," << y << ") ch=" << c
                                      << " gpu=" << static_cast<int>(gpu_lit[o + c])
                                      << " ref=" << ref_u8[c] << '\n';
                        }
                    }
                }
            }
        }

        // Now test WaveFliptriFilter (0x812ff950) on lit_tex using V-flipped quad.
        constexpr std::array<emucorev::paf::WaveGridNormalVertex, 4> kFlippedQuad{{
            {{{0.0f, 1.0f, 0.0f, 0.0f}}},
            {{{0.0f, 0.0f, 0.0f, 1.0f}}},
            {{{1.0f, 1.0f, 1.0f, 0.0f}}},
            {{{1.0f, 0.0f, 1.0f, 1.0f}}},
        }};
        emucorev::paf::WaveFliptriParameters fp{};
        fp.vertex_data = ortho_quad_vertex_data();
        const float blur = 0.5f, inv = 1.0f / float(W), half = 0.5f * inv;
        fp.fragment_data = {
            0.0f - half, inv - half,
            (1.0f - blur) * inv - half, 0.0f - half,
            inv - half, blur * inv - half,
            8.0f, 8.0f};
        fp.viewport_state = {1.f, -1.f, 1.f, 1.f,  1.f, float(W), float(H), 0.5f, 0.5f, 0.f, 0.f, 0.f};
        fp.fragment_state = {0.f, 0.f, 0.f, 0.f,  1.f, 0.f, 0.f, 1.f / W, 1.f / H, 0.f, 16.f, 0.f};

        emucorev::paf::WaveFliptriFilter fliptri;
        fliptri.draw(lit_tex, flip_fbo, W, H, kFlippedQuad, fp);
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GL error in WaveFliptriFilter");

        std::vector<std::uint8_t> gpu_flip(W * H * 4);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, flip_fbo);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, gpu_flip.data());

        std::vector<float> lit_f32(W * H * 4);
        for (std::size_t i = 0; i < gpu_lit.size(); ++i) lit_f32[i] = gpu_lit[i] / 255.0f;

        int max_flip_err = 0;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const float tc_x = (x + 0.5f) / float(W);
                const float tc_y = (y + 0.5f) / float(H);
                const float sx = (x & 1) ? 1.0f : -1.0f;
                const float sy = (y & 1) ? 1.0f : -1.0f;
                const Vec3 s0 = sample_bilinear_rgba(lit_f32, W, H, tc_x + sx * fp.fragment_data[0], tc_y + sy * fp.fragment_data[1]);
                const Vec3 s1 = sample_bilinear_rgba(lit_f32, W, H, tc_x + sx * fp.fragment_data[2], tc_y + sy * fp.fragment_data[3]);
                const Vec3 s2 = sample_bilinear_rgba(lit_f32, W, H, tc_x + sx * fp.fragment_data[4], tc_y + sy * fp.fragment_data[5]);
                const Vec3 avg = (s0 + s1 + s2) * 0.333251953125f;
                const std::array<int, 3> ref_u8{
                    static_cast<int>(std::lround(clamp01(avg.x) * 255.0f)),
                    static_cast<int>(std::lround(clamp01(avg.y) * 255.0f)),
                    static_cast<int>(std::lround(clamp01(avg.z) * 255.0f))};
                const std::size_t o = static_cast<std::size_t>((y * W + x) * 4);
                for (int c = 0; c < 3; ++c) {
                    max_flip_err = std::max(max_flip_err, std::abs(static_cast<int>(gpu_flip[o + c]) - ref_u8[c]));
                }
            }
        }

        glDeleteFramebuffers(1, &lit_fbo);
        glDeleteFramebuffers(1, &flip_fbo);
        glDeleteTextures(1, &pos_tex);
        glDeleteTextures(1, &nor_tex);
        glDeleteTextures(1, &lit_tex);
        glDeleteTextures(1, &flip_out_tex);

        std::cout << "Wave lighting check: max_lit_u8_err=" << max_lit_err
                  << " max_flip_u8_err=" << max_flip_err << '\n';
        if (max_lit_err > 1 || max_flip_err > 1) {
            throw std::runtime_error("GPU output diverged from firmware lighting/fliptri reference");
        }
        std::cout << "PASS wave_lighting_test\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
