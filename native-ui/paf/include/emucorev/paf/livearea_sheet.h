#pragma once

#include <emucorev/paf/page_turn.h>

#include <string>
#include <vector>

namespace emucorev::paf {

inline constexpr PagePoint kLiveAreaSheetSize{840.0f, 500.0f};
inline constexpr PagePoint kLiveAreaSheetPosition{0.0f, -22.0f}; // centered at screen (480, 294)
inline constexpr float kLiveAreaSheetLeft = 60.0f;
inline constexpr float kLiveAreaSheetTop = 44.0f;
// Exact PageTurn constructor defaults from paf.elf (0x3e19999a, 0x3f4ccccd).
inline constexpr float kLiveAreaFoldFraction = 0.15f;
inline constexpr float kLiveAreaCornerAspect = 0.80f;
inline constexpr int kLiveAreaMaxOpenTabs = 6;

enum class LiveAreaPeelState {
    rest,
    returning,
    peeling_close,
};

enum class LiveAreaPeelEndResult {
    none,
    returned,
    started_close,
};

struct LiveAreaTab {
    std::string title_id;
    int catalog_index = -1;
    PageTurnLayout layout{};
    PagePoint fold_point{};
    PagePoint drag_initial{};
    PagePoint drag_offset{};
    bool is_dragging = false;
    LiveAreaPeelState peel_state = LiveAreaPeelState::rest;
    float peel_elapsed = 0.0f;
    float peel_duration = 0.0f;
    PagePoint peel_start{};
    PagePoint peel_target{};
    float sheet_alpha = 1.0f;
    float exit_z = 0.0f;
    float corner_flutter = 0.0f;
};

// Manages PS Vita LiveArea multitasking sheets: firmware 1:1 bubble-tap spin/swell
// opening transition, continuous horizontal pager (page 0 = Home, page 1..N = open tabs
// newest-first), 3D perspective yaw + PageTurn corner wind flutter, and peel-to-close.
class LiveAreaDeck {
public:
    LiveAreaDeck();

    // Opens a title from Home (bubble spin + LiveArea zoom-in) or glides to an existing tab.
    // Returns true if a new tab was opened (caller may emitdecide sound).
    bool open_or_focus(const std::string& title_id, int catalog_index,
        float bubble_x, float bubble_y);

    // Glides horizontally back to Home while keeping all open tabs in the top bar.
    bool slide_to_home();

    // Glides horizontally to an already-open tab index (0..tab_count()-1).
    bool slide_to_tab(int tab_index);

    // Navigates one page left (-1) or right (+1) in [Home, tab0, ..., tabN-1].
    bool slide_left();
    bool slide_right();

    // Horizontal swipe gesture across Home and open LiveArea sheets.
    void begin_horizontal_drag(float screen_x);
    void update_horizontal_drag(float screen_x, float dt_seconds);
    int end_horizontal_drag();

    // Starts peel-to-close animation on the currently active LiveArea tab.
    bool trigger_peel_close();

    // Touch peel interaction on the active LiveArea sheet (logical 960x544 coordinates).
    bool begin_peel_drag(float screen_x, float screen_y);
    bool update_peel_drag(float screen_x, float screen_y);
    LiveAreaPeelEndResult end_peel_drag(bool canceled);

    // Advances open transition, horizontal pager, corner flutter, peel-to-close, and gate pulse.
    void step(float dt_seconds);

    [[nodiscard]] const std::vector<LiveAreaTab>& tabs() const noexcept { return tabs_; }
    [[nodiscard]] int tab_count() const noexcept { return static_cast<int>(tabs_.size()); }
    [[nodiscard]] int active_tab_index() const noexcept { return active_tab_; }
    [[nodiscard]] bool is_home_active() const noexcept { return active_tab_ < 0; }
    [[nodiscard]] bool is_livearea_visible() const noexcept;
    [[nodiscard]] bool is_tab_visible(int tab_index) const noexcept;
    [[nodiscard]] bool is_home_visible() const noexcept;
    [[nodiscard]] const LiveAreaTab* displayed_tab() const noexcept;
    [[nodiscard]] bool is_dragging_peel() const noexcept;
    [[nodiscard]] bool is_dragging_horizontal() const noexcept { return pager_dragging_; }

