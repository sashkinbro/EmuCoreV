#pragma once

namespace renderer {
constexpr bool should_display_pause_overlay(bool paused, bool enabled) {
    return paused && enabled;
}
}
