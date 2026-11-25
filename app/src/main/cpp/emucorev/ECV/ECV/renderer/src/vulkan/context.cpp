// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include <renderer/vulkan/types.h>

#include <renderer/vulkan/functions.h>
#include <renderer/vulkan/context_snapshot.h>
#include <renderer/vulkan/depth_clear.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/render_pass_sync.h>
#include <renderer/vulkan/render_pass_policy.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/surface_sync.h>

#include <cpu/functions.h>
#include <gxm/functions.h>
#include <renderer/functions.h>

#include <util/log.h>
#include <util/overloaded.h>
#include <renderer/vulkan/gpu_resource_cleanup.h>

#include <algorithm>
#include <limits>

#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#include <thread>
#include <android/log.h>
#endif

namespace renderer::vulkan {

void VKContext::wait_thread_function(const MemState &mem) {
    const ActiveGpuRequestReader active_reader(state.gpu_request_reader_count);
    try {
        wait_thread_body(mem);
    } catch (const vk::DeviceLostError &e) {
        // Nothing submitted can complete any more. Never signal syncs/notifications as done:
        // abort the queues so every waiter is released instead.
        state.signal_fatal_error(std::string("GPU wait thread: ") + e.what(), true);
        new_frame_condv.notify_all();
    } catch (const std::exception &e) {
        state.signal_fatal_error(std::string("GPU wait thread: ") + e.what());
        new_frame_condv.notify_all();
    } catch (...) {
        state.signal_fatal_error("GPU wait thread: unknown exception");
        new_frame_condv.notify_all();
    }
}

void VKContext::wait_thread_body(const MemState &mem) {
    // try to wait for multiple fences at the same time if possible
    std::vector<vk::Fence> fences;
    // timeline tracking: the highest value requested since the last wait (one semaphore wait replaces the whole fence list)
    uint64_t timeline_target = 0;
    bool timeline_mixed = false;

    auto wait_for_fences = [&]() {
        while (!fences.empty()) {
            // timeout so we can check for shutdown
            vk::Result result;
            if (timeline_target != 0 && state.submit_timeline) {
                const vk::SemaphoreWaitInfo wait_info{ .semaphoreCount = 1, .pSemaphores = &state.submit_timeline, .pValues = &timeline_target };
                result = state.device.waitSemaphoresKHR(wait_info, 100'000'000ULL);
            } else {
                result = state.device.waitForFences(fences, VK_TRUE, 100'000'000ULL);
            }
            if (result == vk::Result::eSuccess) {
                // don't reset them
                fences.clear();
                timeline_target = 0;
                timeline_mixed = false;
                break;
            }
            if (result == vk::Result::eTimeout) {
                if (state.request_queue.is_aborted()) {
                    fences.clear();
                    timeline_target = 0;
                    timeline_mixed = false;
                    break;
                }
                continue;
            }
            // Any other result means the GPU work is in an unknown state. Do not fall through and
            // let the caller signal syncs/notifications as completed: unwind to wait_thread_function.
            LOG_ERROR("Could not wait for fences: {}", vk::to_string(result));
            fences.clear();
            timeline_target = 0;
            timeline_mixed = false;
            throw vk::SystemError(vk::make_error_code(result), "waitForFences");
        }
    };

    while (true) {
        auto wait_request = state.request_queue.pop();

        if (!wait_request)
            break;

        std::visit(overloaded{
                       [&](FenceWaitRequest &request) {
                           fences.push_back(request.fence);
                           // a request without a value (early submits) forces the fence wait for the whole list
                           if (request.timeline_value == 0)
                               timeline_target = 0, timeline_mixed = true;
                           else if (!timeline_mixed)
                               timeline_target = std::max(timeline_target, request.timeline_value);
                       },
                       [&](NotificationRequest &request) {
                           if (request.notifications[0].address || request.notifications[1].address) {
                               wait_for_fences();

                               const std::shared_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);
                               // same as in handle_sync_surface_data
                               std::unique_lock<std::mutex> lock(state.notification_mutex);

                               write_notification_value(mem, request.notifications[0]);
                               write_notification_value(mem, request.notifications[1]);

                               // unlocking before a notify should be faster
                               lock.unlock();
                               state.notification_ready.notify_all();
                           }
                       },
                       [&](FrameDoneRequest &request) {
                           wait_for_fences();

                           // don't reset them, the reset will be done in the new_frame function
                           // and these fences can still be waited for during texture uploading
                           std::unique_lock<std::mutex> lock(new_frame_mutex);
                           last_frame_waited = request.frame_timestamp;
                           lock.unlock();
                           new_frame_condv.notify_one();
                       },
                       [&](BufferSyncRequest &request) {
                           wait_for_fences();
                           const std::shared_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);
                           auto mem_it = state.mapped_memories.lower_bound(request.location);
                           if (mem_it == state.mapped_memories.end() || request.location < mem_it->first) {
                               LOG_ERROR("Buffer Sync request at {} is not mapped", log_hex(request.location));
                               return;
                           }
                           const uint64_t mapping_offset = static_cast<uint64_t>(request.location - mem_it->first);
                           const uint64_t mapping_size = mem_it->second.size;
                           const auto row_span = request.row_stride != 0
                               ? surface_sync_rows_span(request.row_stride, request.row_bytes, request.row_count)
                               : std::optional<size_t>{};
                           if (mapping_offset > mapping_size || request.size > mapping_size - mapping_offset ||
                               (request.row_stride != 0 && (!row_span || *row_span != request.size))) {
                               LOG_ERROR("Buffer Sync request for {} bytes at {} exceeds its mapping", request.size, log_hex(request.location));
                               return;
                           }
                           uint8_t *src = reinterpret_cast<uint8_t *>(std::get<vkutil::Buffer>(mem_it->second.buffer_impl).mapped_data);
                           src += mapping_offset;
                           uint8_t *dst = reinterpret_cast<uint8_t *>(Ptr<void>(request.location).get(mem));
                           renderer::vulkan::surface_sync_internal_write = true;
                           bool copied = true;
                           if (request.row_stride != 0) {
                               copied = copy_surface_sync_rows(dst, request.size, src,
                                   static_cast<size_t>(mapping_size - mapping_offset), request.row_stride,
                                   request.row_bytes, request.row_count);
                           } else {
                               memcpy(dst, src, request.size);
#if ECV_ORACLE
                               if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_UB_CHECK))
                                   ecv::oracle::gpu::vis_synced(request.location, reinterpret_cast<const uint32_t *>(dst), request.size / 4);
#endif
                           }
                           renderer::vulkan::surface_sync_internal_write = false;
                           if (!copied)
                               LOG_ERROR("Invalid row layout in Buffer Sync request at {}", log_hex(request.location));
                       },
                       [&](PostSurfaceSyncRequest &request) {
                           wait_for_fences();
                           const std::shared_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);
                           renderer::vulkan::surface_sync_internal_write = true;
                           state.surface_cache.perform_post_surface_sync(mem, request.cache_info);
                           renderer::vulkan::surface_sync_internal_write = false;
                       },
                       [&](SyncSignalRequest &request) {
                           wait_for_fences();

                           const std::shared_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);
                           renderer::subject_done(request.sync, request.timestamp);
                       },
                       [&](CallbackRequest &request) {
                           if (request.wait_for_gpu)
                               wait_for_fences();
                           if (request.callback) {
                               (*request.callback)();
                               delete request.callback;
                           }
                       } },
            *wait_request);
    }
}

