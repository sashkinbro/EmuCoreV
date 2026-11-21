#include <emucorev/paf/bubble_renderer.h>
#include <emucorev/paf/bubble_env_renderer.h>
#include <emucorev/paf/bubble_shaders.h>
#include <emucorev/paf/bubble_motion.h>
#include <emucorev/paf/device_status.h>
#include <emucorev/paf/focus_cursor_renderer.h>
#include <emucorev/paf/home_edit.h>
#include <emucorev/paf/home_layout.h>
#include <emucorev/paf/livearea_sheet.h>
#include <emucorev/paf/sprite_renderer.h>
#include <emucorev/paf/busy_indicator.h>
#include <emucorev/paf/charging_indicator.h>
#include <emucorev/paf/page_turn_renderer.h>
#include <emucorev/paf/page_turn_material.h>
#include <emucorev/paf/page_turn_shaders.h>
#include <emucorev/paf/plane_div.h>
#include <emucorev/paf/wave_preset.h>
#include <emucorev/paf/wave_renderer.h>
#include "app_screen.h"
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
#include <optional>
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
struct HomeCatalogItem {
    std::string id, title, content_id;
    std::uint32_t icon_image_id = 0, label_image_id = 0;
};
using ScreenRect = std::array<float, 4>;
struct LiveAreaLayer { std::uint32_t image_id = 0; ScreenRect rect{}; std::uint32_t color = 0xffffffffu; };
struct LiveAreaTarget { std::string id; ScreenRect rect{}; };
struct LiveAreaSnapshot {
    std::string title_id;
    std::uint32_t background_id = 0, gate_id = 0;
    std::uint32_t gate_start_label_id = 0, gate_control_id = 0;
    ScreenRect background_rect{}, gate_rect{};
    std::vector<LiveAreaLayer> frames, texts;
    std::vector<LiveAreaTarget> targets;
    std::vector<emucorev::paf::PlaneDivScreenQuad> gate_frame_quads;
    std::vector<emucorev::paf::PlaneDivScreenQuad> gate_glow_quads;
    std::vector<emucorev::paf::PlaneDivScreenQuad> gate_control_quads;
    float gate_label_width = 136.f;
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
constexpr std::uint32_t home_icon_image_min = 1000, home_icon_image_limit = 2000;
constexpr std::uint32_t home_highlight_image_id = 0xa952ef1eu;
constexpr std::uint32_t home_focus_ring_image_id = 0xcf115394u;
constexpr std::uint32_t home_page_dot_active_id = 0x21ad6d05u;
constexpr std::uint32_t home_page_dot_inactive_id = 0x266627f6u;
constexpr std::uint32_t lock_empty_bg_image_id = 0xca707736u;
constexpr std::uint32_t raw_keylock_image_id = 103u;
constexpr std::uint32_t status_bar_bg_id = 0xbef34d7fu;
constexpr std::uint32_t status_home_icon_id = 0x56aebd5fu;
constexpr std::uint32_t status_notif_orb_idle_id = 0x6821404au;
constexpr std::uint32_t status_notif_orb_glow_id = 0x94de0b89u;
constexpr std::uint32_t status_app_chip_mask_id = 0xfbc7297eu;
constexpr std::uint32_t topmenu_arrow_left_id = 0x8ca9d181u;
constexpr std::uint32_t topmenu_arrow_right_id = 0x168e9e6bu;
constexpr std::uint32_t livearea_gate_frame_id = 0xca6a6db1u;
constexpr std::uint32_t livearea_gate_glow_id = 0xd5dd35e7u;
constexpr std::uint32_t livearea_start_control_id = 0x1e6a33deu;
constexpr std::uint32_t edit_new_page_icon_id = 0x23ab06c5u;
constexpr std::uint32_t edit_wallpaper_icon_id = 0x02024c2cu;
constexpr std::uint32_t edit_dots_badge_id = 0xfede49bfu;
constexpr std::uint32_t edit_callout_bg_id = 0xd29cb815u;
constexpr std::uint32_t edit_back_icon_id = 0x2cac72e8u;
constexpr std::uint32_t folder_panel_bg_id = 0xd8cdfae1u;
constexpr std::uint32_t folder_icon_id = 0xe35fe13au;
constexpr std::uint32_t folder_title_pill_id = 0xf1455f7eu;
constexpr std::uint32_t home_chip_image_base = 40000u;
constexpr std::uint32_t folder_label_image_id = 45000u;
constexpr std::uint32_t edit_create_folder_label_id = 45001u;
constexpr std::uint32_t edit_ungroup_label_id = 45002u;
constexpr std::uint32_t busy_ball_image_id = 0x754463d1u;
constexpr std::uint32_t notification_balloon_id = 0xfec9dfd8u; // indicator_plugin balloon with tail
constexpr std::uint32_t notification_text_image_id = 45010u;
constexpr std::array<float, 3> busy_ball_tint{0.66f, 0.9f, 1.f};
constexpr int sound_decide = 1;
constexpr int sound_cancel = 2;
constexpr int sound_focus = 3;
constexpr int sound_page_scroll = 4;
constexpr int sound_page_turn = 5;
std::unordered_map<std::uint32_t, HostImage> host_images;
std::vector<HomeCatalogItem> home_catalog;
std::uint64_t home_catalog_revision = 0;
std::unordered_map<std::string, LiveAreaSnapshot> livearea_catalog;
std::uint64_t livearea_catalog_revision = 0;
std::string persisted_home_layout;
std::uint64_t persisted_home_layout_revision = 0;
std::atomic<bool> native_back_requested{false};
std::atomic<int> native_ready_scene{-1};
std::atomic<bool> native_preview_failed{false};
std::atomic<bool> native_preview_loading{false};
ALooper* native_preview_looper = nullptr;
JavaVM* preview_java_vm = nullptr;
jobject preview_activity_ref = nullptr;
jmethodID livearea_target_callback = nullptr;
jmethodID request_livearea_callback = nullptr;
jmethodID exit_request_callback = nullptr;
jmethodID app_row_callback = nullptr;
jmethodID app_back_callback = nullptr;
// Host-built application pages waiting for the render thread.
struct PendingAppPage { shell_app::Page page; int transition = 0; };
std::vector<PendingAppPage> pending_app_pages;
bool pending_app_close = false;
jmethodID ui_sound_callback = nullptr;
jmethodID save_home_layout_callback = nullptr;
std::uint64_t image_revision = 0;
std::atomic<int> preview_scene{0}; // 0 bubble, 1 busy indicator, 2 lock screen, 3 home, 4 wave
std::string resource_root;
// Physical safe area from the host window, in surface pixels:
// cutout insets (left, top, right, bottom) and rounded corner radii (TL, TR, BR, BL).
std::array<float, 8> safe_area_px{};
// Settings > Sound & Display > Display Area: 0 full screen, 1 original 960x544.
std::atomic<int> display_area_mode{0};
// Theme: Home page backgrounds (host image ids, page order) replacing the
// wave, and the information bar colour (ARGB, 0 = default).
std::vector<std::uint32_t> home_wallpaper_ids;
std::uint32_t theme_bar_color = 0;
std::string requested_wave_theme = "effects_default_0512_01";
std::uint64_t wave_theme_revision = 1;
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

// Calls a void (String) or (String, String) method on the host Activity from any thread.
void emit_activity_strings(jmethodID* slot, const std::string& first, const std::string* second) {
    JavaVM* vm = nullptr;
    { const std::lock_guard lock_guard(status_mutex); vm = preview_java_vm; }
    if (!vm) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (state != JNI_OK || !env) return;

    jobject activity = nullptr;
    jmethodID callback = nullptr;
    {
        const std::lock_guard lock_guard(status_mutex);
        callback = *slot;
        if (preview_activity_ref && callback) activity = env->NewGlobalRef(preview_activity_ref);
    }
    if (activity && callback) {
        const auto a = env->NewStringUTF(first.c_str());
        const auto b = second ? env->NewStringUTF(second->c_str()) : nullptr;
        if (a && (!second || b)) {
            if (second) env->CallVoidMethod(activity, callback, a, b);
            else env->CallVoidMethod(activity, callback, a);
            if (env->ExceptionCheck()) env->ExceptionClear();
        }
        if (a) env->DeleteLocalRef(a);
        if (b) env->DeleteLocalRef(b);
        env->DeleteGlobalRef(activity);
    }
    if (attached) vm->DetachCurrentThread();
}

void emit_livearea_target_request(const std::string& title_id, const std::string& target) {
    emit_activity_strings(&livearea_target_callback, title_id, &target);
}

// Calls onNativeAppRow(String, int, int) / onNativeAppBack(String, int).
void emit_app_event(jmethodID* slot, const std::string& app, int key, int row, bool with_row) {
    JavaVM* vm = nullptr;
    { const std::lock_guard lock_guard(status_mutex); vm = preview_java_vm; }
    if (!vm) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (state != JNI_OK || !env) return;
    jobject activity = nullptr;
    jmethodID callback = nullptr;
    {
        const std::lock_guard lock_guard(status_mutex);
        callback = *slot;
        if (preview_activity_ref && callback) activity = env->NewGlobalRef(preview_activity_ref);
    }
    if (activity && callback) {
        if (const auto name = env->NewStringUTF(app.c_str())) {
            if (with_row) env->CallVoidMethod(activity, callback, name, jint(key), jint(row));
            else env->CallVoidMethod(activity, callback, name, jint(key));
            if (env->ExceptionCheck()) env->ExceptionClear();
            env->DeleteLocalRef(name);
        }
        env->DeleteGlobalRef(activity);
    }
    if (attached) vm->DetachCurrentThread();
}

// Back on an otherwise idle Home: the host decides whether to leave the shell.
void emit_exit_request() {
    emit_activity_strings(&exit_request_callback, "back", nullptr);
}

void emit_request_livearea(const std::string& title_id) {
    emit_activity_strings(&request_livearea_callback, title_id, nullptr);
}

void emit_ui_sound(int sound_id) {
    JavaVM* vm = nullptr;
    { const std::lock_guard lock_guard(status_mutex); vm = preview_java_vm; }
    if (!vm) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (state != JNI_OK || !env) return;

    jobject activity = nullptr;
    jmethodID callback = nullptr;
    {
        const std::lock_guard lock_guard(status_mutex);
        if (preview_activity_ref && ui_sound_callback)
            activity = env->NewGlobalRef(preview_activity_ref);
        callback = ui_sound_callback;
    }
    if (activity && callback) {
        env->CallVoidMethod(activity, callback, static_cast<jint>(sound_id));
        env->DeleteGlobalRef(activity);
    }
    if (attached) vm->DetachCurrentThread();
}

void emit_save_home_layout(const std::string& layout) {
    JavaVM* vm = nullptr;
    { const std::lock_guard lock_guard(status_mutex); vm = preview_java_vm; }
    if (!vm) return;
    JNIEnv* env = nullptr;
    bool attached = false;
    const jint state = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (state == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        attached = true;
    } else if (state != JNI_OK || !env) return;

    jobject activity = nullptr;
    jmethodID callback = nullptr;
    {
        const std::lock_guard lock_guard(status_mutex);
        if (preview_activity_ref && save_home_layout_callback)
            activity = env->NewGlobalRef(preview_activity_ref);
        callback = save_home_layout_callback;
    }
    if (activity && callback) {
        const auto value = env->NewStringUTF(layout.c_str());
        if (value) {
            env->CallVoidMethod(activity, callback, value);
            env->DeleteLocalRef(value);
        }
        env->DeleteGlobalRef(activity);
    }
    if (attached) vm->DetachCurrentThread();
}

// Shell state that must outlive a surface: Android destroys the native window
// while a game runs, but the Vita returns to the game's LiveArea, unlocked.
struct ShellSession {
    bool unlocked = false;
    emucorev::paf::LiveAreaDeck deck;
    int focus_page = 0, focus_slot = 0;
};
std::optional<ShellSession> shell_session;

struct Window {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint target = 0, color = 0, depth = 0;
    std::unique_ptr<emucorev::paf::BubbleRenderer> bubble;
    std::unique_ptr<emucorev::paf::BubbleEnvRenderer> bubble_env;
    std::unique_ptr<emucorev::paf::FocusCursorRenderer> focus_cursor;
    std::unique_ptr<emucorev::paf::SpriteRenderer> sprites;
    std::unique_ptr<emucorev::paf::PageTurnRenderer> lock;
    std::unique_ptr<emucorev::paf::PageTurnRenderer> livearea_pageturn;
    std::unique_ptr<emucorev::paf::WaveRenderer> wave;
    std::uint64_t applied_wave_theme_revision = 0;
    std::size_t wave_theme_index = 0;
    GLuint livearea_fbo = 0, livearea_tex = 0;

    // Adaptive canvas. The original 960x544 layout stays intact and centred
    // (content origin cx0/cy0); the canvas grows along the longer axis so the
    // background, status bar and edge-anchored chrome fill the whole display
    // instead of letterboxing. safe_* keep edge chrome clear of rounded
    // corners and cutouts (logical units).
    float cw = 960.f, ch = 544.f, cx0 = 0.f, cy0 = 0.f, surf_scale = 1.f;
    float safe_l = 0.f, safe_r = 0.f, safe_t = 0.f, safe_b = 0.f;
    // Notification orb enlargement on panels whose top-right corner is rounded,
    // so the part left visible stays comfortably tappable (1 = original size).
    float orb_scale = 1.f;
    int target_w = 0, target_h = 0;
    EGLint surface_w = 0, surface_h = 0;
    // Content-space coordinates of the visible canvas edges (safe area applied).
    float edge_left() const noexcept { return -cx0 + safe_l; }
    float edge_right() const noexcept { return 960.f + cx0 - safe_r; }
    float edge_bottom() const noexcept { return 544.f + cy0 - safe_b; }
    float canvas_top() const noexcept { return -cy0; }
    // Horizontal pager distance between Home and LiveArea pages on this canvas.
    float pager_scale() const noexcept { return cw / 960.f; }
    void sprites_begin() {
        sprites->begin(cw, ch);
        sprites->set_origin(cx0, cy0);
    }

    struct BubbleVisualSlot {
        emucorev::paf::HomeBubbleFloatState float_state{};
        float wobble_phase_offset = 0.f;
    };
    std::vector<HomeCatalogItem> home_catalog_items;
    std::vector<std::string> home_catalog_ids;
    std::vector<std::unique_ptr<emucorev::paf::BubbleRenderer>> catalog_bubble_renderers;
    std::unordered_map<std::string, BubbleVisualSlot> bubble_slots;
    emucorev::paf::HomeDesktopState desktop;
    emucorev::paf::HomeEditVisualState edit_visual;
    std::uint64_t applied_home_layout_revision = 0;

    emucorev::paf::LiveAreaDeck livearea_deck;
    emucorev::paf::PageTurnMesh livearea_mesh;
    std::unordered_map<std::string, LiveAreaSnapshot> cached_livearea_snapshots;
    std::uint64_t applied_livearea_catalog_revision = 0;

    bool home_visible = false;
    int home_page = 0, home_focus_page = 0, home_focus_slot = 0;
    int home_touch_page = -1, home_touch_slot = -1;
    bool home_touch_down = false;
    float home_hold_elapsed = 0.f;
    float home_enter_elapsed = 1.f;
    emucorev::paf::PagePoint home_touch_start{};
    emucorev::paf::HomeVerticalScroll home_scroll;
    float home_motion_phase = 0.f;
    float home_focus_pulse = 0.f;
    float status_orb_phase = 0.f;
    float home_drag_last_x = 0.f, home_drag_last_y = 0.f;
    std::int64_t home_drag_last_event_ns = 0;
    bool home_drag_moved = false;
    bool home_horiz_drag = false;
    bool topbar_drag = false;

    // Edit Mode & Folder interaction state
    bool edit_drag_active = false;
    int edit_drag_src_page = -1, edit_drag_src_slot = -1;
    float edit_drag_x = 0.f, edit_drag_y = 0.f;
    float edit_edge_scroll_cooldown = 0.f;
    bool callout_open = false;
    int callout_page = -1, callout_slot = -1;
    float callout_x = 0.f, callout_y = 0.f;
    int folder_focus_idx = 0, folder_touch_idx = -1;
    bool peeker_peel_drag = false;

    shell_app::AppScreen app_screen;
    shell_app::Canvas app_canvas() const noexcept {
        return {cw, ch, cx0, cy0, edge_left(), edge_bottom()};
    }
    void wire_app_screen() {
        app_screen.on_row = [](const std::string& app, int key, int row) {
            emit_app_event(&app_row_callback, app, key, row, true);
        };
        app_screen.on_back = [](const std::string& app, int key) {
            emit_app_event(&app_back_callback, app, key, 0, false);
        };
        app_screen.on_sound = [](int id) { emit_ui_sound(id); };
        app_screen.back_icon = 0x352ffde1u;
        app_screen.back_bg = 0xfb2979d5u;
        app_screen.back_highlight = 0xc903feecu;
    }
    void apply_pending_app_pages() {
        std::vector<PendingAppPage> pages;
        bool close = false;
        {
            const std::lock_guard lock_guard(status_mutex);
            pages.swap(pending_app_pages);
            close = pending_app_close;
            pending_app_close = false;
        }
        for (auto& p : pages)
            app_screen.show(std::move(p.page), static_cast<shell_app::Transition>(p.transition));
        if (close) app_screen.close();
    }

    // Notification indicator balloon (top-right orb).
    bool notif_open = false;
    float notif_blend = 0.f;

    GLuint focus_ring_tex = 0;
    std::uint64_t focus_ring_rev = 0;
    std::uint64_t applied_home_catalog_revision = 0;
    std::vector<emucorev::paf::BubbleVertex> bubble_vertices;
    std::vector<std::uint16_t> bubble_indices;
    std::vector<std::uint8_t> bubble_environment;
    std::vector<std::uint8_t> reference_icon;
    GLuint lock_page = 0, lock_corner = 0, lock_exp = 0, lock_empty_bg = 0;
    std::array<std::uint64_t, 4> lock_texture_revisions{};
    std::array<unsigned, 4> lock_texture_widths{}, lock_texture_heights{};
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
    float lock_page_alpha = 1.f;
    float lock_exit_z = 0.f;

