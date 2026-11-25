#include <emucorev/paf/livearea_sheet.h>

#include <algorithm>
#include <cmath>

namespace emucorev::paf {
namespace {

constexpr float kPreviewWidth = 960.0f;
constexpr float kPreviewHeight = 544.0f;
constexpr float kPagerSnapRate = 11.0f;
constexpr float kPagerSnapThreshold = 0.0045f;
constexpr float kOpenTotalDurationSeconds = 0.66f;
constexpr float kPeelCloseDurationSeconds = 0.50f;
constexpr float kPeelExitZDurationSeconds = 0.30f;
constexpr float kPeelFadeStartSeconds = 0.24f;
constexpr float kPeelReleaseThreshold = 220.0f;
constexpr float kHalfPi = 1.5707963267948966f;
constexpr float kTwoPi = 6.28318530717958647692f;

float ease_out_quad(float t) noexcept {
    const float inv = 1.0f - std::clamp(t, 0.0f, 1.0f);
    return 1.0f - inv * inv;
}

float smoothstep(float t) noexcept {
    const float c = std::clamp(t, 0.0f, 1.0f);
    return c * c * (3.0f - 2.0f * c);
}

} // namespace

LiveAreaDeck::LiveAreaDeck() = default;

PagePoint LiveAreaDeck::sheet_fold_point(PagePoint centered_point) noexcept {
    const auto aspect = page_turn_normalized_aspect(kLiveAreaSheetSize);
    const PagePoint root_pos{
        (centered_point.x + kLiveAreaSheetSize.x * 0.5f) / kLiveAreaSheetSize.x,
        (centered_point.y + kLiveAreaSheetSize.y * 0.5f) / kLiveAreaSheetSize.y,
    };
    return {root_pos.x * aspect.x, root_pos.y * aspect.y};
}

int LiveAreaDeck::find_tab(const std::string& title_id) const noexcept {
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (tabs_[static_cast<std::size_t>(i)].title_id == title_id) return i;
    }
    return -1;
}

void LiveAreaDeck::reset_tab_rest(LiveAreaTab& tab) const noexcept {
    tab.layout = page_turn_layout(kLiveAreaSheetSize, kLiveAreaFoldFraction, kLiveAreaCornerAspect);
    tab.fold_point = sheet_fold_point(tab.layout.rest);
    tab.is_dragging = false;
    tab.peel_state = LiveAreaPeelState::rest;
    tab.peel_elapsed = 0.0f;
    tab.peel_duration = 0.0f;
    tab.sheet_alpha = 1.0f;
    tab.exit_z = 0.0f;
    tab.corner_flutter = 0.0f;
}

float LiveAreaDeck::tab_effective_page(int tab_index) const noexcept {
    float p = static_cast<float>(tab_index + 1);
    if (post_peel_gap_active_ && tab_index >= post_peel_gap_idx_) {
        p += 1.0f;
    }
    return p;
}

bool LiveAreaDeck::open_or_focus(const std::string& title_id, int catalog_index,
    float bubble_x, float bubble_y) {
    post_peel_gap_active_ = false;
    post_peel_gap_idx_ = -1;
    pager_dragging_ = false;
    pager_velocity_ = 0.0f;

    int idx = find_tab(title_id);
    const bool newly_opened = (idx < 0);
    const bool was_home = (active_tab_ < 0);

    if (newly_opened) {
        LiveAreaTab tab{};
        tab.title_id = title_id;
        tab.catalog_index = catalog_index;
        reset_tab_rest(tab);
        // PS Vita inserts the newly opened LiveArea immediately next to Home (page 1 = tabs_[0]).
        tabs_.insert(tabs_.begin(), std::move(tab));
        if (static_cast<int>(tabs_.size()) > kLiveAreaMaxOpenTabs) {
            tabs_.pop_back();
        }
        idx = 0;
    } else {
        auto& tab = tabs_[static_cast<std::size_t>(idx)];
        tab.catalog_index = catalog_index;
        if (tab.peel_state == LiveAreaPeelState::peeling_close) {
            reset_tab_rest(tab);
        }
    }

    active_tab_ = idx;
    pager_target_ = static_cast<float>(idx + 1);

    if (was_home && newly_opened) {
        pager_x_ = 1.0f;
        open_anim_active_ = true;
        open_anim_time_ = 0.0f;
        open_catalog_index_ = catalog_index;
        origin_x_ = bubble_x;
        origin_y_ = bubble_y;
    } else {
        open_anim_active_ = false;
    }
    return newly_opened;
}