static void configure_context_scene(VKContext &context, MemState &mem, VKRenderTarget *rt,
    const FeatureState &features, const VKContextSnapshot *resume) {
    const bool restoring = resume != nullptr;
    if (!restoring) {
        context.state.surface_cache.resolve_ds_scene_end(context.scene_wrote_depth);
        context.scene_wrote_depth = false;
        context.scene_has_drawn = false;
        context.scene_macroblock_flushed = false;

        context.rendered_rect_x0 = INT32_MAX;
        context.rendered_rect_y0 = INT32_MAX;
        context.rendered_rect_x1 = 0;
        context.rendered_rect_y1 = 0;
        context.draw_rect_x0 = INT32_MAX;
        context.draw_rect_y0 = INT32_MAX;
        context.draw_rect_x1 = 0;
        context.draw_rect_y1 = 0;
    } else {
        context.scene_wrote_depth = resume->scene_wrote_depth;
        context.scene_has_drawn = resume->scene_has_drawn;
        context.scene_macroblock_flushed = resume->scene_macroblock_flushed;
        context.is_first_scene_draw = resume->is_first_scene_draw;
        context.surface_downscale = resume->surface_downscale;
        context.viewport = vk::Viewport{
            .x = resume->viewport_x,
            .y = resume->viewport_y,
            .width = resume->viewport_width,
            .height = resume->viewport_height,
            .minDepth = resume->viewport_min_depth,
            .maxDepth = resume->viewport_max_depth,
        };
        context.scissor = vk::Rect2D{
            .offset = { resume->scissor_x, resume->scissor_y },
            .extent = { resume->scissor_width, resume->scissor_height },
        };
        context.last_macroblock_x = resume->last_macroblock_x;
        context.last_macroblock_y = resume->last_macroblock_y;
        context.ignore_macroblock = resume->ignore_macroblock;
        context.rendered_rect_x0 = resume->rendered_rect_x0;
        context.rendered_rect_y0 = resume->rendered_rect_y0;
        context.rendered_rect_x1 = resume->rendered_rect_x1;
        context.rendered_rect_y1 = resume->rendered_rect_y1;
        context.draw_rect_x0 = resume->draw_rect_x0;
        context.draw_rect_y0 = resume->draw_rect_y0;
        context.draw_rect_x1 = resume->draw_rect_x1;
        context.draw_rect_y1 = resume->draw_rect_y1;
    }

    context.render_target = rt;
    context.current_render_target = rt;
    if (!restoring)
        context.scene_timestamp++;
    context.state.texture_cache.current_scene_timestamp = context.scene_timestamp;
#if ECV_ORACLE
    ecv::oracle::gpu::scene_begin(context.scene_timestamp);
#endif

    SceGxmColorSurface *color_surface_fin = &context.record.color_surface;
    // set these values for the pipeline cache
    context.record.color_base_format = gxm::get_base_format(color_surface_fin->colorFormat);
    context.record.is_gamma_corrected = static_cast<bool>(color_surface_fin->gamma);
    vk::Format vk_format = color::translate_surface_format(context.record.color_base_format);

    if (color_surface_fin->gamma && vk_format == vk::Format::eR8G8B8A8Unorm) {
        vk_format = vk::Format::eR8G8B8A8Srgb;
    }

    if (color_surface_fin->data.address() == 0) {
        color_surface_fin = nullptr;

        // set back default values
        vk_format = vk::Format::eR8G8B8A8Unorm;
        context.record.color_surface.downscale = rt && static_cast<bool>(rt->multisample_mode);
        context.record.is_gamma_corrected = false;
        context.record.is_maskupdate = false;
        context.record.color_base_format = SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8;
    }
    context.current_color_format = vk_format;

    bool msaa_expanded = false;
    if (rt) {
        rt->width = rt->base_width;
        rt->height = rt->base_height;
        if (rt->multisample_mode && !context.record.color_surface.downscale) {
            // using MSAA without downscaling, emulate this as best as we can by multiplying the width and height of the render target by 2
            rt->width *= 2;
            rt->height *= 2;
            msaa_expanded = true;
        }
    }

    constexpr bool apply_color_surface_downscale = true;
    context.surface_downscale = 1.0f;
    if (apply_color_surface_downscale && color_surface_fin != nullptr
        && context.record.color_surface.downscale && !msaa_expanded
        && color_surface_fin->width > 0 && color_surface_fin->height > 0) {
        const float res_multiplier = context.state.res_multiplier;
        const uint32_t color_width_scaled = static_cast<uint32_t>(color_surface_fin->width * res_multiplier);
        const uint32_t color_height_scaled = static_cast<uint32_t>(color_surface_fin->height * res_multiplier);
        if (rt && color_width_scaled > 0 && color_height_scaled > 0
            && rt->base_width >= color_width_scaled * 2
            && rt->base_height >= color_height_scaled * 2) {
            context.surface_downscale = 0.5f;
            rt->width /= 2;
            rt->height /= 2;
            LOG_INFO_ONCE("Colour surface downscale: rendering a {}x{} target at the surface's {}x{} scale "
                          "(guest {}x{}, res_multiplier {})",
                rt->base_width, rt->base_height, color_width_scaled, color_height_scaled,
                color_surface_fin->width, color_surface_fin->height, res_multiplier);
        }
    }

    constexpr bool log_gxm_scene_state = false; // ~600 lines/sec - only for guest-state investigations
    context.gxmscene_viewport_logged = false;
    if constexpr (log_gxm_scene_state) {
        const SceGxmColorSurface &cs = context.record.color_surface;
        const SceGxmDepthStencilSurface &ds = context.record.depth_stencil_surface;
        LOG_INFO("[GXMSCENE] rt base={}x{} msaa={} -> extent={}x{} expanded={} | color addr=0x{:08X} "
                 "{}x{} stride={} downscale={} gamma={} disabled={} fmt=0x{:08X} type={} | "
                 "ds depth=0x{:08X} stencil=0x{:08X} force_load={} force_store={} | res_mult={}",
            rt->base_width, rt->base_height, static_cast<int>(rt->multisample_mode),
            rt->width, rt->height, msaa_expanded,
            cs.data.address(), cs.width, cs.height, cs.strideInPixels,
            static_cast<uint32_t>(cs.downscale), static_cast<uint32_t>(cs.gamma),
            static_cast<uint32_t>(cs.disabled), static_cast<uint32_t>(cs.colorFormat),
            static_cast<int>(cs.surfaceType),
            ds.depth_data.address(), ds.stencil_data.address(),
            ds.force_load, ds.force_store, context.state.res_multiplier);
    }

    SceGxmDepthStencilSurface *ds_surface_fin = &context.record.depth_stencil_surface;
    // if the depth-stencil buffer is not backed by memory, use the transient attachment instead.
    // Was the Android build that added the changes:
    // || (!ds_surface_fin->force_load && !ds_surface_fin->force_store)) {
    if (!ds_surface_fin->depth_data && !ds_surface_fin->stencil_data) {
        ds_surface_fin = nullptr;
    }

    // P5 depth/stencil model: host format and aspects of this scene's depth/stencil attachment (also read by the bias, which is
    // set when recording starts, so this must come before start_recording)
    {
        namespace dsm = dsmodel;
        const dsm::DsClass ds_cls = ds_surface_fin ? dsm::ds_class(ds_surface_fin->get_format()) : dsm::DsClass::S8D24;
        context.ds_host_format = ds_surface_fin ? context.state.ds_format_for(ds_cls) : context.state.deep_stencil_use;
        // the tile hardware always depth/stencil-tests on chip, the surface format only says what is stored: both tests stay on
        // whenever the host format has the aspect (only a device with no stencil format loses the stencil test)
        context.ds_depth_test = dsm::host_has_depth(context.ds_host_format);
        context.ds_stencil_test = dsm::host_has_stencil(context.ds_host_format);
        context.ds_bias_units_scale = dsm::depth_bias_units_scale(ds_cls, context.ds_host_format);
        context.face_plan = dsm::FacePlan::Front;
        context.split_pipeline = nullptr;
    }

    VKState &state = context.state;
    state.surface_cache.set_render_target(rt);

    context.start_recording(!restoring);

    bool force_load = context.record.depth_stencil_surface.force_load;
    bool force_store = context.record.depth_stencil_surface.force_store;
    if (ds_surface_fin == nullptr) {
        // no memory backing the depth stencil
        force_load = false;
        force_store = false;
    }
    // GXM force_store controls write-back to the depth surface memory, not whether the buffer keeps its contents. The store is derived
    // (render_pass_policy.h): dropped only when the depth is provably dead. A scene may restart its pass and LOAD what the first instance
    // stored (mid-scene flush, casts, fbfetch restarts, depth transfers), and that is not known when the scene begins, so today the
    // answer is always "store" unless a producer of "this scene cannot restart" exists.
    {
        SceneRestartFacts facts;
        facts.shader_interlock = state.features.support_shader_interlock;
        facts.shell_snapshot = state.current_title_id == "SceShell";
        facts.macroblock_sync = rt && rt->has_macroblock_sync;
        facts.depth_backed = ds_surface_fin != nullptr;
        force_store = derive_depth_store({ force_store, ds_surface_fin != nullptr, scene_may_restart(facts) });
    }

#if ECV_ORACLE
    bool oracle_depth_valid = true;
#endif
    bool depth_load = restoring ? true : force_load;
    bool stencil_load = restoring ? true : force_load;
    if (!restoring && ds_surface_fin != nullptr) {
        constexpr bool use_ds_depth_validity = true;
        const bool game_stores = context.record.depth_stencil_surface.force_store;
        const bool depth_content_valid = state.surface_cache.begin_ds_scene_depth_check(*ds_surface_fin, game_stores, context.record.color_surface.data.address());
#if ECV_ORACLE
        oracle_depth_valid = depth_content_valid;
#endif
        if (use_ds_depth_validity && game_stores && !depth_content_valid)
            depth_load = false;
    }
    const bool color_has_raw = context.record.color_base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16;
    if (state.use_dynamic_rendering) {
        // no render pass objects: remember what the pass instances of this scene begin with (same rule as retrieve_render_pass)
        context.current_render_pass = nullptr;
        context.dyn_pass.has_raw = color_has_raw && color_surface_fin != nullptr && state.features.preserve_f16_nan_as_u16 && vk_format == vk::Format::eR16G16B16A16Sfloat;
        context.dyn_pass.depth_load = depth_load;
        context.dyn_pass.stencil_load = stencil_load;
        context.dyn_pass.store = force_store;
    } else {
        context.current_render_pass = context.state.pipeline_cache.retrieve_render_pass(vk_format, depth_load, stencil_load, force_store, color_surface_fin == nullptr, false, color_has_raw, context.ds_host_format);
    }
    context.scene_pass_starts = 0;
    context.scene_depth_unbacked = ds_surface_fin == nullptr;
    if (!restoring) {
        vkutil::sync::stats().scenes++;
        if (context.scene_depth_unbacked)
            vkutil::sync::stats().scenes_depth_unbacked++;
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::scene_cfg(restoring, context.record.depth_stencil_surface.force_load, context.record.depth_stencil_surface.force_store,
            oracle_depth_valid, depth_load, stencil_load, ds_surface_fin ? ds_surface_fin->depth_data.address() : 0,
            ds_surface_fin ? ds_surface_fin->stencil_data.address() : 0, reinterpret_cast<uint64_t>(static_cast<VkRenderPass>(context.current_render_pass)));
#endif
    if (context.state.features.support_shader_interlock && !state.use_dynamic_rendering)
        // also retrieve / create the shader interlock pass
        context.current_shader_interlock_pass = context.state.pipeline_cache.retrieve_render_pass(vk_format, true, true, true, color_surface_fin == nullptr, true, false, context.ds_host_format);

    Framebuffer &framebuffer = state.surface_cache.retrieve_framebuffer_handle(mem, color_surface_fin, ds_surface_fin, context.current_render_pass, context.current_shader_interlock_pass, context.current_color_view, context.current_ds_view);
    context.current_framebuffer = framebuffer.standard;
    context.current_shader_interlock_framebuffer = framebuffer.shader_interlock;
    context.current_color_base_image = framebuffer.base_image;
    context.current_ds_image = framebuffer.ds_image;
    context.current_ds_loaded = depth_load || stencil_load;
    context.current_color_loaded = color_surface_fin != nullptr;
    context.current_fb_width = framebuffer.width;
    context.current_fb_height = framebuffer.height;
    context.current_color_raw_view = framebuffer.raw_image ? framebuffer.raw_image->view : state.default_raw_image.view;
    context.current_color_raw_image = framebuffer.raw_image;
    context.shell_fetch_snapshot = framebuffer.shell_fetch_snapshot.get();

    // make sure we are not keeping any texture from the previous pass
    // (textures can be still bound even though they are not used)
    context.last_vert_texture_count = ~0;
    context.last_frag_texture_count = ~0;
    for (int i = 0; i < 16; i++) {
        context.vertex_textures[i].sampler = nullptr;
        context.fragment_textures[i].sampler = nullptr;
    }

    if (!restoring) {
        context.is_first_scene_draw = true;
        context.last_macroblock_x = ~0;
        context.last_macroblock_y = ~0;
        context.ignore_macroblock = false;
    } else {
        // A new command buffer has no bound pipeline or framebuffer-fetch pass.
        context.last_draw_was_framebuffer_fetch = false;
        context.refresh_pipeline = true;
        context.current_pipeline = nullptr;
        context.split_pipeline = nullptr;
        context.curr_vert_ublock.changed = true;
        context.curr_frag_ublock.changed = true;

        if (resume->visibility_buffer_address)
            sync_visibility_buffer(context, Ptr<uint32_t>(resume->visibility_buffer_address), resume->visibility_stride);
        else
            sync_visibility_buffer(context, Ptr<uint32_t>(0), 0);
        sync_visibility_index(context, resume->visibility_query_index >= 0,
            resume->visibility_query_index >= 0 ? static_cast<uint32_t>(resume->visibility_query_index) : 0,
            resume->visibility_query_increment);
    }
}

void set_context(VKContext &context, MemState &mem, VKRenderTarget *rt, const FeatureState &features) {
    configure_context_scene(context, mem, rt, features, nullptr);
}

bool restore_context_scene_checkpoint(VKContext &context, MemState &mem, VKRenderTarget *rt,
    const VKContextSnapshot &snapshot, const FeatureState &features, std::string &error) {
    if (!snapshot.recording_open || !rt || context.is_recording || context.in_renderpass
        || context.is_in_query || !context.cmdbuffers_to_submit.empty()) {
        error = "Vulkan context is not ready for an empty scene continuation";
        return false;
    }
    configure_context_scene(context, mem, rt, features, &snapshot);
    return context.is_recording && !context.in_renderpass;
}

void VKContext::start_recording(bool first_in_scene) {
    if (is_recording) {
        LOG_ERROR("Attempt to start recording while already recording");
        return;
    }

    if (render_target == nullptr) {
        LOG_ERROR("Recording started without a set command buffer");
        return;
    }

    const std::size_t frame_idx = static_cast<std::size_t>(state.current_frame_idx);
    FrameObject &frame = state.frame();
    const std::size_t cmd_buffer_idx = render_target->command_buffer_cursor.acquire(frame_idx, frame.pool_epoch);

    // safety check
    if (cmd_buffer_idx >= render_target->cmd_buffers[frame_idx].size()) {
        LOG_WARN_ONCE("Render Target is using more scenes per frame than what was planned!");

        // add additional cmd buffers, fences and semaphores
        vk::CommandBufferAllocateInfo cmd_buffer_info{
            .commandPool = frame.render_pool,
            .commandBufferCount = 1
        };
        render_target->cmd_buffers[frame_idx].push_back(state.device.allocateCommandBuffers(cmd_buffer_info)[0]);

        cmd_buffer_info.commandPool = frame.prerender_pool;
        render_target->pre_cmd_buffers[frame_idx].push_back(state.device.allocateCommandBuffers(cmd_buffer_info)[0]);

        // we only use one fence per scene anyway
        vk::FenceCreateInfo fence_info{};
        // make sure the next fence used is the one we created (but only if this is the first recording of the scene)
        auto fence_insert_it = render_target->fences.begin() + render_target->fence_idx;
        if (!first_in_scene)
            fence_insert_it++;
        render_target->fences.insert(fence_insert_it, state.device.createFence(fence_info));
    }

    if (next_fence == nullptr) {
        next_fence = render_target->fences[render_target->fence_idx];
        // only increase the fence index if we used the previous one
        render_target->fence_idx = (render_target->fence_idx + 1) % render_target->fences.size();
    }

    render_cmd = render_target->cmd_buffers[frame_idx][cmd_buffer_idx];
    prerender_cmd = render_target->pre_cmd_buffers[frame_idx][cmd_buffer_idx];

    vk::CommandBufferBeginInfo begin_info{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
    };
    render_cmd.begin(begin_info);
    prerender_cmd.begin(begin_info);

    is_recording = true;

    // set all the dynamic state here
    render_cmd.setViewport(0, viewport);
    render_cmd.setScissor(0, scissor);
    // a new command buffer has no bias set
    applied_bias_valid = false;
    sync_depth_bias(*this);
    sync_point_line_width(*this, true);
    sync_stencil_func(*this, false);
    if (record.two_sided == SCE_GXM_TWO_SIDED_ENABLED) {
        sync_stencil_func(*this, true);
    }
}

// we only need one descriptor per scene, so this does not need to be too big
static constexpr uint32_t DESCRIPTOR_PACK_SIZE = 16;

static vk::DescriptorSet retrieve_color_descriptor(VKState &state, FrameDescriptor &frame_descriptor) {
#if ECV_ORACLE
    if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_FRESH_RTSET)) {
        // debug A/B: a never-reused set for every render pass (pools are leaked until shutdown)
        const uint32_t per_set = state.features.preserve_f16_nan_as_u16 ? 2 : 1;
        std::vector<vk::DescriptorPoolSize> sizes{ { vk::DescriptorType::eInputAttachment, per_set }, { vk::DescriptorType::eStorageImage, per_set + 1 } };
        vk::DescriptorPoolCreateInfo info{ .maxSets = 1 };
        info.setPoolSizes(sizes);
        vk::DescriptorPool pool = state.device.createDescriptorPool(info);
        state.frame_descriptor_pools.push_back(pool);
        vk::DescriptorSetAllocateInfo alloc{ .descriptorPool = pool };
        alloc.setSetLayouts(state.pipeline_cache.attachments_layout);
        return state.device.allocateDescriptorSets(alloc)[0];
    }
