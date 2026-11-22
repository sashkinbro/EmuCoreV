#include <emucorev/live_resolution.h>
#include <emucorev/savestate/physical_gpu_contexts.h>
#include <emucorev/savestate/scoped_gpu_context.h>

#include <config/state.h>
#include <emuenv/state.h>
#include <gxm/savestate.h>
#include <gxm/types.h>
#include <renderer/vulkan/context_snapshot.h>
#include <renderer/vulkan/functions.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/types.h>
#include <util/log.h>

#include <chrono>
#include <exception>
#include <utility>
#include <vector>

namespace emucorev::live_resolution {
namespace {

using renderer::vulkan::VKContext;
using renderer::vulkan::VKContextSnapshot;
using renderer::vulkan::VKRenderTarget;
using renderer::vulkan::VKState;

struct ContextEntry {
    Address address = 0;
    VKContext *context = nullptr;
    VKContextSnapshot snapshot;
};

struct TargetEntry {
    VKRenderTarget *target = nullptr;
    VKRenderTarget::ResizedImages images;
};

bool apply_on_render_thread(EmuEnvState &env, VKState &state, const float multiplier, std::string &error) {
    const float previous = state.res_multiplier;
    const float ratio = multiplier / previous;
    const uint32_t max_dimension = static_cast<uint32_t>(state.get_max_2d_texture_width());

    const auto targets = gxm::runtime_render_targets(env);
    const auto contexts = savestate::physical_gpu_contexts<VKContext>(gxm::runtime_contexts(env));

    // Phase 1: nothing destructive. Any failure leaves the session exactly as it was
    // (an ended scene segment is ordinary mid-scene-flush behaviour).
    for (const auto &[address, target] : targets) {
        if (!target)
            continue;
        const auto &vk_target = static_cast<const VKRenderTarget &>(*target);
        if (!render_target_fits(vk_target.guest_width, vk_target.guest_height,
                vk_target.multisample_mode == SCE_GXM_MULTISAMPLE_4X, multiplier, max_dimension)) {
            error = "a render target does not fit this GPU at the requested resolution";
            return false;
        }
    }

    std::vector<ContextEntry> context_entries;
    for (const auto &[address, context] : contexts) {
        ContextEntry entry;
        entry.address = address;
        entry.context = context;
        if (context->is_recording
            && (context->in_renderpass || context->is_in_query || context->visibility_max_used_idx != -1
                || context->scene_has_drawn)) {
            // Same effect as a guest sceGxmMidSceneFlush without notification: end the work recorded
            // so far, continue with an empty segment.
            const SceGxmNotification no_notification = { Ptr<uint32_t>(0), 0 };
            renderer::vulkan::mid_scene_flush(*context, no_notification);
        }
        Address target_address = 0;
        for (const auto &[candidate_address, candidate] : targets)
            if (candidate && candidate == context->render_target)
                target_address = candidate_address;
        if (!renderer::vulkan::prepare_vk_context_snapshot(*context, error)
            || !renderer::vulkan::capture_vk_context_snapshot(*context, address, target_address, entry.snapshot, error))
            return false;
        context_entries.push_back(std::move(entry));
    }

    std::vector<TargetEntry> target_entries;
    for (const auto &[address, target] : targets) {
        if (!target)
            continue;
        target_entries.emplace_back();
        target_entries.back().target = static_cast<VKRenderTarget *>(target);
        try {
            target_entries.back().target->prepare_resize(state, multiplier, target_entries.back().images);
        } catch (const std::exception &exception) {
            error = std::string("render target allocation failed: ") + exception.what();
            return false;
        }
    }

    // Phase 2: destructive. The new images exist, so only cache rebuilding can still go wrong.
    state.wait_gpu_idle();
    // Cached surfaces are the only holder of GPU-written pixels; keep them in guest RAM so they
    // are re-uploaded at the new size on first use.
    state.flush_surfaces(env.mem);
    for (auto &entry : context_entries)
        entry.context->abandon_empty_recording();
    // Drops surfaces, framebuffers and cached textures.
    state.reset_caches();
    for (auto &entry : target_entries)
        entry.target->commit_resize(multiplier, std::move(entry.images));
    state.res_multiplier = multiplier;

    for (auto &entry : context_entries) {
        rescale_context_snapshot(entry.snapshot, ratio);
        savestate::ScopedGpuContextSelection selection(state.context, *entry.context);
        std::string restore_error;
        if (!renderer::vulkan::restore_vk_context_snapshot(*entry.context, env.mem, entry.context->render_target,
                entry.snapshot, state.features, restore_error)) {
            // The context cannot continue without a recording: wind the session down cleanly.
            LOG_CRITICAL("Live resolution change could not re-enter a scene: {}", restore_error);
            state.signal_fatal_error("live resolution change failed: " + restore_error);
            error = restore_error;
            return false;
        }
    }
    LOG_INFO("Internal resolution changed live: {}x -> {}x", previous, multiplier);
    return true;
}

} // namespace

float max_supported_multiplier(EmuEnvState &env) {
    if (!env.renderer || env.renderer->current_backend != renderer::Backend::Vulkan)
        return 0.0f;
    return max_multiplier_for_image_limit(static_cast<uint32_t>(env.renderer->get_max_2d_texture_width()));
}

Result apply(EmuEnvState &env, const float multiplier, std::string &error) {
    if (!is_supported_multiplier(multiplier) || multiplier > max_supported_multiplier(env)) {
        error = "resolution is not supported by this session";
        return Result::Unsupported;
    }
    auto &state = static_cast<VKState &>(*env.renderer);
    if (state.res_multiplier == multiplier) {
        env.cfg.current_config.resolution_multiplier = multiplier;
        return Result::Unchanged;
    }

    std::string task_error;
    const bool applied = state.run_on_render_thread(
        [&]() { return apply_on_render_thread(env, state, multiplier, task_error); },
        std::chrono::seconds(8));
    if (!applied) {
        error = task_error.empty() ? "the renderer did not accept the change" : task_error;
        return Result::Failed;
    }
    env.cfg.current_config.resolution_multiplier = multiplier;
    return Result::Applied;
}

} // namespace emucorev::live_resolution