void LiveAreaDeck::restore(const std::vector<std::pair<std::string, int>>& tabs, int active) {
    tabs_.clear();
    for (const auto& [title_id, catalog_index] : tabs) {
        if (title_id.empty() || find_tab(title_id) >= 0) continue;
        if (static_cast<int>(tabs_.size()) >= kLiveAreaMaxOpenTabs) break;
        LiveAreaTab tab{};
        tab.title_id = title_id;
        tab.catalog_index = catalog_index;
        reset_tab_rest(tab);
        tabs_.push_back(std::move(tab));
    }
    active_tab_ = std::clamp(active, -1, static_cast<int>(tabs_.size()) - 1);
    pager_x_ = pager_target_ = static_cast<float>(active_tab_ + 1);
    pager_velocity_ = 0.0f;
    pager_dragging_ = false;
    post_peel_gap_active_ = false;
    post_peel_gap_idx_ = -1;
    open_anim_active_ = false;
    open_anim_time_ = 0.0f;
    open_catalog_index_ = -1;
}

void LiveAreaDeck::remap_catalog(const std::function<int(const std::string&)>& lookup) {
    const std::string active_title = active_tab_ >= 0 && active_tab_ < static_cast<int>(tabs_.size())
        ? tabs_[static_cast<std::size_t>(active_tab_)].title_id : std::string();
    bool removed = false, reindexed = false;
    for (std::size_t i = 0; i < tabs_.size();) {
        const int index = lookup(tabs_[i].title_id);
        if (index < 0) {
            tabs_.erase(tabs_.begin() + static_cast<std::ptrdiff_t>(i));
            removed = true;
        } else {
            reindexed = reindexed || tabs_[i].catalog_index != index;
            tabs_[i].catalog_index = index;
            ++i;
        }
    }
    if (!removed && !reindexed) return;
    // The indices behind a running open transition may be stale: finish it instantly.
    open_anim_active_ = false;
    open_catalog_index_ = -1;
    if (!removed) return;
    active_tab_ = find_tab(active_title);
    pager_x_ = pager_target_ = static_cast<float>(active_tab_ + 1);
    pager_velocity_ = 0.0f;
    pager_dragging_ = false;
    post_peel_gap_active_ = false;
    post_peel_gap_idx_ = -1;
}

bool LiveAreaDeck::slide_to_home() {
    if (active_tab_ < 0 && pager_target_ <= 0.0f) return false;
    active_tab_ = -1;
    pager_target_ = 0.0f;
    pager_dragging_ = false;
    open_anim_active_ = false;
    return true;
}

bool LiveAreaDeck::slide_to_tab(int tab_index) {
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs_.size())) return false;
    post_peel_gap_active_ = false;
    post_peel_gap_idx_ = -1;
    active_tab_ = tab_index;
    pager_target_ = static_cast<float>(tab_index + 1);
    pager_dragging_ = false;
    open_anim_active_ = false;
    return true;
}

bool LiveAreaDeck::slide_left() {
    if (active_tab_ < 0) return false;
    if (active_tab_ == 0) return slide_to_home();
    return slide_to_tab(active_tab_ - 1);
}

bool LiveAreaDeck::slide_right() {
    if (tabs_.empty()) return false;
    const int next_tab = active_tab_ + 1;
    if (next_tab >= static_cast<int>(tabs_.size())) return false;
    return slide_to_tab(next_tab);
}

void LiveAreaDeck::begin_horizontal_drag(float screen_x) {
    post_peel_gap_active_ = false;
    post_peel_gap_idx_ = -1;
    open_anim_active_ = false;
    pager_dragging_ = true;
    pager_velocity_ = 0.0f;
    drag_last_x_ = screen_x;
    drag_start_pager_x_ = pager_x_;
}