#endif
    if (frame_descriptor.descriptors_idx < frame_descriptor.sets.size())
        return frame_descriptor.sets[frame_descriptor.descriptors_idx++];

    // we have no more frame descriptor available, create a bunch of new one for this specific layout
    // the type depends on the way we read it; each set holds the color attachment (binding 0)
    const uint32_t attachment_images_per_set = state.features.preserve_f16_nan_as_u16 ? 2 : 1;
    uint32_t storage_images_per_set = state.features.support_shader_interlock ? attachment_images_per_set : 0;
    if (state.features.use_mask_bit)
        storage_images_per_set++;


    std::vector<vk::DescriptorPoolSize> pool_sizes;
    if (!state.features.support_shader_interlock)
        pool_sizes.push_back({ vk::DescriptorType::eInputAttachment, attachment_images_per_set * DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING });
    if (storage_images_per_set > 0)
        pool_sizes.push_back({ vk::DescriptorType::eStorageImage, storage_images_per_set * DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING });

    vk::DescriptorPoolCreateInfo descriptor_pool_info{
        .maxSets = DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING
    };
    descriptor_pool_info.setPoolSizes(pool_sizes);

    vk::DescriptorPool descriptor_pool = state.device.createDescriptorPool(descriptor_pool_info);
    state.frame_descriptor_pools.push_back(descriptor_pool);

    // allocate all the descriptor sets
    const vk::DescriptorSetLayout set_layout = state.pipeline_cache.attachments_layout;
    std::vector<vk::DescriptorSetLayout> layouts(DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING, set_layout);
    vk::DescriptorSetAllocateInfo descr_set_info{
        .descriptorPool = descriptor_pool
    };
    descr_set_info.setSetLayouts(layouts);
    auto descriptor_sets = state.device.allocateDescriptorSets(descr_set_info);

    // distribute them among all frames
    for (int frame_idx = 0; frame_idx < MAX_FRAMES_RENDERING; frame_idx++) {
        FrameDescriptor &frame_descr = state.frames[frame_idx].color_descriptor;

        // insert DESCRIPTOR_PACK_SIZE in each frame descriptor
        auto descr_it = descriptor_sets.begin() + frame_idx * DESCRIPTOR_PACK_SIZE;
        frame_descr.sets.insert(frame_descr.sets.end(), descr_it, descr_it + DESCRIPTOR_PACK_SIZE);
    }

    return frame_descriptor.sets[frame_descriptor.descriptors_idx++];
}

