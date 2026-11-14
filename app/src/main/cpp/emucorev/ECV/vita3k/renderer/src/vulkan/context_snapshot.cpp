#include <renderer/vulkan/context_snapshot.h>
#include <renderer/vulkan/checkpoint_submit.h>
#include <renderer/vulkan/functions.h>
#include <renderer/vulkan/types.h>

#include <algorithm>
#include <cmath>

namespace renderer::vulkan {
namespace {
bool valid_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    const bool empty = x0 == INT32_MAX && y0 == INT32_MAX && x1 == 0 && y1 == 0;
    return empty || (x0 >= 0 && y0 >= 0 && x1 >= x0 && y1 >= y0);
}
}

bool validate_vk_context_snapshot(const VKContextSnapshot &snapshot, std::string &error) {
    const auto fail = [&](const char *reason) {
        error = reason;
        return false;
    };
    if (!snapshot.context_address)
        return fail("missing Vulkan context address");
    if (snapshot.recording_open && !snapshot.render_target_address)
        return fail("active Vulkan context has no render target address");
    if (!std::isfinite(snapshot.surface_downscale)
        || (snapshot.surface_downscale != 1.0f && snapshot.surface_downscale != 0.5f))
        return fail("invalid Vulkan surface downscale");
    if (!std::isfinite(snapshot.viewport_x) || !std::isfinite(snapshot.viewport_y)
        || !std::isfinite(snapshot.viewport_width) || !std::isfinite(snapshot.viewport_height))
        return fail("non-finite Vulkan viewport");
    // Negative height is intentional: sync_viewport_real maps negative GXM yScale
    // directly to VkViewport height, and this renderer requires VK_KHR_maintenance1.
    if (snapshot.viewport_width < 0.0f)
        return fail("negative Vulkan viewport width");
    if (!std::isfinite(snapshot.viewport_min_depth) || !std::isfinite(snapshot.viewport_max_depth)
        || snapshot.viewport_min_depth < 0.0f || snapshot.viewport_max_depth > 1.0f
        || snapshot.viewport_min_depth > snapshot.viewport_max_depth)
        return fail("invalid Vulkan viewport depth range");
    if (!valid_rect(snapshot.rendered_rect_x0, snapshot.rendered_rect_y0,
            snapshot.rendered_rect_x1, snapshot.rendered_rect_y1))
        return fail("invalid Vulkan rendered rectangle");
    if (!valid_rect(snapshot.draw_rect_x0, snapshot.draw_rect_y0,
            snapshot.draw_rect_x1, snapshot.draw_rect_y1))
        return fail("invalid Vulkan draw rectangle");

    if ((!snapshot.visibility_buffer_address && snapshot.visibility_stride)
        || (snapshot.visibility_buffer_address && (snapshot.visibility_stride < sizeof(uint32_t)
            || snapshot.visibility_stride > kMaxVisibilityStrideInSnapshot
            || snapshot.visibility_stride % sizeof(uint32_t) != 0))
        || snapshot.visibility_query_index < -1
        || snapshot.visibility_query_index >= static_cast<int32_t>(kMaxVisibilityQueriesInSnapshot)
        || (snapshot.visibility_query_index >= 0
            && snapshot.visibility_buffer_address
            && static_cast<uint32_t>(snapshot.visibility_query_index) >= snapshot.visibility_stride / sizeof(uint32_t)))
        return fail("invalid Vulkan visibility-buffer checkpoint");

    // The renderer is parked only at batch boundaries. A live recording here is
    // the empty segment opened by a notifying mid-scene flush; pending guest draw
    // work stays in the serialized GXM command graph.
    if (snapshot.recording_open && snapshot.scene_has_drawn)
        return fail("Vulkan recording is not an empty between-batch scene segment");
    return true;
}

bool prepare_vk_context_snapshot(VKContext &context, std::string &error) {
    if (context.cmdbuffers_to_submit.empty()) return true;
    // A nonnull MidSceneFlush notification whose address is zero submits its
    // frontend batch, but ends the backend segment without submitting it. Its
    // new recording is empty. Submit only the ended physical prefix, retaining
    // the scene fence, query selection, scene clocks and pending guest graph.
    if (context.is_recording && (context.in_renderpass || context.is_in_query
            || context.visibility_max_used_idx != -1 || context.scene_has_drawn)) {
        error = "Vulkan checkpoint has an unfinished renderer batch";
        return false;
    }
    return submit_ended_checkpoint_segments(context.state.device, context.state.general_queue,
        context.cmdbuffers_to_submit, error);
}