void LiveAreaDeck::update_horizontal_drag(float screen_x, float dt_seconds) {
    if (!pager_dragging_) return;
    const float dx = screen_x - drag_last_x_;
    drag_last_x_ = screen_x;
    // Swiping left (dx < 0) advances pager_x_ rightward into tabs.
    const float max_page = static_cast<float>(tabs_.size());
    const float delta_page = -dx / kPreviewWidth;
    pager_x_ = std::clamp(pager_x_ + delta_page, 0.0f, max_page);
    if (dt_seconds > 1.0e-4f) {
        const float instant_vel = std::clamp(delta_page / dt_seconds, -12.0f, 12.0f);
        pager_velocity_ = pager_velocity_ * 0.45f + instant_vel * 0.55f;
    }
}

int LiveAreaDeck::end_horizontal_drag() {
    if (!pager_dragging_) return active_tab_ + 1;
    pager_dragging_ = false;
    const int max_page = static_cast<int>(tabs_.size());
    int target_page = static_cast<int>(std::lround(pager_x_));
    if (std::abs(pager_velocity_) > 1.35f) {
        const int base_page = static_cast<int>(std::lround(drag_start_pager_x_));
        target_page = base_page + (pager_velocity_ > 0.0f ? 1 : -1);
    }
    target_page = std::clamp(target_page, 0, max_page);
    pager_target_ = static_cast<float>(target_page);
    active_tab_ = target_page - 1;
    return target_page;
}

bool LiveAreaDeck::trigger_peel_close() {
    if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs_.size())) return false;
    auto& tab = tabs_[static_cast<std::size_t>(active_tab_)];
    if (tab.peel_state == LiveAreaPeelState::peeling_close) return false;
    open_anim_active_ = false;
    tab.is_dragging = false;
    tab.corner_flutter = 0.0f;
    tab.peel_state = LiveAreaPeelState::peeling_close;
    tab.peel_elapsed = 0.0f;
    tab.peel_duration = kPeelCloseDurationSeconds;
    tab.peel_start = tab.fold_point;
    tab.peel_target = sheet_fold_point({-2.0f * kLiveAreaSheetSize.y, -2.0f * kLiveAreaSheetSize.x});
    tab.sheet_alpha = 1.0f;
    tab.exit_z = 0.0f;
    return true;
}

bool LiveAreaDeck::begin_peel_drag(float screen_x, float screen_y) {
    if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs_.size())) return false;
    if (open_anim_active_ && open_anim_time_ < 0.44f) return false;
    auto& tab = tabs_[static_cast<std::size_t>(active_tab_)];
    if (tab.peel_state == LiveAreaPeelState::peeling_close) return false;
    const float sheet_x = tab_offset_x(active_tab_);
    if (std::abs(sheet_x) > 32.0f) return false;
    const PagePoint widget_point{
        screen_x - kPreviewWidth * 0.5f - kLiveAreaSheetPosition.x - sheet_x,
        kPreviewHeight * 0.5f - screen_y - kLiveAreaSheetPosition.y,
    };
    if (!page_turn_hit_test(kLiveAreaSheetSize, tab.layout, widget_point)) return false;
    tab.is_dragging = true;
    tab.peel_state = LiveAreaPeelState::rest;
    tab.drag_initial = widget_point;
    tab.drag_offset = {tab.layout.rest.x - widget_point.x, tab.layout.rest.y - widget_point.y};
    return true;
}

bool LiveAreaDeck::update_peel_drag(float screen_x, float screen_y) {
    if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs_.size())) return false;
    auto& tab = tabs_[static_cast<std::size_t>(active_tab_)];
    if (!tab.is_dragging) return false;
    const PagePoint widget_point{
        screen_x - kPreviewWidth * 0.5f - kLiveAreaSheetPosition.x,
        kPreviewHeight * 0.5f - screen_y - kLiveAreaSheetPosition.y,
    };
    const auto target = page_turn_drag_target(widget_point, tab.drag_initial, tab.drag_offset);
    tab.fold_point = sheet_fold_point(target);
    return true;
}