void VKContext::start_render_pass(bool create_descriptor_set) {
    if (in_renderpass) {
        LOG_ERROR("Starting render pass while already in render pass");
        return;
    }

    if (!is_recording)
        start_recording();

    curr_renderpass_info = vk::RenderPassBeginInfo{
        .renderPass = current_render_pass,
        .framebuffer = current_framebuffer
    };

    if (render_target->has_macroblock_sync && !ignore_macroblock) {
        // set the render area to the correct macroblock
        curr_renderpass_info.renderArea = vk::Rect2D{
            .offset = {
                last_macroblock_x * render_target->macroblock_width,
                last_macroblock_y * render_target->macroblock_height },
            .extent = { render_target->macroblock_width, render_target->macroblock_height }
        };
    } else {
        curr_renderpass_info.renderArea = vk::Rect2D{
            .offset = { 0, 0 },
            .extent = { render_target->width, render_target->height }
        };
    }

    if (current_fb_width != 0) {
        const uint32_t max_w = current_fb_width - std::min<uint32_t>(current_fb_width, static_cast<uint32_t>(curr_renderpass_info.renderArea.offset.x));
        const uint32_t max_h = current_fb_height - std::min<uint32_t>(current_fb_height, static_cast<uint32_t>(curr_renderpass_info.renderArea.offset.y));
        curr_renderpass_info.renderArea.extent.width = std::min(curr_renderpass_info.renderArea.extent.width, max_w);
        curr_renderpass_info.renderArea.extent.height = std::min(curr_renderpass_info.renderArea.extent.height, max_h);
    }

    if (render_target->has_macroblock_sync) {
        const auto &area = curr_renderpass_info.renderArea;
        rendered_rect_x0 = std::min(rendered_rect_x0, area.offset.x);
        rendered_rect_y0 = std::min(rendered_rect_y0, area.offset.y);
        rendered_rect_x1 = std::max(rendered_rect_x1, area.offset.x + static_cast<int32_t>(area.extent.width));
        rendered_rect_y1 = std::max(rendered_rect_y1, area.offset.y + static_cast<int32_t>(area.extent.height));
    }

    // only the depth-stencil attachment may be clear if not force loaded. It can sit at attachment index 1 or 2 (raw u16 attachment present) so provide the value at both
    std::array<vk::ClearValue, 3> curr_clear_values{};
    const vk::ClearDepthStencilValue ds_clear{
        .depth = normalized_depth_clear(record.depth_stencil_surface.background_depth),
        .stencil = record.depth_stencil_surface.stencil
    };
    curr_clear_values[1].depthStencil = ds_clear;
    curr_clear_values[2].depthStencil = ds_clear;
    curr_renderpass_info.setClearValues(curr_clear_values);
    if (!state.use_dynamic_rendering) {
        // sync tracker: the pass' external dependencies cover the tracked state of the attachments, else an explicit barrier first
        namespace sync = vkutil::sync;
        sync::Batch pass_batch;
        const auto begin_attachment = [&](vkutil::Image *img, PassAttachment kind, bool loaded, const vk::ImageSubresourceRange &range) {
            if (!img || !img->image)
                return;
            const sync::PassCoverage cov = pass_coverage(kind, loaded);
            if (loaded && img->sync.layout == vk::ImageLayout::eUndefined) {
                // never seen by the tracker (e.g. restored/legacy path): the render pass has always run on these without a barrier,
                // so adopt the layout the pass expects instead of emitting an UNDEFINED -> X transition that would drop the content
                LOG_WARN_ONCE("Render pass attachment with untracked layout: adopting the pass' initial layout");
                img->sync.layout = cov.initial_layout;
            }
            sync::Barrier b;
            const sync::UseResult r = sync::track_pass_begin(img->sync, cov, b);
            if (r.barrier) {
                sync::stats().pass_explicit++;
                pass_batch.explicit_image(img->image, range, b);
            } else {
                sync::stats().pass_covered++;
            }
        };
        begin_attachment(current_color_base_image, PassAttachment::Color, current_color_loaded, vkutil::color_subresource_range);
        begin_attachment(current_color_raw_image, PassAttachment::ColorRaw, true, vkutil::color_subresource_range);
        begin_attachment(current_ds_image, PassAttachment::DepthStencil, current_ds_loaded, vkutil::aspect_clamped_range(current_ds_image ? current_ds_image->format : vk::Format::eUndefined, vkutil::ds_subresource_range));
        pass_batch.flush(render_cmd);
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::rp_begin(reinterpret_cast<uint64_t>(static_cast<VkRenderPass>(current_render_pass)),
            reinterpret_cast<uint64_t>(static_cast<VkFramebuffer>(current_framebuffer)), is_first_scene_draw, curr_renderpass_info.renderArea.offset.x,
            curr_renderpass_info.renderArea.offset.y, curr_renderpass_info.renderArea.extent.width, curr_renderpass_info.renderArea.extent.height);
    // oracle A/B (dbg-full-barrier): the tracker widens every barrier to ALL_COMMANDS and adds one global barrier here
    vkutil::sync::config().full_barrier = ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_FULL_BARRIER);
    vkutil::sync::debug_full_barrier(render_cmd);
#endif
    if (state.use_dynamic_rendering)
        begin_dynamic_pass(ds_clear);
    else
        render_cmd.beginRenderPass(curr_renderpass_info, vk::SubpassContents::eInline);
    if (++scene_pass_starts == 2) {
        vkutil::sync::stats().scenes_restarted++;
        if (scene_depth_unbacked)
            vkutil::sync::stats().scenes_unbacked_restarted++;
    }

    // set the renderpass info ready in case we need to switch between classic and framebuffer fetch usage
    curr_renderpass_info.setClearValues(nullptr);
    last_draw_was_framebuffer_fetch = false;

    refresh_pipeline = true;
    current_pipeline = nullptr;
    split_pipeline = nullptr;
    in_renderpass = true;

    if (!create_descriptor_set)
        return;

    // update the rendertarget descriptor set
    rendertarget_set = retrieve_color_descriptor(state, state.frame().color_descriptor);

    // update descriptor set for the whole scene with the color attachment
    vk::DescriptorImageInfo descr_color_info{
        .sampler = nullptr,
        .imageView = current_color_view,
        .imageLayout = vk::ImageLayout::eGeneral,
    };
    if (shell_fetch_snapshot)
        descr_color_info.imageView = shell_fetch_snapshot->view;

    const vk::DescriptorType input_type = state.features.support_shader_interlock ? vk::DescriptorType::eStorageImage : vk::DescriptorType::eInputAttachment;
    vk::WriteDescriptorSet write_descr{
        .dstSet = rendertarget_set,
        .dstBinding = 0,
        .dstArrayElement = 0,
        .descriptorType = input_type,
    };
    write_descr.setImageInfo(descr_color_info);

    if (state.features.preserve_f16_nan_as_u16) {
        vk::DescriptorImageInfo descr_raw_info{
            .sampler = nullptr,
            .imageView = current_color_raw_view ? current_color_raw_view : state.default_raw_image.view,
            .imageLayout = vk::ImageLayout::eGeneral,
        };
        vk::WriteDescriptorSet write_raw_descr{
            .dstSet = rendertarget_set,
            .dstBinding = 2,
            .dstArrayElement = 0,
            .descriptorType = input_type,
        };
        write_raw_descr.setImageInfo(descr_raw_info);
        std::array<vk::WriteDescriptorSet, 2> writes = { write_descr, write_raw_descr };
        state.device.updateDescriptorSets(writes, {});
    } else {
        state.device.updateDescriptorSets(write_descr, {});
    }
}

