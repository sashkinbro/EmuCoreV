#include <emucorev/paf/bubble_renderer.h>
#include <emucorev/paf/bubble_shaders.h>
#include <emucorev/paf/bubble_motion.h>
#include <emucorev/paf/device_status.h>
#include <emucorev/paf/sprite_renderer.h>
#include <emucorev/paf/busy_indicator.h>
#include <emucorev/paf/charging_indicator.h>
#include <emucorev/paf/page_turn_renderer.h>
#include <emucorev/paf/page_turn_shaders.h>
#include <android/choreographer.h>
#include <fstream>
#include <android/input.h>
#include <android_native_app_glue.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <unordered_map>
#include <atomic>

namespace {
std::mutex status_mutex;
emucorev::paf::DeviceStatus device_status;
struct HostImage {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    std::uint64_t revision = 0;
};
std::unordered_map<std::uint32_t, HostImage> host_images;
std::uint64_t image_revision = 0;
std::atomic<int> preview_scene{0}; // 0 bubble, 1 busy indicator, 2 lock screen
std::string resource_root;
template<class T> std::vector<T> read_path(const std::string& relative, std::size_t count) {
    std::string path; { const std::lock_guard lock(status_mutex); path = resource_root + "/" + relative; }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != static_cast<std::streamoff>(count * sizeof(T)))
        throw std::runtime_error("Missing or invalid local preview asset: " + path);
    std::vector<T> values(count); file.seekg(0);
    file.read(reinterpret_cast<char*>(values.data()), count * sizeof(T));
    if (!file) throw std::runtime_error("Incomplete preview asset: " + path);
    return values;
}
template<class T> std::vector<T> read(const char* filename, std::size_t count) {
    std::string path;
    {
        const std::lock_guard lock(status_mutex);
        path = resource_root + "/preview/bubble/" + filename;
    }
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != static_cast<std::streamoff>(count * sizeof(T)))
        throw std::runtime_error("Missing or invalid local preview asset: " + path);
    std::vector<T> values(count);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(values.data()), count * sizeof(T));
    if (!file) throw std::runtime_error("Incomplete preview asset: " + path);
    return values;
}

struct Window {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint target = 0, color = 0;
    std::unique_ptr<emucorev::paf::BubbleRenderer> bubble;
    std::unique_ptr<emucorev::paf::SpriteRenderer> sprites;
    std::unique_ptr<emucorev::paf::PageTurnRenderer> lock;
    GLuint lock_page = 0, lock_corner = 0, lock_exp = 0;
    emucorev::paf::PageTurnMesh lock_mesh;
    std::array<float, 112> lock_top_material{};
    std::array<float, 48> lock_bottom_material{};
    std::array<float, 16> lock_host_mvp{};
    bool lock_host_transform = false;
    bool lock_ready = false;
    std::unordered_map<std::uint32_t, std::uint64_t> uploaded_images;
    std::uint64_t uploaded_revision = 0;
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
        sprites.reset();
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