LiveAreaPeelEndResult LiveAreaDeck::end_peel_drag(bool canceled) {
    if (active_tab_ < 0 || active_tab_ >= static_cast<int>(tabs_.size()))
        return LiveAreaPeelEndResult::none;
    auto& tab = tabs_[static_cast<std::size_t>(active_tab_)];
    if (!tab.is_dragging) return LiveAreaPeelEndResult::none;
    tab.is_dragging = false;

    const auto rest_point = sheet_fold_point(tab.layout.rest);
    const float dx = (rest_point.x - tab.fold_point.x) * kLiveAreaSheetSize.y;
    const float dy = (rest_point.y - tab.fold_point.y) * kLiveAreaSheetSize.y;
    const auto release = canceled
        ? PageTurnRelease::within_threshold
        : page_turn_release({dx, dy}, kPeelReleaseThreshold, false, false);
    if (release == PageTurnRelease::beyond_threshold) {
        trigger_peel_close();
        return LiveAreaPeelEndResult::started_close;
    }

    const float dist = std::sqrt(dx * dx + dy * dy);
    const float duration_ms = ((((dist / kLiveAreaSheetSize.y - kLiveAreaFoldFraction)
        * 1.5f + 0.5f) / 1.5f) * 400.0f);
    tab.peel_state = LiveAreaPeelState::returning;
    tab.peel_elapsed = 0.0f;
    tab.peel_duration = std::max(duration_ms, 80.0f) * 0.001f;
    tab.peel_start = tab.fold_point;
    tab.peel_target = rest_point;
    return LiveAreaPeelEndResult::returned;
}

void LiveAreaDeck::step(float dt_seconds) {
    if (dt_seconds <= 0.0f || !std::isfinite(dt_seconds)) return;
    const float dt = std::min(dt_seconds, 0.1f);

    gate_focus_pulse_ += dt * 5.0265482f; // ~0.8 Hz electric-cyan gate pulse
    if (gate_focus_pulse_ > kTwoPi) gate_focus_pulse_ -= kTwoPi;

    if (open_anim_active_) {
        open_anim_time_ += dt;
        if (open_anim_time_ >= kOpenTotalDurationSeconds) {
            open_anim_time_ = kOpenTotalDurationSeconds;
            open_anim_active_ = false;
        }
    }

    const float prev_pager_x = pager_x_;
    if (!pager_dragging_) {
        const float diff = pager_target_ - pager_x_;
        if (std::abs(diff) <= kPagerSnapThreshold) {
            pager_x_ = pager_target_;
            pager_velocity_ = 0.0f;
            if (post_peel_gap_active_) {
                post_peel_gap_active_ = false;
                post_peel_gap_idx_ = -1;
            }
        } else {
            const float alpha = 1.0f - std::exp(-kPagerSnapRate * dt);
            pager_x_ += diff * alpha;
            pager_velocity_ = (pager_x_ - prev_pager_x) / dt;
        }
    }

    const float speed_norm = std::clamp(std::abs(pager_x_ - prev_pager_x) / (dt * 4.0f), 0.0f, 1.0f);

    for (int i = static_cast<int>(tabs_.size()) - 1; i >= 0; --i) {
        auto& tab = tabs_[static_cast<std::size_t>(i)];
        if (tab.peel_state == LiveAreaPeelState::rest) {
            const float target_flutter = tab.is_dragging ? 0.0f : (speed_norm * 0.055f);
            const float flutter_alpha = 1.0f - std::exp(-12.0f * dt);
            tab.corner_flutter += (target_flutter - tab.corner_flutter) * flutter_alpha;
            continue;
        }

        tab.corner_flutter = 0.0f;
        tab.peel_elapsed += dt;
        const float t = (tab.peel_duration > 0.0f)
            ? std::clamp(tab.peel_elapsed / tab.peel_duration, 0.0f, 1.0f) : 1.0f;
        if (tab.peel_state == LiveAreaPeelState::peeling_close) {
            const float exit_t = std::clamp(tab.peel_elapsed / kPeelExitZDurationSeconds, 0.0f, 1.0f);
            tab.exit_z = 200.0f * ease_out_quad(exit_t);
            const float fade_t = std::clamp(
                (tab.peel_elapsed - kPeelFadeStartSeconds) / (kPeelCloseDurationSeconds - kPeelFadeStartSeconds),
                0.0f, 1.0f);
            tab.sheet_alpha = 1.0f - ease_out_quad(fade_t);
        }
        const float factor = (tab.peel_state == LiveAreaPeelState::peeling_close)
            ? std::sin(t * kHalfPi)
            : ease_out_quad(t);
        tab.fold_point = {
            tab.peel_start.x + (tab.peel_target.x - tab.peel_start.x) * factor,
            tab.peel_start.y + (tab.peel_target.y - tab.peel_start.y) * factor,
        };
        if (t >= 1.0f) {
            if (tab.peel_state == LiveAreaPeelState::peeling_close) {
                const int closed_idx = i;
                tabs_.erase(tabs_.begin() + closed_idx);
                active_tab_ = closed_idx - 1;
                pager_target_ = static_cast<float>(active_tab_ + 1);
                pager_x_ = static_cast<float>(closed_idx + 1);
                if (closed_idx < static_cast<int>(tabs_.size())) {
                    post_peel_gap_idx_ = closed_idx;
                    post_peel_gap_active_ = true;
                } else {
                    post_peel_gap_idx_ = -1;
                    post_peel_gap_active_ = false;
                }
            } else {
                tab.peel_state = LiveAreaPeelState::rest;
                tab.sheet_alpha = 1.0f;
                tab.exit_z = 0.0f;
            }
        }
    }
}

