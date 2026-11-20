#include <emucorev/paf/home_layout.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void require_near(float actual, float expected, float eps, const char* message) {
    if (std::abs(actual - expected) > eps) {
        std::cerr << message << " actual=" << actual << " expected=" << expected << '\n';
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    using namespace emucorev::paf;
    try {
        // Exact shell.elf 10-slot Home coordinates (0x810d4190).
        const auto p0 = home_slot_screen_position(0, 0, 0.0f);
        require_near(p0.x, 251.0f, 1.0e-4f, "Home slot 0 X");
        require_near(p0.y, 130.0f, 1.0e-4f, "Home slot 0 Y");

        const auto p1 = home_slot_screen_position(0, 1, 0.0f);
        require_near(p1.x, 480.0f, 1.0e-4f, "Home slot 1 X");
        require_near(p1.y, 130.0f, 1.0e-4f, "Home slot 1 Y");

        const auto p3 = home_slot_screen_position(0, 3, 0.0f);
        require_near(p3.x, 144.0f, 1.0e-4f, "Home slot 3 X");
        require_near(p3.y, 284.0f, 1.0e-4f, "Home slot 3 Y");

        const auto p6 = home_slot_screen_position(0, 6, 0.0f);
        require_near(p6.x, 816.0f, 1.0e-4f, "Home slot 6 X");
        require_near(p6.y, 284.0f, 1.0e-4f, "Home slot 6 Y");

        const auto p7 = home_slot_screen_position(0, 7, 0.0f);
        require_near(p7.x, 251.0f, 1.0e-4f, "Home slot 7 X");
        require_near(p7.y, 438.0f, 1.0e-4f, "Home slot 7 Y");

        // Second page sits 512 px below page 0 when scroll_y == 0, and aligns when scroll_y == 512.
        const auto p1_s0_unscrolled = home_slot_screen_position(1, 0, 0.0f);
        require_near(p1_s0_unscrolled.y, 130.0f + 512.0f, 1.0e-4f, "Page 1 slot 0 unscrolled Y");
        const auto p1_s0_scrolled = home_slot_screen_position(1, 0, 512.0f);
        require_near(p1_s0_scrolled.x, 251.0f, 1.0e-4f, "Page 1 slot 0 scrolled X");
        require_near(p1_s0_scrolled.y, 130.0f, 1.0e-4f, "Page 1 slot 0 scrolled Y");

        // Folder offsets (0x810bb69a).
        const auto f4 = home_slot_screen_position(0, 4, 0.0f, HomeGridLayout::folder);
        require_near(f4.x, 380.0f, 1.0e-4f, "Folder slot 4 X");
        require_near(f4.y, 304.0f, 1.0e-4f, "Folder slot 4 Y");

        // DPAD 4-way navigation across rows, sparse last page, and vertical page boundaries.
        require(home_dpad_neighbor(0, 0, 14, 2, HomeDpadDirection::right) == std::pair{0, 1},
            "Right within row 0");
        require(home_dpad_neighbor(0, 0, 14, 2, HomeDpadDirection::down) == std::pair{0, 3},
            "Down from row 0 slot 0 hits nearest slot 3");
        require(home_dpad_neighbor(0, 7, 14, 2, HomeDpadDirection::down) == std::pair{1, 0},
            "Down from row 2 crosses to page 1 row 0");
        require(home_dpad_neighbor(1, 0, 14, 2, HomeDpadDirection::up) == std::pair{0, 7},
            "Up from page 1 row 0 returns to page 0 row 2");
        require(home_dpad_neighbor(1, 1, 14, 2, HomeDpadDirection::down) == std::pair{1, 3},
            "Down on sparse 4-item page lands on row 1 slot 3 without stepping out of range");

        // Per-bubble ambient sway + 60Hz spring impulse damping.
        auto float_state = make_bubble_float_state(0, 4, 0.0f);
        const auto o0 = bubble_float_offset(float_state);
        for (int i = 0; i < 60; ++i) {
            step_bubble_float(float_state, 1.0f / 60.0f, 0.0f, false);
        }
        const auto o1 = bubble_float_offset(float_state);
        require(std::abs(o1.x - o0.x) > 0.5f, "Bubble should sway horizontally over 1s at rest");

        step_bubble_float(float_state, 1.0f / 60.0f, 20.0f, false);
        require(std::abs(float_state.vel_y) > 0.1f, "Scroll motion should inject vertical bubble spring velocity");
        for (int i = 0; i < 180; ++i) {
            step_bubble_float(float_state, 1.0f / 60.0f, 20.0f, false);
        }
        require(std::abs(float_state.spring_y) < 0.25f, "Bubble spring offset must settle smoothly back to rest");

        // HomeVerticalScroll drag flick & rubber-band snap.
        HomeVerticalScroll scroll;
        scroll.set_page_count(3);
        scroll.begin_drag();
        scroll.drag_by(-160.0f, 0.12f); // Upward swipe (-1333 px/s finger -> +1333 px/s scroll)
        require(scroll.scroll_y() > 100.0f, "Upward swipe increases vertical scroll");
        const int target_page = scroll.end_drag();
        require(target_page == 1, "Upward flick should target page 1");
        for (int i = 0; i < 60; ++i) {
            scroll.step(1.0f / 60.0f);
        }
        require_near(scroll.scroll_y(), 512.0f, 0.25f, "Scroll must settle at 512 px for page 1");

        // Overscroll above page 0 should apply rubber-band elasticity and snap back to 0.
        scroll.jump_to_page(0);
        scroll.begin_drag();
        scroll.drag_by(200.0f, 0.5f);
        require(scroll.scroll_y() < 0.0f && scroll.scroll_y() > -120.0f,
            "Overscroll past top applies elastic resistance");
        require(scroll.end_drag() == 0, "Releasing top overscroll clamps to page 0");

        std::cout << "PASS home_layout_test\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
