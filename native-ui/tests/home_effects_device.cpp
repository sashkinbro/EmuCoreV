#include <emucorev/paf/bubble_env_renderer.h>
#include <emucorev/paf/focus_cursor_renderer.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

using namespace emucorev::paf;

void require(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s (glGetError=0x%x)\n", msg, glGetError());
        std::exit(1);
    }
}

struct EglContext {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;

    EglContext() {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        require(display != EGL_NO_DISPLAY, "eglGetDisplay");
        require(eglInitialize(display, nullptr, nullptr) == EGL_TRUE, "eglInitialize");
        const EGLint cfg_attr[]{
            EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
            EGL_NONE};
        EGLConfig config{};
        EGLint count = 0;
        require(eglChooseConfig(display, cfg_attr, &config, 1, &count) == EGL_TRUE && count == 1,
            "eglChooseConfig");
        const EGLint pbuf_attr[]{EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, pbuf_attr);
        require(surface != EGL_NO_SURFACE, "eglCreatePbufferSurface");
        const EGLint ctx_attr[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx_attr);
        require(context != EGL_NO_CONTEXT, "eglCreateContext");
        require(eglMakeCurrent(display, surface, surface, context) == EGL_TRUE, "eglMakeCurrent");
    }

    ~EglContext() {
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            eglTerminate(display);
        }
    }
};

GLuint create_rgba_texture(int width, int height, const std::vector<std::uint8_t>& rgba) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    return tex;
}

std::array<std::uint8_t, 4> sample_pixel(const std::vector<std::uint8_t>& fb, int width, int height,
    int x_screen, int y_screen) {
    // OpenGL glReadPixels Y=0 is bottom; flip screen Y to read bottom-up buffer cleanly.
    const int x = std::clamp(x_screen, 0, width - 1);
    const int y = std::clamp(height - 1 - y_screen, 0, height - 1);
    const std::size_t idx = static_cast<std::size_t>(y * width + x) * 4u;
    return {fb[idx], fb[idx + 1], fb[idx + 2], fb[idx + 3]};
}

} // namespace