// ---- RenderPath::DynamicRendering ------------------------------------------------------------------------------------------
// Without VkRenderPass objects nothing declares the external dependencies and layout transitions any more: the tracker emits them.
//  begin: attachments -> attachment use (colour: ColorAttachmentOutput|Fragment, read|write|input read, GENERAL; depth: fragment tests,
//         DEPTH_STENCIL_ATTACHMENT_OPTIMAL). Unloaded attachments are `discard` transitions.
//  end:   the attachments are made visible to everything that samples or copies them (what dependency [2] of the render pass did) and
//         the depth goes back to DEPTH_STENCIL_READ_ONLY, the layout the rest of the renderer (sampling, copies) expects.
void VKContext::begin_dynamic_pass(const vk::ClearDepthStencilValue &ds_clear) {
    namespace sync = vkutil::sync;
    const sync::Stage color_stages = sync::S::eColorAttachmentOutput | sync::S::eFragmentShader;
    const sync::Access color_access = sync::A::eColorAttachmentRead | sync::A::eColorAttachmentWrite | sync::A::eInputAttachmentRead;
    const sync::Stage ds_stages = sync::S::eEarlyFragmentTests | sync::S::eLateFragmentTests;
    const sync::Access ds_access = sync::A::eDepthStencilAttachmentRead | sync::A::eDepthStencilAttachmentWrite;

    sync::Batch batch;
    const auto prepare = [&](vkutil::Image *img, sync::Stage stages, sync::Access access, vk::ImageLayout layout, bool loaded, const vk::ImageSubresourceRange &range) {
        if (!img || !img->image)
            return;
        if (loaded && img->sync.layout == vk::ImageLayout::eUndefined) {
            // never seen by the tracker: the previous path ran on these without a barrier, keep their content
            LOG_WARN_ONCE("Dynamic rendering attachment with untracked layout: assuming the layout the sampling code leaves it in");
            img->sync.layout = layout == vk::ImageLayout::eDepthStencilAttachmentOptimal ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : layout;
        }
        batch.use_image(img->sync, img->image, range, stages, access, layout, !loaded);
    };
    prepare(current_color_base_image, color_stages, color_access, vk::ImageLayout::eGeneral, current_color_loaded, vkutil::color_subresource_range);
    if (dyn_pass.has_raw)
        prepare(current_color_raw_image, color_stages, color_access, vk::ImageLayout::eGeneral, true, vkutil::color_subresource_range);
    const vk::Format ds_format = current_ds_image ? current_ds_image->format : vk::Format::eUndefined;
    const bool ds_has_stencil = ds_format == vk::Format::eD24UnormS8Uint || ds_format == vk::Format::eD32SfloatS8Uint
        || ds_format == vk::Format::eD16UnormS8Uint || ds_format == vk::Format::eS8Uint;
    // the pass loads the depth/stencil it is told to; an aspect that is cleared counts as "not loaded" for the layout transition only when both are
    prepare(current_ds_image, ds_stages, ds_access, vk::ImageLayout::eDepthStencilAttachmentOptimal, dyn_pass.depth_load || dyn_pass.stencil_load,
        vkutil::aspect_clamped_range(ds_format, vkutil::ds_subresource_range));
    batch.flush(render_cmd);

    std::array<vk::RenderingAttachmentInfo, 2> colors{};
    colors[0] = vk::RenderingAttachmentInfo{
        .imageView = current_color_view,
        .imageLayout = vk::ImageLayout::eGeneral,
        .loadOp = current_color_loaded ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eDontCare,
        .storeOp = current_color_loaded ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
    };
    colors[1] = vk::RenderingAttachmentInfo{
        .imageView = current_color_raw_view,
        .imageLayout = vk::ImageLayout::eGeneral,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    const vk::AttachmentStoreOp store_op = dyn_pass.store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
    vk::RenderingAttachmentInfo depth{
        .imageView = current_ds_view,
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = dyn_pass.depth_load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
        .storeOp = store_op,
    };
    depth.clearValue.depthStencil = ds_clear;
    vk::RenderingAttachmentInfo stencil{
        .imageView = current_ds_view,
        .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
        .loadOp = dyn_pass.stencil_load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
        .storeOp = store_op,
    };
    stencil.clearValue.depthStencil = ds_clear;

    vk::RenderingInfo rendering{
        .renderArea = curr_renderpass_info.renderArea,
        .layerCount = 1,
        .colorAttachmentCount = dyn_pass.has_raw ? 2u : 1u,
        .pColorAttachments = colors.data(),
        .pDepthAttachment = &depth,
        .pStencilAttachment = ds_has_stencil ? &stencil : nullptr,
    };
    render_cmd.beginRenderingKHR(rendering);
}

void VKContext::end_dynamic_pass() {
    namespace sync = vkutil::sync;
    render_cmd.endRenderingKHR();

    const sync::Stage sampled = sync::S::eVertexShader | sync::S::eFragmentShader | sync::S::eTransfer;
    const sync::Access readable = sync::A::eShaderRead | sync::A::eTransferRead | sync::A::eInputAttachmentRead;
    sync::Batch batch;
    const auto finish = [&](vkutil::Image *img, vk::ImageLayout layout, const vk::ImageSubresourceRange &range) {
        if (img && img->image)
            batch.use_image(img->sync, img->image, range, sampled, readable, layout, false);
    };
    finish(current_color_base_image, vk::ImageLayout::eGeneral, vkutil::color_subresource_range);
    if (dyn_pass.has_raw)
        finish(current_color_raw_image, vk::ImageLayout::eGeneral, vkutil::color_subresource_range);
    finish(current_ds_image, vk::ImageLayout::eDepthStencilReadOnlyOptimal,
        vkutil::aspect_clamped_range(current_ds_image ? current_ds_image->format : vk::Format::eUndefined, vkutil::ds_subresource_range));
    batch.flush(render_cmd);
}

void VKContext::stop_render_pass() {
    if (!in_renderpass) {
        LOG_ERROR("Stopping render pass while not in render pass");
        return;
    }

    // do this before ending the render pass
    if (is_in_query) {
        render_cmd.endQuery(current_visibility_buffer->query_pool, current_query_idx);
        is_in_query = false;
    }

#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::rp_end();
#endif
    if (state.use_dynamic_rendering) {
        end_dynamic_pass();
        in_renderpass = false;
        return;
    }
    render_cmd.endRenderPass();
    prev_draw_fbfetch = false;

    // sync tracker: what the pass wrote and what its outgoing dependency made visible
    if (current_color_base_image && current_color_base_image->image)
        note_pass_end(current_color_base_image->sync, PassAttachment::Color);
    if (current_color_raw_image && current_color_raw_image->image)
        note_pass_end(current_color_raw_image->sync, PassAttachment::ColorRaw);
    if (current_ds_image && current_ds_image->image)
        note_pass_end(current_ds_image->sync, PassAttachment::DepthStencil);

    in_renderpass = false;
}

void VKContext::abandon_empty_recording() {
    if (!is_recording)
        return;
    if (in_renderpass)
        stop_render_pass();
    prerender_cmd.end();
    render_cmd.end();
    prerender_cmd = nullptr;
    render_cmd = nullptr;
    is_recording = false;
}

void VKContext::stop_recording(const SceGxmNotification &notif1, const SceGxmNotification &notif2, bool submit) {
    if (!is_recording) {
        LOG_ERROR("Stopping recording while not recording");
        return;
    }

#if ECV_ORACLE
    ecv::oracle::gpu::rp_reason("stop_recording");
#endif
    if (in_renderpass)
        stop_render_pass();

    struct VisibilityRange {
        uint32_t offset;
        uint32_t size;
    };
    std::vector<VisibilityRange> occlusion_ranges;
    if (visibility_max_used_idx != -1) {
        // get all the entry ranges that were used
        bool in_range = false;
        uint32_t range_start = 0;
        for (uint32_t entry = 0; entry <= visibility_max_used_idx + 1; entry++) {
            if (current_visibility_buffer->queries_used[entry] == in_range)
                continue;

            if (in_range) {
                occlusion_ranges.push_back({ range_start, entry - range_start });
                in_range = false;
            } else {
                range_start = entry;
                in_range = true;
            }
        }

        for (auto &range : occlusion_ranges) {
            // reset before the beginning of the render pass
            prerender_cmd.resetQueryPool(current_visibility_buffer->query_pool, range.offset, range.size);

            // wait for the range at the end
            // TODO: this will be wrong with upscaling enabled and precise mode set
            render_cmd.copyQueryPoolResults(current_visibility_buffer->query_pool, range.offset, range.size,
                current_visibility_buffer->gpu_buffer, current_visibility_buffer->buffer_offset + range.offset * sizeof(uint32_t),
                sizeof(uint32_t), vk::QueryResultFlagBits::eWait);
        }
        visibility_max_used_idx = -1;
        // The range builder reads one false sentinel after the final query.
        current_visibility_buffer->queries_used.assign(current_visibility_buffer->size + 1, false);
    }

    // remember which part of the colour surface this scene's draws could have touched (for write-back bounds)
    state.surface_cache.note_scene_draw_rect(draw_rect_x0, draw_rect_y0, draw_rect_x1, draw_rect_y1);
    ColorSurfaceCacheInfo *surface_info = nullptr;
    if (state.features.enable_memory_mapping && !state.disable_surface_sync && submit)
        surface_info = state.surface_cache.perform_surface_sync();

    prerender_cmd.end();
    render_cmd.end();
#if ECV_ORACLE
    if (submit)
        ecv::oracle::gpu::mirror_verify_at_submit();
    if (submit)
        ecv::oracle::gpu::dbg_stall_once(ecv::oracle::gpu::DBG_STALL_SUBMIT, "before submit");
#endif

    // the prerender cmd must be submitted before the render cmd, the pipeline barriers do the rest
    cmdbuffers_to_submit.push_back(prerender_cmd);
    cmdbuffers_to_submit.push_back(render_cmd);

    render_cmd = nullptr;
    prerender_cmd = nullptr;
    is_recording = false;

    if (!submit)
        return;

    if (render_target->multisample_mode && !record.color_surface.downscale) {
        // revert changes made in set_context
        render_target->width /= 2;
        render_target->height /= 2;
    }

    vk::Fence fence = next_fence;
    next_fence = nullptr;

    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(cmdbuffers_to_submit);
    // timeline submit tracking: this submit also signals the next value; the fence stays for the per-scene recycling
    uint64_t timeline_value = 0;
    vk::TimelineSemaphoreSubmitInfo timeline_info{};
    if (state.submit_timeline) {
        timeline_value = ++state.submit_counter;
        timeline_info.signalSemaphoreValueCount = 1;
        timeline_info.pSignalSemaphoreValues = &timeline_value;
        submit_info.pNext = &timeline_info;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &state.submit_timeline;
        state.frame().rendered_timeline_value = timeline_value;
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_IDLE_SUBMIT))
        state.general_queue.waitIdle();
#endif

    try {
        state.general_queue.submit(submit_info, fence);
    } catch (const vk::DeviceLostError &) {
        state.log_device_fault();
        throw;
    }
    cmdbuffers_to_submit.clear();
    state.frame().rendered_fences.push_back(fence);

    if (state.features.enable_memory_mapping) {
        // send it to the wait queue
        state.request_queue.push(FenceWaitRequest{ fence, timeline_value });

        if (state.mapping_method == MappingMethod::DoubleBuffer) {
            // sync all the visibility buffers
            for (auto &range : occlusion_ranges) {
#if ECV_ORACLE
                if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_UB_CHECK))
                    ecv::oracle::gpu::vis_note(current_visibility_buffer->address + range.offset * 4, range.size * 4);
                if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_NO_VIS_SYNC))
                    continue;
