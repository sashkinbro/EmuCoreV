#pragma once

#include <array>
#include <cstdint>
#include <utility>

namespace emucorev::paf {

inline constexpr float kHomeLogicalWidth = 960.0f;
inline constexpr float kHomeLogicalHeight = 544.0f;
inline constexpr float kHomeStatusBarHeight = 32.0f;
inline constexpr float kHomeScrollCenterX = 480.0f;
inline constexpr float kHomeScrollCenterY = 288.0f;
inline constexpr float kHomePageHeight = 512.0f;
inline constexpr float kHomeBubbleSize = 138.0f;
inline constexpr float kHomeFocusRingSize = 207.0f; // 138 + 69 (shell.elf 0x810d4134)
inline constexpr int kHomeSlotsPerPage = 10;

struct HomeSlotOffset {
    float dx; // +right from scroll container center
    float dy; // +up in PAF coordinate space (subtract from center Y for screen Y)
};

enum class HomeGridLayout {
    home,
    folder,
};

enum class HomeDpadDirection {
    left,
    right,
    up,
    down,
};

struct HomeScreenPoint {
    float x;
    float y;
};

// Reconstructed 10-slot 3-4-3 bubble tables from shell.elf (0x810d4190 / 0x810bb6f8).
const std::array<HomeSlotOffset, kHomeSlotsPerPage>& home_slot_offsets(HomeGridLayout layout) noexcept;

HomeScreenPoint home_slot_screen_position(int page, int slot, float scroll_y,
    HomeGridLayout layout = HomeGridLayout::home) noexcept;

// Computes the nearest occupied (page, slot) target for 4-way DPAD navigation across
// the 3-4-3 honeycomb and vertical pages.
std::pair<int, int> home_dpad_neighbor(int page, int slot, int item_count, int page_count,
    HomeDpadDirection direction, HomeGridLayout layout = HomeGridLayout::home) noexcept;

// Per-bubble 60Hz spring + ambient horizontal/vertical float state matching shell.elf FUN_810ccdfe.
struct HomeBubbleFloatState {
    float sway_phase_x = 0.0f;
    float sway_phase_y = 0.0f;
    float sway_freq_x = 0.21f;
    float sway_freq_y = 0.17f;
    float sway_amp_x = 6.5f;
    float sway_amp_y = 2.2f;
    float spring_x = 0.0f;
    float spring_y = 0.0f;
    float vel_x = 0.0f;
    float vel_y = 0.0f;
    float prev_scroll_y = 0.0f;
    float press_scale = 1.0f;
    std::uint32_t seed = 0;
};

HomeBubbleFloatState make_bubble_float_state(int page, int slot, float initial_scroll_y = 0.0f) noexcept;

void step_bubble_float(HomeBubbleFloatState& state, float dt_seconds, float scroll_y, bool is_pressed) noexcept;

HomeScreenPoint bubble_float_offset(const HomeBubbleFloatState& state) noexcept;

// Vertical multi-page ScrollView model for PS Vita LiveArea Home (page height = 512 px, snap power = 25).
class HomeVerticalScroll {
public:
    static constexpr float kSnapPower = 25.0f;
    static constexpr float kOverscrollElasticity = 0.35f;
    static constexpr float kFlickVelocityThreshold = 180.0f;

    void set_page_count(int page_count) noexcept;
    [[nodiscard]] int page_count() const noexcept { return page_count_; }
    [[nodiscard]] int current_page() const noexcept { return current_page_; }
    [[nodiscard]] float scroll_y() const noexcept { return scroll_y_; }
    [[nodiscard]] float velocity_y() const noexcept { return velocity_y_; }
    [[nodiscard]] bool is_dragging() const noexcept { return dragging_; }
    [[nodiscard]] bool is_settling() const noexcept;

    void begin_drag() noexcept;
    void drag_by(float finger_delta_y, float dt_seconds) noexcept;
    // Ends pointer drag; returns the newly targeted page index.
    int end_drag() noexcept;
    void snap_to_page(int page) noexcept;
    void jump_to_page(int page) noexcept;
    void step(float dt_seconds) noexcept;

private:
    float min_scroll_y() const noexcept { return 0.0f; }
    float max_scroll_y() const noexcept;

    int page_count_ = 1;
    int current_page_ = 0;
    float scroll_y_ = 0.0f;
    float target_y_ = 0.0f;
    float velocity_y_ = 0.0f;
    bool dragging_ = false;
};

} // namespace emucorev::paf