    std::unordered_map<std::uint32_t, std::uint64_t> uploaded_images;
    std::uint64_t uploaded_revision = 0;
    std::array<float, 96> vertex{};
    std::array<float, 96> base_vertex{};
    std::array<float, 10> material{};
    emucorev::paf::BubbleBasis initial_basis{};
    float initial_phase = 0;

    ~Window() {
        const int scene = preview_scene.load();
        if ((scene == 2 || scene == 3) && (home_visible || lock_anim_state == LockAnimState::unlocked)) {
            ShellSession session;
            session.unlocked = true;
            session.deck = livearea_deck;
            session.focus_page = home_focus_page;
            session.focus_slot = home_focus_slot;
            shell_session = std::move(session);
        }
        // GPU resources must die before their context, including partial init.
        if (context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE)
            eglMakeCurrent(display, surface, surface, context);
        // Catalog bubble renderers own GL objects; destroy them while their
        // originating context is current, before EGL teardown below.
        catalog_bubble_renderers.clear();
        bubble_env.reset();
        focus_cursor.reset();
        wave.reset();
        bubble.reset();
        sprites.reset();
        livearea_pageturn.reset();
        lock.reset();
        if (livearea_fbo) glDeleteFramebuffers(1, &livearea_fbo);
        if (livearea_tex) glDeleteTextures(1, &livearea_tex);
        if (focus_ring_tex) glDeleteTextures(1, &focus_ring_tex);
        if (lock_page) glDeleteTextures(1, &lock_page);
        if (lock_corner) glDeleteTextures(1, &lock_corner);
        if (lock_exp) glDeleteTextures(1, &lock_exp);
        if (lock_empty_bg) glDeleteTextures(1, &lock_empty_bg);
        if (target) glDeleteFramebuffers(1, &target);
        if (depth) glDeleteRenderbuffers(1, &depth);
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

        // Busy preview is isolated; lock and Home scenes share this original
        // bubble mesh, material, and environment resource set.
        if (preview_scene.load() == 0 || preview_scene.load() == 2 || preview_scene.load() == 3) {
            bubble_vertices = read<emucorev::paf::BubbleVertex>("stream-0.bin", 608);
            bubble_indices = read<std::uint16_t>("indices.bin", 1220);
            const auto transform = read<float>("vertex-ubo-0.bin", 96);
            const auto parameters = read<float>("fragment-ubo-0.bin", 10);
            std::copy(transform.begin(), transform.end(), vertex.begin());
            base_vertex = vertex;
            initial_phase = std::atan2(vertex[14 * 4 + 1], vertex[14 * 4]);
            initial_basis = emucorev::paf::bubble_wobble_basis(std::cos(initial_phase), std::sin(initial_phase), 1);
            std::copy(parameters.begin(), parameters.end(), material.begin());
            const auto palette = read<std::uint8_t>("fragment-texture-0-palette.bin", 1024);
            const auto indexed = read<std::uint8_t>("fragment-texture-0.bin", 128 * 128);
            bubble_environment = read<std::uint8_t>("environment.rgba", 64 * 64 * 4);
            std::vector<std::uint8_t> icon(128 * 128 * 4);
            for (std::size_t i = 0; i < indexed.size(); ++i)
                std::copy_n(palette.data() + indexed[i] * 4, 4, icon.data() + i * 4);
            reference_icon = std::move(icon);
            if (preview_scene.load() == 0) {
                bubble = std::make_unique<emucorev::paf::BubbleRenderer>(
                    emucorev::paf::opaque_bubble_vertex_shader(), emucorev::paf::opaque_bubble_fragment_shader(),
                    bubble_vertices, bubble_indices,
                    emucorev::paf::BubbleImage{128, 128, reference_icon},
                    emucorev::paf::BubbleImage{64, 64, bubble_environment});
            }
        }
        sprites = std::make_unique<emucorev::paf::SpriteRenderer>();
        if (preview_scene.load() == 2 || preview_scene.load() == 3) {
            bubble_env = std::make_unique<emucorev::paf::BubbleEnvRenderer>();
            focus_cursor = std::make_unique<emucorev::paf::FocusCursorRenderer>();
            livearea_pageturn = std::make_unique<emucorev::paf::PageTurnRenderer>(
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_bottom_vertex_shader(),
                    emucorev::paf::page_turn_bottom_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_top_vertex_shader(),
                    emucorev::paf::page_turn_top_fragment_shader()});
            glGenTextures(1, &livearea_tex);
            glBindTexture(GL_TEXTURE_2D, livearea_tex);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 840, 500);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &livearea_fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, livearea_fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, livearea_tex, 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("Cannot create 840x500 LiveArea sheet framebuffer");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        if (preview_scene.load() == 2) {
            lock = std::make_unique<emucorev::paf::PageTurnRenderer>(
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_bottom_round_vertex_shader(),
                    emucorev::paf::page_turn_bottom_round_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_top_round_vertex_shader(),
                    emucorev::paf::page_turn_top_round_fragment_shader()},
                emucorev::paf::PageSurfaceShaders{},
                emucorev::paf::PageSurfaceShaders{emucorev::paf::page_turn_empty_back_vertex_shader(),
                    emucorev::paf::page_turn_empty_back_fragment_shader()});

            lock_anim_state = LockAnimState::idle;
            lock_page_alpha = 1.f;
            lock_exit_z = 0.f;
            lock_layout = emucorev::paf::page_turn_layout(lock_page_config.size,
                lock_page_config.fold_fraction, lock_page_config.corner_aspect);
            update_lock_fold(lock_fold_point(lock_layout.rest));
        }
        home_visible = preview_scene.load() == 3;

        EGLint sw = 0, sh = 0;
        eglQuerySurface(display, surface, EGL_WIDTH, &sw);
        eglQuerySurface(display, surface, EGL_HEIGHT, &sh);
        ensure_canvas(sw, sh);

