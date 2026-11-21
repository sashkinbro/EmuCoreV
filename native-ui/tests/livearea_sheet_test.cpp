#include <emucorev/paf/livearea_sheet.h>

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) throw std::runtime_error(msg);
}
} // namespace

int main() {
    using namespace emucorev::paf;
    try {
        LiveAreaDeck deck;
        require(deck.is_home_active() && !deck.is_livearea_visible(), "Starts on Home");
        require(std::abs(kLiveAreaFoldFraction - 0.15f) < 1e-6f
            && std::abs(kLiveAreaCornerAspect - 0.80f) < 1e-6f,
            "Exact paf.elf PageTurn corner defaults");

        require(deck.open_or_focus("PCSE00089", 0, 251.0f, 130.0f), "Opens new LiveArea tab");
        require(deck.tab_count() == 1 && deck.is_livearea_visible(), "Tab active and visible");
        require(deck.is_open_anim_active() && deck.is_open_bubble_visible(), "Bubble spin starts active");
        require(deck.sheet_open_scale() < 0.2f && !deck.is_opening_sheet_visible(),
            "LiveArea sheet hidden until bubble shows metallic backside");

        // Step ~180ms: bubble flipped to metallic silver backside, still opaque.
        for (int i = 0; i < 11; ++i) deck.step(1.0f / 60.0f);
        require(deck.open_bubble_spin_y_rad() > 2.5f && deck.open_bubble_alpha() > 0.9f,
            "Mid-spin bubble shows metallic backside");

        // Step to ~450ms: LiveArea sheet reaches 1.076 overshoot while bubble finishes dissolving.
        for (int i = 0; i < 16; ++i) deck.step(1.0f / 60.0f);
        require(deck.is_opening_sheet_visible() && deck.sheet_open_scale() > 1.03f,
            "LiveArea sheet exhibits firmware overshoot before settling");

        // Finish open animation (660ms total).
        for (int i = 0; i < 20; ++i) deck.step(1.0f / 60.0f);
        require(!deck.is_open_anim_active(), "Open animation finishes cleanly");
        require(std::abs(deck.sheet_open_scale() - 1.0f) < 1e-4f, "Zoom settles at 1.0");
        require(std::abs(deck.tab_offset_x(0)) < 1e-4f, "Sheet centered at x=0");

        require(deck.slide_to_home(), "Slide back to Home");
        require(deck.is_home_active() && deck.tab_count() == 1, "Tab remains minimized in deck");
        for (int i = 0; i < 40; ++i) deck.step(1.0f / 60.0f);
        require(!deck.is_livearea_visible(), "Sheet slid offscreen after settle");
        require(std::abs(deck.home_offset_x()) < 1e-4f, "Home centered after slide");

        // Open a second app: inserted at index 0 right next to Home, pushing previous tab to index 1.
        require(deck.open_or_focus("PCSE00291", 1, 480.0f, 130.0f), "Opens second LiveArea tab");
        require(deck.tab_count() == 2 && deck.tabs()[0].title_id == "PCSE00291"
            && deck.tabs()[1].title_id == "PCSE00089", "Newest tab inserted at index 0");
        for (int i = 0; i < 45; ++i) deck.step(1.0f / 60.0f);
        require(deck.slide_right() && deck.active_tab_index() == 1, "Slide right to older tab");
        for (int i = 0; i < 40; ++i) deck.step(1.0f / 60.0f);
        require(std::abs(deck.tab_offset_x(1)) < 1e-4f, "Older tab centered after slide_right");

        require(deck.slide_to_tab(0), "Slide back to newest tab");
        require(deck.is_livearea_visible() && deck.tab_offset_x(0) < -900.0f, "Tab 0 starts off left");
        for (int i = 0; i < 40; ++i) deck.step(1.0f / 60.0f);
        require(std::abs(deck.tab_offset_x(0)) < 1e-4f, "Tab 0 glided back to center");

        require(deck.begin_peel_drag(875.0f, 70.0f), "Top-right corner hit-test succeeds");
        require(deck.update_peel_drag(835.0f, 110.0f), "Drag updates fold point");
        require(deck.end_peel_drag(false) == LiveAreaPeelEndResult::returned, "Short drag returns");
        for (int i = 0; i < 40; ++i) deck.step(1.0f / 60.0f);
        require(deck.displayed_tab()->peel_state == LiveAreaPeelState::rest, "Returned to rest");

        require(deck.begin_peel_drag(875.0f, 70.0f), "Corner hit-test for full peel");
        require(deck.update_peel_drag(420.0f, 380.0f), "Long diagonal drag");
        require(deck.end_peel_drag(false) == LiveAreaPeelEndResult::started_close, "Long drag starts peel close");
        // Peel completes at 0.50s (30 frames), leaving gap so tab 1 doesn't pop while sliding to Home!
        for (int i = 0; i < 31; ++i) deck.step(1.0f / 60.0f);
        require(deck.tab_count() == 1 && deck.is_home_active(), "Peel removes tab and sets Home active");
        require(deck.tab_offset_x(0) >= 900.0f, "Remaining tab stays offscreen right during Home return slide");
        for (int i = 0; i < 40; ++i) deck.step(1.0f / 60.0f);
        require(std::abs(deck.home_offset_x()) < 1e-4f, "Home glided cleanly to center");
        require(std::abs(deck.tab_offset_x(0) - 960.0f) < 1e-4f, "Remaining tab settles at +960");

        std::cout << "PASS livearea_sheet_test\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
