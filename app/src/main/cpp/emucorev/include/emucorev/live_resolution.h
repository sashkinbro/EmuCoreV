#pragma once

#include <renderer/vulkan/context_snapshot.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

struct EmuEnvState;

// Changing the internal render resolution of a running Vulkan session.
//
// The change runs on the render thread between two batches (State::run_on_render_thread):
// every context is brought to an empty scene-segment checkpoint, ended segments are submitted,
// the GPU is drained, cached surfaces are written back to guest RAM and dropped, render target
// images are rebuilt and the contexts are re-entered through the save-state checkpoint path.
namespace emucorev::live_resolution {

// Same grid as the settings screen: 0.5x to 8x in 0.5x steps.
constexpr float kMinMultiplier = 0.5f;
constexpr float kMaxMultiplier = 8.0f;
constexpr float kStep = 0.5f;

constexpr bool is_supported_multiplier(float value) {
    if (!(value >= kMinMultiplier) || !(value <= kMaxMultiplier))
        return false;
    const float steps = value / kStep;
    return steps == static_cast<float>(static_cast<int>(steps));
}

// A guest render target is at most 960 px wide in practice and its MSAA depth buffer is twice
// that; the image must also stay within the device's maxImageDimension2D. The result is on the
// step grid and 0 when not even the minimum fits.
inline float max_multiplier_for_image_limit(uint32_t max_image_dimension) {
    constexpr uint32_t widest_guest_target = 960 * 2;
    const float limit = std::floor(static_cast<float>(max_image_dimension) / static_cast<float>(widest_guest_target) / kStep) * kStep;
    const float capped = std::min(limit, kMaxMultiplier);
    return capped >= kMinMultiplier ? capped : 0.0f;
}

// Whether a guest-sized render target still fits the device limit at `multiplier`.
inline bool render_target_fits(uint32_t guest_width, uint32_t guest_height, bool msaa_4x,
    float multiplier, uint32_t max_image_dimension) {
    const double factor = msaa_4x ? 2.0 : 1.0;
    return guest_width * double(multiplier) * factor <= max_image_dimension
        && guest_height * double(multiplier) * factor <= max_image_dimension;
}

// Context state that is stored in scaled pixels follows the multiplier.
inline void rescale_context_snapshot(renderer::vulkan::VKContextSnapshot &snapshot, float ratio) {
    snapshot.viewport_x *= ratio;
    snapshot.viewport_y *= ratio;
    snapshot.viewport_width *= ratio;
    snapshot.viewport_height *= ratio;
    snapshot.scissor_x = static_cast<int32_t>(std::lround(snapshot.scissor_x * ratio));
    snapshot.scissor_y = static_cast<int32_t>(std::lround(snapshot.scissor_y * ratio));
    snapshot.scissor_width = static_cast<uint32_t>(std::lround(snapshot.scissor_width * ratio));
    snapshot.scissor_height = static_cast<uint32_t>(std::lround(snapshot.scissor_height * ratio));

    // INT32_MAX / 0 mark an empty rectangle and must stay as they are
    const auto min_edge = [ratio](int32_t value) {
        return value == INT32_MAX ? value : static_cast<int32_t>(std::floor(value * ratio));
    };
    const auto max_edge = [ratio](int32_t value) {
        return value == 0 ? value : static_cast<int32_t>(std::ceil(value * ratio));
    };
    snapshot.rendered_rect_x0 = min_edge(snapshot.rendered_rect_x0);
    snapshot.rendered_rect_y0 = min_edge(snapshot.rendered_rect_y0);
    snapshot.rendered_rect_x1 = max_edge(snapshot.rendered_rect_x1);
    snapshot.rendered_rect_y1 = max_edge(snapshot.rendered_rect_y1);
    snapshot.draw_rect_x0 = min_edge(snapshot.draw_rect_x0);
    snapshot.draw_rect_y0 = min_edge(snapshot.draw_rect_y0);
    snapshot.draw_rect_x1 = max_edge(snapshot.draw_rect_x1);
    snapshot.draw_rect_y1 = max_edge(snapshot.draw_rect_y1);
}

enum class Result {
    Applied,
    Unchanged,
    // not a Vulkan session, value not on the grid, or too large for this GPU
    Unsupported,
    Failed,
    // only with allow_mid_scene = false: a scene is open on some context, nothing was changed (retry later)
    SceneOpen,
};

// A context is inside a scene (between its begin and end, or with work recorded but not ended).
template <typename Context>
bool context_has_open_scene(const Context &context) {
    return context.is_recording
        && (context.in_renderpass || context.is_in_query || context.visibility_max_used_idx != -1 || context.scene_has_drawn);
}

// Largest multiplier the active GPU can hold; 0 when a live change is not possible.
float max_supported_multiplier(EmuEnvState &env);

// Callable from any thread except the render thread. The game should be paused by the caller.
// With allow_mid_scene = false the change is only made when no context is inside a scene (a frame boundary);
// otherwise Result::SceneOpen is returned and the session is untouched. Contents of the render targets are
// scaled into the new images rather than discarded.
Result apply(EmuEnvState &env, float multiplier, std::string &error, bool allow_mid_scene = true);

} // namespace emucorev::live_resolution
