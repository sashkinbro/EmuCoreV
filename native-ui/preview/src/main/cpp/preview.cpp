#include <emucorev/paf/bubble_renderer.h>
#include <emucorev/paf/bubble_shaders.h>
#include <emucorev/paf/bubble_motion.h>
#include <emucorev/paf/device_status.h>
#include <android/choreographer.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace {
std::mutex status_mutex;
emucorev::paf::DeviceStatus device_status;
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
    std::array<float, 96> base_vertex{};
    std::array<float, 10> material{};
    emucorev::paf::BubbleBasis initial_basis{};
    float initial_phase = 0;

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
        base_vertex = vertex;
        initial_phase = std::atan2(vertex[14 * 4 + 1], vertex[14 * 4]);
        initial_basis = emucorev::paf::bubble_wobble_basis(std::cos(initial_phase), std::sin(initial_phase), 1);
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

    void set_phase(float phase) {
        const auto basis = emucorev::paf::bubble_wobble_basis(std::cos(phase), std::sin(phase), 1);
        // Development placement retains the supplied reference transform while
        // applying the recovered cone motion relative to its initial basis.
        // This is not yet the shell's complete per-widget animation controller.
        std::array<std::array<float, 3>, 3> delta{};
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                for (unsigned axis = 0; axis < 3; ++axis)
                    delta[row][column] += basis[axis][row] * initial_basis[axis][column];
        for (const unsigned first : {4u, 12u})
            for (unsigned column = first; column < first + 3; ++column)
                for (unsigned row = 0; row < 3; ++row) {
                    float value = 0;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        value += delta[row][axis] * base_vertex[column * 4 + axis];
                    vertex[column * 4 + row] = value;
                }
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
    AChoreographer* choreographer = nullptr;
    bool resumed = false, focused = false, callback_pending = false;
    bool phase_initialized = false;
    float phase = 0;
    int64_t last_draw = 0, next_draw = 0;

    bool active() const { return window && resumed && focused; }
    void schedule() {
        if (!active() || callback_pending) return;
        if (!choreographer) choreographer = AChoreographer_getInstance();
        if (!choreographer) throw std::runtime_error("No native frame scheduler");
        callback_pending = true;
        AChoreographer_postFrameCallback64(choreographer, [](int64_t time, void* data) {
            auto& preview = *static_cast<Preview*>(data);
            preview.callback_pending = false;
            if (!preview.active()) return;
            try {
                // Vsync timestamps round fractional refresh periods to ns.
                // Do not skip a 60 Hz frame merely because it is a few ns
                // earlier than the accumulated 16,666,667 ns deadline.
                if (time + 500000 >= preview.next_draw) {
                    const float elapsed = preview.last_draw ? float(time - preview.last_draw) * 1e-9f : 0;
                    if (elapsed > 0 && elapsed <= 1) {
                        preview.phase = emucorev::paf::advance_bubble_phase(preview.phase, elapsed, 1);
                        preview.window->set_phase(preview.phase);
                    }
                    preview.last_draw = time;
                    if (!preview.next_draw) preview.next_draw = time;
                    constexpr int64_t interval = 16666667;
                    preview.next_draw += (std::max<int64_t>(time - preview.next_draw, 0) / interval + 1) * interval;
                    preview.window->draw();
                }
                preview.schedule();
            } catch (const std::exception& error) {
                __android_log_print(ANDROID_LOG_ERROR, "NativeUiPreview", "%s", error.what());
                preview.focused = false;
            }
        }, this);
    }

    void command(android_app* app, int32_t command) {
        try {
            if (command == APP_CMD_TERM_WINDOW) {
                window.reset();
                last_draw = next_draw = 0;
            }
            else if (command == APP_CMD_INIT_WINDOW && app->window) {
                auto next = std::make_unique<Window>();
                next->initialize(app);
                window = std::move(next);
                if (!phase_initialized) {
                    phase = window->initial_phase;
                    phase_initialized = true;
                } else window->set_phase(phase);
                window->draw();
            } else if ((command == APP_CMD_WINDOW_REDRAW_NEEDED || command == APP_CMD_WINDOW_RESIZED
                        || command == APP_CMD_GAINED_FOCUS) && window) window->draw();
            if (command == APP_CMD_RESUME) resumed = true;
            if (command == APP_CMD_GAINED_FOCUS) focused = true;
            if (command == APP_CMD_LOST_FOCUS) focused = false;
            if (command == APP_CMD_PAUSE || command == APP_CMD_STOP) resumed = false;
            if (!active()) last_draw = next_draw = 0;
            schedule();
        } catch (const std::exception& error) {
            __android_log_print(ANDROID_LOG_ERROR, "NativeUiPreview", "%s", error.what());
            window.reset();
            ANativeActivity_finish(app->activity);
        }
    }
};
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeuipreview_PreviewActivity_nativeStatus(JNIEnv* env, jobject,
    jint battery, jboolean charging, jint power, jint wifi, jboolean connected, jint rssi,
    jint bluetooth, jlong minute, jstring zone, jboolean clock24) {
    const char* text = zone ? env->GetStringUTFChars(zone, nullptr) : nullptr;
    if (zone && !text) return;
    {
        const std::lock_guard lock(status_mutex);
        device_status.battery_percent = battery;
        device_status.charging = charging;
        device_status.power_source = power;
        device_status.wifi_enabled = static_cast<emucorev::paf::RadioState>(wifi);
        device_status.wifi_connected = connected;
        device_status.wifi_rssi = rssi;
        device_status.bluetooth_enabled = static_cast<emucorev::paf::RadioState>(bluetooth);
        device_status.epoch_minute = minute;
        device_status.time_zone = text ? text : "";
        device_status.use_24_hour_clock = clock24;
    }
    if (text) env->ReleaseStringUTFChars(zone, text);
}

void android_main(android_app* app) {
    Preview preview;
    app->userData = &preview;
    app->onAppCmd = [](android_app* state, int32_t command) {
        static_cast<Preview*>(state->userData)->command(state, command);
    };
    // Native frame callbacks wake this looper only while visible/focused.
    // Background work blocks on events, with no render loop or polling timer.
    while (!app->destroyRequested) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source)
            source->process(app, source);
    }
}