int main() {
    EglContext egl;

    // 1. Test BubbleEnvRenderer capture + FBO/viewport state preservation.
    std::vector<std::uint8_t> wave_rgba(64 * 64 * 4u);
    for (int y = 0; y < 64; ++y) {
        // Note OpenGL bottom-up row layout: y=63 is sky, y=0 is bottom trough.
        const float t = static_cast<float>(y) / 63.0f;
        const auto r = static_cast<std::uint8_t>(10.0f + 40.0f * t);
        const auto g = static_cast<std::uint8_t>(40.0f + 130.0f * t);
        const auto b = static_cast<std::uint8_t>(90.0f + 150.0f * t);
        for (int x = 0; x < 64; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * 64 + x) * 4u;
            wave_rgba[idx + 0] = r;
            wave_rgba[idx + 1] = g;
            wave_rgba[idx + 2] = b;
            wave_rgba[idx + 3] = 255;
        }
    }
    const GLuint mock_wave_tex = create_rgba_texture(64, 64, wave_rgba);

    // Prepare caller FBO (256x256) and bind it BEFORE calling BubbleEnvRenderer::capture.
    GLuint target_tex = 0;
    GLuint target_fbo = 0;
    glGenTextures(1, &target_tex);
    glBindTexture(GL_TEXTURE_2D, target_tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 256, 256);
    glGenFramebuffers(1, &target_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target_tex, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "target FBO");
    glViewport(0, 0, 256, 256);

    BubbleEnvRenderer env_renderer;
    env_renderer.capture(mock_wave_tex);

    GLint restored_fbo = -1;
    std::array<GLint, 4> restored_vp{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restored_fbo);
    glGetIntegerv(GL_VIEWPORT, restored_vp.data());
    require(restored_fbo == static_cast<GLint>(target_fbo), "capture must restore caller FBO");
    require(restored_vp[2] == 256 && restored_vp[3] == 256, "capture must restore caller viewport");

    // Read captured 64x64 env texture via scratch FBO.
    GLuint env_read_fbo = 0;
    glGenFramebuffers(1, &env_read_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, env_read_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, env_renderer.texture(), 0);
    std::vector<std::uint8_t> env_fb(64 * 64 * 4u);
    glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, env_fb.data());

    double sum_luma = 0.0;
    double sum_luma_sq = 0.0;
    for (std::size_t i = 0; i < env_fb.size(); i += 4u) {
        const double l = (0.2126 * env_fb[i] + 0.7152 * env_fb[i + 1] + 0.0722 * env_fb[i + 2]) / 255.0;
        sum_luma += l;
        sum_luma_sq += l * l;
    }
    const double mean_luma = sum_luma / (64.0 * 64.0);
    const double std_luma = std::sqrt(std::max(0.0, sum_luma_sq / (64.0 * 64.0) - mean_luma * mean_luma));
    require(mean_luma > 0.12 && mean_luma < 0.90, "env map luma in visible glossy range");
    require(std_luma > 0.015, "env map must vary from center to rim");
    glDeleteFramebuffers(1, &env_read_fbo);

    // 2. Test FocusCursorRenderer with synthetic cf115394-shaped quadrant texture.
    std::vector<std::uint8_t> quad_rgba(64 * 64 * 4u, 0);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            // Mirror cf115394 convention: (0,0) outer corner = dark, (63,63) center = R=57,
            // ring around dist_from_center == 26 (i.e. radius 26 of 64) has strong G & R.
            const float dx = static_cast<float>(63 - x);
            const float dy = static_cast<float>(63 - y);
            const float r = std::sqrt(dx * dx + dy * dy);
            const float ring = std::exp(-std::pow((r - 28.0f) / 6.0f, 2.0f));
            const float inside = (r <= 28.0f) ? 1.0f : 0.0f;
            const std::size_t idx = static_cast<std::size_t>(y * 64 + x) * 4u;
            quad_rgba[idx + 0] = static_cast<std::uint8_t>(std::min(255.0f, ring * 70.0f + inside * 57.0f));
            quad_rgba[idx + 1] = static_cast<std::uint8_t>(std::min(255.0f, ring * 180.0f));
            quad_rgba[idx + 2] = static_cast<std::uint8_t>(ring * 20.0f);
            quad_rgba[idx + 3] = 255;
        }
    }
    const GLuint quad_tex = create_rgba_texture(64, 64, quad_rgba);

    FocusCursorRenderer focus_renderer;
    auto render_and_read_focus = [&](float phase) {
        glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
        glViewport(0, 0, 256, 256);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        focus_renderer.draw(quad_tex, 128.0f, 128.0f, 207.0f, phase, 256, 256);
        std::vector<std::uint8_t> fb(256 * 256 * 4u);
        glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, fb.data());
        return fb;
    };

    const auto fb_peak = render_and_read_focus(1.5707963f);   // sin = +1 -> pulse = 1.0
    const auto fb_trough = render_and_read_focus(-1.5707963f); // sin = -1 -> pulse = 0.0

    const auto corner = sample_pixel(fb_peak, 256, 256, 4, 4);
    require(corner[0] == 0 && corner[1] == 0 && corner[2] == 0, "Focus outer corner must stay transparent/dark");

    // 4-way quadrant symmetry check around ring radius (~28/64 * 103.5 ~= 45 px from center 128).
    const auto q_left  = sample_pixel(fb_peak, 256, 256, 128 - 45, 128);
    const auto q_right = sample_pixel(fb_peak, 256, 256, 128 + 45, 128);
    const auto q_up    = sample_pixel(fb_peak, 256, 256, 128, 128 - 45);
    const auto q_down  = sample_pixel(fb_peak, 256, 256, 128, 128 + 45);
    require(q_left[2] > 100 && q_left[1] > 80 && q_left[2] >= q_left[0], "Ring must glow bright cyan");
    require(std::abs(static_cast<int>(q_left[1]) - static_cast<int>(q_right[1])) <= 3, "Horizontal ring symmetry");
    require(std::abs(static_cast<int>(q_up[1]) - static_cast<int>(q_down[1])) <= 3, "Vertical ring symmetry");
    require(std::abs(static_cast<int>(q_left[1]) - static_cast<int>(q_up[1])) <= 6, "X/Y quadrant symmetry");

    const auto t_left = sample_pixel(fb_trough, 256, 256, 128 - 45, 128);
    require(q_left[2] > t_left[2] + 30, "Peak pulse ring must be brighter than trough pulse");

    require(glGetError() == GL_NO_ERROR, "No GL errors during home effects rendering");

    glDeleteTextures(1, &quad_tex);
    glDeleteTextures(1, &mock_wave_tex);
    glDeleteFramebuffers(1, &target_fbo);
    glDeleteTextures(1, &target_tex);

    std::printf("PASS home_effects_device (env_mean=%.3f env_std=%.3f peak_ring_B=%u trough_ring_B=%u)\n",
        mean_luma, std_luma, q_left[2], t_left[2]);
    return 0;
}