#endif
                state.request_queue.push(BufferSyncRequest{ current_visibility_buffer->address + range.offset * 4, range.size * 4 });
            }

            // we must sync the two buffers
            if (surface_info && surface_info->need_buffer_sync) {
                const bool partial_sync = surface_info->partial_write_back
                    || surface_info->post_sync_x0 != 0 || surface_info->post_sync_y0 != 0
                    || surface_info->post_sync_width < surface_info->original_width
                    || surface_info->post_sync_height < surface_info->original_height;
                if (partial_sync) {
                    const uint32_t bpp = gxm::bits_per_pixel(surface_info->format) / 8;
                    const uint32_t row_stride_bytes = surface_info->stride_bytes;
                    const int32_t x0 = surface_info->post_sync_x0;
                    const int32_t y0 = surface_info->post_sync_y0;
                    const uint32_t width = surface_info->post_sync_width;
                    const uint32_t height = surface_info->post_sync_height;
                    const bool rect_in_bounds = bpp != 0 && x0 >= 0 && y0 >= 0
                        && static_cast<uint32_t>(x0) <= surface_info->original_width
                        && static_cast<uint32_t>(y0) <= surface_info->original_height
                        && width <= surface_info->original_width - static_cast<uint32_t>(x0)
                        && height <= surface_info->original_height - static_cast<uint32_t>(y0);
                    const uint64_t row_bytes64 = static_cast<uint64_t>(width) * bpp;
                    const uint64_t column_offset = static_cast<uint64_t>(x0) * bpp;
                    const bool row_in_bounds = column_offset <= row_stride_bytes
                        && row_bytes64 <= row_stride_bytes - column_offset;
                    const uint64_t address_offset = static_cast<uint64_t>(y0) * row_stride_bytes + column_offset;
                    const uint64_t address64 = static_cast<uint64_t>(surface_info->data.address()) + address_offset;
                    const auto span = rect_in_bounds && row_in_bounds && row_bytes64 <= std::numeric_limits<uint32_t>::max()
                        ? surface_sync_rows_span(row_stride_bytes, static_cast<size_t>(row_bytes64), height)
                        : std::optional<size_t>{};
                    if (span && *span > 0 && *span <= std::numeric_limits<uint32_t>::max()
                        && address64 <= std::numeric_limits<Address>::max()) {
                        state.request_queue.push(BufferSyncRequest{
                            static_cast<Address>(address64),
                            static_cast<uint32_t>(*span),
                            row_stride_bytes,
                            static_cast<uint32_t>(row_bytes64),
                            height });
                    } else {
                        LOG_ERROR("Skipping invalid partial surface buffer sync");
                    }
                } else {
                    state.request_queue.push(BufferSyncRequest{ surface_info->data.address(), static_cast<uint32_t>(surface_info->total_bytes) });
                }
            }
        }

        // U2F10F10F10/SE5M9M9M9 guest write-back is throttled so run it after the notifications
        const bool post_sync_after_notifications = surface_info && surface_info->need_post_surface_sync && (surface_info->format == SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10 || surface_info->format == SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9);

        if (surface_info && surface_info->need_post_surface_sync && !post_sync_after_notifications) {
            state.request_queue.push(PostSurfaceSyncRequest{ surface_info });
        }

        if (notif1.address || notif2.address) {
            NotificationRequest request = {
                .notifications = { notif1, notif2 },
            };
            state.request_queue.push(request);
        }

        if (post_sync_after_notifications) {
            state.request_queue.push(PostSurfaceSyncRequest{ surface_info });
        }
    }
}

