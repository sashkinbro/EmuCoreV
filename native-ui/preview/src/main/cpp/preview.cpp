#include <emucorev/paf/bubble_renderer.h>
#include <emucorev/paf/bubble_shaders.h>
#include <emucorev/paf/bubble_motion.h>
#include <emucorev/paf/device_status.h>
#include <emucorev/paf/sprite_renderer.h>
#include <emucorev/paf/busy_indicator.h>
#include <emucorev/paf/charging_indicator.h>
#include <emucorev/paf/page_turn_renderer.h>
#include <emucorev/paf/page_turn_material.h>
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
#include <bit>
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
struct LockPageConfig {
    emucorev::paf::PagePoint size{960.f, 512.f};
    emucorev::paf::PagePoint position{0.f, -16.f}; // recovered centered, Y-up PageTurn SetPos
    emucorev::paf::PagePoint margin{32.f, 32.f}; // original Graphic inset from the shell
    float fold_fraction = std::bit_cast<float>(0x3e6b851fu); // PageTurnBase(.23, .85)
    float corner_aspect = std::bit_cast<float>(0x3f59999au);
};
constexpr LockPageConfig lock_page_config{};
constexpr float preview_width = 960.f, preview_height = 544.f;
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
    std::array<std::uint64_t, 3> lock_texture_revisions{};
    std::array<unsigned, 3> lock_texture_widths{}, lock_texture_heights{};
    emucorev::paf::PageTurnMesh lock_mesh;
    std::array<float, 112> lock_top_material{};
    std::array<float, 48> lock_bottom_material{};
    std::array<float, 36> lock_empty_material{};
    emucorev::paf::PageTurnMaterialStyle lock_material_style{};
    emucorev::paf::PageTurnEffectStyle lock_effect_style{};
    bool lock_ready = false;
    emucorev::paf::PagePoint lock_point{0.f, 0.f};
    emucorev::paf::PageTurnLayout lock_layout{};
    emucorev::paf::PagePoint drag_initial{0.f, 0.f};
    emucorev::paf::PagePoint drag_offset{0.f, 0.f};
    bool is_dragging = false;

    enum class LockAnimState { idle, returning, peeling, unlocked };
    LockAnimState lock_anim_state = LockAnimState::idle;
    float anim_elapsed = 0.f;
    float anim_duration = 0.f;
    emucorev::paf::PagePoint anim_start_point{0.f, 0.f};
    emucorev::paf::PagePoint anim_target_point{0.f, 0.f};

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
        lock.reset();
        if (lock_page) glDeleteTextures(1, &lock_page);
        if (lock_corner) glDeleteTextures(1, &lock_corner);
        if (lock_exp) glDeleteTextures(1, &lock_exp);
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
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_bottom_round_vertex_shader(),
                    emucorev::paf::page_turn_bottom_round_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_top_round_vertex_shader(),
                    emucorev::paf::page_turn_top_round_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_empty_back_vertex_shader(),
                    emucorev::paf::page_turn_empty_back_fragment_shader()});

            lock_layout = emucorev::paf::page_turn_layout(lock_page_config.size,
                lock_page_config.fold_fraction, lock_page_config.corner_aspect);
            update_lock_fold(lock_fold_point(lock_layout.rest));
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

    emucorev::paf::PagePoint lock_render_size() const {
        return {lock_page_config.size.x - 2.f * lock_page_config.margin.x,
            lock_page_config.size.y - 2.f * lock_page_config.margin.y};
    }

    emucorev::paf::PagePoint lock_fold_point(emucorev::paf::PagePoint centered_page_point) const {
        const auto inner_aspect = emucorev::paf::page_turn_normalized_aspect(lock_render_size());
        const emucorev::paf::PagePoint root_position{
            (centered_page_point.x + lock_page_config.size.x * .5f) / lock_page_config.size.x,
            (centered_page_point.y + lock_page_config.size.y * .5f) / lock_page_config.size.y};
        return {root_position.x * inner_aspect.x, root_position.y * inner_aspect.y};
    }

    emucorev::paf::PageTurnPose lock_pose() const {
        emucorev::paf::PageTurnPose pose{};
        pose.position = {lock_page_config.position.x, lock_page_config.position.y, 0.f};
        return pose;
    }

    emucorev::paf::PageTurnMatrix lock_projection() const {
        return {2.f / preview_width, 0.f, 0.f, 0.f,
            0.f, 2.f / preview_height, 0.f, 0.f,
            0.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 0.f, 1.f};
    }

    void update_lock_fold(emucorev::paf::PagePoint point) {
        lock_point = point;
        const auto size = lock_render_size();
        const auto aspect = emucorev::paf::page_turn_normalized_aspect(size);
        const auto shape = emucorev::paf::page_turn_fold_shape(point, aspect);
        emucorev::paf::page_turn_mesh(shape, lock_mesh);
        if (lock) lock->upload_mesh(lock_mesh);

        const auto style = emucorev::paf::PageTurnShapeStyle{};
        const auto shape_mat = emucorev::paf::page_turn_shape_material(point, shape, style);
        const auto lights = emucorev::paf::page_turn_light_parameters({});
        const auto transforms = emucorev::paf::page_turn_transforms(
            size, lock_pose(), lock_projection(), emucorev::paf::page_turn_identity);
        const auto page_material = emucorev::paf::page_turn_material_pack(
            shape_mat.fresnel_multiplier, lock_material_style);
        // The original Graphics object is inset 32px on each side of the
        // 960x512 PageTurn root. The uploaded 960x544 image includes the
        // 32px status band, so map the surface to its recovered pixel rect.
        constexpr std::array<float, 4> page_uv_rect{
            896.f / 960.f, 448.f / 544.f, 32.f / 960.f, 64.f / 544.f};

        // 1. Bottom material (48 floats):
        std::copy(transforms.model_view_projection.begin(), transforms.model_view_projection.end(),
            lock_bottom_material.begin());
        lock_bottom_material[16] = lock_effect_style.shadow;
        lock_bottom_material[17] = lock_effect_style.shadow_level;
        lock_bottom_material[18] = 1.0f;
        lock_bottom_material[19] = 1.0f;
        std::copy(page_uv_rect.begin(), page_uv_rect.end(), lock_bottom_material.begin() + 20);
        lock_bottom_material[24] = 1.f;
        lock_bottom_material[25] = 1.f;
        lock_bottom_material[26] = 1.f;
        lock_bottom_material[27] = 1.f;
        lock_bottom_material[28] = 5.f;
        lock_bottom_material[29] = 0.2f;
        lock_bottom_material[30] = shape_mat.deepest[0];
        lock_bottom_material[31] = shape_mat.deepest[1];
        lock_bottom_material[32] = shape_mat.deepest[2];
        lock_bottom_material[33] = shape_mat.deepest[3];
        lock_bottom_material[34] = shape_mat.grep.x;
        lock_bottom_material[35] = shape_mat.grep.y;
        std::copy(shape_mat.params.begin(), shape_mat.params.end(), lock_bottom_material.begin() + 36);
        std::copy(shape_mat.fold_plane.begin(), shape_mat.fold_plane.end(), lock_bottom_material.begin() + 40);
        std::copy(shape_mat.aspect.begin(), shape_mat.aspect.end(), lock_bottom_material.begin() + 44);

        // 2. Top material (112 floats):
        std::copy(transforms.model_view_projection.begin(), transforms.model_view_projection.end(),
            lock_top_material.begin());
        std::copy(transforms.model.begin(), transforms.model.end(), lock_top_material.begin() + 16);
        std::copy(page_material.begin(), page_material.end(), lock_top_material.begin() + 32);
        std::copy(page_uv_rect.begin(), page_uv_rect.end(), lock_top_material.begin() + 48);
        lock_top_material[52] = 1.f;
        lock_top_material[53] = 1.f;
        lock_top_material[54] = 1.f;
        lock_top_material[55] = 1.f;
        std::copy(shape_mat.deepest.begin(), shape_mat.deepest.end(), lock_top_material.begin() + 56);
        lock_top_material[60] = shape_mat.grep.x;
        lock_top_material[61] = shape_mat.grep.y;
        lock_top_material[62] = shape_mat.normal_scale_z;
        lock_top_material[63] = 0.f;
        std::copy(shape_mat.params.begin(), shape_mat.params.end(), lock_top_material.begin() + 64);
        std::copy(shape_mat.fold_plane.begin(), shape_mat.fold_plane.end(), lock_top_material.begin() + 68);
        std::copy(shape_mat.aspect.begin(), shape_mat.aspect.end(), lock_top_material.begin() + 72);
        std::copy(lights.color.begin(), lights.color.end(), lock_top_material.begin() + 76);
        std::copy(lights.position.begin(), lights.position.end(), lock_top_material.begin() + 92);
        lock_top_material[108] = 0.f;
        lock_top_material[109] = 0.f;
        lock_top_material[110] = 0.f;
        lock_top_material[111] = 1.f;

        // 3. Empty back material (36 floats):
        std::copy(transforms.model_view_projection.begin(), transforms.model_view_projection.end(),
            lock_empty_material.begin());
        lock_empty_material[16] = lock_effect_style.empty_shadow;
        lock_empty_material[17] = lock_effect_style.empty_shadow_level;
        lock_empty_material[18] = 0.f;
        lock_empty_material[19] = 0.f;
        std::copy(page_uv_rect.begin(), page_uv_rect.end(), lock_empty_material.begin() + 20);
        lock_empty_material[24] = 1.f;
        lock_empty_material[25] = 1.f;
        lock_empty_material[26] = 1.f;
        lock_empty_material[27] = 1.f;
        std::copy(shape_mat.fold_plane.begin(), shape_mat.fold_plane.end(), lock_empty_material.begin() + 28);
        std::copy(shape_mat.aspect.begin(), shape_mat.aspect.end(), lock_empty_material.begin() + 32);
    }

    void tick_lock(float elapsed) {
        if (lock_anim_state == LockAnimState::idle || lock_anim_state == LockAnimState::unlocked)
            return;
        anim_elapsed += elapsed;
        const float t = anim_duration > 0.f ? std::clamp(anim_elapsed / anim_duration, 0.f, 1.f) : 1.f;
        // PAF curve 0x19 is sine-in for the exit peel. Curve 0x15 is
        // quadratic-out for a return to the resting fold point.
        const float factor = lock_anim_state == LockAnimState::peeling
            ? std::sin(t * 1.5707963267948966f)
            : 1.f - (1.f - t) * (1.f - t);
        const emucorev::paf::PagePoint next_point{
            anim_start_point.x + (anim_target_point.x - anim_start_point.x) * factor,
            anim_start_point.y + (anim_target_point.y - anim_start_point.y) * factor
        };
        update_lock_fold(next_point);
        if (t >= 1.f) {
            if (lock_anim_state == LockAnimState::peeling) {
                lock_anim_state = LockAnimState::unlocked;
            } else {
                lock_anim_state = LockAnimState::idle;
            }
        }
    }

    int32_t handle_input(AInputEvent* event) {
        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
        const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        EGLint width = 0, height = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        if (width <= 0 || height <= 0) return 0;

        const float scale = std::min((width - 32.f) / 960.f, (height - 32.f) / 544.f);
        const int w = static_cast<int>(960 * scale), h = static_cast<int>(544 * scale);
        const int ox = (width - w) / 2, oy = (height - h) / 2;

        const float raw_x = AMotionEvent_getX(event, 0);
        const float raw_y = AMotionEvent_getY(event, 0);

        const float px = (raw_x - ox) / scale;
        const float py = (raw_y - oy) / scale;

        // Convert to the PageTurn widget's centered, Y-up local coordinates.
        const emucorev::paf::PagePoint widget_point{
            px - preview_width * .5f - lock_page_config.position.x,
            preview_height * .5f - py - lock_page_config.position.y};
        const auto rest_point = lock_fold_point(lock_layout.rest);

        if (action == AMOTION_EVENT_ACTION_DOWN) {
            if (lock_anim_state == LockAnimState::unlocked) {
                lock_anim_state = LockAnimState::idle;
                update_lock_fold(rest_point);
                return 1;
            }
            if (emucorev::paf::page_turn_hit_test(lock_page_config.size, lock_layout, widget_point)) {
                is_dragging = true;
                drag_initial = widget_point;
                drag_offset = { lock_layout.rest.x - widget_point.x, lock_layout.rest.y - widget_point.y };
                lock_anim_state = LockAnimState::idle;
                return 1;
            }
        } else if (action == AMOTION_EVENT_ACTION_MOVE) {
            if (is_dragging) {
                const auto target = emucorev::paf::page_turn_drag_target(widget_point, drag_initial, drag_offset);
                update_lock_fold(lock_fold_point(target));
                return 1;
            }
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            if (is_dragging) {
                is_dragging = false;
                const auto render_size = lock_render_size();
                const float dx = (rest_point.x - lock_point.x) * render_size.y;
                const float dy = (rest_point.y - lock_point.y) * render_size.y;
                const auto release = action == AMOTION_EVENT_ACTION_CANCEL
                    ? emucorev::paf::PageTurnRelease::within_threshold
                    : emucorev::paf::page_turn_release({dx, dy}, 280.f, false, false);
                if (release == emucorev::paf::PageTurnRelease::beyond_threshold) {
                    lock_anim_state = LockAnimState::peeling;
                    anim_elapsed = 0.f;
                    anim_duration = 0.500f;
                    anim_start_point = lock_point;
                    anim_target_point = lock_fold_point({
                        -2.f * lock_page_config.size.y, -2.f * lock_page_config.size.x});
                } else {
                    const float d_corner_x = (rest_point.x - lock_point.x) * render_size.y;
                    const float d_corner_y = (rest_point.y - lock_point.y) * render_size.y;
                    const float dist = std::sqrt(d_corner_x * d_corner_x + d_corner_y * d_corner_y);
                    const float duration_ms = ((((dist / render_size.y - lock_page_config.fold_fraction)
                        * 1.5f + 0.5f) / 1.5f) * 400.0f);
                    lock_anim_state = LockAnimState::returning;
                    anim_elapsed = 0.f;
                    anim_duration = std::max(duration_ms, 0.f) * 0.001f;
                    anim_start_point = lock_point;
                    anim_target_point = rest_point;
                }
                return 1;
            }
        }
        return 0;
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
        auto make = [&](std::uint32_t id, std::size_t slot, GLuint& texture) {
            const auto image = host_images.find(id);
            if (image == host_images.end()) return;
            const auto& source = image->second;
            if (texture && lock_texture_revisions[slot] == source.revision) return;
            if (!texture) glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            const bool same_size = lock_texture_widths[slot] == source.width
                && lock_texture_heights[slot] == source.height;
            if (same_size && lock_texture_revisions[slot] != 0) {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source.width, source.height,
                    GL_RGBA, GL_UNSIGNED_BYTE, source.rgba.data());
            } else {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, source.width, source.height, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, source.rgba.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            lock_texture_widths[slot] = source.width;
            lock_texture_heights[slot] = source.height;
            lock_texture_revisions[slot] = source.revision;
        };
        make(100, 0, lock_page); make(101, 1, lock_corner); make(102, 2, lock_exp);
        lock_ready = lock_page && lock_corner && lock_exp;
    }

    void draw_lock() {
        ensure_lock_textures();
        if (!lock || !lock_ready || !lock_mesh.vertex_count) return;
        if (lock_anim_state == LockAnimState::unlocked) return;

        // PAF's 0.08 corner inset is scaled by the inverse frame aspect before
        // the rounded-page shader receives {1-q, 1-q, 1/q}.
        constexpr float corner_size_x = std::bit_cast<float>(0x3f75c28fu);
        constexpr float corner_size_y = std::bit_cast<float>(0x3f6b851fu);
        constexpr float corner_inv_x = std::bit_cast<float>(0x41c80000u);
        constexpr float corner_inv_y = std::bit_cast<float>(0x41480000u);

        // Common fragment state with iterator_written_mask = 112 (0x70) to enable
        // TexCoord0 (16), TexCoord1/Corner (32), TexCoord2/Lighting (64)
        const std::array<float, 12> round_frag_state{
            0.f, 0.f, 1.f, 0.f,
            1.f, 0.f, 0.f,
            1.f / 960.f, 1.f / 544.f,
            0.f, 112.f, 0.f
        };

        // 1. Folded curled surface (Top round) - Event 127 in libpaf proto
        std::array<float, 40> top_round_fragment_material{};
        top_round_fragment_material[0] = corner_size_x;
        top_round_fragment_material[1] = corner_size_y;
        top_round_fragment_material[2] = corner_inv_x;
        top_round_fragment_material[3] = corner_inv_y;
        top_round_fragment_material[4] = lock_material_style.back_side_opacity;
        top_round_fragment_material[5] = 0.0f;
        std::copy_n(lock_top_material.begin() + 32, 4, top_round_fragment_material.begin() + 6);
        std::copy_n(lock_top_material.begin() + 76, 16, top_round_fragment_material.begin() + 22);

        emucorev::paf::PageSurfaceDraw folded_draw{};
        folded_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, 960.f, 544.f, .5f,  .5f, 0.f, 0.f, 0.f};
        folded_draw.fragment_state = round_frag_state;
        folded_draw.textures = {lock_page, lock_corner, 0, 0, lock_exp};
        folded_draw.blend = true;
        folded_draw.vertex_parameters = lock_top_material;
        folded_draw.fragment_parameters = top_round_fragment_material;

        // 2. Front surface (Bottom round) - Event 132 in libpaf proto
        std::array<float, 8> bottom_round_fragment_material{};
        bottom_round_fragment_material[0] = corner_size_x;
        bottom_round_fragment_material[1] = corner_size_y;
        bottom_round_fragment_material[2] = corner_inv_x;
        bottom_round_fragment_material[3] = corner_inv_y;

        emucorev::paf::PageSurfaceDraw front_draw{};
        front_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, 960.f, 544.f, .5f,  .5f, 0.f, 0.f, 0.f};
        front_draw.fragment_state = round_frag_state;
        front_draw.textures = {lock_page, lock_corner, 0, 0, lock_exp};
        front_draw.blend = true;
        front_draw.vertex_parameters = lock_bottom_material;
        front_draw.fragment_parameters = bottom_round_fragment_material;

        // 3. Empty back folded surface - Event 138 in libpaf proto
        std::array<float, 16> empty_back_fragment_material{};
        empty_back_fragment_material[0] = corner_size_x;
        empty_back_fragment_material[1] = corner_size_y;
        empty_back_fragment_material[2] = corner_inv_x;
        empty_back_fragment_material[3] = corner_inv_y;
        empty_back_fragment_material[4] = lock_effect_style.empty_corner_shadow;
        empty_back_fragment_material[5] = 0.0f;
        empty_back_fragment_material[6] = lock_effect_style.empty_color[0];
        empty_back_fragment_material[7] = lock_effect_style.empty_color[1];
        empty_back_fragment_material[8] = lock_effect_style.empty_color[2];
        empty_back_fragment_material[9] = 1.0f;
        empty_back_fragment_material[10] = lock_effect_style.empty_shadow_color[0];
        empty_back_fragment_material[11] = lock_effect_style.empty_shadow_color[1];
        empty_back_fragment_material[12] = lock_effect_style.empty_shadow_color[2];
        empty_back_fragment_material[13] = 0.0f;
        empty_back_fragment_material[14] = 1.0f;
        empty_back_fragment_material[15] = 1.0f;

        emucorev::paf::PageSurfaceDraw empty_draw{};
        empty_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, 960.f, 544.f, .5f,  .5f, 0.f, 0.f, 0.f};
        empty_draw.fragment_state = round_frag_state;
        empty_draw.textures = {lock_corner, lock_page, 0, 0, 0};
        empty_draw.blend = true;
        empty_draw.vertex_parameters = lock_empty_material;
        empty_draw.fragment_parameters = empty_back_fragment_material;
        // Painter order follows the original surface relationship: the page
        // body is laid down first, its underside next, and the curled page
        // face last so it remains visible where the meshes overlap.
        lock->draw(emucorev::paf::PageSurface::front, front_draw);
        lock->draw(emucorev::paf::PageSurface::empty_back, empty_draw);
        lock->draw(emucorev::paf::PageSurface::folded, folded_draw);
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
        if (preview_scene.load() == 2) {
            sprites->begin(960, 544);
            // The raw wallpaper is the underlying screen; the clock/date are
            // part of texture 100 and move with the PageTurn surface.
            sprites->draw(103, 0, 0, 960, 544);
            draw_lock();
            draw_status(charging_texture);
        }
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
                    if (preview_scene.load() == 0 || preview_scene.load() == 2) {
                        const std::lock_guard lock(status_mutex);
                        preview.charging.advance(device_status.charging, device_status.battery_percent);
                    }
                    const float elapsed = preview.last_draw ? float(time - preview.last_draw) * 1e-9f : 0;
                    if (elapsed > 0 && elapsed <= 1) {
                        if (preview_scene.load() == 1) preview.busy.tick(elapsed);
                        else if (preview_scene.load() == 0) {
                            preview.phase = emucorev::paf::advance_bubble_phase(preview.phase, elapsed, 1);
                            preview.window->set_phase(preview.phase);
                        } else if (preview_scene.load() == 2 && preview.window) {
                            preview.window->tick_lock(elapsed);
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
        if (!preview.active()) return 0;
        const int scene = preview_scene.load();
        if (scene == 1) {
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
        } else if (scene == 2 && preview.window) {
            const int handled = preview.window->handle_input(event);
            if (handled) {
                preview.last_draw = preview.next_draw = 0;
                preview.schedule();
            }
            return handled;
        }
        return 0;
    };
    // Native frame callbacks wake this looper only while visible/focused.
    // Background work blocks on events, with no render loop or polling timer.
    while (!app->destroyRequested) {
        android_poll_source* source = nullptr;
        if (ALooper_pollOnce(-1, nullptr, nullptr, reinterpret_cast<void**>(&source)) >= 0 && source)
            source->process(app, source);
    }
}
