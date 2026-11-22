#pragma once

// Pure touch-region helpers (no SDL / emulator state). A region is an inclusive rectangle in
// panel coordinates; sceTouchPeekRegion / sceTouchReadRegion report only the contacts inside the
// rectangle of the requested region. Regions are numbered 0..MAX_REGION. A region that was never
// defined does not restrict anything (provisional: the firmware table layout was not recovered).

#include <array>
#include <cstdint>

namespace touch_region {

constexpr int MAX_REGION = 0xFF;

struct Rect {
    std::int16_t min_x = 0;
    std::int16_t min_y = 0;
    std::int16_t max_x = 0;
    std::int16_t max_y = 0;
};

struct Region {
    bool defined = false;
    bool active = true;
    Rect rect;
};

using Table = std::array<Region, MAX_REGION + 1>;

constexpr bool region_id_valid(int region) { return region >= 0 && region <= MAX_REGION; }

constexpr bool contains(const Rect &r, int x, int y) {
    return x >= r.min_x && x <= r.max_x && y >= r.min_y && y <= r.max_y;
}

// A rectangle is usable if it is not inverted.
constexpr bool rect_valid(const Rect &r) { return r.min_x <= r.max_x && r.min_y <= r.max_y; }

constexpr bool restricts(const Region &r) { return r.defined && r.active; }

// Compacts `reports` (any type with .x/.y) in place keeping the ones inside the region, preserving
// order. Returns the new count. Undefined or deactivated regions keep every report.
template <typename Report>
constexpr std::uint32_t filter_reports(Report *reports, std::uint32_t count, const Region &region) {
    if (!restricts(region))
        return count;
    std::uint32_t kept = 0;
    for (std::uint32_t i = 0; i < count; i++) {
        if (contains(region.rect, reports[i].x, reports[i].y)) {
            if (kept != i)
                reports[kept] = reports[i];
            kept++;
        }
    }
    return kept;
}

} // namespace touch_region