void VKContext::check_for_macroblock_change(bool is_draw) {
    if (!render_target || !render_target->has_macroblock_sync)
        return;

    if (!ignore_macroblock && (scissor.extent.width > render_target->macroblock_width || scissor.extent.height > render_target->macroblock_height)) {
        // flower does not specify a scissor adapted to the current macroblock
        // so fallback to the slow path (one scene per draw, can't really do better)
        // TODO: with the feedback loop extension we can do better
        ignore_macroblock = true;
        // in this case we must load and store the depth stencil each time
        if (state.use_dynamic_rendering)
            dyn_pass.depth_load = dyn_pass.stencil_load = dyn_pass.store = true;
        else
            current_render_pass = state.pipeline_cache.retrieve_render_pass(current_color_format, true, true, true, !record.color_surface.data, false, record.color_base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16, ds_host_format);
    }

    // use the scissor to know in which macroblock we are
    uint16_t curr_macroblock_x = scissor.offset.x / render_target->macroblock_width;
    uint16_t curr_macroblock_y = scissor.offset.y / render_target->macroblock_height;

    if ((ignore_macroblock && is_draw) || curr_macroblock_x != last_macroblock_x || curr_macroblock_y != last_macroblock_y) {
        // we changed the current macroblock, restart the renderpass
        last_macroblock_x = curr_macroblock_x;
        last_macroblock_y = curr_macroblock_y;

        // the finished block's content is now visible to samples within this scene
        scene_macroblock_flushed = true;

        if (in_renderpass) {
#if ECV_ORACLE
    ecv::oracle::gpu::rp_reason("macroblock_change");
#endif
            if (state.features.use_texture_viewport) {
                // no need to copy the texture after this renderpass
                stop_render_pass();
            } else {
                SceGxmNotification empty_notification{};
                stop_recording(empty_notification, empty_notification, false);
                start_recording();
                scene_timestamp++;
            }
        }
    }
}

