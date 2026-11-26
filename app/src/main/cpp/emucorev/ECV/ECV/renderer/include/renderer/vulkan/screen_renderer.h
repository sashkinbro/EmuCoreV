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

#pragma once

#include <vkutil/objects.h>

#include "screen_filters.h"
#include "emucorev/shaders/vulkan_shader_chain.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace renderer::vulkan {

struct VKState;

#ifdef __ANDROID__
bool has_android_surface();
#endif

class ScreenRenderer {
public:
    VKState &state;

    vk::SurfaceKHR surface;
    vk::SwapchainKHR swapchain;

    vk::SurfaceCapabilitiesKHR surface_capabilities;
    vk::SurfaceFormatKHR surface_format;
    vk::PresentModeKHR present_mode{};
    // v-sync setting, applied to the present mode on the next swapchain (re)build
    bool vsync_enabled = true;
    void select_present_mode();
    vk::Extent2D extent;
    uint32_t swapchain_size{};
    std::vector<vk::Image> swapchain_images;
    std::vector<vk::ImageView> swapchain_views;
    std::vector<vk::Framebuffer> swapchain_framebuffers;
    std::vector<vk::CommandBuffer> command_buffers;
    std::vector<vk::Fence> fences;
    std::vector<vk::Semaphore> image_acquired_semaphores;
    std::vector<vk::Semaphore> image_ready_semaphores;
    bool swapchain_has_storage = false;

    // renderpass used when no effect is done previously (clear the swapchain content)
    vk::RenderPass default_render_pass;
    // renderpass used after a post-processing pass clearing the swapchain, compatible with the default render pass
    vk::RenderPass post_filter_render_pass;
    // renderpass used to (partially) prevent a driver bug using stock adreno drivers
    vk::RenderPass stock_adreno_pass;

    std::unique_ptr<ScreenFilter> filter;

    std::vector<vkutil::Image> vita_surface;
    vma::Allocation vita_surface_staging_alloc;
    vma::AllocationInfo vita_surface_staging_info;
    vk::Buffer vita_surface_staging;

    vk::CommandBuffer current_cmd_buffer;

    // these are used by the gui
    uint32_t swapchain_image_idx = ~0;
    // between 0 and swapchain_size - 1, used as the index for semaphores
    int current_frame = 0;
    // set when the swapchain needs to be rebuilt before the next acquire
    bool need_rebuild = false;
    // a size disagreement that lasts one frame should not cause a swapchain rebuild
    int size_mismatch_frames = 0;
    // why the last rebuild happened, for the log
    const char *rebuild_reason = "initial";
    // the swapchain images were created with eTransferDst (required by render_transfer)
    bool support_swapchain_transfer_dst = false;

    // set when we also need to recreate the surface before rebuilding swapchain
    bool need_surface_recreate = false;

    ScreenRenderer(VKState &state);

    bool create();
    // called after the logical device has been created
    bool setup();
    void cleanup();

    bool acquire_swapchain_image();
    void begin_default_render_pass();
    void render(vk::ImageView image_view, vk::ImageLayout layout, const Viewport &viewport,
        const emucorev::shaders::ShaderChainImage *shader_source = nullptr);
    bool swap_window();
    void set_filter(const std::string_view &filter);
    void set_retroarch_shader_preset(std::string absolute_path);
    void set_retroarch_shader_parameter(std::string name, float value);
    void clear_retroarch_shader_parameters();
    std::string retroarch_shader_error() const;

private:
    struct PresentRequest {
        vk::SwapchainKHR swapchain;
        uint32_t image_index = 0;
        vk::Semaphore wait_semaphore;
    };

    // Present thread: one request waits in the slot while the previous one is inside vkQueuePresentKHR, so the
    // render thread keeps recording while a FIFO present blocks on the panel.
    void start_present_thread();
    void stop_present_thread();
    // waits until no present is queued or running (before the swapchain, its semaphores or the device go away)
    void drain_present();
    void present_thread_main();
    vk::Result present_now(const PresentRequest &request);
    // applies the outcome of a present (the result of the previous asynchronous one when presenting on the thread)
    bool handle_present_result(vk::Result result);

    std::thread present_thread;
    std::mutex present_mutex;
    std::condition_variable present_cv;
    std::optional<PresentRequest> present_pending;
    bool present_busy = false;
    bool present_quit = false;
    // vk::Result of the last failed or suboptimal asynchronous present not yet consumed by the render thread;
    // 0 (success) = none
    std::atomic<int> present_async_result{ 0 };

    void create_render_pass();
    void create_layout_sync();
    bool create_swapchain();
    bool note_size_mismatch(bool mismatched);
    vk::Pipeline create_graphics_pipeline_impl(std::array<vk::PipelineShaderStageCreateInfo, 2> &shader_stages);
    bool create_graphics_pipelines();
    void copy_to_vao(const void *data);
    void create_surface_image();
    void destroy_swapchain();
    bool ensure_swapchain();
    bool rebuild_swapchain_if_visible();
    bool surface_matches_window_size();
    vk::Viewport display_viewport() const;
    emucorev::shaders::VulkanShaderChain shader_chain;
};
} // namespace renderer::vulkan
