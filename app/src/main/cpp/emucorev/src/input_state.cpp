#include <emucorev/input_state.h>

#include <array>
#include <mutex>

namespace emucorev::input_state {
namespace {

struct PadState {
    uint32_t buttons = 0;
    std::array<float, 4> axes{};
};

std::mutex g_state_mutex;
std::array<PadState, MAX_PADS> g_pad_states{};

bool valid_pad(int pad_index) {
    return pad_index >= 0 && pad_index < MAX_PADS;
}

void merge_state(const PadState &state, uint32_t &buttons, float axes[4]) {
    buttons |= state.buttons;
    for (int index = 0; index < 4; ++index)
        axes[index] += state.axes[index];
}

} // namespace

void set_pad_state(int pad_index, uint32_t buttons, float left_x, float left_y, float right_x, float right_y) {
    if (!valid_pad(pad_index))
        return;

    const std::lock_guard lock(g_state_mutex);
    g_pad_states[pad_index] = PadState{ buttons, { left_x, left_y, right_x, right_y } };
}

void reset_all() {
    const std::lock_guard lock(g_state_mutex);
    for (PadState &state : g_pad_states)
        state = {};
}

void apply_pad_state(int pad_index, uint32_t &buttons, float axes[4]) {
    if (!valid_pad(pad_index))
        return;

    const std::lock_guard lock(g_state_mutex);
    merge_state(g_pad_states[pad_index], buttons, axes);
}

void apply_all_pad_states(uint32_t &buttons, float axes[4]) {
    const std::lock_guard lock(g_state_mutex);
    for (const PadState &state : g_pad_states)
        merge_state(state, buttons, axes);
}

} // namespace emucorev::input_state
