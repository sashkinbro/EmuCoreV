#include <emucorev/paf/wave_renderer.h>

#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <numeric>
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

struct FrameStats {
    double mean_r = 0;
    double mean_g = 0;
    double mean_b = 0;
    double top_luma = 0;
    double bottom_luma = 0;
    double std_luma = 0;
};

FrameStats compute_stats(const std::vector<std::uint8_t>& rgba, int width, int height) {
    FrameStats s{};
    const int n = width * height;
    double sum_l = 0, sum_l2 = 0, top_l = 0, bot_l = 0;
    int top_n = 0, bot_n = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y * width + x) * 4u;
            const double r = rgba[idx + 0] / 255.0;
            const double g = rgba[idx + 1] / 255.0;
            const double b = rgba[idx + 2] / 255.0;
            const double l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            s.mean_r += r;
            s.mean_g += g;
            s.mean_b += b;
            sum_l += l;
            sum_l2 += l * l;
            if (y < height / 4) {
                bot_l += l;
                ++bot_n;
            } else if (y >= height * 3 / 4) {
                top_l += l;
                ++top_n;
            }
        }
    }
    s.mean_r /= n;
    s.mean_g /= n;
    s.mean_b /= n;
    const double mean_l = sum_l / n;
    s.std_luma = std::sqrt(std::max(0.0, sum_l2 / n - mean_l * mean_l));
    s.top_luma = top_l / top_n;
    s.bottom_luma = bot_l / bot_n;
    return s;
}

} // namespace

int main() {
    EglContext egl;
    constexpr int kW = 960;
    constexpr int kH = 544;

    GLuint color = 0, depth = 0, fbo = 0;
    glGenTextures(1, &color);
    glBindTexture(GL_TEXTURE_2D, color);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, kW, kH);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kW, kH);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "main FBO");

    WaveRenderer renderer("effects_default_0512_01");
    require(glGetError() == GL_NO_ERROR, "ctor glGetError");

    std::vector<std::uint8_t> frame0(static_cast<std::size_t>(kW * kH) * 4u);
    std::vector<std::uint8_t> frame1(frame0.size());
    std::vector<std::uint8_t> ruby(frame0.size());

    renderer.draw(fbo, kW, kH);
    require(glGetError() == GL_NO_ERROR, "draw frame0 glGetError");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, frame0.data());
    const FrameStats s0 = compute_stats(frame0, kW, kH);
    std::printf("default t=0: rgb=(%.3f, %.3f, %.3f) std_l=%.4f bot=%.3f top=%.3f\n",
        s0.mean_r, s0.mean_g, s0.mean_b, s0.std_luma, s0.bottom_luma, s0.top_luma);
    require(s0.mean_b > s0.mean_r && s0.std_luma > 0.01, "default blue wave non-uniform");

    for (int i = 0; i < 6; ++i) renderer.step(0.08f);
    renderer.draw(fbo, kW, kH);
    require(glGetError() == GL_NO_ERROR, "draw frame1 glGetError");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, frame1.data());
    int diff_pixels = 0;
    for (std::size_t i = 0; i + 3 < frame0.size(); i += 4) {
        if (std::abs(int(frame0[i]) - int(frame1[i])) > 2 ||
            std::abs(int(frame0[i + 1]) - int(frame1[i + 1])) > 2 ||
            std::abs(int(frame0[i + 2]) - int(frame1[i + 2])) > 2) {
            ++diff_pixels;
        }
    }
    std::printf("animation diff_pixels=%d / %d\n", diff_pixels, kW * kH);
    require(diff_pixels > 5000, "wave animates across frames");

    renderer.set_theme("effects_0406_ruby");
    renderer.draw(fbo, kW, kH);
    require(glGetError() == GL_NO_ERROR, "draw ruby glGetError");
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, ruby.data());
    const FrameStats sr = compute_stats(ruby, kW, kH);
    std::printf("ruby t=0: rgb=(%.3f, %.3f, %.3f) std_l=%.4f\n",
        sr.mean_r, sr.mean_g, sr.mean_b, sr.std_luma);
    require(sr.mean_r > sr.mean_b && sr.std_luma > 0.01, "ruby theme red dominant");

    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &depth);
    glDeleteTextures(1, &color);
    std::printf("PASS wave_renderer_test (%s)\n", glGetString(GL_RENDERER));
    return 0;
}
