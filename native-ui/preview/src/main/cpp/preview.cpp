#include <emucorev/paf/bubble_renderer.h>
#include <emucorev/paf/bubble_shaders.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
template<class T> std::vector<T> read(const std::filesystem::path& path, std::size_t count) {
    if (std::filesystem::file_size(path) != count * sizeof(T))
        throw std::runtime_error("Unexpected preview resource size: " + path.filename().string());
    std::vector<T> values(count);
    std::ifstream input(path, std::ios::binary);
    input.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
    if (!input) throw std::runtime_error("Cannot read preview resource");
    return values;
}

struct Window {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint target = 0, color = 0;
    std::unique_ptr<emucorev::paf::BubbleRenderer> bubble;
    std::array<float, 96> vertex{};
    std::array<float, 10> material{};

    ~Window() {
        // GPU resources must die before their context, including partial init.
        if (context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE)
            eglMakeCurrent(display, surface, surface, context);
        bubble.reset();
        if (target) glDeleteFramebuffers(1, &target);
        if (color) glDeleteTextures(1, &color);
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            eglTerminate(display);
        }
    }

    void initialize(android_app* app) {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major, minor, count;
        if (!eglInitialize(display, &major, &minor)) throw std::runtime_error("Cannot initialize preview EGL");
        constexpr EGLint config_attributes[]{EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
        EGLConfig config;
        if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || !count)
            throw std::runtime_error("No preview EGL configuration");
        EGLint format;
        eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format);
        ANativeWindow_setBuffersGeometry(app->window, 0, 0, format);
        surface = eglCreateWindowSurface(display, config, app->window, nullptr);
        constexpr EGLint context_attributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (!eglMakeCurrent(display, surface, surface, context)) throw std::runtime_error("Cannot activate preview context");
        eglSwapInterval(display, 1);

        const auto root = std::filesystem::path(app->activity->internalDataPath) / "bubble";
        const auto vertices = read<emucorev::paf::BubbleVertex>(root / "stream-0.bin", 608);
        const auto indices = read<std::uint16_t>(root / "indices.bin", 1220);
        const auto transform = read<float>(root / "vertex-ubo-0.bin", 96);
        const auto parameters = read<float>(root / "fragment-ubo-0.bin", 10);
        std::copy(transform.begin(), transform.end(), vertex.begin());
        std::copy(parameters.begin(), parameters.end(), material.begin());
        const auto palette = read<std::uint8_t>(root / "fragment-texture-0-palette.bin", 1024);
        const auto indexed = read<std::uint8_t>(root / "fragment-texture-0.bin", 128 * 128);
        const auto environment = read<std::uint8_t>(root / "environment.rgba", 64 * 64 * 4);
        std::vector<std::uint8_t> icon(128 * 128 * 4);
        for (std::size_t i = 0; i < indexed.size(); ++i)
            std::copy_n(palette.data() + indexed[i] * 4, 4, icon.data() + i * 4);
        bubble = std::make_unique<emucorev::paf::BubbleRenderer>(
            emucorev::paf::opaque_bubble_vertex_shader(), emucorev::paf::opaque_bubble_fragment_shader(),
            vertices, indices, emucorev::paf::BubbleImage{128, 128, icon},
            emucorev::paf::BubbleImage{64, 64, environment});

        // Fixed 1x rasterization, independently of the physical display size.
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 960, 544);
        glGenFramebuffers(1, &target);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cannot create 1x preview framebuffer");
    }

    void draw() {
        EGLint width, height;
        if (!eglQuerySurface(display, surface, EGL_WIDTH, &width)
            || !eglQuerySurface(display, surface, EGL_HEIGHT, &height) || width <= 0 || height <= 0) return;
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(0, 0, 960, 544);
        glClearColor(12.f / 255, 40.f / 255, 150.f / 255, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        bubble->draw(vertex, material);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // Preserve the reference aspect without stretching or corner clipping.
        const float scale = std::min((width - 32.f) / 960, (height - 32.f) / 544);
        const int w = static_cast<int>(960 * scale), h = static_cast<int>(544 * scale);
        const int x = (width - w) / 2, y = (height - h) / 2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
        glBlitFramebuffer(0, 0, 960, 544, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        if (!eglSwapBuffers(display, surface)) throw std::runtime_error("Preview presentation failed");
    }
};

struct Preview {
    std::unique_ptr<Window> window;
    void command(android_app* app, int32_t command) {
        try {
            if (command == APP_CMD_TERM_WINDOW) window.reset();
            else if (command == APP_CMD_INIT_WINDOW && app->window) {
                auto next = std::make_unique<Window>();
                next->initialize(app);
                window = std::move(next);
                window->draw();
            } else if ((command == APP_CMD_WINDOW_REDRAW_NEEDED || command == APP_CMD_WINDOW_RESIZED
                        || command == APP_CMD_GAINED_FOCUS) && window) window->draw();
        } catch (const std::exception& error) {
            __android_log_print(ANDROID_LOG_ERROR, "NativeUiPreview", "%s", error.what());
            window.reset();
            ANativeActivity_finish(app->activity);
        }
    }
};
}

void android_main(android_app* app) {
    Preview preview;
    app->userData = &preview;
    app->onAppCmd = [](android_app* state, int32_t command) {
        static_cast<Preview*>(state->userData)->command(state, command);
    };
    // No animation is implemented yet. Sleep completely while this static
    // preview is idle; there is no render loop or polling timer in the background.
    while (!app->destroyRequested) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source)
            source->process(app, source);
    }
}