    // Continuous pager position in page units (0.0 = Home, 1.0 = tabs[0], ...).
    [[nodiscard]] float pager_position() const noexcept { return pager_x_; }
    // Horizontal translation of Home layer in 960px units (0 at Home, -960 at tab 0).
    [[nodiscard]] float home_offset_x() const noexcept;
    // Horizontal translation of tab i in 960px units (0 when centered, +960 to right).
    [[nodiscard]] float tab_offset_x(int tab_index) const noexcept;
    // Subtle 3D Y-axis tilt (radians) of tab i during horizontal pager glides.
    [[nodiscard]] float tab_yaw_rad(int tab_index) const noexcept;
    // Legacy convenience offset of active/displayed tab in 960px units.
    [[nodiscard]] float sheet_offset_x() const noexcept;
    // Effective fold point including swipe-induced corner wind flutter.
    [[nodiscard]] PagePoint effective_fold_point(int tab_index) const noexcept;

    // 1:1 Bubble-tap -> LiveArea open transition getters (0..660ms).
    [[nodiscard]] bool is_open_anim_active() const noexcept { return open_anim_active_; }
    [[nodiscard]] int open_bubble_catalog_index() const noexcept { return open_catalog_index_; }
    [[nodiscard]] bool is_open_bubble_visible() const noexcept;
    [[nodiscard]] PagePoint open_bubble_pos() const noexcept;
    [[nodiscard]] float open_bubble_scale() const noexcept;
    [[nodiscard]] float open_bubble_spin_y_rad() const noexcept;
    [[nodiscard]] float open_bubble_alpha() const noexcept;
    [[nodiscard]] float open_other_home_alpha() const noexcept;
    [[nodiscard]] float open_other_home_offset_x() const noexcept;
    [[nodiscard]] bool is_opening_sheet_visible() const noexcept;
    [[nodiscard]] float sheet_open_scale() const noexcept;
    [[nodiscard]] float sheet_gate_alpha() const noexcept;
    [[nodiscard]] float sheet_gate_glow_intro() const noexcept;
    [[nodiscard]] float sheet_frames_alpha() const noexcept;
    [[nodiscard]] float gate_focus_pulse() const noexcept { return gate_focus_pulse_; }

    // Converts a PageTurn widget local point to normalized fold space for 840x500.
    static PagePoint sheet_fold_point(PagePoint centered_point) noexcept;

private:
    int find_tab(const std::string& title_id) const noexcept;
    void reset_tab_rest(LiveAreaTab& tab) const noexcept;
    float tab_effective_page(int tab_index) const noexcept;

    std::vector<LiveAreaTab> tabs_;
    int active_tab_ = -1; // -1 = Home, 0..N-1 = open tab

    // Continuous horizontal pager (0.0 = Home, 1.0 = tabs_[0], ...).
    float pager_x_ = 0.0f;
    float pager_target_ = 0.0f;
    float pager_velocity_ = 0.0f;
    bool pager_dragging_ = false;
    float drag_last_x_ = 0.0f;
    float drag_start_pager_x_ = 0.0f;

    // Post-peel layout gap prevents behind-tabs from snapping onto screen while sliding left.
    int post_peel_gap_idx_ = -1;
    bool post_peel_gap_active_ = false;

    // 1:1 Bubble-tap -> LiveArea open transition state (660ms).
    bool open_anim_active_ = false;
    float open_anim_time_ = 0.0f;
    int open_catalog_index_ = -1;
    float origin_x_ = 480.0f;
    float origin_y_ = 294.0f;

    float gate_focus_pulse_ = 0.0f;
};

} // namespace emucorev::paf