        wire_app_screen();
        if ((preview_scene.load() == 2 || preview_scene.load() == 3) && shell_session) {
            livearea_deck = shell_session->deck;
            home_focus_page = shell_session->focus_page;
            home_focus_slot = shell_session->focus_slot;
            if (shell_session->unlocked) {
                lock_anim_state = LockAnimState::unlocked;
                home_visible = true;
                home_enter_elapsed = 1.f;
            }
        }
    }

    // 1x rasterization at the original UI density: the logical canvas keeps
    // 544 rows (or 960 columns on narrower panels) and extends the other axis
    // to the display aspect.
    void ensure_canvas(EGLint width, EGLint height) {
        float w = 960.f, h = 544.f;
        const bool original_area = display_area_mode.load() == 1;
        if (width > 0 && height > 0 && !original_area) {
            const float aspect = float(width) / float(height);
            if (aspect >= 960.f / 544.f) w = std::min(std::round(544.f * aspect), 1600.f);
            else h = std::min(std::round(960.f / aspect), 900.f);
        }
        float l = 0, t = 0, r = 0, b = 0;
        if (width > 0 && height > 0) {
            std::array<float, 8> area{};
            { const std::lock_guard lock_guard(status_mutex); area = safe_area_px; }
            const float scale = std::min(float(width) / w, float(height) / h);
            // A rounded corner of radius R clips content at distance d from an
            // edge by R - sqrt(R^2 - (R - d)^2); probe the status bar centre line.
            auto corner = [&](float radius_px) {
                const float d = 16.f * scale;
                if (radius_px <= d) return 0.f;
                const float k = radius_px - d;
                return (radius_px - std::sqrt(radius_px * radius_px - k * k)) / scale;
            };
            if (!original_area) {
                l = std::max(area[0] / scale, std::max(corner(area[4]), corner(area[7])));
                r = std::max(area[2] / scale, std::max(corner(area[5]), corner(area[6])));
                t = area[1] / scale;
                b = area[3] / scale;
            }
            surf_scale = scale;
            orb_scale = original_area ? 1.f : 1.f + std::clamp(area[5] / scale / 72.f, 0.f, 0.45f);
            surface_w = width;
            surface_h = height;
        }
        safe_l = l; safe_r = r; safe_t = t; safe_b = b;
        const bool resized = cw != w || ch != h;
        cw = w; ch = h;
        cx0 = (w - 960.f) * .5f;
        cy0 = (h - 544.f) * .5f;
        const int tw = int(w), th = int(h);
        // Lock PageTurn transforms bake the canvas projection; rebuild them.
        if (resized && lock && lock_mesh.vertex_count) update_lock_fold(lock_point);
        if (tw == target_w && th == target_h && target) return;
        if (target) glDeleteFramebuffers(1, &target);
        if (depth) glDeleteRenderbuffers(1, &depth);
        if (color) glDeleteTextures(1, &color);
        target = depth = color = 0;
        glGenTextures(1, &color);
        glBindTexture(GL_TEXTURE_2D, color);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, tw, th);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenRenderbuffers(1, &depth);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, tw, th);
        glGenFramebuffers(1, &target);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Cannot create 1x preview framebuffer");
        target_w = tw;
        target_h = th;
    }

    // Maps a physical touch point to the original 960x544 content space.
    std::pair<float, float> to_content(float raw_x, float raw_y) const {
        const float ox = (float(surface_w) - cw * surf_scale) * .5f;
        const float oy = (float(surface_h) - ch * surf_scale) * .5f;
        return {(raw_x - ox) / surf_scale - cx0, (raw_y - oy) / surf_scale - cy0};
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
        const float zoom = 1.f / std::max(0.25f, 1.f - lock_exit_z / 960.f);
        return {(2.f / cw) * zoom, 0.f, 0.f, 0.f,
            0.f, (2.f / ch) * zoom, 0.f, 0.f,
            0.f, 0.f, 0.f, 0.f,
            0.f, 0.f, 0.f, 1.f};
    }

    void persist_home_layout() const {
        if (home_catalog_ids.empty()) return;
        emit_save_home_layout(desktop.serialize_layout(home_catalog_ids));
    }

    emucorev::paf::HomeScreenPoint entry_screen_position(
        int page, int slot, const std::string& entry_id, float offset_x = 0.f) const {
        const auto base = emucorev::paf::home_slot_screen_position(
            page, slot, home_scroll.scroll_y());
        const auto it = bubble_slots.find(entry_id);
        emucorev::paf::HomeScreenPoint float_off{};
        if (it != bubble_slots.end()) {
            float_off = emucorev::paf::bubble_float_offset(it->second.float_state);
        }
        const float lift_y = edit_visual.edit_wiggle_lift_y(page, slot);
        return {base.x + float_off.x + offset_x, base.y + float_off.y + lift_y};
    }

    void ensure_focus_ring_texture() {
        const std::lock_guard lock_guard(status_mutex);
        const auto it = host_images.find(home_focus_ring_image_id);
        if (it == host_images.end()) return;
        const auto& src = it->second;
        if (focus_ring_tex && focus_ring_rev == src.revision) return;
        if (!focus_ring_tex) glGenTextures(1, &focus_ring_tex);
        glBindTexture(GL_TEXTURE_2D, focus_ring_tex);
        if (focus_ring_rev != 0) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src.width, src.height,
                GL_RGBA, GL_UNSIGNED_BYTE, src.rgba.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, src.width, src.height, 0,
                GL_RGBA, GL_UNSIGNED_BYTE, src.rgba.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        focus_ring_rev = src.revision;
    }

    void sync_visual_slots_with_desktop() {
        std::unordered_map<std::string, BubbleVisualSlot> next;
        for (int p = 0; p < desktop.page_count(); ++p) {
            for (int s = 0; s < emucorev::paf::kHomeSlotsPerPage; ++s) {
                const auto* entry = desktop.entry_at(p, s);
                if (!entry) continue;
                const auto existing = bubble_slots.find(entry->id);
                if (existing != bubble_slots.end()) {
                    next.emplace(entry->id, existing->second);
                } else {
                    BubbleVisualSlot slot_vis{};
                    slot_vis.float_state = emucorev::paf::make_bubble_float_state(
                        p, s, home_scroll.scroll_y());
                    slot_vis.wobble_phase_offset = static_cast<float>(s) * 0.73f
                        + static_cast<float>(p) * 1.41f;
                    next.emplace(entry->id, slot_vis);
                }
            }
        }
        bubble_slots = std::move(next);
    }

    bool ensure_home_initialized() {
        std::vector<HomeCatalogItem> catalog;
        std::uint64_t cat_rev = 0;
        std::uint64_t layout_rev = 0;
        std::string layout_str;
        {
            const std::lock_guard lock_guard(status_mutex);
            cat_rev = home_catalog_revision;
            layout_rev = persisted_home_layout_revision;
            if (!cat_rev) return false;
            if (cat_rev == applied_home_catalog_revision
                && layout_rev == applied_home_layout_revision) {
                return true;
            }
            if (cat_rev != applied_home_catalog_revision) catalog = home_catalog;
            if (layout_rev != applied_home_layout_revision) layout_str = persisted_home_layout;
        }
        if (cat_rev != applied_home_catalog_revision) {
            home_catalog_items = std::move(catalog);
            home_catalog_ids.clear();
            home_catalog_ids.reserve(home_catalog_items.size());
            for (const auto& item : home_catalog_items) home_catalog_ids.push_back(item.id);
            catalog_bubble_renderers.clear();
            catalog_bubble_renderers.resize(home_catalog_items.size());
            desktop.sync_with_catalog(home_catalog_ids);
            applied_home_catalog_revision = cat_rev;
        }
        if (layout_rev != applied_home_layout_revision) {
            if (!layout_str.empty()) {
                desktop.apply_serialized_layout(layout_str, home_catalog_ids);
            }
            applied_home_layout_revision = layout_rev;
        }
        sync_visual_slots_with_desktop();
        const auto* focused = desktop.entry_at(home_focus_page, home_focus_slot);
        if (!focused) {
            const auto first_occ = desktop.find_first_occupied(0);
            home_focus_page = first_occ.first;
            home_focus_slot = first_occ.second;
        }
        home_scroll.set_page_count(desktop.page_count());
        home_page = std::clamp(home_focus_page, 0, desktop.page_count() - 1);
        home_scroll.jump_to_page(home_page);
        return true;
    }

    emucorev::paf::BubbleRenderer* ensure_catalog_bubble_renderer(int catalog_index) {
        if (catalog_index < 0 || static_cast<std::size_t>(catalog_index) >= home_catalog_items.size())
            return nullptr;
        auto& slot_ptr = catalog_bubble_renderers[static_cast<std::size_t>(catalog_index)];
        if (slot_ptr) return slot_ptr.get();
        HostImage icon;
        {
            const std::lock_guard lock_guard(status_mutex);
            const auto image = host_images.find(
                home_catalog_items[static_cast<std::size_t>(catalog_index)].icon_image_id);
            if (image == host_images.end()) return nullptr;
            icon = image->second;
        }
        slot_ptr = std::make_unique<emucorev::paf::BubbleRenderer>(
            emucorev::paf::opaque_bubble_vertex_shader(), emucorev::paf::opaque_bubble_fragment_shader(),
            bubble_vertices, bubble_indices,
            emucorev::paf::BubbleImage{icon.width, icon.height, icon.rgba},
            emucorev::paf::BubbleImage{64, 64, bubble_environment});
        return slot_ptr.get();
    }

    std::array<float, 96> home_bubble_transform(float phase, float phase_offset,
        float screen_x, float screen_y, float scale,
        float spin_y_rad = 0.f, float roll_z_rad = 0.f) const {
        auto output = base_vertex;
        const float item_phase = phase + phase_offset;
        const auto basis = emucorev::paf::bubble_wobble_basis(std::cos(item_phase), std::sin(item_phase), 1.f);
        std::array<std::array<float, 3>, 3> rot{};
        for (unsigned row = 0; row < 3; ++row)
            for (unsigned column = 0; column < 3; ++column)
                for (unsigned axis = 0; axis < 3; ++axis)
                    rot[row][column] += basis[axis][row] * initial_basis[axis][column];

        auto mul_left = [&](const std::array<std::array<float, 3>, 3>& lhs) {
            std::array<std::array<float, 3>, 3> out{};
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned c = 0; c < 3; ++c)
                    for (unsigned k = 0; k < 3; ++k)
                        out[r][c] += lhs[r][k] * rot[k][c];
            rot = out;
        };
        if (std::abs(spin_y_rad) > 1.0e-4f) {
            const float cy = std::cos(spin_y_rad), sy = std::sin(spin_y_rad);
            mul_left({{{cy, 0.f, sy}, {0.f, 1.f, 0.f}, {-sy, 0.f, cy}}});
        }
        if (std::abs(roll_z_rad) > 1.0e-4f) {
            const float cz = std::cos(roll_z_rad), sz = std::sin(roll_z_rad);
            mul_left({{{cz, -sz, 0.f}, {sz, cz, 0.f}, {0.f, 0.f, 1.f}}});
        }

        for (const unsigned first : {4u, 12u})
            for (unsigned column = first; column < first + 3; ++column)
                for (unsigned row = 0; row < 3; ++row) {
                    float value = 0;
                    for (unsigned axis = 0; axis < 3; ++axis)
                        value += rot[row][axis] * base_vertex[column * 4 + axis];
                    output[column * 4 + row] = (first == 4u) ? (value * scale) : value;
                }

        // Keep the bubble's original pixel size on the adaptive canvas.
        output[0] *= 960.f / cw;
        output[5] *= 544.f / ch;
        const float depth = output[7 * 4 + 2];
        const float clip_w = output[2 * 4 + 3] * depth + output[3 * 4 + 3];
        const float ndc_x = ((screen_x + cx0) / (cw * .5f)) - 1.f;
        const float ndc_y = 1.f - ((screen_y + cy0) / (ch * .5f));
        output[7 * 4] = ndc_x * clip_w / output[0];
        output[7 * 4 + 1] = ndc_y * clip_w / output[5];
        return output;
    }

    void draw_nine_slice(std::uint32_t tex_id, float cx, float cy, float w, float h,
        std::uint16_t tex_w, std::uint16_t tex_h, std::uint16_t mh, std::uint16_t mv,
        std::array<float, 2> div_h, std::array<float, 2> div_v,
        std::array<float, 4> tint = {1.f, 1.f, 1.f, 1.f}) {
        emucorev::paf::PlaneDivSpec spec{};
        spec.size_x = w;
        spec.size_y = h;
        spec.tex_width = static_cast<std::int16_t>(tex_w);
        spec.tex_height = static_cast<std::int16_t>(tex_h);
        spec.div_mode = emucorev::paf::PlaneDivMode::Grid3x3;
        spec.div_ratio_h = div_h;
        spec.div_ratio_v = div_v;
        spec.margin_left = spec.margin_right = mh;
        spec.margin_top = spec.margin_bottom = mv;
        const auto quads = emucorev::paf::build_plane_div_screen_quads(spec, cx, cy);
        for (const auto& q : quads) {
            sprites->draw_patch(tex_id, q.x, q.y, q.width, q.height,
                q.u, q.v, q.uv_width, q.uv_height, tint);
        }
    }

    void draw_folder_bubble_badge(const emucorev::paf::HomeSlotEntry& entry,
        float bx, float by, float scale, float alpha) {
        const float d = emucorev::paf::kHomeBubbleSize * scale;
        sprites->draw(status_app_chip_mask_id, bx - d * 0.5f, by - d * 0.5f, d, d,
            {0.12f, 0.24f, 0.44f, 0.72f * alpha});
        sprites->draw(folder_icon_id, bx - 38.f * scale, by - 28.f * scale,
            76.f * scale, 57.f * scale, {0.88f, 0.94f, 1.f, 0.52f * alpha});
        const int show_count = std::min(4, static_cast<int>(entry.members.size()));
        for (int m = 0; m < show_count; ++m) {
            const float mx = (m % 2 == 0) ? -19.f * scale : 19.f * scale;
            const float my = (m < 2) ? -18.f * scale : 18.f * scale;
            const std::uint32_t chip_id = home_chip_image_base
                + static_cast<std::uint32_t>(entry.members[static_cast<std::size_t>(m)]);
            const float chip_s = 34.f * scale;
            sprites->draw(chip_id, bx + mx - chip_s * 0.5f, by + my - chip_s * 0.5f,
                chip_s, chip_s, {1.f, 1.f, 1.f, alpha});
        }
    }

    void draw_home_bubbles(float home_offset_x = 0.f, float home_alpha = 1.f,
        int exclude_catalog_idx = -1) {
        if (!ensure_home_initialized() || home_alpha <= 0.005f) return;
        const int pages = desktop.page_count();
        const float scroll_y = home_scroll.scroll_y();
        const int p_min = std::clamp(
            static_cast<int>(std::floor(scroll_y / emucorev::paf::kHomePageHeight)), 0, pages - 1);
        const int p_max = std::clamp(
            static_cast<int>(std::ceil(scroll_y / emucorev::paf::kHomePageHeight)), 0, pages - 1);

        ensure_focus_ring_texture();
        if (bubble_env) bubble_env->capture(color);
        const std::uint32_t env_tex = bubble_env ? bubble_env->texture() : 0u;

        // 1. Edit mode frosted page frames and empty slot indicator dots.
        if (edit_visual.edit_blend > 0.01f) {
            sprites_begin();
            const float eb = edit_visual.edit_blend * home_alpha;
            for (int p = p_min; p <= p_max; ++p) {
                const auto center = emucorev::paf::home_slot_screen_position(p, 5, scroll_y);
                const float page_cy = center.y - 4.f;
                draw_nine_slice(folder_panel_bg_id, 480.f + home_offset_x, page_cy, 888.f, 466.f,
                    64, 64, 16, 16, {0.5f, 0.5f}, {0.5f, 0.5f},
                    {0.85f, 0.92f, 1.f, 0.34f * eb});
                for (int s = 0; s < emucorev::paf::kHomeSlotsPerPage; ++s) {
                    if (desktop.entry_at(p, s)) continue;
                    const auto sp = emucorev::paf::home_slot_screen_position(p, s, scroll_y);
                    sprites->draw(home_page_dot_active_id, sp.x + home_offset_x - 6.f, sp.y - 6.f,
                        12.f, 12.f, {1.f, 1.f, 1.f, 0.72f * eb});
                }
            }
        }

        // Helper computes entrance stagger slide/scale after lock unlock peel.
        auto enter_params = [&](int slot, float& dx_out, float& s_out, float& a_out) {
            dx_out = 0.f;
            s_out = 1.f;
            a_out = home_alpha;
            if (home_enter_elapsed < 0.36f) {
                const float t = std::clamp(
                    (home_enter_elapsed - 0.12f - static_cast<float>(slot) * 0.016f) / 0.19f,
                    0.f, 1.f);
                const float e = 1.f - (1.f - t) * (1.f - t);
                dx_out = -95.f * (1.f - e);
                s_out = 0.72f + 0.28f * e;
                a_out *= e;
            }
        };

        // 2. Render opaque 3D app bubbles (excluding actively dragged or transitioning bubble).
        auto mat_copy = material;
        for (int p = p_min; p <= p_max; ++p) {
            for (int s = 0; s < emucorev::paf::kHomeSlotsPerPage; ++s) {
                const auto* entry = desktop.entry_at(p, s);
                if (!entry || entry->kind != emucorev::paf::HomeEntryKind::app) continue;
                if (entry->catalog_index == exclude_catalog_idx) continue;
                if (edit_drag_active && p == edit_drag_src_page && s == edit_drag_src_slot) continue;
                auto* renderer = ensure_catalog_bubble_renderer(entry->catalog_index);
                if (!renderer) continue;
                const auto pos = entry_screen_position(p, s, entry->id, home_offset_x);
                float enter_dx = 0.f, enter_s = 1.f, enter_a = 1.f;
                enter_params(s, enter_dx, enter_s, enter_a);
                if (enter_a <= 0.01f) continue;
                const auto& slot_vis = bubble_slots[entry->id];
                const float roll = edit_visual.edit_wiggle_roll_rad(p, s);
                const auto params = home_bubble_transform(home_motion_phase,
                    slot_vis.wobble_phase_offset, pos.x + enter_dx, pos.y,
                    slot_vis.float_state.press_scale * enter_s, 0.f, roll);
                mat_copy[0] = enter_a;
                renderer->draw(params, mat_copy, env_tex);
            }
        }

        // 3a. Focus halo: the genuine shell draws title text above the cursor ring.
        if (!edit_visual.edit_mode && !edit_visual.folder_open && home_alpha > 0.85f) {
            const auto* focused = desktop.entry_at(home_focus_page, home_focus_slot);
            if (focused && focused->catalog_index != exclude_catalog_idx
                && home_focus_page >= p_min && home_focus_page <= p_max) {
                const auto fpos = entry_screen_position(
                    home_focus_page, home_focus_slot, focused->id, home_offset_x);
                const float fscale = bubble_slots[focused->id].float_state.press_scale;
                if (focus_cursor && focus_ring_tex) {
                    focus_cursor->draw(focus_ring_tex, fpos.x + cx0, fpos.y + cy0,
                        emucorev::paf::kHomeFocusRingSize * fscale,
                        home_focus_pulse, std::uint32_t(cw), std::uint32_t(ch));
                } else {
                    sprites_begin();
                    const float r = emucorev::paf::kHomeBubbleSize * 0.5f;
                    sprites->draw(home_highlight_image_id, fpos.x - r, fpos.y - r, r * 2.f, r * 2.f,
                        {1.f, 1.f, 1.f, 128.f / 255.f});
                }
            }
        }

        // 3. Render folder icons, labels, Edit '...' badges, focus cursor, and page dots.
        sprites_begin();
        for (int p = p_min; p <= p_max; ++p) {
            for (int s = 0; s < emucorev::paf::kHomeSlotsPerPage; ++s) {
                const auto* entry = desktop.entry_at(p, s);
                if (!entry) continue;
                if (entry->kind == emucorev::paf::HomeEntryKind::app
                    && entry->catalog_index == exclude_catalog_idx) continue;
                if (edit_drag_active && p == edit_drag_src_page && s == edit_drag_src_slot) continue;
                const auto pos = entry_screen_position(p, s, entry->id, home_offset_x);
                float enter_dx = 0.f, enter_s = 1.f, enter_a = 1.f;
                enter_params(s, enter_dx, enter_s, enter_a);
                if (enter_a <= 0.01f) continue;
                const float bx = pos.x + enter_dx;
                const float by = pos.y;
                if (entry->kind == emucorev::paf::HomeEntryKind::folder) {
                    draw_folder_bubble_badge(*entry, bx, by,
                        bubble_slots[entry->id].float_state.press_scale * enter_s, enter_a);
                }
                const std::uint32_t label_id = (entry->kind == emucorev::paf::HomeEntryKind::folder)
                    ? folder_label_image_id
                    : home_catalog_items[static_cast<std::size_t>(entry->catalog_index)].label_image_id;
                {
                    const std::lock_guard lock_guard(status_mutex);
                    const auto label = host_images.find(label_id);
                    if (label_id && label != host_images.end()) {
                        sprites->draw(label_id, bx - float(label->second.width) * .5f,
                            by + 40.f, float(label->second.width), float(label->second.height),
                            {1.f, 1.f, 1.f, enter_a});
                    }
                }
                if (edit_visual.edit_blend > 0.05f) {
                    const float ba = edit_visual.edit_blend * home_alpha;
                    const float badge_cx = bx + 46.f, badge_cy = by - 36.f;
                    sprites->draw(status_app_chip_mask_id, badge_cx - 17.f, badge_cy - 17.f,
                        34.f, 34.f, {0.95f, 0.96f, 0.98f, 0.94f * ba});
                    sprites->draw(edit_dots_badge_id, badge_cx - 19.f, badge_cy - 19.f,
                        38.f, 38.f, {0.2f, 0.2f, 0.25f, ba});
                }
            }
        }

        // 5. Render dragged entry on top of other bubbles at cursor position.
        if (edit_drag_active) {
            const auto* dragged = desktop.entry_at(edit_drag_src_page, edit_drag_src_slot);
            if (dragged) {
                if (dragged->kind == emucorev::paf::HomeEntryKind::app) {
                    if (auto* r = ensure_catalog_bubble_renderer(dragged->catalog_index)) {
                        const auto& slot_vis = bubble_slots[dragged->id];
                        const auto params = home_bubble_transform(home_motion_phase,
                            slot_vis.wobble_phase_offset, edit_drag_x, edit_drag_y, 1.12f, 0.f, 0.f);
                        mat_copy[0] = 0.92f;
                        r->draw(params, mat_copy, env_tex);
                    }
                } else {
                    sprites_begin();
                    draw_folder_bubble_badge(*dragged, edit_drag_x, edit_drag_y, 1.12f, 0.92f);
                }
            }
        }

        // 6. Left-rail page indicator dots + Edit Mode corner tools + Callout.
        sprites_begin();
        const int active_dot_page = std::clamp(
            static_cast<int>(std::lround(scroll_y / emucorev::paf::kHomePageHeight)), 0, pages - 1);
        constexpr float dot_size = 22.f;
        constexpr float dot_spacing = 24.f;
        const float dot_start_y = 272.f - static_cast<float>(pages - 1) * dot_spacing * 0.5f;
        for (int p = 0; p < pages; ++p) {
            const std::uint32_t dot_id = (p == active_dot_page)
                ? home_page_dot_active_id : home_page_dot_inactive_id;
            sprites->draw(dot_id, edge_left() + 19.5f + home_offset_x - dot_size * 0.5f,
                dot_start_y + static_cast<float>(p) * dot_spacing - dot_size * 0.5f,
                dot_size, dot_size, {1.f, 1.f, 1.f, home_alpha});
        }

        if (edit_visual.edit_blend > 0.01f) {
            const float eb = edit_visual.edit_blend * home_alpha;
            const float el = edge_left() + home_offset_x, er = edge_right() + home_offset_x;
            const float eb_y = edge_bottom();
            sprites->draw(status_app_chip_mask_id, el - 16.f, eb_y - 68.f, 84.f, 84.f,
                {0.08f, 0.14f, 0.24f, 0.68f * eb});
            sprites->draw(edit_back_icon_id, el + 6.f, eb_y - 56.f, 48.f, 48.f,
                {1.f, 1.f, 1.f, eb});
            sprites->draw(edit_wallpaper_icon_id, er - 152.f, eb_y - 60.f, 64.f, 48.f,
                {1.f, 1.f, 1.f, eb});
            sprites->draw(edit_new_page_icon_id, er - 80.f, eb_y - 64.f, 70.f, 56.f,
                {1.f, 1.f, 1.f, eb});

            if (callout_open) {
                const auto* target = desktop.entry_at(callout_page, callout_slot);
                if (target) {
                    const float cx = std::clamp(callout_x + home_offset_x + 42.f, 118.f, 840.f);
                    const float cy = std::clamp(callout_y - 68.f, 56.f, 430.f);
                    sprites->draw(edit_callout_bg_id, cx - 104.f, cy - 46.f, 208.f, 92.f,
                        {1.f, 1.f, 1.f, eb});
                    const std::uint32_t lbl = (target->kind == emucorev::paf::HomeEntryKind::folder)
                        ? edit_ungroup_label_id : edit_create_folder_label_id;
                    sprites->draw(lbl, cx - 104.f, cy - 26.f, 208.f, 42.f,
                        {0.10f, 0.14f, 0.20f, eb});
                }
            }
        }
    }

    void draw_folder_overlay() {
        if (!edit_visual.is_folder_visible()) return;
        const auto* folder = desktop.entry_at(
            edit_visual.open_folder_page, edit_visual.open_folder_slot);
        if (!folder || folder->kind != emucorev::paf::HomeEntryKind::folder) return;

        const float t = edit_visual.folder_blend;
        const float cx = edit_visual.folder_origin_x + (480.f - edit_visual.folder_origin_x) * t;
        const float cy = edit_visual.folder_origin_y + (284.f - edit_visual.folder_origin_y) * t;
        const float pw = 133.f + (836.f - 133.f) * t;
        const float ph = 133.f + (454.f - 133.f) * t;

        sprites_begin();
        sprites->draw(0, -cx0, -cy0, cw, ch, {0.f, 0.f, 0.f, 0.38f * t});
        draw_nine_slice(folder_panel_bg_id, cx, cy, pw, ph, 64, 64, 16, 16,
            {0.5f, 0.5f}, {0.5f, 0.5f}, {0.82f, 0.90f, 1.f, 0.92f * t});

        if (t > 0.25f) {
            const float inner_a = std::clamp((t - 0.25f) / 0.75f, 0.f, 1.f);
            const float top_y = cy - ph * 0.5f + 16.f;
            sprites->draw(folder_title_pill_id, cx - 110.f, top_y, 220.f, 40.f,
                {1.f, 1.f, 1.f, inner_a});
            sprites->draw(folder_label_image_id, cx - 84.f, top_y - 34.f, 168.f, 60.f,
                {1.f, 1.f, 1.f, inner_a});
        }

        if (t > 0.18f && !folder->members.empty()) {
            const std::uint32_t env_tex = bubble_env ? bubble_env->texture() : 0u;
            const float member_a = std::clamp((t - 0.18f) / 0.82f, 0.f, 1.f);
            auto mat_copy = material;
            mat_copy[0] = member_a;
            const int n = std::min(static_cast<int>(folder->members.size()),
                emucorev::paf::kHomeSlotsPerPage);
            for (int m = 0; m < n; ++m) {
                const int cat_idx = folder->members[static_cast<std::size_t>(m)];
                auto* renderer = ensure_catalog_bubble_renderer(cat_idx);
                if (!renderer) continue;
                const auto sp = emucorev::paf::home_slot_screen_position(
                    0, m, 0.f, emucorev::paf::HomeGridLayout::folder);
                const float mx = edit_visual.folder_origin_x
                    + (sp.x - edit_visual.folder_origin_x) * t;
                const float my = edit_visual.folder_origin_y
                    + ((sp.y + 18.f) - edit_visual.folder_origin_y) * t;
                const float press = (m == folder_touch_idx) ? 0.93f : 1.0f;
                const auto params = home_bubble_transform(home_motion_phase,
                    static_cast<float>(m) * 0.73f, mx, my, (0.4f + 0.52f * t) * press);
                renderer->draw(params, mat_copy, env_tex);
            }
            if (t > 0.55f && folder_focus_idx >= 0 && folder_focus_idx < n) {
                const auto sp = emucorev::paf::home_slot_screen_position(
                    0, folder_focus_idx, 0.f, emucorev::paf::HomeGridLayout::folder);
                if (focus_cursor && focus_ring_tex) {
                    focus_cursor->draw(focus_ring_tex, sp.x + cx0, sp.y + 18.f + cy0,
                        emucorev::paf::kHomeFocusRingSize * 0.92f, home_focus_pulse,
                        std::uint32_t(cw), std::uint32_t(ch));
                }
            }
            sprites_begin();
            for (int m = 0; m < n; ++m) {
                const int cat_idx = folder->members[static_cast<std::size_t>(m)];
                const std::uint32_t lid =
                    home_catalog_items[static_cast<std::size_t>(cat_idx)].label_image_id;
                const auto sp = emucorev::paf::home_slot_screen_position(
                    0, m, 0.f, emucorev::paf::HomeGridLayout::folder);
                const float mx = edit_visual.folder_origin_x
                    + (sp.x - edit_visual.folder_origin_x) * t;
                const float my = edit_visual.folder_origin_y
                    + ((sp.y + 18.f) - edit_visual.folder_origin_y) * t;
                const std::lock_guard lock_guard(status_mutex);
                const auto label = host_images.find(lid);
                if (lid && label != host_images.end()) {
                    sprites->draw(lid, mx - float(label->second.width) * .5f,
                        my + 35.f, float(label->second.width), float(label->second.height),
                        {1.f, 1.f, 1.f, member_a});
                }
            }
        }
    }

    const LiveAreaSnapshot* ensure_livearea_snapshot(const std::string& title_id) {
        if (title_id.empty()) return nullptr;
        std::uint64_t revision = 0;
        {
            const std::lock_guard lock_guard(status_mutex);
            revision = livearea_catalog_revision;
            if (revision != applied_livearea_catalog_revision) {
                cached_livearea_snapshots.clear();
                applied_livearea_catalog_revision = revision;
            }
            if (const auto cached = cached_livearea_snapshots.find(title_id);
                cached != cached_livearea_snapshots.end()) {
                return &cached->second;
            }
            const auto entry = livearea_catalog.find(title_id);
            if (entry == livearea_catalog.end()) return nullptr;
            LiveAreaSnapshot copy = entry->second;
            const auto& gate = copy.gate_rect;
            const float gcx = gate[0] + gate[2] * 0.5f - emucorev::paf::kLiveAreaSheetLeft;
            const float gcy = gate[1] + gate[3] * 0.5f - emucorev::paf::kLiveAreaSheetTop;

            emucorev::paf::PlaneDivSpec frame_spec{};
            frame_spec.size_x = 292.f;
            frame_spec.size_y = 170.f;
            frame_spec.tex_width = 74;
            frame_spec.tex_height = 82;
            frame_spec.div_mode = emucorev::paf::PlaneDivMode::Grid3x3;
            frame_spec.div_ratio_h = {0.487800002f, 0.487800002f};
            frame_spec.div_ratio_v = {0.487800002f, 0.487800002f};
            frame_spec.margin_left = frame_spec.margin_right = 5;
            frame_spec.margin_top = frame_spec.margin_bottom = 9;
            copy.gate_frame_quads = emucorev::paf::build_plane_div_screen_quads(frame_spec, gcx, gcy);

            emucorev::paf::PlaneDivSpec glow_spec{};
            glow_spec.size_x = 292.f;
            glow_spec.size_y = 170.f;
            glow_spec.tex_width = 82;
            glow_spec.tex_height = 82;
            glow_spec.div_mode = emucorev::paf::PlaneDivMode::Grid3x3;
            glow_spec.div_ratio_h = {0.49000001f, 0.49000001f};
            glow_spec.div_ratio_v = {0.49000001f, 0.49000001f};
            copy.gate_glow_quads = emucorev::paf::build_plane_div_screen_quads(glow_spec, gcx, gcy);

            emucorev::paf::PlaneDivSpec control_spec{};
            // Widened with the localized label (host raster is 136..260 px wide).
            control_spec.size_x = 136.f;
            if (const auto label = host_images.find(copy.gate_start_label_id); label != host_images.end())
                control_spec.size_x = std::clamp(float(label->second.width), 136.f, 260.f);
            copy.gate_label_width = control_spec.size_x;
            control_spec.size_y = 36.f;
            control_spec.tex_width = 40;
            control_spec.tex_height = 36;
            control_spec.div_mode = emucorev::paf::PlaneDivMode::Grid3x1;
            control_spec.div_ratio_h = emucorev::paf::resolve_plane_div_ratio(
                emucorev::paf::PlaneDivRatioPreset::Third);
            control_spec.div_ratio_v = emucorev::paf::resolve_plane_div_ratio(
                emucorev::paf::PlaneDivRatioPreset::Third);
            copy.gate_control_quads = emucorev::paf::build_plane_div_screen_quads(
                control_spec, gcx, gcy + 54.f);

            const auto [it, _] = cached_livearea_snapshots.emplace(title_id, std::move(copy));
            return &it->second;
        }
    }

    void render_livearea_sheet_to_fbo(const emucorev::paf::LiveAreaTab& tab,
        float gate_alpha, float glow_intro, float frames_alpha) {
        if (!livearea_fbo || !livearea_tex) return;
        const auto* page = ensure_livearea_snapshot(tab.title_id);
        glBindFramebuffer(GL_FRAMEBUFFER, livearea_fbo);
        glViewport(0, 0, 840, 500);
        glClearColor(0.05f, 0.18f, 0.46f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        sprites->begin(840.f, 500.f);

        auto shift_x = [](float sx) { return sx - emucorev::paf::kLiveAreaSheetLeft; };
        auto shift_y = [](float sy) { return sy - emucorev::paf::kLiveAreaSheetTop; };
        auto draw_layer = [&](std::uint32_t id, const ScreenRect& rect,
            std::array<float, 4> tint = {1.f, 1.f, 1.f, 1.f}) {
            if (!id || rect[2] <= 0.f || rect[3] <= 0.f || tint[3] <= 0.005f) return;
            sprites->draw(id, shift_x(rect[0]), shift_y(rect[1]), rect[2], rect[3], tint);
        };

        if (page) {
            draw_layer(page->background_id, page->background_rect);
            if (frames_alpha > 0.005f) {
                for (const auto& frame : page->frames) {
                    draw_layer(frame.image_id, frame.rect, {1.f, 1.f, 1.f, frames_alpha});
                }
                for (const auto& text : page->texts) {
                    const float inv = 1.f / 255.f;
                    const std::array<float, 4> tint{
                        float((text.color >> 16) & 0xff) * inv,
                        float((text.color >> 8) & 0xff) * inv,
                        float(text.color & 0xff) * inv,
                        float((text.color >> 24) & 0xff) * inv * frames_alpha};
                    draw_layer(text.image_id, text.rect, tint);
                }
            }
            if (gate_alpha > 0.005f) {
                if (page->gate_id) {
                    draw_layer(page->gate_id, page->gate_rect, {1.f, 1.f, 1.f, gate_alpha});
                } else if (tab.catalog_index >= 0
                    && static_cast<std::size_t>(tab.catalog_index) < home_catalog_items.size()) {
                    const auto& g = page->gate_rect;
                    const float icon_x = shift_x(g[0] + g[2] * 0.5f) - 48.f;
                    const float icon_y = shift_y(g[1] + g[3] * 0.5f) - 17.f - 48.f;
                    sprites->draw(home_catalog_items[static_cast<std::size_t>(tab.catalog_index)]
                        .icon_image_id, icon_x, icon_y, 96.f, 96.f, {1.f, 1.f, 1.f, gate_alpha});
                }
                for (const auto& quad : page->gate_frame_quads) {
                    sprites->draw_patch(livearea_gate_frame_id, quad.x, quad.y, quad.width, quad.height,
                        quad.u, quad.v, quad.uv_width, quad.uv_height, {1.f, 1.f, 1.f, gate_alpha});
                }
                const std::uint32_t ctrl_id = page->gate_control_id
                    ? page->gate_control_id : livearea_start_control_id;
                for (const auto& quad : page->gate_control_quads) {
                    sprites->draw_patch(ctrl_id, quad.x, quad.y, quad.width, quad.height,
                        quad.u, quad.v, quad.uv_width, quad.uv_height, {1.f, 1.f, 1.f, gate_alpha});
                }
                if (page->gate_start_label_id) {
                    const auto& g = page->gate_rect;
                    const float lw = page->gate_label_width;
                    const float lx = shift_x(g[0] + g[2] * 0.5f) - lw * .5f;
                    const float ly = shift_y(g[1] + g[3] * 0.5f) + 54.f - 18.f;
                    sprites->draw(page->gate_start_label_id, lx, ly, lw, 36.f,
                        {1.f, 1.f, 1.f, gate_alpha});
                }
                if (glow_intro > 0.005f) {
                    const float pulse = 0.5f + 0.5f * std::sin(livearea_deck.gate_focus_pulse());
                    const float glow_alpha = (0.20f + 0.72f * pulse) * glow_intro * gate_alpha;
                    sprites->set_additive_blend(true);
                    for (const auto& quad : page->gate_glow_quads) {
                        sprites->draw_patch(livearea_gate_glow_id, quad.x, quad.y,
                            quad.width, quad.height, quad.u, quad.v, quad.uv_width, quad.uv_height,
                            {0.05f, 0.96f, 1.f, glow_alpha});
                    }
                    sprites->set_additive_blend(false);
                }
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(0, 0, target_w, target_h);
    }

    void draw_livearea_tab(int tab_index) {
        if (!livearea_pageturn || !livearea_deck.is_tab_visible(tab_index)) return;
        const bool is_opening_tab = (tab_index == 0) && livearea_deck.is_open_anim_active();
        if (is_opening_tab && !livearea_deck.is_opening_sheet_visible()) return;
        ensure_exp_texture();
        if (!lock_exp) return;

        const auto& tab = livearea_deck.tabs()[static_cast<std::size_t>(tab_index)];
        const float gate_a = is_opening_tab ? livearea_deck.sheet_gate_alpha() : 1.f;
        const float glow_a = is_opening_tab ? livearea_deck.sheet_gate_glow_intro() : 1.f;
        const float frames_a = is_opening_tab ? livearea_deck.sheet_frames_alpha() : 1.f;
        render_livearea_sheet_to_fbo(tab, gate_a, glow_a, frames_a);

        const auto fold_pt = (tab.peel_state == emucorev::paf::LiveAreaPeelState::rest && !tab.is_dragging)
            ? livearea_deck.effective_fold_point(tab_index) : tab.fold_point;
        const auto aspect = emucorev::paf::page_turn_normalized_aspect(emucorev::paf::kLiveAreaSheetSize);
        const auto shape = emucorev::paf::page_turn_fold_shape(fold_pt, aspect);
        emucorev::paf::page_turn_mesh(shape, livearea_mesh);
        livearea_pageturn->upload_mesh(livearea_mesh);

        const float offset_x = livearea_deck.tab_offset_x(tab_index) * pager_scale();
        const float yaw = livearea_deck.tab_yaw_rad(tab_index);
        const float slide_frac = std::min(std::abs(offset_x) / preview_width, 1.f);
        const float depth_arc = -85.f * std::sin(slide_frac * 1.5707963f);

        emucorev::paf::PageTurnPose pose{};
        pose.position = {
            emucorev::paf::kLiveAreaSheetPosition.x + offset_x,
            emucorev::paf::kLiveAreaSheetPosition.y,
            tab.exit_z + depth_arc,
        };
        const float s = is_opening_tab ? livearea_deck.sheet_open_scale() : 1.f;
        pose.scale = {s, s, s};
        pose.rotation = {0.f, std::sin(yaw * 0.5f), 0.f, std::cos(yaw * 0.5f)};

        const emucorev::paf::PageTurnMatrix persp{
            2.f / cw, 0.f, 0.f, 0.f,
            0.f, 2.f / ch, 0.f, 0.f,
            0.f, 0.f, 0.f, -1.f / preview_width,
            0.f, 0.f, 0.f, 1.f,
        };

        const emucorev::paf::PageTurnShapeStyle shape_style{};
        const auto shape_mat = emucorev::paf::page_turn_shape_material(fold_pt, shape, shape_style);
        const auto lights = emucorev::paf::page_turn_light_parameters({});
        const auto transforms = emucorev::paf::page_turn_transforms(
            emucorev::paf::kLiveAreaSheetSize, pose, persp, emucorev::paf::page_turn_identity);
        const auto page_material = emucorev::paf::page_turn_material_pack(
            shape_mat.fresnel_multiplier, lock_material_style);
        constexpr std::array<float, 4> fbo_uv_rect{1.f, -1.f, 0.f, 1.f};

        std::array<float, 48> bottom_mat{};
        std::copy(transforms.model_view_projection.begin(), transforms.model_view_projection.end(),
            bottom_mat.begin());
        bottom_mat[16] = lock_effect_style.shadow;
        bottom_mat[17] = lock_effect_style.shadow_level;
        bottom_mat[18] = 1.f;
        bottom_mat[19] = 1.f;
        std::copy(fbo_uv_rect.begin(), fbo_uv_rect.end(), bottom_mat.begin() + 20);
        bottom_mat[24] = 1.f; bottom_mat[25] = 1.f; bottom_mat[26] = 1.f; bottom_mat[27] = 1.f;
        bottom_mat[28] = 5.f;
        bottom_mat[29] = 0.2f;
        std::copy_n(shape_mat.deepest.begin(), 4, bottom_mat.begin() + 30);
        bottom_mat[34] = shape_mat.grep.x;
        bottom_mat[35] = shape_mat.grep.y;
        std::copy(shape_mat.params.begin(), shape_mat.params.end(), bottom_mat.begin() + 36);
        std::copy(shape_mat.fold_plane.begin(), shape_mat.fold_plane.end(), bottom_mat.begin() + 40);
        std::copy(shape_mat.aspect.begin(), shape_mat.aspect.end(), bottom_mat.begin() + 44);

        std::array<float, 112> top_mat{};
        std::copy(transforms.model_view_projection.begin(), transforms.model_view_projection.end(),
            top_mat.begin());
        std::copy(transforms.model.begin(), transforms.model.end(), top_mat.begin() + 16);
        std::copy(page_material.begin(), page_material.end(), top_mat.begin() + 32);
        std::copy(fbo_uv_rect.begin(), fbo_uv_rect.end(), top_mat.begin() + 48);
        top_mat[52] = 1.f; top_mat[53] = 1.f; top_mat[54] = 1.f; top_mat[55] = 1.f;
        std::copy(shape_mat.deepest.begin(), shape_mat.deepest.end(), top_mat.begin() + 56);
        top_mat[60] = shape_mat.grep.x;
        top_mat[61] = shape_mat.grep.y;
        top_mat[62] = shape_mat.normal_scale_z;
        top_mat[63] = 0.f;
        std::copy(shape_mat.params.begin(), shape_mat.params.end(), top_mat.begin() + 64);
        std::copy(shape_mat.fold_plane.begin(), shape_mat.fold_plane.end(), top_mat.begin() + 68);
        std::copy(shape_mat.aspect.begin(), shape_mat.aspect.end(), top_mat.begin() + 72);
        std::copy(lights.color.begin(), lights.color.end(), top_mat.begin() + 76);
        std::copy(lights.position.begin(), lights.position.end(), top_mat.begin() + 92);
        top_mat[108] = 0.f; top_mat[109] = 0.f; top_mat[110] = 0.f; top_mat[111] = 1.f;

        const std::array<float, 12> flat_frag_state{
            0.f, 0.f, 1.f, 0.f,
            1.f, 0.f, 0.f,
            1.f / cw, 1.f / ch,
            0.f, 80.f, 0.f
        };

        std::array<float, 8> flat_bottom_frag{};
        std::array<float, 36> flat_top_frag{};
        flat_top_frag[0] = lock_material_style.back_side_opacity;
        std::copy_n(page_material.begin(), 4, flat_top_frag.begin() + 2);
        std::copy_n(lights.color.begin(), 16, flat_top_frag.begin() + 18);

        emucorev::paf::PageSurfaceDraw front_draw{};
        front_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, cw, ch, .5f, .5f, 0.f, 0.f, 0.f};
        front_draw.fragment_state = flat_frag_state;
        front_draw.textures = {livearea_tex, 0, 0, 0, lock_exp};
        front_draw.blend = true;
        front_draw.vertex_parameters = bottom_mat;
        front_draw.fragment_parameters = flat_bottom_frag;
        front_draw.meta_alpha = tab.sheet_alpha;

        emucorev::paf::PageSurfaceDraw folded_draw{};
        folded_draw.viewport = front_draw.viewport;
        folded_draw.fragment_state = flat_frag_state;
        folded_draw.textures = {livearea_tex, 0, 0, 0, lock_exp};
        folded_draw.blend = true;
        folded_draw.vertex_parameters = top_mat;
        folded_draw.fragment_parameters = flat_top_frag;
        folded_draw.meta_alpha = tab.sheet_alpha;

        livearea_pageturn->draw(emucorev::paf::PageSurface::front, front_draw);
        livearea_pageturn->draw(emucorev::paf::PageSurface::folded, folded_draw);
    }

    void draw_livearea_and_home_composition() {
        const bool opening_bubble = livearea_deck.is_open_anim_active();
        const int exclude_cat = opening_bubble ? livearea_deck.open_bubble_catalog_index() : -1;
        if (livearea_deck.is_home_visible()) {
            const float hx = livearea_deck.home_offset_x() * pager_scale();
            const float ha = opening_bubble ? livearea_deck.open_other_home_alpha() : 1.f;
            draw_home_bubbles(hx, ha, exclude_cat);
        }
        for (int i = livearea_deck.tab_count() - 1; i >= 0; --i) {
            if (i != livearea_deck.active_tab_index()) draw_livearea_tab(i);
        }
        if (livearea_deck.active_tab_index() >= 0) {
            draw_livearea_tab(livearea_deck.active_tab_index());
        }
        // Swelling/spinning bubble renders on top of emerging LiveArea sheet during 0..430ms!
        if (livearea_deck.is_open_bubble_visible()) {
            const int cat_idx = livearea_deck.open_bubble_catalog_index();
            if (auto* renderer = ensure_catalog_bubble_renderer(cat_idx)) {
                if (bubble_env) bubble_env->capture(color);
                const std::uint32_t env_tex = bubble_env ? bubble_env->texture() : 0u;
                const auto bpos = livearea_deck.open_bubble_pos();
                const auto params = home_bubble_transform(home_motion_phase, 0.f, bpos.x, bpos.y,
                    livearea_deck.open_bubble_scale(), livearea_deck.open_bubble_spin_y_rad(), 0.f);
                auto mat_copy = material;
                mat_copy[0] = livearea_deck.open_bubble_alpha();
                renderer->draw(params, mat_copy, env_tex);
            }
        }
        draw_folder_overlay();
        if (!edit_visual.edit_mode && !edit_visual.is_folder_visible()) {
            sprites_begin();
            if (livearea_deck.pager_position() > 0.25f) {
                sprites->draw(topmenu_arrow_left_id, edge_left(), 258.f, 60.f, 60.f);
            }
            if (livearea_deck.tab_count() > 0
                && livearea_deck.pager_position() < static_cast<float>(livearea_deck.tab_count()) - 0.25f) {
                sprites->draw(topmenu_arrow_right_id, edge_right() - 60.f, 258.f, 60.f, 60.f);
            }
        }
    }

    bool notification_orb_hit(float px, float py) const {
        if (edit_visual.edit_mode || edit_visual.is_folder_visible()) return false;
        return py - canvas_top() < 64.f * orb_scale && px > 960.f + cx0 - 84.f * orb_scale;
    }

    // Geometry measured on the emulated SceShell frame (Lenovo, 3.333x):
    // balloon body 85.5 px tall from y=57, text centred with 29 px padding,
    // tail tip at (orb right edge - 59.4, 60). Texture fec9dfd8 is 90x94:
    // body columns 7..49, tail in columns 30..90 / rows 0..58.
    void draw_notification_overlay() {
        if (notif_blend <= 0.003f) return;
        const float b = notif_blend;
        sprites_begin();
        // Home below the status bar dims while the balloon is shown.
        sprites->draw(0, -cx0, -cy0, cw, ch, {0.f, 0.f, 0.f, 0.32f * b});
        float text_w = 0.f, text_h = 0.f;
        {
            const std::lock_guard lock_guard(status_mutex);
            if (const auto it = host_images.find(notification_text_image_id); it != host_images.end()) {
                text_w = float(it->second.width);
                text_h = float(it->second.height);
            }
        }
        const float body_w = std::max(text_w - 8.f + 58.f, 120.f);
        const float tex_w = body_w + 48.f, tex_h = 99.5f;
        const float tip_x = 960.f + cx0 - 59.4f * orb_scale;
        const float right = tip_x + 6.f, left = right - tex_w, top = canvas_top() + 53.f;
        constexpr float tw = 90.f, th = 94.f;
        const std::array<float, 4> xs{0.f, 20.f, 30.f, 90.f};
        const std::array<float, 4> ys{0.f, 58.f, 68.f, 94.f};
        const std::array<float, 4> dx{left, left + 20.f, right - 60.f, right};
        const std::array<float, 4> dy{top, top + 58.f, top + tex_h - 26.f, top + tex_h};
        const std::array<float, 4> tint{1.f, 1.f, 1.f, b};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                sprites->draw_patch(notification_balloon_id, dx[c], dy[r], dx[c + 1] - dx[c], dy[r + 1] - dy[r],
                    xs[c] / tw, ys[r] / th, (xs[c + 1] - xs[c]) / tw, (ys[r + 1] - ys[r]) / th, tint);
        if (text_w > 0.f) {
            const float body_left = left + 7.f;
            sprites->draw(notification_text_image_id, body_left + (body_w - (text_w - 8.f)) * .5f - 4.f,
                canvas_top() + 110.f - 32.f, text_w, text_h, tint);
        }
    }

    void open_livearea_tab(const std::string& title_id, int catalog_index, float x, float y) {
        emit_request_livearea(title_id);
        livearea_deck.open_or_focus(title_id, catalog_index, x, y);
    }

    // Returns whether Back was consumed by an open shell surface.
    bool close_livearea() {
        if (app_screen.active()) {
            app_screen.key(0, 0, 0, AKEYCODE_BACK, AKEYCODE_BACK);
            return true;
        }
        if (notif_open) {
            notif_open = false;
            emit_ui_sound(sound_cancel);
            return true;
        }
        if (!livearea_deck.is_home_active()) {
            if (livearea_deck.trigger_peel_close()) {
                emit_ui_sound(sound_page_turn);
            }
        } else if (edit_visual.folder_open) {
            edit_visual.close_folder();
            emit_ui_sound(sound_cancel);
        } else if (edit_visual.edit_mode) {
            edit_visual.set_edit_mode(false);
            callout_open = false;
            desktop.prune_trailing_empty_pages();
            home_scroll.set_page_count(desktop.page_count());
            persist_home_layout();
            emit_ui_sound(sound_cancel);
        } else {
            return false;
        }
        return true;
    }

    bool livearea_target_at(float x, float y, std::string& target) {
        const int active_tab = livearea_deck.active_tab_index();
        if (active_tab < 0 || active_tab >= livearea_deck.tab_count()) return false;
        const auto* snap = ensure_livearea_snapshot(
            livearea_deck.tabs()[static_cast<std::size_t>(active_tab)].title_id);
        if (!snap) return false;
        const float ox = livearea_deck.tab_offset_x(active_tab) * pager_scale();
        const float lx = x - ox;
        for (const auto& hit : snap->targets) {
            const auto& r = hit.rect;
            if (lx >= r[0] && y >= r[1] && lx < r[0] + r[2] && y < r[1] + r[3]) {
                target = hit.id;
                return true;
            }
        }
        return false;
    }

    void open_entry_at(int page, int slot, float screen_x, float screen_y) {
        const auto* entry = desktop.entry_at(page, slot);
        if (!entry) return;
        home_touch_page = -1;
        home_touch_slot = -1;
        home_touch_down = false;
        home_drag_moved = false;
        if (entry->kind == emucorev::paf::HomeEntryKind::folder) {
            folder_focus_idx = 0;
            folder_touch_idx = -1;
            edit_visual.open_folder(page, slot, screen_x, screen_y);
            emit_ui_sound(sound_decide);
            return;
        }
        if (entry->catalog_index < 0
            || static_cast<std::size_t>(entry->catalog_index) >= home_catalog_items.size()) return;
        emit_ui_sound(sound_decide);
        open_livearea_tab(entry->id, entry->catalog_index, screen_x, screen_y);
    }

    void tick_home(float elapsed) {
        constexpr float kTwoPi = 6.28318530717958647692f;
        home_motion_phase = emucorev::paf::advance_bubble_phase(home_motion_phase, elapsed, 1.f);
        home_focus_pulse += elapsed * 4.71238898f;
        if (home_focus_pulse > kTwoPi) home_focus_pulse -= kTwoPi;
        status_orb_phase += elapsed * 2.2f;
        if (status_orb_phase > kTwoPi) status_orb_phase -= kTwoPi;
        if (home_enter_elapsed < 1.f) home_enter_elapsed += elapsed;
        if (edit_edge_scroll_cooldown > 0.f) edit_edge_scroll_cooldown = std::max(0.f, edit_edge_scroll_cooldown - elapsed);

        livearea_deck.step(elapsed);
        edit_visual.step(elapsed);
        apply_pending_app_pages();
        app_screen.step(elapsed);
        // Provisional 200 ms linear fade: the emulated reference confirms the
        // end states (dimmed Home, opaque balloon) but not the exact curve.
        notif_blend = std::clamp(notif_blend + (notif_open ? elapsed : -elapsed) / 0.2f, 0.f, 1.f);

        if (home_touch_down && !home_drag_moved && !edit_visual.edit_mode
            && !edit_visual.folder_open && livearea_deck.is_home_active()) {
            home_hold_elapsed += elapsed;
            if (home_hold_elapsed >= 0.55f) {
                edit_visual.set_edit_mode(true);
                home_touch_down = false;
                home_touch_page = -1;
                home_touch_slot = -1;
                emit_ui_sound(sound_decide);
            }
        }

        home_scroll.step(elapsed);
        home_page = home_scroll.current_page();
        const float sy = home_scroll.scroll_y();
        for (auto& [id, vis] : bubble_slots) {
            const auto* cur_touch = (home_touch_page >= 0 && home_touch_slot >= 0)
                ? desktop.entry_at(home_touch_page, home_touch_slot) : nullptr;
            const bool pressed = cur_touch && cur_touch->id == id;
            emucorev::paf::step_bubble_float(vis.float_state, elapsed, sy, pressed);
        }
    }

    std::pair<int, int> hit_test_home_slot(float px, float py, float max_dist) const {
        const float scroll_y = home_scroll.scroll_y();
        const int pages = desktop.page_count();
        const int p_min = std::clamp(
            static_cast<int>(std::floor(scroll_y / emucorev::paf::kHomePageHeight)), 0, pages - 1);
        const int p_max = std::clamp(
            static_cast<int>(std::ceil(scroll_y / emucorev::paf::kHomePageHeight)), 0, pages - 1);
        float best = max_dist * max_dist;
        int bp = -1, bs = -1;
        for (int p = p_min; p <= p_max; ++p) {
            for (int s = 0; s < emucorev::paf::kHomeSlotsPerPage; ++s) {
                const auto sp = emucorev::paf::home_slot_screen_position(p, s, scroll_y);
                const float dx = px - sp.x, dy = py - sp.y;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= best) {
                    best = d2;
                    bp = p;
                    bs = s;
                }
            }
        }
        return {bp, bs};
    }

    int32_t handle_app_input(AInputEvent* event) {
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
            if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_DOWN) return 1;
            int key = AKeyEvent_getKeyCode(event);
            if (key == AKEYCODE_ENTER || key == AKEYCODE_DPAD_CENTER) key = AKEYCODE_BUTTON_A;
            if (key == AKEYCODE_ESCAPE || key == AKEYCODE_BACK) key = AKEYCODE_BUTTON_B;
            app_screen.key(AKEYCODE_DPAD_UP, AKEYCODE_DPAD_DOWN, AKEYCODE_BUTTON_A, AKEYCODE_BUTTON_B, key);
            return 1;
        }
        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION || surface_w <= 0) return 0;
        const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const auto [px, py] = to_content(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
        const std::int64_t t = AMotionEvent_getEventTime(event);
        if (action == AMOTION_EVENT_ACTION_DOWN) { app_last_event_ns = t; app_screen.touch_down(px, py); }
        else if (action == AMOTION_EVENT_ACTION_MOVE) {
            const float dt = std::clamp(float(t - app_last_event_ns) * 1e-9f, 0.001f, 0.1f);
            app_last_event_ns = t;
            app_screen.touch_move(px, py, dt);
        } else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL)
            app_screen.touch_up(px, py, action == AMOTION_EVENT_ACTION_CANCEL);
        return 1;
    }
    std::int64_t app_last_event_ns = 0;

    int32_t handle_shell_input(AInputEvent* event) {
        if (!ensure_home_initialized()) return 0;
        if (app_screen.active()) return handle_app_input(event);

        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
            if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_DOWN || AKeyEvent_getRepeatCount(event) != 0)
                return 0;
            const int key = AKeyEvent_getKeyCode(event);
            if (notif_open) {
                if (key == AKEYCODE_BACK || key == AKEYCODE_BUTTON_B || key == AKEYCODE_ESCAPE
                    || key == AKEYCODE_BUTTON_A || key == AKEYCODE_ENTER) {
                    notif_open = false;
                    emit_ui_sound(sound_cancel);
                }
                return 1;
            }
            if (key == AKEYCODE_BACK || key == AKEYCODE_BUTTON_B || key == AKEYCODE_ESCAPE) {
                if (!livearea_deck.is_home_active() || edit_visual.folder_open || edit_visual.edit_mode) {
                    close_livearea();
                    return 1;
                }
                return 0;
            }
            if (edit_visual.folder_open) {
                const auto* f = desktop.entry_at(edit_visual.open_folder_page, edit_visual.open_folder_slot);
                const int n = f ? static_cast<int>(f->members.size()) : 0;
                if (n > 0) {
                    if (key == AKEYCODE_DPAD_RIGHT && folder_focus_idx + 1 < n) {
                        ++folder_focus_idx; emit_ui_sound(sound_focus); return 1;
                    }
                    if (key == AKEYCODE_DPAD_LEFT && folder_focus_idx > 0) {
                        --folder_focus_idx; emit_ui_sound(sound_focus); return 1;
                    }
                    if ((key == AKEYCODE_BUTTON_A || key == AKEYCODE_ENTER)
                        && folder_focus_idx >= 0 && folder_focus_idx < n) {
                        const int cat_idx = f->members[static_cast<std::size_t>(folder_focus_idx)];
                        const auto sp = emucorev::paf::home_slot_screen_position(
                            0, folder_focus_idx, 0.f, emucorev::paf::HomeGridLayout::folder);
                        edit_visual.close_folder();
                        emit_ui_sound(sound_decide);
                        open_livearea_tab(
                            home_catalog_items[static_cast<std::size_t>(cat_idx)].id, cat_idx, sp.x, sp.y + 18.f);
                        return 1;
                    }
                }
                return 1;
            }
            if (!livearea_deck.is_home_active()) {
                if (key == AKEYCODE_BUTTON_L1 || key == AKEYCODE_DPAD_LEFT) {
                    if (livearea_deck.slide_left()) { emit_ui_sound(sound_page_scroll); return 1; }
                } else if (key == AKEYCODE_BUTTON_R1 || key == AKEYCODE_DPAD_RIGHT) {
                    if (livearea_deck.slide_right()) { emit_ui_sound(sound_page_scroll); return 1; }
                } else if (key == AKEYCODE_BUTTON_A || key == AKEYCODE_ENTER) {
                    const int idx = livearea_deck.active_tab_index();
                    if (idx >= 0 && idx < livearea_deck.tab_count()) {
                        const auto* snap = ensure_livearea_snapshot(
                            livearea_deck.tabs()[static_cast<std::size_t>(idx)].title_id);
                        if (snap && !snap->targets.empty()) {
                            emit_ui_sound(sound_decide);
                            emit_livearea_target_request(
                                livearea_deck.tabs()[static_cast<std::size_t>(idx)].title_id,
                                snap->targets.front().id);
                        }
                    }
                    return 1;
                }
                return 0;
            }
            // Home DPAD navigation
            emucorev::paf::HomeDpadDirection dir{};
            bool is_dir = true;
            if (key == AKEYCODE_DPAD_LEFT) dir = emucorev::paf::HomeDpadDirection::left;
            else if (key == AKEYCODE_DPAD_RIGHT) dir = emucorev::paf::HomeDpadDirection::right;
            else if (key == AKEYCODE_DPAD_UP) dir = emucorev::paf::HomeDpadDirection::up;
            else if (key == AKEYCODE_DPAD_DOWN) dir = emucorev::paf::HomeDpadDirection::down;
            else is_dir = false;
            if (is_dir) {
                const auto [np, ns] = desktop.dpad_neighbor(home_focus_page, home_focus_slot, dir);
                if (np != home_focus_page || ns != home_focus_slot) {
                    const int prev_page = home_page;
                    home_focus_page = np;
                    home_focus_slot = ns;
                    if (np != prev_page) {
                        home_scroll.snap_to_page(np);
                        home_page = np;
                        emit_ui_sound(sound_page_scroll);
                    } else {
                        emit_ui_sound(sound_focus);
                    }
                }
                return 1;
            }
            if (key == AKEYCODE_BUTTON_R1 && livearea_deck.tab_count() > 0) {
                if (livearea_deck.slide_right()) { emit_ui_sound(sound_page_scroll); return 1; }
            }
            if (key == AKEYCODE_BUTTON_A || key == AKEYCODE_ENTER) {
                const auto sp = emucorev::paf::home_slot_screen_position(
                    home_focus_page, home_focus_slot, home_scroll.scroll_y());
                open_entry_at(home_focus_page, home_focus_slot, sp.x, sp.y);
                return 1;
            }
            return 0;
        }

        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
        const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (surface_w <= 0 || surface_h <= 0) return 0;
        const auto [px, py] = to_content(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
        const std::int64_t event_ns = AMotionEvent_getEventTime(event);

        // Folder overlay modal input
        if (edit_visual.folder_open) {
            const auto* folder = desktop.entry_at(
                edit_visual.open_folder_page, edit_visual.open_folder_slot);
            if (action == AMOTION_EVENT_ACTION_DOWN) {
                home_touch_start = {px, py};
                folder_touch_idx = -1;
                if (folder) {
                    const int n = std::min(static_cast<int>(folder->members.size()),
                        emucorev::paf::kHomeSlotsPerPage);
                    for (int m = 0; m < n; ++m) {
                        const auto sp = emucorev::paf::home_slot_screen_position(
                            0, m, 0.f, emucorev::paf::HomeGridLayout::folder);
                        if (std::hypot(px - sp.x, py - (sp.y + 18.f)) <= 66.f) {
                            folder_touch_idx = m;
                            break;
                        }
                    }
                }
                return 1;
            }
            if (action == AMOTION_EVENT_ACTION_UP) {
                if (folder && folder_touch_idx >= 0
                    && folder_touch_idx < static_cast<int>(folder->members.size())
                    && std::hypot(px - home_touch_start.x, py - home_touch_start.y) < 30.f) {
                    const int cat_idx = folder->members[static_cast<std::size_t>(folder_touch_idx)];
                    const auto sp = emucorev::paf::home_slot_screen_position(
                        0, folder_touch_idx, 0.f, emucorev::paf::HomeGridLayout::folder);
                    edit_visual.close_folder();
                    folder_touch_idx = -1;
                    emit_ui_sound(sound_decide);
                    open_livearea_tab(
                        home_catalog_items[static_cast<std::size_t>(cat_idx)].id, cat_idx,
                        sp.x, sp.y + 18.f);
                    return 1;
                }
                folder_touch_idx = -1;
                if (px < 62.f || px > 898.f || py < 56.f || py > 512.f) {
                    edit_visual.close_folder();
                    emit_ui_sound(sound_cancel);
                }
            }
            return 1;
        }

        // Notification balloon: modal while open, any touch dismisses it.
        if (notif_open) {
            if (action == AMOTION_EVENT_ACTION_DOWN) {
                notif_open = false;
                emit_ui_sound(sound_cancel);
            }
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_DOWN && notification_orb_hit(px, py)) {
            notif_open = true;
            emit_ui_sound(sound_decide);
            return 1;
        }

        if (action == AMOTION_EVENT_ACTION_DOWN) {
            home_touch_start = {px, py};
            home_drag_last_x = px;
            home_drag_last_y = py;
            home_drag_last_event_ns = event_ns;
            home_drag_moved = false;
            home_horiz_drag = false;
            topbar_drag = false;
            peeker_peel_drag = false;
            home_touch_down = false;
            home_hold_elapsed = 0.f;
            home_touch_page = -1;
            home_touch_slot = -1;

            // Top bar Home/App chip strip tap (only when Edit Mode inactive).
            if (py - canvas_top() < 36.f && !edit_visual.edit_mode && livearea_deck.tab_count() > 0) {
                topbar_drag = true;
                return 1;
            }
            // Active LiveArea top-right peel corner hit-test.
            if (!livearea_deck.is_home_active() && livearea_deck.begin_peel_drag(px, py)) {
                peeker_peel_drag = true;
                return 1;
            }
            // Edit mode pointer down: callout, '...', corners, or bubble pick-up.
            if (livearea_deck.is_home_active() && edit_visual.edit_mode) {
                if (callout_open) {
                    const auto* c_target = desktop.entry_at(callout_page, callout_slot);
                    const float cx = std::clamp(callout_x + 42.f, 118.f, 840.f);
                    const float cy = std::clamp(callout_y - 68.f, 56.f, 430.f);
                    if (c_target && std::abs(px - cx) <= 104.f && std::abs(py - cy) <= 46.f) {
                        if (c_target->kind == emucorev::paf::HomeEntryKind::folder) {
                            desktop.ungroup_folder(callout_page, callout_slot);
                        } else {
                            desktop.create_folder_at(callout_page, callout_slot, "New Folder");
                        }
                        callout_open = false;
                        home_scroll.set_page_count(desktop.page_count());
                        sync_visual_slots_with_desktop();
                        persist_home_layout();
                        emit_ui_sound(sound_decide);
                        return 1;
                    }
                    callout_open = false;
                }
                // Bottom-left Done/Back button or bottom-right Wallpaper / '+' New Page.
                if (px < edge_left() + 72.f && py > edge_bottom() - 70.f) {
                    edit_visual.set_edit_mode(false);
                    desktop.prune_trailing_empty_pages();
                    home_scroll.set_page_count(desktop.page_count());
                    home_page = home_scroll.current_page();
                    persist_home_layout();
                    emit_ui_sound(sound_cancel);
                    return 1;
                }
                if (px >= edge_right() - 156.f && px <= edge_right() - 84.f && py > edge_bottom() - 68.f) {
                    // As on the Vita: the wallpaper tool picks a background image.
                    emit_ui_sound(sound_decide);
                    emit_activity_strings(&exit_request_callback, "wallpaper", nullptr);
                    return 1;
                }
                if (px > edge_right() - 84.f && py > edge_bottom() - 68.f) {
                    const int new_p = desktop.add_page();
                    home_scroll.set_page_count(desktop.page_count());
                    home_scroll.snap_to_page(new_p);
                    home_page = new_p;
                    persist_home_layout();
                    emit_ui_sound(sound_page_scroll);
                    return 1;
                }
                // Hit-test '...' badge first, then occupied bubble to drag-reorder.
                const float sy = home_scroll.scroll_y();
                const int p_min = std::clamp(
                    static_cast<int>(std::floor(sy / emucorev::paf::kHomePageHeight)),
                    0, desktop.page_count() - 1);
                const int p_max = std::clamp(
                    static_cast<int>(std::ceil(sy / emucorev::paf::kHomePageHeight)),
                    0, desktop.page_count() - 1);
                for (int p = p_min; p <= p_max; ++p) {
                    for (int s = 0; s < emucorev::paf::kHomeSlotsPerPage; ++s) {
                        const auto* entry = desktop.entry_at(p, s);
                        if (!entry) continue;
                        const auto pos = entry_screen_position(p, s, entry->id);
                        if (std::hypot(px - (pos.x + 46.f), py - (pos.y - 36.f)) <= 24.f) {
                            callout_open = true;
                            callout_page = p;
                            callout_slot = s;
                            callout_x = pos.x;
                            callout_y = pos.y;
                            emit_ui_sound(sound_focus);
                            return 1;
                        }
                        if (std::hypot(px - pos.x, py - pos.y) <= emucorev::paf::kHomeBubbleSize * 0.5f) {
                            edit_drag_active = true;
                            edit_drag_src_page = p;
                            edit_drag_src_slot = s;
                            edit_drag_x = px;
                            edit_drag_y = py;
                            return 1;
                        }
                    }
                }
                return 1;
            }
            // Normal Home hit test
            if (livearea_deck.is_home_active()) {
                home_touch_down = true;
                const auto [hp, hs] = hit_test_home_slot(px, py, emucorev::paf::kHomeBubbleSize * 0.5f);
                if (hp >= 0 && desktop.entry_at(hp, hs)) {
                    home_touch_page = hp;
                    home_touch_slot = hs;
                }
            }
            return 1;
        }

        if (action == AMOTION_EVENT_ACTION_MOVE) {
            if (peeker_peel_drag) {
                livearea_deck.update_peel_drag(px, py);
                return 1;
            }
            if (edit_drag_active) {
                edit_drag_x = px;
                edit_drag_y = py;
                if (edit_edge_scroll_cooldown <= 0.f) {
                    if (py > 482.f && home_page + 1 < desktop.page_count()) {
                        home_scroll.snap_to_page(home_page + 1);
                        home_page = home_page + 1;
                        edit_edge_scroll_cooldown = 0.55f;
                        emit_ui_sound(sound_page_scroll);
                    } else if (py < 68.f && home_page > 0) {
                        home_scroll.snap_to_page(home_page - 1);
                        home_page = home_page - 1;
                        edit_edge_scroll_cooldown = 0.55f;
                        emit_ui_sound(sound_page_scroll);
                    }
                }
                return 1;
            }
            if (topbar_drag) return 1;

            const float dx_total = px - home_touch_start.x;
            const float dy_total = py - home_touch_start.y;
            const float dt = std::clamp(
                float(event_ns - home_drag_last_event_ns) * 1.0e-9f, 0.001f, 0.1f);
            if (!home_drag_moved && !home_horiz_drag && std::hypot(dx_total, dy_total) > 14.f) {
                home_touch_down = false;
                home_touch_page = -1;
                home_touch_slot = -1;
                if (livearea_deck.tab_count() > 0 && !edit_visual.edit_mode
                    && std::abs(dx_total) > std::abs(dy_total) * 1.15f) {
                    home_horiz_drag = true;
                    livearea_deck.begin_horizontal_drag(px / pager_scale());
                } else if (livearea_deck.is_home_active()) {
                    home_drag_moved = true;
                    home_scroll.begin_drag();
                }
            }
            if (home_horiz_drag) {
                livearea_deck.update_horizontal_drag(px / pager_scale(), dt);
            } else if (home_drag_moved) {
                home_scroll.drag_by(py - home_drag_last_y, dt);
            }
            home_drag_last_x = px;
            home_drag_last_y = py;
            home_drag_last_event_ns = event_ns;
            return 1;
        }

        if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL) {
            if (peeker_peel_drag) {
                peeker_peel_drag = false;
                const auto res = livearea_deck.end_peel_drag(action == AMOTION_EVENT_ACTION_CANCEL);
                if (res == emucorev::paf::LiveAreaPeelEndResult::started_close) {
                    emit_ui_sound(sound_page_turn);
                }
                return 1;
            }
            if (edit_drag_active) {
                edit_drag_active = false;
                if (action == AMOTION_EVENT_ACTION_UP) {
                    const auto [dp, ds] = hit_test_home_slot(px, py, 76.f);
                    if (dp >= 0 && (dp != edit_drag_src_page || ds != edit_drag_src_slot)) {
                        const auto* src_e = desktop.entry_at(edit_drag_src_page, edit_drag_src_slot);
                        const bool allow_folder = src_e && src_e->kind == emucorev::paf::HomeEntryKind::app;
                        if (desktop.drop_onto_slot(edit_drag_src_page, edit_drag_src_slot,
                                dp, ds, allow_folder, "New Folder")) {
                            sync_visual_slots_with_desktop();
                            home_scroll.set_page_count(desktop.page_count());
                            persist_home_layout();
                            emit_ui_sound(sound_decide);
                        }
                    }
                }
                edit_drag_src_page = -1;
                edit_drag_src_slot = -1;
                return 1;
            }
            if (topbar_drag) {
                topbar_drag = false;
                if (action == AMOTION_EVENT_ACTION_UP
                    && std::hypot(px - home_touch_start.x, py - home_touch_start.y) < 28.f) {
                    const int count = 1 + livearea_deck.tab_count();
                    const float pitch = 32.f;
                    const float start_x = 480.f - static_cast<float>(count) * pitch * 0.5f;
                    const int idx = static_cast<int>(std::floor((px - start_x) / pitch));
                    if (idx == 0) {
                        if (livearea_deck.slide_to_home()) emit_ui_sound(sound_page_scroll);
                    } else if (idx > 0 && idx - 1 < livearea_deck.tab_count()) {
                        if (livearea_deck.slide_to_tab(idx - 1)) emit_ui_sound(sound_page_scroll);
                    }
                }
                return 1;
            }
            if (home_horiz_drag) {
                home_horiz_drag = false;
                const int prev = livearea_deck.active_tab_index();
                const int next_page = livearea_deck.end_horizontal_drag();
                if (next_page - 1 != prev) emit_ui_sound(sound_page_scroll);
                return 1;
            }
            if (home_drag_moved) {
                const int old_page = home_page;
                const int next_page = home_scroll.end_drag();
                home_page = next_page;
                if (next_page != old_page) emit_ui_sound(sound_page_scroll);
                const auto first_occ = desktop.find_first_occupied(next_page);
                if (home_focus_page != next_page) {
                    home_focus_page = first_occ.first;
                    home_focus_slot = first_occ.second;
                }
            } else if (action == AMOTION_EVENT_ACTION_UP
                && std::hypot(px - home_touch_start.x, py - home_touch_start.y) < 30.f) {
                if (!edit_visual.edit_mode) {
                    if (livearea_deck.pager_position() > 0.25f && px < edge_left() + 64.f
                        && py >= 220.f && py <= 356.f) {
                        if (livearea_deck.slide_left()) emit_ui_sound(sound_page_scroll);
                        home_touch_down = false;
                        return 1;
                    }
                    if (livearea_deck.tab_count() > 0
                        && livearea_deck.pager_position() < static_cast<float>(livearea_deck.tab_count()) - 0.25f
                        && px > edge_right() - 64.f && py >= 220.f && py <= 356.f) {
                        if (livearea_deck.slide_right()) emit_ui_sound(sound_page_scroll);
                        home_touch_down = false;
                        return 1;
                    }
                }
                if (!livearea_deck.is_home_active()) {
                    std::string target;
                    if (livearea_target_at(px, py, target)) {
                        emit_ui_sound(sound_decide);
                        emit_livearea_target_request(
                            livearea_deck.tabs()[static_cast<std::size_t>(livearea_deck.active_tab_index())].title_id,
                            target);
                    }
                } else if (home_touch_page >= 0 && home_touch_slot >= 0) {
                    home_focus_page = home_touch_page;
                    home_focus_slot = home_touch_slot;
                    open_entry_at(home_touch_page, home_touch_slot, px, py);
                }
            }
            home_touch_down = false;
            home_touch_page = -1;
            home_touch_slot = -1;
            home_drag_moved = false;
            return 1;
        }
        return 0;
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

        // 3. Empty back material (36 floats): p[4].zw hold shadow & shadow_level (page_turn_empty_back_vertex_shader).
        std::copy(transforms.model_view_projection.begin(), transforms.model_view_projection.end(),
            lock_empty_material.begin());
        lock_empty_material[16] = 0.f;
        lock_empty_material[17] = 0.f;
        lock_empty_material[18] = lock_effect_style.empty_shadow;
        lock_empty_material[19] = lock_effect_style.empty_shadow_level;
        constexpr std::array<float, 4> empty_uv_rect{1.f, 1.f, 0.f, 0.f};
        std::copy(empty_uv_rect.begin(), empty_uv_rect.end(), lock_empty_material.begin() + 20);
        lock_empty_material[24] = 1.f;
        lock_empty_material[25] = 1.f;
        lock_empty_material[26] = 1.f;
        lock_empty_material[27] = 1.f;
        std::copy(shape_mat.fold_plane.begin(), shape_mat.fold_plane.end(), lock_empty_material.begin() + 28);
        std::copy(shape_mat.aspect.begin(), shape_mat.aspect.end(), lock_empty_material.begin() + 32);
    }

    void tick_lock(float elapsed) {
        if (lock_anim_state == LockAnimState::unlocked) {
            tick_home(elapsed);
            return;
        }
        if (lock_anim_state == LockAnimState::idle)
            return;
        if (lock_anim_state == LockAnimState::peeling) tick_home(elapsed);
        anim_elapsed += elapsed;
        const float t = anim_duration > 0.f ? std::clamp(anim_elapsed / anim_duration, 0.f, 1.f) : 1.f;
        if (lock_anim_state == LockAnimState::peeling) {
            const float alpha_t = std::clamp(anim_elapsed / 0.300f, 0.f, 1.f);
            const float alpha_eased = 1.f - (1.f - alpha_t) * (1.f - alpha_t);
            lock_page_alpha = 1.f - alpha_eased;
            lock_exit_z = 200.f * alpha_eased;
        } else {
            lock_exit_z = 0.f;
        }
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
                lock_exit_z = 0.f;
                home_visible = true;
            } else {
                lock_anim_state = LockAnimState::idle;
            }
        }
    }

    bool lock_needs_background() const noexcept {
        return home_visible || lock_anim_state == LockAnimState::peeling;
    }

    bool is_livearea_visible() const noexcept { return livearea_deck.is_livearea_visible(); }

    void ensure_wave() {
        std::string theme_name;
        std::uint64_t rev = 0;
        {
            const std::lock_guard lock_guard(status_mutex);
            theme_name = requested_wave_theme;
            rev = wave_theme_revision;
        }
        auto sync_index = [&](const emucorev::paf::WaveThemePreset& active) {
            const auto presets = emucorev::paf::wave_theme_presets();
            for (std::size_t i = 0; i < presets.size(); ++i) {
                if (presets[i].name == active.name) {
                    wave_theme_index = i;
                    break;
                }
            }
        };
        if (!wave) {
            wave = std::make_unique<emucorev::paf::WaveRenderer>(theme_name);
            sync_index(wave->theme());
            applied_wave_theme_revision = rev;
        } else if (rev != applied_wave_theme_revision) {
            wave->set_theme(theme_name);
            sync_index(wave->theme());
            applied_wave_theme_revision = rev;
        }
    }

    void tick_wave(float elapsed) {
        ensure_wave();
        wave->step(elapsed);
    }

    // Home background: theme/photo page images when set (cover-scaled,
    // cross-faded while scrolling between pages), otherwise the wave.
    void draw_wave() {
        std::vector<std::uint32_t> wallpapers;
        {
            const std::lock_guard lock_guard(status_mutex);
            wallpapers = home_wallpaper_ids;
        }
        if (!wallpapers.empty()) {
            glBindFramebuffer(GL_FRAMEBUFFER, target);
            glViewport(0, 0, target_w, target_h);
            glDepthMask(GL_TRUE);
            glClear(GL_DEPTH_BUFFER_BIT);
            glDepthMask(GL_FALSE);
            sprites_begin();
            const float page_f = std::max(0.f, home_scroll.scroll_y() / emucorev::paf::kHomePageHeight);
            const int p0 = int(std::floor(page_f));
            const float t = page_f - float(p0);
            auto image_for = [&](int page) {
                return wallpapers[std::size_t(std::clamp(page, 0, int(wallpapers.size()) - 1))];
            };
            auto draw_cover = [&](std::uint32_t id, float alpha) {
                float iw = 960.f, ih = 544.f;
                {
                    const std::lock_guard lock_guard(status_mutex);
                    if (const auto it = host_images.find(id); it != host_images.end()) {
                        iw = float(it->second.width);
                        ih = float(it->second.height);
                    }
                }
                const float scale = std::max(cw / iw, ch / ih);
                const float w = iw * scale, h = ih * scale;
                sprites->draw(id, 480.f - w * .5f, 272.f - h * .5f, w, h, {1.f, 1.f, 1.f, alpha});
            };
            draw_cover(image_for(p0), 1.f);
            if (t > 0.001f && image_for(p0 + 1) != image_for(p0)) draw_cover(image_for(p0 + 1), t);
            return;
        }
        ensure_wave();
        wave->draw(target, std::uint32_t(target_w), std::uint32_t(target_h));
        // Clear wave depth so 2D sprites, Home bubbles, and Lock PageTurn compose cleanly on top.
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(0, 0, target_w, target_h);
        glDepthMask(GL_TRUE);
        glClear(GL_DEPTH_BUFFER_BIT);
        glDepthMask(GL_FALSE);
    }

    int32_t handle_wave_input(AInputEvent* event) {
        const auto presets = emucorev::paf::wave_theme_presets();
        if (presets.empty()) return 0;
        auto advance_theme = [&](int delta) {
            ensure_wave();
            const std::size_t n = presets.size();
            wave_theme_index = (wave_theme_index + n + static_cast<std::size_t>(delta % static_cast<int>(n))) % n;
            wave->set_theme(presets[wave_theme_index]);
        };
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
            if (AKeyEvent_getAction(event) != AKEY_EVENT_ACTION_DOWN || AKeyEvent_getRepeatCount(event) != 0)
                return 0;
            const int key = AKeyEvent_getKeyCode(event);
            if (key == AKEYCODE_DPAD_RIGHT || key == AKEYCODE_BUTTON_A || key == AKEYCODE_ENTER) {
                advance_theme(1);
                return 1;
            }
            if (key == AKEYCODE_DPAD_LEFT) {
                advance_theme(-1);
                return 1;
            }
            return 0;
        }
        if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
            const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
            if (action == AMOTION_EVENT_ACTION_UP) advance_theme(1);
            return 1;
        }
        return 0;
    }

    int32_t handle_input(AInputEvent* event) {
        if (preview_scene.load() == 4) return handle_wave_input(event);
        if (home_visible) return handle_shell_input(event);
        if (lock_anim_state == LockAnimState::peeling) return 1;
        if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
        const int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (surface_w <= 0 || surface_h <= 0) return 0;
        const auto [px, py] = to_content(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));

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
                    home_enter_elapsed = 0.f;
                    lock_page_alpha = 1.f;
                    lock_exit_z = 0.f;
                    anim_elapsed = 0.f;
                    anim_duration = 0.500f;
                    anim_start_point = lock_point;
                    anim_target_point = lock_fold_point({
                        -2.f * lock_page_config.size.y, -2.f * lock_page_config.size.x});
                    emit_ui_sound(sound_page_turn);
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
                // BubbleRenderer owns icon textures on demand for the active
                // 10-item page; never mirror the full catalog into sprites.
                if (id >= home_icon_image_min && id < home_icon_image_limit) continue;
                if (uploaded_images[id] == image.revision) continue;
                sprites->upload(id, image.width, image.height, image.rgba);
                uploaded_images[id] = image.revision;
            }
            uploaded_revision = image_revision;
        }
    }

    void draw_status(std::uint32_t charging_texture) {
        const std::lock_guard lock(status_mutex);
        const int scene = preview_scene.load();
        const float bar_y = (scene == 3 || home_visible)
            ? (-32.f * edit_visual.edit_blend) : 0.f;
        if (bar_y <= -31.5f) return;

        // Canvas coordinates: the bar spans the whole display width.
        sprites->begin(cw, ch);
        if (theme_bar_color) {
            const auto c = theme_bar_color;
            sprites->draw(0, 0, bar_y, cw, 32, {float((c >> 16) & 0xff) / 255.f, float((c >> 8) & 0xff) / 255.f,
                float(c & 0xff) / 255.f, float(c >> 24) / 255.f});
        } else if (!sprites->draw(status_bar_bg_id, 0, bar_y, cw, 32)) {
            sprites->draw(0, 0, bar_y, cw, 32, {0, 0, 0, 0.5f});
        }
        const float right_edge = cw - safe_r;
        const bool show_home_chrome = (scene == 3)
            || (scene == 2 && (home_visible || lock_anim_state == LockAnimState::peeling));

        if (show_home_chrome) {
            const int open_tabs = livearea_deck.tab_count();
            const int total_items = 1 + open_tabs;
            constexpr float strip_pitch = 32.f;
            const float strip_start = cw * .5f - static_cast<float>(total_items) * strip_pitch * 0.5f;
            const float home_alpha = livearea_deck.is_home_active() ? 1.f : 0.45f;
            sprites->draw(status_home_icon_id, strip_start + 2.f, bar_y + 3.f, 28.f, 26.f,
                {1.f, 1.f, 1.f, home_alpha});
            for (int i = 0; i < open_tabs; ++i) {
                const auto& tab = livearea_deck.tabs()[static_cast<std::size_t>(i)];
                if (tab.catalog_index < 0) continue;
                const float chip_alpha = (livearea_deck.active_tab_index() == i) ? 1.f : 0.55f;
                const float cx = strip_start + static_cast<float>(1 + i) * strip_pitch + 3.f;
                const std::uint32_t chip_id = home_chip_image_base
                    + static_cast<std::uint32_t>(tab.catalog_index);
                if (!sprites->draw(chip_id, cx, bar_y + 3.f, 26.f, 26.f,
                        {1.f, 1.f, 1.f, chip_alpha})) {
                    const auto snap_it = livearea_catalog.find(tab.title_id);
                    if (snap_it != livearea_catalog.end() && snap_it->second.gate_id) {
                        sprites->draw(snap_it->second.gate_id, cx, bar_y + 4.f, 24.f, 24.f,
                            {1.f, 1.f, 1.f, chip_alpha});
                    }
                }
            }
            // The orb is a corner element: it stays flush with the physical
            // corner (a rounded panel corner trims it like the Vita bezel).
            sprites->draw(status_notif_orb_idle_id, cw - 72.f * orb_scale, bar_y, 72.f * orb_scale, 70.f * orb_scale);
            const float glow_alpha = 0.03f + 0.08f * (0.5f + 0.5f * std::sin(status_orb_phase));
            sprites->set_additive_blend(true);
            sprites->draw(status_notif_orb_glow_id, cw - 72.f * orb_scale, bar_y, 72.f * orb_scale, 70.f * orb_scale,
                {1.f, 1.f, 1.f, glow_alpha});
            sprites->set_additive_blend(false);
        }

        constexpr std::array<std::uint32_t, 5> battery{
            0x78600544, 0x4a8ae255, 0x9e9cfcab, 0xf85f0b80, 0x56e72f16};
        const auto& status = device_status;
        auto battery_id = status.battery_percent < 0 ? 0xa5aafd9bu
            : battery[std::min(4, std::clamp(status.battery_percent, 0, 100) * 4 / 100)];
        const float bat_x = show_home_chrome ? cw - 62.f - 72.f * orb_scale : right_edge - 56.f;
        const std::array<float, 4> bat_tint = status.charging
            ? std::array<float, 4>{0.72f, 1.f, 0.72f, 1.f}
            : std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
        sprites->draw(battery_id, bat_x, bar_y + 2.f, 48, 28, bat_tint);
        if (charging_texture && status.charging && status.battery_percent >= 0 && status.battery_percent < 100)
            sprites->draw(charging_texture, bat_x, bar_y + 2.f, 48, 28, bat_tint);
        if (const auto clock = host_images.find(1); clock != host_images.end()) {
            const float clock_right = show_home_chrome ? cw - 64.f - 72.f * orb_scale : right_edge - 58.f;
            sprites->draw(1, clock_right - float(clock->second.width), bar_y,
                float(clock->second.width), float(clock->second.height));
        }
        float left = 8 + safe_l;
        if (status.wifi_enabled == emucorev::paf::RadioState::on && status.wifi_connected) {
            constexpr std::array<std::uint32_t, 4> wifi{0x89dc8410, 0x2278fb2a, 0xee958f99, 0xa13376bc};
            const int level = status.wifi_rssi == INT32_MIN ? 0
                : std::clamp((std::clamp(status.wifi_rssi, -100, -55) + 100) * 3 / 45, 0, 3);
            sprites->draw(wifi[level], left, bar_y + 4.f, 23, 24);
            left += 31;
        }
        if (status.bluetooth_enabled == emucorev::paf::RadioState::on)
            sprites->draw(0xca76be07, left, bar_y + 4.f, 19, 24);
    }

    void upload_lock_texture_slot(std::uint32_t id, std::size_t slot, GLuint& texture) {
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
    }

    void ensure_exp_texture() {
        const std::lock_guard lock_guard(status_mutex);
        upload_lock_texture_slot(102, 2, lock_exp);
    }

    void ensure_lock_textures() {
        const std::lock_guard lock_guard(status_mutex);
        upload_lock_texture_slot(100, 0, lock_page);
        upload_lock_texture_slot(101, 1, lock_corner);
        upload_lock_texture_slot(102, 2, lock_exp);
        upload_lock_texture_slot(lock_empty_bg_image_id, 3, lock_empty_bg);
        lock_ready = lock_page && lock_corner && lock_exp && lock_empty_bg;
    }

    // Extends the lock bezel image to the adaptive canvas edges by repeating
    // its outermost texel row/column (a uniform dark bezel), never scaling it.
    void draw_keylock_bleed(float alpha) {
        const std::array<float, 4> tint{1.f, 1.f, 1.f, alpha};
        const float eu = 0.5f / preview_width, ev = 0.5f / preview_height;
        if (cx0 > 0.f) {
            sprites->draw_patch(raw_keylock_image_id, -cx0, 0.f, cx0, preview_height, 0.f, 0.f, eu, 1.f, tint);
            // The right bezel edge carries the PS symbol watermark near the
            // bottom; continue that band with the plain left-edge column.
            constexpr float split = 440.f;
            sprites->draw_patch(raw_keylock_image_id, preview_width, 0.f, cx0, split,
                1.f - eu, 0.f, eu, split / preview_height, tint);
            sprites->draw_patch(raw_keylock_image_id, preview_width, split, cx0, preview_height - split,
                0.f, split / preview_height, eu, 1.f - split / preview_height, tint);
        }
        if (cy0 > 0.f) {
            sprites->draw_patch(raw_keylock_image_id, -cx0, -cy0, cw, cy0, 0.f, 0.f, 1.f, ev, tint);
            // The bottom bezel row carries the PS symbol watermark: continue it
            // with the plain dark bottom-left texel instead of streaking it.
            sprites->draw_patch(raw_keylock_image_id, -cx0, preview_height, cw, cy0, 0.f, 1.f - ev, eu, ev, tint);
        }
    }

    void draw_lock() {
        ensure_lock_textures();
        if (!lock || !lock_ready || !lock_mesh.vertex_count) return;
        if (lock_anim_state == LockAnimState::unlocked) return;

        sprites_begin();
        if (!lock_needs_background()) {
            // Draw full pristine bezel wallpaper behind PageTurn at rest so rounded corners show cleanly.
            sprites->draw(raw_keylock_image_id, 0.f, 0.f, preview_width, preview_height,
                {1.f, 1.f, 1.f, lock_page_alpha});
            draw_keylock_bleed(lock_page_alpha);
        } else {
            // Draw the 4 non-overlapping outer border strips around [32..928, 64..512] during peel.
            const float zoom = 1.f / std::max(0.25f, 1.f - lock_exit_z / 960.f);
            auto sx = [&](float x) { return preview_width * .5f + (x - preview_width * .5f) * zoom; };
            auto sy = [&](float y) { return preview_height * .5f + (y - preview_height * .5f) * zoom; };
            auto draw_border = [&](float x0, float y0, float x1, float y1) {
                const float dx0 = sx(x0), dy0 = sy(y0), dx1 = sx(x1), dy1 = sy(y1);
                sprites->draw_patch(raw_keylock_image_id, dx0, dy0, dx1 - dx0, dy1 - dy0,
                    x0 / preview_width, y0 / preview_height,
                    (x1 - x0) / preview_width, (y1 - y0) / preview_height,
                    {1.f, 1.f, 1.f, lock_page_alpha});
            };
            draw_border(0.f, 0.f, 960.f, 64.f);
            draw_border(0.f, 512.f, 960.f, 544.f);
            draw_border(0.f, 64.f, 32.f, 512.f);
            draw_border(928.f, 64.f, 960.f, 512.f);
            draw_keylock_bleed(lock_page_alpha);
        }

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
            1.f / cw, 1.f / ch,
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
        folded_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, cw, ch, .5f,  .5f, 0.f, 0.f, 0.f};
        folded_draw.fragment_state = round_frag_state;
        folded_draw.textures = {lock_page, lock_corner, 0, 0, lock_exp};
        folded_draw.blend = true;
        folded_draw.vertex_parameters = lock_top_material;
        folded_draw.fragment_parameters = top_round_fragment_material;
        folded_draw.meta_alpha = lock_page_alpha;

        // 2. Front surface (Bottom round) - Event 132 in libpaf proto
        std::array<float, 8> bottom_round_fragment_material{};
        bottom_round_fragment_material[0] = corner_size_x;
        bottom_round_fragment_material[1] = corner_size_y;
        bottom_round_fragment_material[2] = corner_inv_x;
        bottom_round_fragment_material[3] = corner_inv_y;

        emucorev::paf::PageSurfaceDraw front_draw{};
        front_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, cw, ch, .5f,  .5f, 0.f, 0.f, 0.f};
        front_draw.fragment_state = round_frag_state;
        front_draw.textures = {lock_page, lock_corner, 0, 0, lock_exp};
        front_draw.blend = true;
        front_draw.vertex_parameters = lock_bottom_material;
        front_draw.fragment_parameters = bottom_round_fragment_material;
        front_draw.meta_alpha = lock_page_alpha;

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
        empty_draw.viewport = {1.f, 1.f, 1.f, 1.f,  1.f, cw, ch, .5f,  .5f, 0.f, 0.f, 0.f};
        empty_draw.fragment_state = round_frag_state;
        empty_draw.textures = {lock_corner, lock_empty_bg, 0, 0, 0};
        empty_draw.blend = true;
        empty_draw.vertex_parameters = lock_empty_material;
        empty_draw.fragment_parameters = empty_back_fragment_material;
        empty_draw.meta_alpha = lock_page_alpha;
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
        ensure_canvas(width, height);
        glBindFramebuffer(GL_FRAMEBUFFER, target);
        glViewport(0, 0, target_w, target_h);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        upload_images();
        if (preview_scene.load() == 2) {
            if (lock_needs_background()) {
                draw_wave();
                draw_livearea_and_home_composition();
                app_screen.draw(*sprites, app_canvas());
                draw_notification_overlay();
            }
            draw_lock();
            draw_status(charging_texture);
        } else if (preview_scene.load() == 3) {
            draw_wave();
            draw_livearea_and_home_composition();
            app_screen.draw(*sprites, app_canvas());
            draw_notification_overlay();
            draw_status(charging_texture);
        } else if (preview_scene.load() == 4) {
            draw_wave();
        }
        else if (busy) {
            sprites_begin();
            if (busy->state != emucorev::paf::BusyState::idle)
                for (const auto& dot : busy->dots)
                    if (dot.size > 0 && dot.alpha)
                        sprites->draw(busy_ball_image_id, 480 + dot.x - dot.size * .5f,
                            272 - dot.y - dot.size * .5f, dot.size, dot.size,
                            {busy_ball_tint[0], busy_ball_tint[1], busy_ball_tint[2],
                                float(dot.alpha) * (1.f / 255.f)});
        } else {
            bubble->draw(vertex, material);
            draw_status(charging_texture);
        }

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        // The canvas already matches the display aspect: fill the surface.
        const int w = static_cast<int>(std::lround(cw * surf_scale));
        const int h = static_cast<int>(std::lround(ch * surf_scale));
        const int x = (width - w) / 2, y = (height - h) / 2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, target);
        glBlitFramebuffer(0, 0, target_w, target_h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        if (!eglSwapBuffers(display, surface)) throw std::runtime_error("Preview presentation failed");
    }
};