bool capture_vk_context_snapshot(const VKContext &context, Address context_address,
    Address render_target_address, VKContextSnapshot &snapshot, std::string &error) {
    if (!context_address) {
        error = "missing guest context address";
        return false;
    }
    if (!context.cmdbuffers_to_submit.empty()) {
        error = "Vulkan context has unsubmitted scene segments";
        return false;
    }
    if (context.is_recording
        && (context.in_renderpass || context.is_in_query || context.visibility_max_used_idx != -1
            || context.scene_has_drawn || !context.render_target || !context.render_cmd || !context.prerender_cmd)) {
        error = "Vulkan context is not at an empty scene-segment checkpoint";
        return false;
    }

    VKContextSnapshot staged;
    staged.context_address = context_address;
    staged.render_target_address = render_target_address;
    staged.recording_open = context.is_recording;
    staged.scene_wrote_depth = context.scene_wrote_depth;
    staged.scene_has_drawn = context.scene_has_drawn;
    staged.scene_macroblock_flushed = context.scene_macroblock_flushed;
    staged.is_first_scene_draw = context.is_first_scene_draw;
    staged.surface_downscale = context.surface_downscale;

    staged.viewport_x = context.viewport.x;
    staged.viewport_y = context.viewport.y;
    staged.viewport_width = context.viewport.width;
    staged.viewport_height = context.viewport.height;
    staged.viewport_min_depth = context.viewport.minDepth;
    staged.viewport_max_depth = context.viewport.maxDepth;
    staged.scissor_x = context.scissor.offset.x;
    staged.scissor_y = context.scissor.offset.y;
    staged.scissor_width = context.scissor.extent.width;
    staged.scissor_height = context.scissor.extent.height;

    staged.last_macroblock_x = context.last_macroblock_x;
    staged.last_macroblock_y = context.last_macroblock_y;
    staged.ignore_macroblock = context.ignore_macroblock;
    staged.rendered_rect_x0 = context.rendered_rect_x0;
    staged.rendered_rect_y0 = context.rendered_rect_y0;
    staged.rendered_rect_x1 = context.rendered_rect_x1;
    staged.rendered_rect_y1 = context.rendered_rect_y1;
    staged.draw_rect_x0 = context.draw_rect_x0;
    staged.draw_rect_y0 = context.draw_rect_y0;
    staged.draw_rect_x1 = context.draw_rect_x1;
    staged.draw_rect_y1 = context.draw_rect_y1;

    if (context.current_visibility_buffer) {
        staged.visibility_buffer_address = context.current_visibility_buffer->address;
        staged.visibility_stride = context.current_visibility_buffer->size * sizeof(uint32_t);
    }
    staged.visibility_query_index = context.current_query_idx;
    staged.visibility_query_increment = context.is_query_op_increment;

    if (!validate_vk_context_snapshot(staged, error))
        return false;
    snapshot = staged;
    return true;
}

bool restore_vk_context_snapshot(VKContext &context, MemState &mem, VKRenderTarget *render_target,
    const VKContextSnapshot &snapshot, const FeatureState &features, std::string &error) {
    if (!validate_vk_context_snapshot(snapshot, error))
        return false;
    if (!snapshot.recording_open) {
        // Backend sticky GXM settings survive outside an open render scene too.
        // Restore those independently; only active checkpoints need framebuffer
        // reconstruction and a fresh command recording.
        context.render_target = render_target;
        context.current_render_target = render_target;
        context.scene_wrote_depth = snapshot.scene_wrote_depth;
        context.scene_has_drawn = snapshot.scene_has_drawn;
        context.scene_macroblock_flushed = snapshot.scene_macroblock_flushed;
        context.is_first_scene_draw = snapshot.is_first_scene_draw;
        context.surface_downscale = snapshot.surface_downscale;
        context.viewport = vk::Viewport{
            .x = snapshot.viewport_x,
            .y = snapshot.viewport_y,
            .width = snapshot.viewport_width,
            .height = snapshot.viewport_height,
            .minDepth = snapshot.viewport_min_depth,
            .maxDepth = snapshot.viewport_max_depth,
        };
        context.scissor = vk::Rect2D{
            .offset = { snapshot.scissor_x, snapshot.scissor_y },
            .extent = { snapshot.scissor_width, snapshot.scissor_height },
        };
        context.last_macroblock_x = snapshot.last_macroblock_x;
        context.last_macroblock_y = snapshot.last_macroblock_y;
        context.ignore_macroblock = snapshot.ignore_macroblock;
        context.rendered_rect_x0 = snapshot.rendered_rect_x0;
        context.rendered_rect_y0 = snapshot.rendered_rect_y0;
        context.rendered_rect_x1 = snapshot.rendered_rect_x1;
        context.rendered_rect_y1 = snapshot.rendered_rect_y1;
        context.draw_rect_x0 = snapshot.draw_rect_x0;
        context.draw_rect_y0 = snapshot.draw_rect_y0;
        context.draw_rect_x1 = snapshot.draw_rect_x1;
        context.draw_rect_y1 = snapshot.draw_rect_y1;
        if (snapshot.visibility_buffer_address)
            sync_visibility_buffer(context, Ptr<uint32_t>(snapshot.visibility_buffer_address), snapshot.visibility_stride);
        else
            sync_visibility_buffer(context, Ptr<uint32_t>(0), 0);
        sync_visibility_index(context, snapshot.visibility_query_index >= 0,
            snapshot.visibility_query_index >= 0 ? static_cast<uint32_t>(snapshot.visibility_query_index) : 0,
            snapshot.visibility_query_increment);
        return true;
    }
    if (!render_target) {
        error = "missing Vulkan render target for active context";
        return false;
    }
    return restore_context_scene_checkpoint(context, mem, render_target, snapshot, features, error);
}

} // namespace renderer::vulkan