bool LiveAreaDeck::is_livearea_visible() const noexcept {
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (is_tab_visible(i)) return true;
    }
    return false;
}

bool LiveAreaDeck::is_tab_visible(int tab_index) const noexcept {
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs_.size())) return false;
    // Sheet spans x=[60..900]; offscreen beyond |offset_x| >= 920px (Home rest is 960px).
    return std::abs(tab_offset_x(tab_index)) < 920.0f;
}

bool LiveAreaDeck::is_home_visible() const noexcept {
    if (open_anim_active_) return open_other_home_alpha() > 0.005f;
    return std::abs(home_offset_x()) <= (kPreviewWidth + 60.0f);
}

const LiveAreaTab* LiveAreaDeck::displayed_tab() const noexcept {
    if (tabs_.empty()) return nullptr;
    if (active_tab_ >= 0 && active_tab_ < static_cast<int>(tabs_.size()) && is_tab_visible(active_tab_)) {
        return &tabs_[static_cast<std::size_t>(active_tab_)];
    }
    int best = -1;
    float best_abs = kPreviewWidth + 60.0f;
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        const float d = std::abs(tab_offset_x(i));
        if (d < best_abs) {
            best_abs = d;
            best = i;
        }
    }
    return best >= 0 ? &tabs_[static_cast<std::size_t>(best)] : nullptr;
}

bool LiveAreaDeck::is_dragging_peel() const noexcept {
    const auto* tab = displayed_tab();
    return tab != nullptr && tab->is_dragging;
}

float LiveAreaDeck::home_offset_x() const noexcept {
    if (open_anim_active_) return open_other_home_offset_x();
    return -pager_x_ * kPreviewWidth;
}

float LiveAreaDeck::tab_offset_x(int tab_index) const noexcept {
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs_.size())) return kPreviewWidth * 2.0f;
    return (tab_effective_page(tab_index) - pager_x_) * kPreviewWidth;
}

float LiveAreaDeck::tab_yaw_rad(int tab_index) const noexcept {
    return std::clamp(-tab_offset_x(tab_index) * 0.00038f, -0.22f, 0.22f);
}

float LiveAreaDeck::sheet_offset_x() const noexcept {
    if (active_tab_ >= 0 && active_tab_ < static_cast<int>(tabs_.size())) {
        return tab_offset_x(active_tab_);
    }
    if (!tabs_.empty()) return tab_offset_x(0);
    return kPreviewWidth;
}

PagePoint LiveAreaDeck::effective_fold_point(int tab_index) const noexcept {
    if (tab_index < 0 || tab_index >= static_cast<int>(tabs_.size())) return {};
    const auto& tab = tabs_[static_cast<std::size_t>(tab_index)];
    return {
        tab.fold_point.x - tab.corner_flutter * 1.15f,
        tab.fold_point.y - tab.corner_flutter,
    };
}

bool LiveAreaDeck::is_open_bubble_visible() const noexcept {
    return open_anim_active_ && open_anim_time_ < 0.43f;
}

