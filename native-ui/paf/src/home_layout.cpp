#include <emucorev/paf/home_layout.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace emucorev::paf {

namespace {

// Exact 10-slot Home Page layout offsets around ScrollView center (480, 288)
// reconstructed from shell.elf FUN_810d3b90 / DAT_810d4190.
constexpr std::array<HomeSlotOffset, kHomeSlotsPerPage> kHomeOffsets = {{
    {-229.0f,  158.0f}, // Row 0 (3): left
    {   0.0f,  158.0f}, // Row 0 (3): center
    { 229.0f,  158.0f}, // Row 0 (3): right
    {-336.0f,    4.0f}, // Row 1 (4): far-left
    {-112.0f,    4.0f}, // Row 1 (4): mid-left
    { 112.0f,    4.0f}, // Row 1 (4): mid-right
    { 336.0f,    4.0f}, // Row 1 (4): far-right
    {-229.0f, -150.0f}, // Row 2 (3): left
    {   0.0f, -150.0f}, // Row 2 (3): center
    { 229.0f, -150.0f}, // Row 2 (3): right
}};

// Exact 10-slot Folder overlay offsets from shell.elf 0x810bb69a / 0x810bb6f8.
constexpr std::array<HomeSlotOffset, kHomeSlotsPerPage> kFolderOffsets = {{
    {-200.0f,  124.0f},
    {   0.0f,  124.0f},
    { 200.0f,  124.0f},
    {-300.0f,  -16.0f},
    {-100.0f,  -16.0f},
    { 100.0f,  -16.0f},
    { 300.0f,  -16.0f},
    {-200.0f, -156.0f},
    {   0.0f, -156.0f},
    { 200.0f, -156.0f},
}};

constexpr float kSixtyHzStep = 1.0f / 60.0f;
constexpr float kTwoPi = 6.28318530717958647692f;

int slot_row(int slot) noexcept {
    if (slot < 3) return 0;
    if (slot < 7) return 1;
    return 2;
}

std::pair<int, int> row_bounds(int row) noexcept {
    if (row == 0) return {0, 3};
    if (row == 1) return {3, 7};
    return {7, 10};
}

std::uint32_t hash_slot(int page, int slot) noexcept {
    std::uint32_t h = static_cast<std::uint32_t>(page * 31 + slot * 17 + 0x9e3779b9u);
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

float unit_hash(std::uint32_t h) noexcept {
    return static_cast<float>(h & 0xFFFFu) / 65535.0f;
}

int pick_closest_occupied_in_range(int page, int begin_slot, int end_slot, float target_dx,
    int item_count, HomeGridLayout layout) noexcept {
    const auto& offsets = home_slot_offsets(layout);
    const int page_base = page * kHomeSlotsPerPage;
    int best_slot = -1;
    float best_dist = std::numeric_limits<float>::max();
    for (int s = begin_slot; s < end_slot; ++s) {
        if (page_base + s >= item_count) continue;
        const float d = std::abs(offsets[static_cast<std::size_t>(s)].dx - target_dx);
        if (d < best_dist) {
            best_dist = d;
            best_slot = s;
        }
    }
    return best_slot;
}

int pick_closest_occupied_on_page(int page, float target_dx, float target_dy, int item_count,
    HomeGridLayout layout) noexcept {
    const auto& offsets = home_slot_offsets(layout);
    const int page_base = page * kHomeSlotsPerPage;
    int best_slot = -1;
    float best_dist_sq = std::numeric_limits<float>::max();
    for (int s = 0; s < kHomeSlotsPerPage; ++s) {
        if (page_base + s >= item_count) continue;
        const auto& o = offsets[static_cast<std::size_t>(s)];
        const float dx = o.dx - target_dx;
        const float dy = o.dy - target_dy;
        const float d2 = dx * dx + dy * dy;
        if (d2 < best_dist_sq) {
            best_dist_sq = d2;
            best_slot = s;
        }
    }
    return best_slot;
}

} // namespace

const std::array<HomeSlotOffset, kHomeSlotsPerPage>& home_slot_offsets(HomeGridLayout layout) noexcept {
    return layout == HomeGridLayout::folder ? kFolderOffsets : kHomeOffsets;
}

HomeScreenPoint home_slot_screen_position(int page, int slot, float scroll_y,
    HomeGridLayout layout) noexcept {
    const int clamped_slot = std::clamp(slot, 0, kHomeSlotsPerPage - 1);
    const auto& offset = home_slot_offsets(layout)[static_cast<std::size_t>(clamped_slot)];
    const float page_offset = static_cast<float>(page) * kHomePageHeight - scroll_y;
    return {
        kHomeScrollCenterX + offset.dx,
        kHomeScrollCenterY - offset.dy + page_offset,
    };
}

std::pair<int, int> home_dpad_neighbor(int page, int slot, int item_count, int page_count,
    HomeDpadDirection direction, HomeGridLayout layout) noexcept {
    if (item_count <= 0 || page_count <= 0) return {0, 0};
    const int clamped_page = std::clamp(page, 0, page_count - 1);
    const int clamped_slot = std::clamp(slot, 0, kHomeSlotsPerPage - 1);
    const int page_base = clamped_page * kHomeSlotsPerPage;
    if (page_base + clamped_slot >= item_count) {
        const int last_item = item_count - 1;
        return {last_item / kHomeSlotsPerPage, last_item % kHomeSlotsPerPage};
    }

    const auto& offsets = home_slot_offsets(layout);
    const float cur_dx = offsets[static_cast<std::size_t>(clamped_slot)].dx;
    const int row = slot_row(clamped_slot);

    if (direction == HomeDpadDirection::left || direction == HomeDpadDirection::right) {
        const auto [r_begin, r_end] = row_bounds(row);
        const int step = (direction == HomeDpadDirection::left) ? -1 : 1;
        for (int s = clamped_slot + step; s >= r_begin && s < r_end; s += step) {
            if (page_base + s < item_count) return {clamped_page, s};
        }
        return {clamped_page, clamped_slot};
    }

    if (direction == HomeDpadDirection::down) {
        for (int r = row + 1; r <= 2; ++r) {
            const auto [r_begin, r_end] = row_bounds(r);
            const int candidate = pick_closest_occupied_in_range(
                clamped_page, r_begin, r_end, cur_dx, item_count, layout);
            if (candidate >= 0) return {clamped_page, candidate};
        }
        if (clamped_page + 1 < page_count) {
            const int next_page = clamped_page + 1;
            const int row0 = pick_closest_occupied_in_range(
                next_page, 0, 3, cur_dx, item_count, layout);
            if (row0 >= 0) return {next_page, row0};
            const int fallback = pick_closest_occupied_on_page(
                next_page, cur_dx, 200.0f, item_count, layout);
            if (fallback >= 0) return {next_page, fallback};
        }
        return {clamped_page, clamped_slot};
    }

    // HomeDpadDirection::up
    for (int r = row - 1; r >= 0; --r) {
        const auto [r_begin, r_end] = row_bounds(r);
        const int candidate = pick_closest_occupied_in_range(
            clamped_page, r_begin, r_end, cur_dx, item_count, layout);
        if (candidate >= 0) return {clamped_page, candidate};
    }
    if (clamped_page > 0) {
        const int prev_page = clamped_page - 1;
        for (int r = 2; r >= 0; --r) {
            const auto [r_begin, r_end] = row_bounds(r);
            const int candidate = pick_closest_occupied_in_range(
                prev_page, r_begin, r_end, cur_dx, item_count, layout);
            if (candidate >= 0) return {prev_page, candidate};
        }
    }
    return {clamped_page, clamped_slot};
}

HomeBubbleFloatState make_bubble_float_state(int page, int slot, float initial_scroll_y) noexcept {
    HomeBubbleFloatState state;
    const std::uint32_t h0 = hash_slot(page, slot);
    const std::uint32_t h1 = hash_slot(page + 7, slot + 19);
    const std::uint32_t h2 = hash_slot(page + 23, slot + 41);
    state.seed = h0;
    state.sway_phase_x = unit_hash(h0) * kTwoPi;
    state.sway_phase_y = unit_hash(h1) * kTwoPi;
    state.sway_freq_x = 0.16f + 0.10f * unit_hash(h2);
    state.sway_freq_y = 0.13f + 0.08f * unit_hash(h1 ^ 0x5555u);
    state.sway_amp_x = 5.5f + 2.5f * unit_hash(h0 ^ 0x3333u);
    state.sway_amp_y = 1.6f + 1.2f * unit_hash(h2 ^ 0x7777u);
    state.prev_scroll_y = initial_scroll_y;
    return state;
}

void step_bubble_float(HomeBubbleFloatState& state, float dt_seconds, float scroll_y, bool is_pressed) noexcept {
    if (dt_seconds <= 0.0f || !std::isfinite(dt_seconds)) return;
    const float dt = std::min(dt_seconds, 0.1f);
    const float n60 = dt / kSixtyHzStep;

    state.sway_phase_x += dt * state.sway_freq_x * kTwoPi;
    if (state.sway_phase_x > kTwoPi) state.sway_phase_x -= kTwoPi;
    state.sway_phase_y += dt * state.sway_freq_y * kTwoPi;
    if (state.sway_phase_y > kTwoPi) state.sway_phase_y -= kTwoPi;

    // 60Hz-normalized spring/lag physics derived from shell.elf FUN_810ccdfe:
    // vertical scroll motion injects vertical drag lag (capped at +-40 px/s equivalent) and small horizontal sway.
    const float scroll_delta = scroll_y - state.prev_scroll_y;
    state.prev_scroll_y = scroll_y;
    const float scroll_vel = std::clamp(scroll_delta / std::max(dt, 1.0e-4f), -4000.0f, 4000.0f);
    if (std::abs(scroll_vel) > 60.0f) {
        const float transfer = 1.0f - std::pow(0.4f, n60);
        const float horiz_bias = (unit_hash(state.seed ^ 0xA5A5u) - 0.5f) * 0.08f;
        state.vel_y += (-scroll_vel * 0.028f) * transfer;
        state.vel_x += (scroll_vel * horiz_bias) * transfer;
    }

    const float vel_damp = std::pow(0.95f, n60);
    const float pos_damp = std::pow(0.90f, n60);
    state.vel_x = std::clamp(state.vel_x * vel_damp - state.spring_x * (6.0f * dt), -100.0f, 100.0f);
    state.vel_y = std::clamp(state.vel_y * vel_damp - state.spring_y * (6.0f * dt), -100.0f, 100.0f);
    state.spring_x = std::clamp((state.spring_x + state.vel_x * dt) * pos_damp, -28.0f, 28.0f);
    state.spring_y = std::clamp((state.spring_y + state.vel_y * dt) * pos_damp, -40.0f, 40.0f);

    const float target_scale = is_pressed ? 0.93f : 1.0f;
    const float scale_blend = 1.0f - std::pow(0.75f, n60);
    state.press_scale += (target_scale - state.press_scale) * scale_blend;
}

HomeScreenPoint bubble_float_offset(const HomeBubbleFloatState& state) noexcept {
    return {
        std::sin(state.sway_phase_x) * state.sway_amp_x + state.spring_x,
        std::sin(state.sway_phase_y) * state.sway_amp_y + state.spring_y,
    };
}

float HomeVerticalScroll::max_scroll_y() const noexcept {
    return static_cast<float>(std::max(0, page_count_ - 1)) * kHomePageHeight;
}

bool HomeVerticalScroll::is_settling() const noexcept {
    return !dragging_ && (std::abs(scroll_y_ - target_y_) > 0.25f || std::abs(velocity_y_) > 1.0f);
}

void HomeVerticalScroll::set_page_count(int page_count) noexcept {
    page_count_ = std::max(1, page_count);
    current_page_ = std::clamp(current_page_, 0, page_count_ - 1);
    target_y_ = static_cast<float>(current_page_) * kHomePageHeight;
    scroll_y_ = std::clamp(scroll_y_, min_scroll_y(), max_scroll_y());
}

void HomeVerticalScroll::begin_drag() noexcept {
    dragging_ = true;
    velocity_y_ = 0.0f;
}

void HomeVerticalScroll::drag_by(float finger_delta_y, float dt_seconds) noexcept {
    if (!dragging_) begin_drag();
    // Moving finger up (finger_delta_y < 0) advances scroll_y downward into next pages.
    const float delta_scroll = -finger_delta_y;
    const float max_y = max_scroll_y();
    float applied = delta_scroll;
    if (delta_scroll < 0.0f && scroll_y_ + delta_scroll < min_scroll_y()) {
        const float before = std::max(0.0f, scroll_y_ - min_scroll_y());
        const float normal_part = std::min(before, -delta_scroll);
        const float excess = (-delta_scroll) - normal_part;
        applied = -(normal_part + excess * kOverscrollElasticity);
    } else if (delta_scroll > 0.0f && scroll_y_ + delta_scroll > max_y) {
        const float before = std::max(0.0f, max_y - scroll_y_);
        const float normal_part = std::min(before, delta_scroll);
        const float excess = delta_scroll - normal_part;
        applied = normal_part + excess * kOverscrollElasticity;
    }
    scroll_y_ += applied;
    if (dt_seconds > 1.0e-4f) {
        const float instant_vel = std::clamp(applied / dt_seconds, -4000.0f, 4000.0f);
        velocity_y_ = velocity_y_ * 0.45f + instant_vel * 0.55f;
    }
}

int HomeVerticalScroll::end_drag() noexcept {
    dragging_ = false;
    int target_page = static_cast<int>(std::lround(scroll_y_ / kHomePageHeight));
    if (std::abs(velocity_y_) > kFlickVelocityThreshold) {
        const int base = (velocity_y_ > 0.0f)
            ? static_cast<int>(std::floor(scroll_y_ / kHomePageHeight)) + 1
            : static_cast<int>(std::ceil(scroll_y_ / kHomePageHeight)) - 1;
        target_page = std::clamp(base, current_page_ - 1, current_page_ + 1);
    }
    current_page_ = std::clamp(target_page, 0, page_count_ - 1);
    target_y_ = static_cast<float>(current_page_) * kHomePageHeight;
    return current_page_;
}

void HomeVerticalScroll::snap_to_page(int page) noexcept {
    dragging_ = false;
    velocity_y_ = 0.0f;
    current_page_ = std::clamp(page, 0, page_count_ - 1);
    target_y_ = static_cast<float>(current_page_) * kHomePageHeight;
}

void HomeVerticalScroll::jump_to_page(int page) noexcept {
    snap_to_page(page);
    scroll_y_ = target_y_;
}

void HomeVerticalScroll::step(float dt_seconds) noexcept {
    if (dragging_ || dt_seconds <= 0.0f || !std::isfinite(dt_seconds)) return;
    const float dt = std::min(dt_seconds, 0.1f);
    // Exponential snap toward target page (snap_power = 25 matches PS Vita ScrollView feel).
    const float alpha = 1.0f - std::exp(-kSnapPower * 0.55f * dt);
    scroll_y_ += (target_y_ - scroll_y_) * alpha;
    velocity_y_ *= std::exp(-kSnapPower * 0.55f * dt);
    if (std::abs(target_y_ - scroll_y_) <= 0.2f && std::abs(velocity_y_) <= 1.0f) {
        scroll_y_ = target_y_;
        velocity_y_ = 0.0f;
    }
}

} // namespace emucorev::paf