struct Preview {
    emucorev::paf::BusyIndicator busy{72, 16};
    emucorev::paf::ChargingIndicator charging;
    std::unique_ptr<Window> window;
    android_app* app = nullptr;
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
        if (preview_scene.load() == 1 && busy.snapshot().state == emucorev::paf::BusyState::idle
            && native_ready_scene.load() < 0 && !native_preview_failed.load()) return;
        if (!choreographer) choreographer = AChoreographer_getInstance();
        if (!choreographer) throw std::runtime_error("No native frame scheduler");
        callback_pending = true;
        AChoreographer_postFrameCallback64(choreographer, [](int64_t time, void* data) {
            auto& preview = *static_cast<Preview*>(data);
            preview.callback_pending = false;
            if (!preview.active()) return;
            try {
                    // JNI can run on Android's UI thread. It only sets this
                    // atomic request; native scene state is changed here on
                    // the render/looper thread.
                    if (native_back_requested.exchange(false) && preview.window
                        && !preview.window->close_livearea())
                        emit_exit_request();
                    if (native_preview_failed.exchange(false)) {
                        preview.busy.stop();
                    }
                    int ready_scene = native_ready_scene.load();
                    if (ready_scene >= 0 && preview.app && preview.app->window
                        && native_ready_scene.compare_exchange_strong(ready_scene, -1)) {
                        preview_scene.store(ready_scene);
                        native_preview_loading.store(false);
                        preview.window.reset();
                        auto next = std::make_unique<Window>();
                        next->initialize(preview.app);
                        preview.window = std::move(next);
                        if (!preview.phase_initialized) {
                            preview.phase = preview.window->initial_phase;
                            preview.phase_initialized = true;
                        } else if (ready_scene == 0) {
                            preview.window->set_phase(preview.phase);
                        }
                        preview.last_draw = preview.next_draw = 0;
                    }
                // Vsync timestamps round fractional refresh periods to ns.
                // Do not skip a 60 Hz frame merely because it is a few ns
                // earlier than the accumulated 16,666,667 ns deadline.
                if (time + 500000 >= preview.next_draw) {
                    if (preview_scene.load() == 0 || preview_scene.load() == 2 || preview_scene.load() == 3) {
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
                            if (preview.window->lock_needs_background())
                                preview.window->tick_wave(elapsed);
                        } else if (preview_scene.load() == 3 && preview.window) {
                            preview.window->tick_home(elapsed);
                            preview.window->tick_wave(elapsed);
                        } else if (preview_scene.load() == 4 && preview.window) {
                            preview.window->tick_wave(elapsed);
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
            this->app = app;
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
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeResourceRoot(JNIEnv* env, jobject, jstring path) {
    const char* text = env->GetStringUTFChars(path, nullptr);
    if (!text) return;
    {
        const std::lock_guard lock(status_mutex);
        resource_root = text;
    }
    env->ReleaseStringUTFChars(path, text);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativePreviewScene(JNIEnv*, jobject, jint scene) {
    native_ready_scene.store(-1);
    native_preview_failed.store(false);
    native_back_requested.store(false);
    preview_scene.store(scene);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeSetWaveTheme(JNIEnv* env, jobject, jstring theme) {
    if (!theme) return;
    const char* text = env->GetStringUTFChars(theme, nullptr);
    if (!text) return;
    {
        const std::lock_guard lock(status_mutex);
        if (*text != '\0') requested_wave_theme = text;
        ++wave_theme_revision;
    }
    env->ReleaseStringUTFChars(theme, text);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativePreviewReady(JNIEnv*, jobject, jint scene) {
    if (scene < 0 || scene > 4) return;
    native_preview_failed.store(false);
    native_ready_scene.store(scene);
    {
        const std::lock_guard lock(status_mutex);
        if (native_preview_looper) ALooper_wake(native_preview_looper);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativePreviewFailed(JNIEnv*, jobject) {
    native_ready_scene.store(-1);
    native_preview_loading.store(false);
    native_preview_failed.store(true);
    {
        const std::lock_guard lock(status_mutex);
        if (native_preview_looper) ALooper_wake(native_preview_looper);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativePreviewLoading(JNIEnv*, jobject, jboolean loading) {
    native_preview_loading.store(loading == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeAppPage(JNIEnv* env, jobject,
    jstring app, jint key, jint transition, jint background, jint header, jfloatArray header_rect,
    jfloatArray list_rect, jintArray row_images, jintArray row_plates, jintArray row_glows,
    jfloatArray row_geometry, jbooleanArray row_selectable, jint focus) {
    if (!app || !header_rect || !list_rect || !row_images || !row_plates || !row_glows || !row_geometry
        || !row_selectable || env->GetArrayLength(header_rect) != 4 || env->GetArrayLength(list_rect) != 4) return;
    const jsize n = env->GetArrayLength(row_images);
    if (n > 4096 || env->GetArrayLength(row_plates) != n || env->GetArrayLength(row_glows) != n
        || env->GetArrayLength(row_selectable) != n || env->GetArrayLength(row_geometry) != n * 5) return;
    shell_app::Page page;
    const char* app_text = env->GetStringUTFChars(app, nullptr);
    if (!app_text) return;
    page.app = app_text;
    env->ReleaseStringUTFChars(app, app_text);
    page.key = key;
    page.background = std::uint32_t(background);
    page.header = std::uint32_t(header);
    env->GetFloatArrayRegion(header_rect, 0, 4, page.header_rect.data());
    env->GetFloatArrayRegion(list_rect, 0, 4, page.list_rect.data());
    const auto count = static_cast<std::size_t>(n);
    std::vector<jint> images(count), plates(count), glows(count);
    std::vector<jfloat> geometry(count * 5);
    std::vector<jboolean> selectable(count);
    if (n) {
        env->GetIntArrayRegion(row_images, 0, n, images.data());
        env->GetIntArrayRegion(row_plates, 0, n, plates.data());
        env->GetIntArrayRegion(row_glows, 0, n, glows.data());
        env->GetFloatArrayRegion(row_geometry, 0, n * 5, geometry.data());
        env->GetBooleanArrayRegion(row_selectable, 0, n, selectable.data());
    }
    page.rows.reserve(std::size_t(n));
    for (jsize i = 0; i < n; ++i) {
        const auto g = geometry.data() + std::size_t(i) * 5;
        shell_app::Row row;
        row.image = std::uint32_t(images[std::size_t(i)]);
        row.plate = std::uint32_t(plates[std::size_t(i)]);
        row.glow = std::uint32_t(glows[std::size_t(i)]);
        row.x = g[0]; row.width = g[1]; row.height = std::max(1.f, g[2]);
        row.image_width = g[3]; row.image_height = g[4];
        row.selectable = selectable[std::size_t(i)] == JNI_TRUE;
        page.rows.push_back(row);
    }
    page.focus = (focus >= 0 && focus < n) ? focus : -1;
    const std::lock_guard lock(status_mutex);
    pending_app_pages.push_back({std::move(page), transition});
    if (native_preview_looper) ALooper_wake(native_preview_looper);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeWaveThemes(JNIEnv* env, jobject) {
    const auto presets = emucorev::paf::wave_theme_presets();
    jclass string_class = env->FindClass("java/lang/String");
    if (!string_class) return nullptr;
    jobjectArray result = env->NewObjectArray(jsize(presets.size()), string_class, nullptr);
    for (std::size_t i = 0; result && i < presets.size(); ++i) {
        const std::string name(presets[i].name);
        jstring value = env->NewStringUTF(name.c_str());
        env->SetObjectArrayElement(result, jsize(i), value);
        env->DeleteLocalRef(value);
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeSetHomeTheme(JNIEnv* env, jobject,
    jintArray wallpapers, jint bar_color) {
    std::vector<std::uint32_t> ids;
    if (wallpapers) {
        const jsize n = std::min<jsize>(env->GetArrayLength(wallpapers), 10);
        std::vector<jint> values(static_cast<std::size_t>(n));
        if (n) env->GetIntArrayRegion(wallpapers, 0, n, values.data());
        for (const auto v : values) if (v) ids.push_back(std::uint32_t(v));
    }
    const std::lock_guard lock(status_mutex);
    home_wallpaper_ids = std::move(ids);
    theme_bar_color = std::uint32_t(bar_color);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeSetDisplayArea(JNIEnv*, jobject, jint mode) {
    display_area_mode.store(mode == 1 ? 1 : 0);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeAppClose(JNIEnv*, jobject) {
    const std::lock_guard lock(status_mutex);
    pending_app_close = true;
    if (native_preview_looper) ALooper_wake(native_preview_looper);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeSafeArea(JNIEnv* env, jobject, jfloatArray area) {
    if (!area || env->GetArrayLength(area) != 8) return;
    std::array<float, 8> values{};
    env->GetFloatArrayRegion(area, 0, 8, values.data());
    for (auto& v : values) v = std::clamp(std::isfinite(v) ? v : 0.f, 0.f, 4096.f);
    const std::lock_guard lock(status_mutex);
    safe_area_px = values;
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeBackRequested(JNIEnv*, jobject) {
    native_back_requested.store(true);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeImage(JNIEnv* env, jobject,
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
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeStatus(JNIEnv* env, jobject,
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
    {
        const std::lock_guard lock(status_mutex);
        native_preview_looper = app->looper;
    }
    app->userData = &preview;
    app->onAppCmd = [](android_app* state, int32_t command) {
        static_cast<Preview*>(state->userData)->command(state, command);
    };
    app->onInputEvent = [](android_app* state, AInputEvent* event) -> int32_t {
        auto& preview = *static_cast<Preview*>(state->userData);
        if (!preview.active()) return 0;
        const int scene = preview_scene.load();
        if (scene == 1) {
            if (native_preview_loading.load()) return 1;
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
        } else if ((scene == 2 || scene == 3 || scene == 4) && preview.window) {
            const int handled = preview.window->handle_input(event);
            if (handled) {
                // Draw the response on the next vsync, but keep the clock:
                // resetting last_draw made every frame during a touch tick
                // with elapsed = 0, freezing animations while a finger moved.
                preview.next_draw = 0;
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
        if ((native_ready_scene.load() >= 0 || native_preview_failed.load()) && preview.active())
            preview.schedule();
    }
    {
        const std::lock_guard lock(status_mutex);
        native_preview_looper = nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeHomeCatalog(JNIEnv* env, jobject activity,
    jobjectArray ids, jobjectArray titles, jobjectArray content_ids,
    jintArray icon_image_ids, jintArray label_image_ids) {
    if (!ids || !titles || !content_ids || !icon_image_ids || !label_image_ids) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Missing Home catalog array");
        return;
    }
    const jsize count = env->GetArrayLength(ids);
    if (count > 512 || env->GetArrayLength(titles) != count
        || env->GetArrayLength(content_ids) != count || env->GetArrayLength(icon_image_ids) != count
        || env->GetArrayLength(label_image_ids) != count) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Invalid Home catalog lengths");
        return;
    }
    std::vector<jint> icons(static_cast<std::size_t>(count)), labels(static_cast<std::size_t>(count));
    if (count) {
        env->GetIntArrayRegion(icon_image_ids, 0, count, icons.data());
        env->GetIntArrayRegion(label_image_ids, 0, count, labels.data());
    }
    if (env->ExceptionCheck()) return;
    auto read_string = [&](jobjectArray array, jsize index, bool required) {
        auto value = static_cast<jstring>(env->GetObjectArrayElement(array, index));
        if (!value) throw std::invalid_argument("Null Home catalog string");
        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (!chars) { env->DeleteLocalRef(value); throw std::runtime_error("Cannot read Home catalog string"); }
        std::string result(chars);
        env->ReleaseStringUTFChars(value, chars);
        env->DeleteLocalRef(value);
        if ((required && result.empty()) || result.size() > 256)
            throw std::invalid_argument("Invalid Home catalog string");
        return result;
    };
    try {
        std::vector<HomeCatalogItem> next;
        next.reserve(static_cast<std::size_t>(count));
        for (jsize i = 0; i < count; ++i) {
            next.push_back({read_string(ids, i, true), read_string(titles, i, true), read_string(content_ids, i, false),
                static_cast<std::uint32_t>(icons[static_cast<std::size_t>(i)]),
                static_cast<std::uint32_t>(labels[static_cast<std::size_t>(i)])});
        }
        {
            const std::lock_guard lock_guard(status_mutex);
            home_catalog = std::move(next);
            ++home_catalog_revision;
        }
        JavaVM* vm = nullptr;
        env->GetJavaVM(&vm);
        jclass activity_class = env->GetObjectClass(activity);
        const auto callback = activity_class
            ? env->GetMethodID(activity_class, "onNativeLiveAreaTarget", "(Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        const auto request_cb = activity_class
            ? env->GetMethodID(activity_class, "onNativeRequestLiveArea", "(Ljava/lang/String;)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        const auto exit_cb = activity_class
            ? env->GetMethodID(activity_class, "onNativeExitRequested", "(Ljava/lang/String;)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        const auto app_row_cb = activity_class
            ? env->GetMethodID(activity_class, "onNativeAppRow", "(Ljava/lang/String;II)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        const auto app_back_cb = activity_class
            ? env->GetMethodID(activity_class, "onNativeAppBack", "(Ljava/lang/String;I)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        const auto sound_cb = activity_class
            ? env->GetMethodID(activity_class, "onNativeUiSound", "(I)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        const auto save_cb = activity_class
            ? env->GetMethodID(activity_class, "onNativeSaveHomeLayout", "(Ljava/lang/String;)V") : nullptr;
        if (env->ExceptionCheck()) env->ExceptionClear();
        jobject activity_global = (callback || sound_cb || save_cb) ? env->NewGlobalRef(activity) : nullptr;
        if (activity_class) env->DeleteLocalRef(activity_class);
        if (activity_global && vm) {
            const std::lock_guard lock_guard(status_mutex);
            if (preview_activity_ref) env->DeleteGlobalRef(preview_activity_ref);
            preview_java_vm = vm;
            preview_activity_ref = activity_global;
            livearea_target_callback = callback;
            request_livearea_callback = request_cb;
            exit_request_callback = exit_cb;
            app_row_callback = app_row_cb;
            app_back_callback = app_back_cb;
            ui_sound_callback = sound_cb;
            save_home_layout_callback = save_cb;
        } else if (activity_global) env->DeleteGlobalRef(activity_global);
    } catch (const std::exception& error) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), error.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeHomeLiveArea(JNIEnv* env, jobject,
    jstring title_id, jint background_id, jfloatArray background_rect, jint gate_id, jfloatArray gate_rect,
    jint gate_start_label_id, jint gate_control_id,
    jintArray frame_image_ids, jfloatArray frame_rects, jintArray text_image_ids, jfloatArray text_rects,
    jintArray text_colors, jobjectArray target_ids, jfloatArray target_rects) {
    if (!title_id || !background_rect || !gate_rect || !frame_image_ids || !frame_rects
        || !text_image_ids || !text_rects || !text_colors || !target_ids || !target_rects) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), "Missing LiveArea snapshot array");
        return;
    }
    auto read_string = [&](jobjectArray array, jsize index) {
        auto value = static_cast<jstring>(env->GetObjectArrayElement(array, index));
        if (!value) throw std::invalid_argument("Null LiveArea string");
        const char* chars = env->GetStringUTFChars(value, nullptr);
        if (!chars) { env->DeleteLocalRef(value); throw std::runtime_error("Cannot read LiveArea string"); }
        std::string result(chars);
        env->ReleaseStringUTFChars(value, chars);
        env->DeleteLocalRef(value);
        if (result.size() > 1024) throw std::invalid_argument("Oversized LiveArea string");
        return result;
    };
    auto read_rect = [&](jfloatArray values) {
        if (env->GetArrayLength(values) != 4) throw std::invalid_argument("LiveArea rect must contain four values");
        ScreenRect rect{};
        env->GetFloatArrayRegion(values, 0, 4, rect.data());
        if (env->ExceptionCheck()) throw std::runtime_error("Cannot read LiveArea rect");
        for (const auto value : rect)
            if (!std::isfinite(value)) throw std::invalid_argument("Non-finite LiveArea rect");
        if (rect[2] < 0.f || rect[3] < 0.f) throw std::invalid_argument("Negative LiveArea rect size");
        return rect;
    };
    try {
        LiveAreaSnapshot page{};
        const char* title_chars = env->GetStringUTFChars(title_id, nullptr);
        if (!title_chars) throw std::runtime_error("Cannot read LiveArea title ID");
        page.title_id = title_chars;
        env->ReleaseStringUTFChars(title_id, title_chars);
        if (page.title_id.empty() || page.title_id.size() > 256)
            throw std::invalid_argument("Invalid LiveArea title ID");
        page.background_id = static_cast<std::uint32_t>(background_id);
        page.gate_id = static_cast<std::uint32_t>(gate_id);
        page.gate_start_label_id = static_cast<std::uint32_t>(gate_start_label_id);
        page.gate_control_id = static_cast<std::uint32_t>(gate_control_id);
        page.background_rect = read_rect(background_rect);
        page.gate_rect = read_rect(gate_rect);

        const jsize frame_count = env->GetArrayLength(frame_image_ids);
        const jsize text_count = env->GetArrayLength(text_image_ids);
        const jsize target_count = env->GetArrayLength(target_ids);
        if (frame_count > 64 || text_count > 128 || target_count > 64
            || env->GetArrayLength(frame_rects) != frame_count * 4
            || env->GetArrayLength(text_rects) != text_count * 4
            || env->GetArrayLength(text_colors) != text_count
            || env->GetArrayLength(target_rects) != target_count * 4)
            throw std::invalid_argument("Invalid LiveArea layer counts");

        std::vector<jint> frame_ids(static_cast<std::size_t>(frame_count));
        std::vector<jint> text_ids(static_cast<std::size_t>(text_count));
        std::vector<jint> colors(static_cast<std::size_t>(text_count));
        std::vector<jfloat> frame_bounds(static_cast<std::size_t>(frame_count) * 4);
        std::vector<jfloat> text_bounds(static_cast<std::size_t>(text_count) * 4);
        std::vector<jfloat> target_bounds(static_cast<std::size_t>(target_count) * 4);
        if (frame_count) {
            env->GetIntArrayRegion(frame_image_ids, 0, frame_count, frame_ids.data());
            env->GetFloatArrayRegion(frame_rects, 0, frame_count * 4, frame_bounds.data());
        }
        if (text_count) {
            env->GetIntArrayRegion(text_image_ids, 0, text_count, text_ids.data());
            env->GetFloatArrayRegion(text_rects, 0, text_count * 4, text_bounds.data());
            env->GetIntArrayRegion(text_colors, 0, text_count, colors.data());
        }
        if (target_count) env->GetFloatArrayRegion(target_rects, 0, target_count * 4, target_bounds.data());
        if (env->ExceptionCheck()) throw std::runtime_error("Cannot read LiveArea arrays");

        auto rect_at = [](const std::vector<jfloat>& values, std::size_t index) {
            ScreenRect rect{values[index * 4], values[index * 4 + 1],
                values[index * 4 + 2], values[index * 4 + 3]};
            for (const auto value : rect)
                if (!std::isfinite(value)) throw std::invalid_argument("Non-finite LiveArea rect");
            if (rect[2] < 0.f || rect[3] < 0.f) throw std::invalid_argument("Negative LiveArea rect size");
            return rect;
        };
        for (jsize i = 0; i < frame_count; ++i)
            page.frames.push_back({static_cast<std::uint32_t>(frame_ids[static_cast<std::size_t>(i)]),
                rect_at(frame_bounds, static_cast<std::size_t>(i)), 0xffffffffu});
        for (jsize i = 0; i < text_count; ++i)
            page.texts.push_back({static_cast<std::uint32_t>(text_ids[static_cast<std::size_t>(i)]),
                rect_at(text_bounds, static_cast<std::size_t>(i)),
                static_cast<std::uint32_t>(colors[static_cast<std::size_t>(i)])});
        for (jsize i = 0; i < target_count; ++i) {
            auto target = read_string(target_ids, i);
            if (target.empty()) throw std::invalid_argument("Empty LiveArea target");
            page.targets.push_back({std::move(target), rect_at(target_bounds, static_cast<std::size_t>(i))});
        }
        const std::lock_guard lock_guard(status_mutex);
        livearea_catalog[page.title_id] = std::move(page);
        ++livearea_catalog_revision;
    } catch (const std::exception& error) {
        env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"), error.what());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeLoadHomeLayout(JNIEnv* env, jobject, jstring layout) {
    if (!layout) return;
    const char* text = env->GetStringUTFChars(layout, nullptr);
    if (!text) return;
    {
        const std::lock_guard lock_guard(status_mutex);
        persisted_home_layout = text;
        ++persisted_home_layout_revision;
    }
    env->ReleaseStringUTFChars(layout, text);
}

extern "C" JNIEXPORT void JNICALL
Java_com_sbro_emucorev_nativeui_shell_NativeShellActivity_nativeReleaseHomeCallback(JNIEnv* env, jobject) {
    const std::lock_guard lock_guard(status_mutex);
    if (preview_activity_ref) env->DeleteGlobalRef(preview_activity_ref);
    preview_activity_ref = nullptr;
    livearea_target_callback = nullptr;
    request_livearea_callback = nullptr;
    exit_request_callback = nullptr;
    app_row_callback = nullptr;
    app_back_callback = nullptr;
    ui_sound_callback = nullptr;
    save_home_layout_callback = nullptr;
    preview_java_vm = nullptr;
}
