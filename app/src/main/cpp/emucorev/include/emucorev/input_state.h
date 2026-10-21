#pragma once

#include <cstdint>

namespace emucorev::input_state {

constexpr int MAX_PADS = 4;

/**
 * EmuCoreV keyboard/mouse snapshots. They are merged into the Vita3K controller
 * read path as an independent source so physical controllers, the touch overlay
 * and keyboard/mouse can never clear each other's held buttons.
 */
void set_pad_state(int pad_index, uint32_t buttons, float left_x, float left_y, float right_x, float right_y);

void reset_all();

void apply_pad_state(int pad_index, uint32_t &buttons, float axes[4]);

void apply_all_pad_states(uint32_t &buttons, float axes[4]);

} // namespace emucorev::input_state