        // The isolated busy scene needs no bubble geometry/material resources.
        if (preview_scene.load() == 0) {
            const auto vertices = read<emucorev::paf::BubbleVertex>("stream-0.bin", 608);
            const auto indices = read<std::uint16_t>("indices.bin", 1220);
            const auto transform = read<float>("vertex-ubo-0.bin", 96);
            const auto parameters = read<float>("fragment-ubo-0.bin", 10);
            std::copy(transform.begin(), transform.end(), vertex.begin());
            base_vertex = vertex;
            initial_phase = std::atan2(vertex[14 * 4 + 1], vertex[14 * 4]);
            initial_basis = emucorev::paf::bubble_wobble_basis(std::cos(initial_phase), std::sin(initial_phase), 1);
            std::copy(parameters.begin(), parameters.end(), material.begin());
            const auto palette = read<std::uint8_t>("fragment-texture-0-palette.bin", 1024);
            const auto indexed = read<std::uint8_t>("fragment-texture-0.bin", 128 * 128);
            const auto environment = read<std::uint8_t>("environment.rgba", 64 * 64 * 4);
            std::vector<std::uint8_t> icon(128 * 128 * 4);
            for (std::size_t i = 0; i < indexed.size(); ++i)
                std::copy_n(palette.data() + indexed[i] * 4, 4, icon.data() + i * 4);
            bubble = std::make_unique<emucorev::paf::BubbleRenderer>(
                emucorev::paf::opaque_bubble_vertex_shader(), emucorev::paf::opaque_bubble_fragment_shader(),
                vertices, indices, emucorev::paf::BubbleImage{128, 128, icon},
                emucorev::paf::BubbleImage{64, 64, environment});
        }
        sprites = std::make_unique<emucorev::paf::SpriteRenderer>();
        if (preview_scene.load() == 2) {
            lock = std::make_unique<emucorev::paf::PageTurnRenderer>(
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_bottom_vertex_shader(),
                    emucorev::paf::page_turn_bottom_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_top_vertex_shader(),
                    emucorev::paf::page_turn_top_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_quad_vertex_shader(),
                    emucorev::paf::page_turn_shadow_fragment_shader()});
            const auto material = read_path<float>("preview/lock/rest-material.bin", 160);
            std::copy_n(material.begin(), 112, lock_top_material.begin());
            std::copy_n(material.begin() + 112, 48, lock_bottom_material.begin());
            const std::array<float, 8> bottom_uv_squeeze{1, 1, 0, 0, 1, 1, 1, 1};
            std::copy_n(bottom_uv_squeeze.begin(), 8, lock_bottom_material.begin() + 20);
            // Rest-state engine transforms for direct 960x544 submission:
            // UVTransform (scale, offset) and SqueezeZ (xy scale, height/w scale).
            const std::array<float, 8> uv_squeeze{1, 1, 0, 0, 1, 1, 1, 1};
            std::copy_n(uv_squeeze.begin(), 8, lock_top_material.begin() + 48);
            const auto aspect = emucorev::paf::page_turn_normalized_aspect({960, 544});
            const auto shape = emucorev::paf::page_turn_fold_shape({1.7447f, .99f}, aspect);
            emucorev::paf::page_turn_mesh(shape, lock_mesh);
            lock->upload_mesh(lock_mesh);
            // Host presentation transform: fit the recovered page mesh to the
            // 960x544 target without using the shell's engine-side screen matrices.
            // Fit the physical page rectangle (shape corners, not the folded
            // curl) to the 95% safe area of the 960x544 target.
            float min_x = shape.corners[0].x, max_x = shape.corners[0].x;
            float min_y = shape.corners[0].y, max_y = shape.corners[0].y;
            for (const auto& corner : shape.corners) {
                min_x = std::min(min_x, corner.x); max_x = std::max(max_x, corner.x);
                min_y = std::min(min_y, corner.y); max_y = std::max(max_y, corner.y);
            }
            const float center_x = (min_x + max_x) * .5f, center_y = (min_y + max_y) * .5f;
            const float scale_x = (max_x - min_x) > 0 ? .97f * 2.f / (max_x - min_x) : 1.f;
            const float scale_y = (max_y - min_y) > 0 ? .97f * 2.f / (max_y - min_y) : 1.f;
            const std::array<float, 16> host_mvp{scale_x,0,0,0, 0,scale_y,0,0, 0,0,1,0,
                -center_x * scale_x, -center_y * scale_y, 0, 1};
            lock_host_mvp = host_mvp;
        }

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

    void upload_images() {
        // Packaged textures are decoded once by Kotlin and uploaded only when
        // changed. A recreated GL context gets its own complete texture cache.
        const std::lock_guard lock(status_mutex);
        if (uploaded_revision != image_revision) {
            for (const auto& [id, image] : host_images) {
                if (uploaded_images[id] == image.revision) continue;
                sprites->upload(id, image.width, image.height, image.rgba);
                uploaded_images[id] = image.revision;
            }
            uploaded_revision = image_revision;
        }
    }

    void draw_status(std::uint32_t charging_texture) {
        const std::lock_guard lock(status_mutex);
        sprites->begin(960, 544);
        // Development composition; exact original anchors/clock metrics and
        // charging animation are still being reconstructed, not parity claims.
        sprites->draw(0, 0, 0, 960, 32, {0, 0, 0, 1});
        constexpr std::array<std::uint32_t, 5> battery{
            0x78600544, 0x4a8ae255, 0x9e9cfcab, 0xf85f0b80, 0x56e72f16};
        const auto& status = device_status;
        auto battery_id = status.battery_percent < 0 ? 0xa5aafd9bu
            : battery[std::min(4, std::clamp(status.battery_percent, 0, 100) * 4 / 100)];
        sprites->draw(battery_id, 904, 2, 48, 28);
        if (charging_texture && status.charging && status.battery_percent >= 0 && status.battery_percent < 100)
            sprites->draw(charging_texture, 904, 2, 48, 28);
        if (const auto clock = host_images.find(1); clock != host_images.end())
            sprites->draw(1, 892.f - clock->second.width, 0, clock->second.width, clock->second.height);
        float left = 8;
        if (status.wifi_enabled == emucorev::paf::RadioState::on && status.wifi_connected) {
            constexpr std::array<std::uint32_t, 4> wifi{0x89dc8410, 0x2278fb2a, 0xee958f99, 0xa13376bc};
            // Android's four-bin RSSI scale. Original Vita radio service is
            // intentionally absent; unknown RSSI does not fabricate strength.
            const int level = status.wifi_rssi == INT32_MIN ? 0
                : std::clamp((std::clamp(status.wifi_rssi, -100, -55) + 100) * 3 / 45, 0, 3);
            sprites->draw(wifi[level], left, 4, 23, 24);
            left += 31;
        }
        if (status.bluetooth_enabled == emucorev::paf::RadioState::on)
            sprites->draw(0xca76be07, left, 4, 19, 24);
    }

    void ensure_lock_textures() {
        const std::lock_guard lock_guard(status_mutex);
        auto make = [&](std::uint32_t id, GLuint& texture) {
            if (texture) return; const auto image = host_images.find(id);
            if (image == host_images.end()) return;
            glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image->second.width, image->second.height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, image->second.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        make(100, lock_page); make(101, lock_corner); make(102, lock_exp);
        lock_ready = lock_page && lock_corner && lock_exp;
    }

    void draw_lock() {
        ensure_lock_textures();
        static int reported = 0;
        if (reported < 5) {
            const std::lock_guard lock_guard(status_mutex);
            __android_log_print(ANDROID_LOG_WARN, "NativeUiPreview",
                "lock ready=%d page=%u corner=%u exp=%u verts=%u images=%zu lock=%d", lock_ready,
                lock_page, lock_corner, lock_exp, lock_mesh.vertex_count, host_images.size(),
                lock ? 1 : 0);
            ++reported;
        }
        if (!lock || !lock_ready || !lock_mesh.vertex_count) return;
        emucorev::paf::PageSurfaceDraw material{};
        material.vertex_parameters = lock_top_material;
        std::copy(lock_host_mvp.begin(), lock_host_mvp.end(), lock_top_material.begin());
        std::copy(lock_host_mvp.begin(), lock_host_mvp.end(), lock_bottom_material.begin());
        std::array<float, 8> host_uv_squeeze{-1, 1, 1, 0, 1, 1, 1, 1};
        std::copy(host_uv_squeeze.begin(), host_uv_squeeze.end(), lock_top_material.begin() + 48);
        std::copy(host_uv_squeeze.begin(), host_uv_squeeze.end(), lock_bottom_material.begin() + 20);
        // Rest-state page shading: flat page is fully visible; exponential
        // lookup range comes from the prepared original table (5 / reciprocal .2).
        const std::array<float, 4> shadow_one{1, 1, 1, 1};
        std::copy(shadow_one.begin(), shadow_one.end(), lock_bottom_material.begin() + 16);
        lock_bottom_material[28] = 5.f;
        lock_bottom_material[29] = .2f;
        material.viewport = {1,1,1,1, 1, 960, 544, 0, 1, 0, 0, 0};
        material.fragment_state = {0,0,1,0, 1,0,0, 1.f/960, 1.f/544, 0, 16, 0};
        material.textures = {lock_page, 0, 0, 0, lock_exp};
        material.blend = false;
        material.vertex_parameters = lock_bottom_material;
        lock->draw(emucorev::paf::PageSurface::front, material);
        // folded surface temporarily disabled while isolating the front page
    }

    void draw(const emucorev::paf::BusySnapshot* busy, std::uint32_t charging_texture) {
        EGLint width, height;
        if (!eglQuerySurface(display, surface, EGL_WIDTH, &width)
            || !eglQuerySurface(display, surface, EGL_HEIGHT, &height) || width <= 0 || height <= 0) return;
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(0, 0, 960, 544);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        upload_images();
        if (preview_scene.load() == 2) { draw_lock(); }
        else if (busy) {
            sprites->begin(960, 544);
            if (busy->state != emucorev::paf::BusyState::idle)
                for (const auto& dot : busy->dots)
                    if (dot.size > 0 && dot.alpha)
                        sprites->draw(0xba195a37, 480 + dot.x - dot.size * .5f,
                            272 - dot.y - dot.size * .5f, dot.size, dot.size,
                            {1, 1, 1, float(dot.alpha) * (1.f / 255.f)});
        } else {
            bubble->draw(vertex, material);
            draw_status(charging_texture);
        }

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
    emucorev::paf::BusyIndicator busy{80};
    emucorev::paf::ChargingIndicator charging;
    std::unique_ptr<Window> window;
    AChoreographer* choreographer = nullptr;
    bool resumed = false, focused = false, callback_pending = false;
    bool phase_initialized = false;
    float phase = 0;
    int64_t last_draw = 0, next_draw = 0;

    Preview() { busy.start(); }
    void render() { window->draw(preview_scene.load() == 1 ? &busy.snapshot() : nullptr, charging.texture()); }

    bool active() const { return window && resumed && focused; }
    void schedule() {
        if (!active() || callback_pending) return;
        if (preview_scene.load() == 1 && busy.snapshot().state == emucorev::paf::BusyState::idle) return;
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
                    if (preview_scene.load() == 0) {
                        const std::lock_guard lock(status_mutex);
                        preview.charging.advance(device_status.charging, device_status.battery_percent);
                    }
                    const float elapsed = preview.last_draw ? float(time - preview.last_draw) * 1e-9f : 0;
                    if (elapsed > 0 && elapsed <= 1) {
                        if (preview_scene.load() == 1) preview.busy.tick(elapsed);
                        else if (preview_scene.load() == 0) {
                            preview.phase = emucorev::paf::advance_bubble_phase(preview.phase, elapsed, 1);
                            preview.window->set_phase(preview.phase);
                        }
                    }
                    preview.last_draw = time;
                    if (!preview.next_draw) preview.next_draw = time;
                    constexpr int64_t interval = 16666667;
                    preview.next_draw += (std::max<int64_t>(time - preview.next_draw, 0) / interval + 1) * interval;
                    preview.render();
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
                render();
            } else if ((command == APP_CMD_WINDOW_REDRAW_NEEDED || command == APP_CMD_WINDOW_RESIZED
                        || command == APP_CMD_GAINED_FOCUS) && window) render();
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
Java_com_sbro_emucorev_nativeuipreview_PreviewActivity_nativeResourceRoot(JNIEnv* env, jobject, jstring path) {
    const char* text = env->GetStringUTFChars(path, nullptr);
    if (!text) return;
    {
        const std::lock_guard lock(status_mutex);
        resource_root = text;
    }
    env->ReleaseStringUTFChars(path, text);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeuipreview_PreviewActivity_nativePreviewScene(JNIEnv*, jobject, jint scene) {
    preview_scene.store(scene);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeuipreview_PreviewActivity_nativeImage(JNIEnv* env, jobject,
    jint id, jint width, jint height, jbyteArray rgba) {
    if (!rgba || width <= 0 || height <= 0 || width > 4096 || height > 4096
        || env->GetArrayLength(rgba) != static_cast<jlong>(width) * height * 4) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid native UI image");
        return;
    }
    HostImage image;
    image.width = width;
    image.height = height;
    image.rgba.resize(std::size_t(width) * height * 4);
    env->GetByteArrayRegion(rgba, 0, static_cast<jsize>(image.rgba.size()),
        reinterpret_cast<jbyte*>(image.rgba.data()));
    if (env->ExceptionCheck()) return;
    const std::lock_guard lock(status_mutex);
    image.revision = ++image_revision;
    host_images[static_cast<std::uint32_t>(id)] = std::move(image);
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
    app->onInputEvent = [](android_app* state, AInputEvent* event) -> int32_t {
        auto& preview = *static_cast<Preview*>(state->userData);
        if (preview_scene.load() != 1 || !preview.active()) return 0;
        const bool tap = AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION
            && (AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) == AMOTION_EVENT_ACTION_UP;
        const bool confirm = AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY
            && AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN
            && AKeyEvent_getKeyCode(event) == AKEYCODE_BUTTON_A && AKeyEvent_getRepeatCount(event) == 0;
        if (!tap && !confirm) return 0;
        const auto phase = preview.busy.snapshot().state;
        if (phase == emucorev::paf::BusyState::idle || phase == emucorev::paf::BusyState::stopping)
            preview.busy.start();
        else preview.busy.stop();
        if (phase == emucorev::paf::BusyState::idle) preview.last_draw = preview.next_draw = 0;
        preview.schedule();
        return 1;
    };
    // Native frame callbacks wake this looper only while visible/focused.
    // Background work blocks on events, with no render loop or polling timer.
    while (!app->destroyRequested) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source)
            source->process(app, source);
    }
}