void new_frame(VKContext &context) {
    if (context.state.features.enable_memory_mapping) {
        FrameDoneRequest request = { context.frame_timestamp };
        context.state.request_queue.push(request);

        context.state.surface_cache.clear_surfaces_changed();
    }

    context.frame_timestamp++;
#if ECV_ORACLE
    ecv::oracle::gpu::frame_begin(context.frame_timestamp, context.state.vita_fs_path.c_str());
    {
        // debug A/B: a ~75 ms renderer-thread stall at the frame boundary once per second
        static std::chrono::steady_clock::time_point oracle_next_stall{};
        const auto now = std::chrono::steady_clock::now();
        if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_STALL_FRAME) && now >= oracle_next_stall) {
            oracle_next_stall = now + std::chrono::milliseconds(1050);
            std::this_thread::sleep_for(std::chrono::milliseconds(75));
        }
        // debug A/B: keep the GPU at most one frame behind the renderer
        if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_FRAME_SYNC))
            context.state.general_queue.waitIdle();
        static bool oracle_logged_mapping = false;
        if (!oracle_logged_mapping) {
            oracle_logged_mapping = true;
            __android_log_print(ANDROID_LOG_WARN, "GPUTRACE", "memory mapping enabled=%d method=%d", context.state.features.enable_memory_mapping,
                static_cast<int>(context.state.mapping_method));
        }
    }
#endif
    context.state.frame_epoch++;
    context.state.current_frame_idx = context.frame_timestamp % MAX_FRAMES_RENDERING;

    vk::Device device = context.state.device;
    FrameObject &frame = context.state.frame();

    // wait on all fences still present to make sure
    if (!frame.rendered_fences.empty()) {
        // wait for the fences, then reset them

        if (context.state.features.enable_memory_mapping) {
            // this will underflow for the first MAX_FRAMES_RENDERING frames
            // but that's not an issue as frame.rendered_fences will be empty
            uint64_t previous_frame_timestamp = context.frame_timestamp - MAX_FRAMES_RENDERING;

            // the wait is done by the wait thread
            // abort paths (shutdown or fatal GPU error) must release this wait: the wait thread may be gone
            std::unique_lock<std::mutex> lock(context.new_frame_mutex);
            const auto frame_done_or_abort = [&]() {
                return context.last_frame_waited >= previous_frame_timestamp
                    || context.state.request_queue.is_aborted()
                    || context.state.render_abort.load(std::memory_order_relaxed);
            };
            // the bounded wait also covers an abort flag set without a notification
            while (!context.new_frame_condv.wait_for(lock, std::chrono::milliseconds(50), frame_done_or_abort)) {
            }
            if (context.last_frame_waited < previous_frame_timestamp) {
                // aborted before the GPU finished: leave the fences/pools alone, the session is stopping
                return;
            }
        } else {
            // bounded wait so shutdown or a fatal error is noticed; device loss throws and is
            // handled by the render thread
            vk::Result result;
            do {
                result = device.waitForFences(frame.rendered_fences, VK_TRUE, 100'000'000ULL);
                if (result == vk::Result::eTimeout && context.state.render_abort.load(std::memory_order_relaxed))
                    return;
            } while (result == vk::Result::eTimeout);
            if (result != vk::Result::eSuccess) {
                LOG_ERROR("Could not wait for fences: {}", vk::to_string(result));
                context.state.signal_fatal_error("waitForFences failed: " + vk::to_string(result));
                return;
            }
        }

        // timeline tracking: the semaphore value can be reached a moment before the same submit's fence is signalled, and a fence must be
        // signalled before it is reset. Bounded like the waits above.
        if (context.state.submit_timeline) {
            vk::Result fence_result;
            do {
                fence_result = device.waitForFences(frame.rendered_fences, VK_TRUE, 100'000'000ULL);
                if (fence_result == vk::Result::eTimeout && context.state.render_abort.load(std::memory_order_relaxed))
                    return;
            } while (fence_result == vk::Result::eTimeout);
            frame.rendered_timeline_value = 0;
        }
        // reset the fences in both case (the wait thread does not do that as they can still be used)
        device.resetFences(frame.rendered_fences);
        frame.rendered_fences.clear();
    }

    device.resetCommandPool(frame.prerender_pool);
    device.resetCommandPool(frame.render_pool);

    // set the position in the used descriptor queue back to the beginning
    for (int i = 0; i < 16; i++) {
        frame.vert_descriptors[i].descriptors_idx = 0;
        frame.frag_descriptors[i].descriptors_idx = 0;
    }
    frame.color_descriptor.descriptors_idx = 0;

    // deferred destruction of the objects
    frame.destroy_queue.destroy_objects();

    context.last_vert_texture_count = ~0;
    context.last_frag_texture_count = ~0;

    frame.pool_epoch = context.state.frame_epoch;
}

void signal_sync_object(VKState &state, SceGxmSyncObject *sync_object, uint32_t timestamp) {
    assert(state.features.enable_memory_mapping);

    SyncSignalRequest request{
        .sync = sync_object,
        .timestamp = timestamp
    };
    state.request_queue.push(request);
}

} // namespace renderer::vulkan