PagePoint LiveAreaDeck::open_bubble_pos() const noexcept {
    if (!open_anim_active_) return {480.0f, 294.0f};
    const float u = ease_out_quad(std::clamp(open_anim_time_ / 0.42f, 0.0f, 1.0f));
    // Swelling bubble drifts inward/downward smoothly over the LiveArea gate region.
    const float target_x = origin_x_ * 0.45f + 480.0f * 0.55f;
    const float target_y = origin_y_ * 0.20f + 235.0f * 0.80f;
    return {
        origin_x_ + (target_x - origin_x_) * u,
        origin_y_ + (target_y - origin_y_) * u,
    };
}

float LiveAreaDeck::open_bubble_scale() const noexcept {
    if (!open_anim_active_) return 1.0f;
    const float u = std::clamp(open_anim_time_ / 0.42f, 0.0f, 1.0f);
    return 1.0f + 2.15f * std::pow(u, 0.85f);
}

float LiveAreaDeck::open_bubble_spin_y_rad() const noexcept {
    if (!open_anim_active_) return 0.0f;
    const float u = std::clamp(open_anim_time_ / 0.42f, 0.0f, 1.0f);
    // ~387 deg spin across 0..420ms: mirrors Vita's metallic silver backside (~170ms) -> front dissolve.
    return u * 6.75f;
}

float LiveAreaDeck::open_bubble_alpha() const noexcept {
    if (!open_anim_active_) return 0.0f;
    if (open_anim_time_ <= 0.19f) return 1.0f;
    const float u = std::clamp((open_anim_time_ - 0.19f) / (0.42f - 0.19f), 0.0f, 1.0f);
    return 1.0f - smoothstep(u);
}

float LiveAreaDeck::open_other_home_alpha() const noexcept {
    if (!open_anim_active_) return 0.0f;
    const float u = std::clamp(open_anim_time_ / 0.07f, 0.0f, 1.0f);
    return 1.0f - ease_out_quad(u);
}

float LiveAreaDeck::open_other_home_offset_x() const noexcept {
    if (!open_anim_active_) return 0.0f;
    const float u = std::clamp(open_anim_time_ / 0.07f, 0.0f, 1.0f);
    return -220.0f * ease_out_quad(u);
}

bool LiveAreaDeck::is_opening_sheet_visible() const noexcept {
    return !open_anim_active_ || open_anim_time_ >= 0.195f;
}

float LiveAreaDeck::sheet_open_scale() const noexcept {
    if (!open_anim_active_) return 1.0f;
    if (open_anim_time_ <= 0.195f) return 0.08f;
    if (open_anim_time_ <= 0.440f) {
        const float u = std::clamp((open_anim_time_ - 0.195f) / (0.440f - 0.195f), 0.0f, 1.0f);
        return 0.08f + (1.076f - 0.08f) * ease_out_quad(u);
    }
    const float u = std::clamp((open_anim_time_ - 0.440f) / (kOpenTotalDurationSeconds - 0.440f), 0.0f, 1.0f);
    return 1.076f - 0.076f * smoothstep(u);
}

float LiveAreaDeck::sheet_gate_alpha() const noexcept {
    if (!open_anim_active_) return 1.0f;
    if (open_anim_time_ <= 0.28f) return 0.0f;
    const float u = std::clamp((open_anim_time_ - 0.28f) / (0.42f - 0.28f), 0.0f, 1.0f);
    return smoothstep(u);
}

float LiveAreaDeck::sheet_gate_glow_intro() const noexcept {
    if (!open_anim_active_) return 1.0f;
    if (open_anim_time_ <= 0.47f) return 0.0f;
    const float u = std::clamp((open_anim_time_ - 0.47f) / (0.62f - 0.47f), 0.0f, 1.0f);
    return smoothstep(u);
}

float LiveAreaDeck::sheet_frames_alpha() const noexcept {
    if (!open_anim_active_) return 1.0f;
    if (open_anim_time_ <= 0.48f) return 0.0f;
    const float u = std::clamp((open_anim_time_ - 0.48f) / (0.64f - 0.48f), 0.0f, 1.0f);
    return smoothstep(u);
}

} // namespace emucorev::paf
