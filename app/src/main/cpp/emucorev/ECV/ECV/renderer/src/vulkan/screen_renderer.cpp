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

#include <util/frame_pacing.h>
#include "renderer/vulkan/screen_renderer.h"
#include "renderer/vulkan/surface_capability_selection.h"

#include "renderer/vulkan/state.h"
#include "util/log.h"
#include "vkutil/vkutil.h"
#include "emucorev/shaders/vulkan_shader_chain.h"
#include "emucorev/shaders/shader_preset_bridge.h"

#include <cstdint>
#ifdef __ANDROID__
#include <pthread.h>
#endif
#include <cmath>
#include <exception>

#ifdef _WIN32
#include <vulkan/vulkan_win32.h>
#elif defined(__APPLE__)
#include <vkutil/native_surface.h>
#include <vulkan/vulkan_metal.h>
#elif defined(__linux__) && !defined(__ANDROID__)
#if defined(HAVE_X11)
#include <vulkan/vulkan_xlib.h>
#if defined(VK_USE_PLATFORM_XCB_KHR)
#include <vulkan/vulkan_xcb.h>
#endif
#endif
#if defined(HAVE_WAYLAND)
#include <vulkan/vulkan_wayland.h>
#endif
#endif

#ifdef __ANDROID__
#include <SDL3/SDL_vulkan.h>
#include <jni.h>

// Number of live EmuSurface surfaces. A process hosts one Emulator activity at a
// time, but when a game is closed and the next one opened the old activity's
// surfaceDestroyed can arrive after the new one's surfaceCreated; a single bool
// then said "no surface" for the whole next session and it never presented (B151).
static std::atomic<int> live_surfaces{ 0 };

extern "C" JNIEXPORT void JNICALL
ECV_NativeBridge_EmuSurface_setSurfaceStatus(JNIEnv *, jobject, jboolean surface_present) {
    if (surface_present) {
        live_surfaces.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    int current = live_surfaces.load(std::memory_order_acquire);
    while (current > 0 && !live_surfaces.compare_exchange_weak(current, current - 1, std::memory_order_acq_rel)) {
    }
}
#endif

namespace renderer::vulkan {

namespace {

bool window_has_drawable_size(const VKState &state) {
    const auto *frame_host = static_cast<const renderer::State &>(state).frame;
    return frame_host->drawable_width() > 0 && frame_host->drawable_height() > 0;
}

} // namespace

#ifdef __ANDROID__
bool has_android_surface() {
    return live_surfaces.load(std::memory_order_acquire) > 0;
}
#endif

ScreenRenderer::ScreenRenderer(VKState &state)
    : state(state) {
}

bool ScreenRenderer::create() {
    if (this->surface) {
#ifdef __ANDROID__
        SDL_Vulkan_DestroySurface(state.instance, this->surface, nullptr);
#else
        state.instance.destroySurfaceKHR(this->surface);
#endif
        this->surface = nullptr;
    }

    auto *frame_host = static_cast<renderer::State &>(state).frame;

    const renderer::DisplayHandle display_handle = frame_host->handle();
    bool surface_created = false;

    if (const auto *handle = std::get_if<renderer::Win32DisplayHandle>(&display_handle)) {
#ifdef _WIN32
        vk::Win32SurfaceCreateInfoKHR create_info{};
        create_info.hinstance = GetModuleHandle(nullptr);
        create_info.hwnd = reinterpret_cast<HWND>(handle->hwnd);
        this->surface = state.instance.createWin32SurfaceKHR(create_info);
        surface_created = true;
#endif
    } else if (const auto *handle = std::get_if<renderer::MacOSDisplayHandle>(&display_handle)) {
#ifdef __APPLE__
        void *metal_layer = get_metal_layer_from_view(handle->view);
        if (!metal_layer) {
            LOG_ERROR("Failed to get CAMetalLayer from NSView");
            return false;
        }

        vk::MetalSurfaceCreateInfoEXT create_info{};
        create_info.pLayer = static_cast<const CAMetalLayer *>(metal_layer);
        this->surface = state.instance.createMetalSurfaceEXT(create_info);
        surface_created = true;
#endif
    } else if (const auto *handle = std::get_if<renderer::AndroidDisplayHandle>(&display_handle)) {
#ifdef __ANDROID__
        if (!handle->window) {
            LOG_WARN("Android SDL window is not ready yet; deferring Vulkan surface recreation");
            return false;
        }

        VkSurfaceKHR surface_handle = VK_NULL_HANDLE;
        if (!SDL_Vulkan_CreateSurface(handle->window, state.instance, nullptr, &surface_handle)) {
            LOG_WARN("SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
            return false;
        }

        this->surface = surface_handle;
        surface_created = true;
#endif
    } else if (const auto *handle = std::get_if<renderer::WaylandDisplayHandle>(&display_handle)) {
#if defined(HAVE_WAYLAND)
        vk::WaylandSurfaceCreateInfoKHR create_info{};
        create_info.display = static_cast<struct wl_display *>(handle->display);
        create_info.surface = static_cast<struct wl_surface *>(handle->surface);
        this->surface = state.instance.createWaylandSurfaceKHR(create_info);
        surface_created = true;
#endif
    } else if (const auto *handle = std::get_if<renderer::X11DisplayHandle>(&display_handle)) {
#if defined(HAVE_X11)
        if (state.linux_surface_type == LinuxSurfaceType::Xlib && handle->display) {
            vk::XlibSurfaceCreateInfoKHR create_info{};
            create_info.dpy = static_cast<Display *>(handle->display);
            create_info.window = static_cast<Window>(handle->window);
            this->surface = state.instance.createXlibSurfaceKHR(create_info);
            surface_created = true;
        }
#if defined(VK_USE_PLATFORM_XCB_KHR)
        else if (state.linux_surface_type == LinuxSurfaceType::Xcb && handle->connection) {
            vk::XcbSurfaceCreateInfoKHR create_info{};
            create_info.connection = static_cast<xcb_connection_t *>(handle->connection);
            create_info.window = static_cast<xcb_window_t>(handle->window);
            this->surface = state.instance.createXcbSurfaceKHR(create_info);
            surface_created = true;
        }
#endif
#endif
    }

    if (!surface_created) {
        LOG_ERROR("Failed to create Vulkan surface from frame host");
        return false;
    }

    if (!this->surface) {
        LOG_ERROR("Failed to create Vulkan surface from native window handle");
        return false;
    }

    return true;
}

bool ScreenRenderer::setup() {
    const auto surface_formats = state.physical_device.getSurfaceFormatsKHR(surface);
    if (!select_surface_format(surface_formats, surface_format)) {
        LOG_ERROR("Vulkan surface reports no supported formats");
        return false;
    }

    // Probe every depth/stencil format a GXM surface can map to (attachment + sampled: the guest reads its depth as a texture)
    // and pick the host format of each GXM class from the features, see dsmodel::select_host_format.
    dsmodel::HostDsSupport ds_support;
    for (const vk::Format candidate : dsmodel::host_candidates)
        ds_support.set(candidate, dsmodel::format_usable(state.physical_device.getFormatProperties(candidate).optimalTilingFeatures));

    state.deep_stencil_use = dsmodel::select_combined_format(ds_support);
    if (state.deep_stencil_use == vk::Format::eUndefined) {
        // the spec guarantees D16 attachment + sampling; reaching this means a broken driver
        LOG_WARN_ONCE("Your device reports no usable depth/stencil format, using D16 anyway");
        state.deep_stencil_use = vk::Format::eD16Unorm;
    } else if (!dsmodel::host_has_stencil(state.deep_stencil_use)) {
        LOG_WARN_ONCE("Your device has no depth/stencil format with a stencil aspect: the stencil test of the guest is lost");
    } else {
        LOG_INFO_ONCE("Depth/stencil host format (combined): {}", vk::to_string(state.deep_stencil_use));
    }
    for (size_t i = 0; i < state.ds_class_format.size(); i++) {
        const vk::Format f = dsmodel::select_host_format(static_cast<dsmodel::DsClass>(i), ds_support);
        // nothing usable for this class (stencil class on a device without stencil): use the combined format
        state.ds_class_format[i] = f == vk::Format::eUndefined ? state.deep_stencil_use : f;
        LOG_INFO("Depth/stencil class {} -> {}", i, vk::to_string(state.ds_class_format[i]));
    }

    // Probe the host format of every colour surface format (attachment, sampling, blending), see colourmodel::select_host_format
    {
        std::array<std::pair<vk::Format, colourmodel::FormatSupport>, 24> probed{};
        size_t probed_count = 0;
        const auto supports = [&](const vk::Format format) {
            for (size_t i = 0; i < probed_count; i++)
                if (probed[i].first == format)
                    return probed[i].second;
            const vk::FormatFeatureFlags f = state.physical_device.getFormatProperties(format).optimalTilingFeatures;
            colourmodel::FormatSupport s;
            s.attach = (f & vk::FormatFeatureFlagBits::eColorAttachment) && (f & vk::FormatFeatureFlagBits::eSampledImage);
            s.blend = s.attach && (f & vk::FormatFeatureFlagBits::eColorAttachmentBlend);
            if (probed_count < probed.size())
                probed[probed_count++] = { format, s };
            return s;
        };
        for (size_t i = 0; i < colourmodel::fmt_count; i++) {
            const auto entry = colourmodel::select_host_format(static_cast<colourmodel::Fmt>(i), supports);
            state.gpu_caps.colour_format[i] = { static_cast<uint32_t>(entry.format), static_cast<uint8_t>(entry.repack), entry.blend_ok, entry.lossy, entry.forced };
            const colourmodel::Chain &chain = colourmodel::chain_of(static_cast<colourmodel::Fmt>(i));
            if (entry.format != chain.list[0].format || !entry.blend_ok || entry.forced)
                LOG_WARN("Colour surface format {}: host {}{}{}{}", i, vk::to_string(entry.format), entry.blend_ok ? "" : ", no blending on this format",
                    entry.lossy ? ", reduced precision" : "", entry.forced ? ", NOT supported by the device (primary format forced)" : "");
        }
    }

    // the swapchain below picks its present mode from the configured v-sync
    if (const int initial_vsync = state.pending_vsync.exchange(-1, std::memory_order_relaxed); initial_vsync >= 0)
        vsync_enabled = initial_vsync != 0;

    create_render_pass();

    if (!create_swapchain())
        return false;

    // these functions do not need to be called when the swapchain is resized
    create_surface_image();

    filter = std::make_unique<BilinearScreenFilter>(*this);
    filter->init();
    emucorev::shaders::registerScreenRenderer(this);

    start_present_thread();
    return true;
}

void ScreenRenderer::select_present_mode() {
    // v-sync on (the default): FIFO, one image per panel refresh, never drops or tears, which is what
    // an even frame cadence needs. v-sync off: the best mode that does not tear (mailbox), then the rest.
    frame_pacing::PresentModeSupport support;
    for (const auto &mode : state.physical_device.getSurfacePresentModesKHR(surface)) {
        support.mailbox |= mode == vk::PresentModeKHR::eMailbox;
        support.immediate |= mode == vk::PresentModeKHR::eImmediate;
        support.fifo_relaxed |= mode == vk::PresentModeKHR::eFifoRelaxed;
    }
    const vk::PresentModeKHR previous = present_mode;
#ifdef __ANDROID__
    constexpr bool allow_immediate = false; // IMMEDIATE exhausts the BufferQueue on Android
#else
    constexpr bool allow_immediate = true;
#endif
    switch (frame_pacing::choose_present_mode(vsync_enabled, support, allow_immediate)) {
    case frame_pacing::PresentMode::Mailbox: present_mode = vk::PresentModeKHR::eMailbox; break;
    case frame_pacing::PresentMode::Immediate: present_mode = vk::PresentModeKHR::eImmediate; break;
    case frame_pacing::PresentMode::FifoRelaxed: present_mode = vk::PresentModeKHR::eFifoRelaxed; break;
    case frame_pacing::PresentMode::Fifo: present_mode = vk::PresentModeKHR::eFifo; break;
    }
    if (present_mode != previous)
        LOG_INFO("Present mode: {} (v-sync {})", vk::to_string(present_mode), vsync_enabled ? "on" : "off");
}

bool ScreenRenderer::create_swapchain() {
    select_present_mode();
    surface_capabilities = state.physical_device.getSurfaceCapabilitiesKHR(surface);
    SwapchainCapabilitiesSelection capabilities_selection;
    if (!select_swapchain_capabilities(surface_capabilities, !state.is_adreno_turnip, capabilities_selection)) {
        LOG_ERROR("Vulkan surface has no supported swapchain capability combination");
        return false;
    }

    if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = surface_capabilities.currentExtent;
    } else {
        auto *frame_host = static_cast<renderer::State &>(state).frame;
        extent.width = std::clamp<uint32_t>(static_cast<uint32_t>(frame_host->drawable_width()), surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width);
        extent.height = std::clamp<uint32_t>(static_cast<uint32_t>(frame_host->drawable_height()), surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0)
        return false;

    swapchain_size = surface_capabilities.minImageCount + 1;
    if (surface_capabilities.maxImageCount != 0)
        swapchain_size = std::min(swapchain_size, surface_capabilities.maxImageCount);

    // Create Swapchain
    {
        const vk::ImageUsageFlags surface_usage = capabilities_selection.image_usage;
        support_swapchain_transfer_dst = capabilities_selection.transfer_dst;
        swapchain_has_storage = capabilities_selection.storage;

        LOG_INFO("swapchain: format={} colorspace={} extent={}x{} supportedUsage=0x{:X} chosenUsage=0x{:X} images={} currentTransform={} usedTransform={}",
            vk::to_string(surface_format.format), vk::to_string(surface_format.colorSpace), extent.width, extent.height,
            static_cast<uint32_t>(surface_capabilities.supportedUsageFlags), static_cast<uint32_t>(surface_usage),
            swapchain_size, vk::to_string(surface_capabilities.currentTransform),
            vk::to_string(capabilities_selection.pre_transform));

        vk::SwapchainCreateInfoKHR swapchain_info{
            .surface = surface,
            .minImageCount = swapchain_size,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = surface_usage,
            .imageSharingMode = vk::SharingMode::eExclusive,
            .preTransform = capabilities_selection.pre_transform,
            .compositeAlpha = capabilities_selection.composite_alpha,
            .presentMode = present_mode,
            .clipped = true,
        };

        swapchain = state.device.createSwapchainKHR(swapchain_info);
    }

    // Get Swapchain Images
    swapchain_images = state.device.getSwapchainImagesKHR(swapchain);
    swapchain_size = swapchain_images.size();

    // Get Image views
    swapchain_views.resize(swapchain_size);
    for (uint32_t i = 0; i < swapchain_size; i++) {
        vk::ImageViewCreateInfo view_info{
            .image = swapchain_images[i],
            .viewType = vk::ImageViewType::e2D,
            .format = surface_format.format,
            .components = vkutil::default_comp_mapping,
            .subresourceRange = vkutil::color_subresource_range
        };

        swapchain_views[i] = state.device.createImageView(view_info);
    }

    swapchain_framebuffers.resize(swapchain_size);
    for (uint32_t i = 0; i < swapchain_size; i++) {
        vk::FramebufferCreateInfo fb_info{
            .renderPass = default_render_pass,
            .width = extent.width,
            .height = extent.height,
            .layers = 1
        };
        fb_info.setAttachments(swapchain_views[i]);

        swapchain_framebuffers[i] = state.device.createFramebuffer(fb_info);
    }

    if (filter) {
        if (command_buffers.size() < swapchain_size) {
            // if the swapchain size increased, we need to reset the filter
            std::string filter_name{ filter->get_name() };
            filter.reset();
            set_filter(filter_name);
        } else {
            filter->on_resize();
        }
    }

    create_layout_sync();
    return true;
}

void ScreenRenderer::destroy_swapchain() {
    for (vk::Framebuffer framebuffer : swapchain_framebuffers)
        state.device.destroy(framebuffer);
    swapchain_framebuffers.clear();

    for (vk::ImageView view : swapchain_views)
        state.device.destroy(view);
    swapchain_views.clear();

    if (swapchain) {
        state.device.destroySwapchainKHR(swapchain);
        swapchain = nullptr;
    }
}

void ScreenRenderer::cleanup() {
    emucorev::shaders::unregisterScreenRenderer(this);
    stop_present_thread();
    state.wait_device_idle();

    shader_chain.destroy();
    filter.reset();

    for (auto &img : vita_surface)
        img.destroy();
    vita_surface.clear();

    if (vita_surface_staging) {
        state.allocator.destroyBuffer(vita_surface_staging, vita_surface_staging_alloc);
        vita_surface_staging = nullptr;
    }

    for (vk::Framebuffer fb : swapchain_framebuffers)
        state.device.destroy(fb);
    swapchain_framebuffers.clear();

    state.device.destroy(default_render_pass);
    default_render_pass = nullptr;
    state.device.destroy(post_filter_render_pass);
    post_filter_render_pass = nullptr;

#ifdef __ANDROID__
    state.device.destroy(stock_adreno_pass);
    stock_adreno_pass = nullptr;
#endif

    for (vk::ImageView view : swapchain_views)
        state.device.destroy(view);
    swapchain_views.clear();

    state.device.destroy(swapchain);
    swapchain = nullptr;

    for (vk::Fence fence : fences) {
        if (fence)
            state.device.destroy(fence);
    }
    fences.clear();

    for (vk::Semaphore semaphore : image_acquired_semaphores) {
        if (semaphore)
            state.device.destroy(semaphore);
    }
    image_acquired_semaphores.clear();

    for (vk::Semaphore semaphore : image_ready_semaphores) {
        if (semaphore)
            state.device.destroy(semaphore);
    }
    image_ready_semaphores.clear();

    command_buffers.clear();

#ifdef __ANDROID__
    SDL_Vulkan_DestroySurface(state.instance, surface, nullptr);
#else
    state.instance.destroy(surface);
#endif
    surface = nullptr;
}

// Bounded so a stalled presentation engine cannot block the render thread forever: the waits below
// are retried until they succeed or the session is aborted.
static constexpr uint64_t next_image_timeout = 100'000'000ULL; // 100 ms

bool ScreenRenderer::acquire_swapchain_image() {
    if (!window_has_drawable_size(state)) {
        need_rebuild = true;
        current_cmd_buffer = nullptr;
        swapchain_image_idx = 0xDEADBEAF;
        return false;
    }

    vk::Result acquire_result = vk::Result::eErrorOutOfDateKHR;

    // vkAcquireNextImageKHR and vkQueuePresentKHR both need the swapchain externally synchronised, and
    // with minImageCount + 1 images the next image may only become available once the previous one is
    // presented: the previous present finishes first. The present thread overlaps it with everything
    // the render thread does between swap_window() and this acquire (the next guest frame).
    drain_present();
    // outcome of the last present made by the present thread
    if (const int async_result = present_async_result.exchange(0, std::memory_order_acquire); async_result != 0)
        handle_present_result(static_cast<vk::Result>(async_result));

    current_frame++;
    if (current_frame == swapchain_size + 1)
        current_frame = 0;

    if (!ensure_swapchain()) {
        current_cmd_buffer = nullptr;
        swapchain_image_idx = 0xDEADBEAF;
        return false;
    }

    if (swapchain) {
        do {
            acquire_result = state.device.acquireNextImageKHR(swapchain,
                next_image_timeout, image_acquired_semaphores[current_frame], vk::Fence(), &swapchain_image_idx);
            // a timeout leaves the semaphore unsignaled, so retrying with it is valid
        } while (acquire_result == vk::Result::eTimeout && !state.render_abort.load(std::memory_order_relaxed));
    }

    const bool has_acquired_image = acquire_result == vk::Result::eSuccess || acquire_result == vk::Result::eSuboptimalKHR;
    if (!has_acquired_image) {
        if (acquire_result == vk::Result::eTimeout || acquire_result == vk::Result::eNotReady) {
            current_cmd_buffer = nullptr;
            swapchain_image_idx = 0xDEADBEAF;
            return false;
        }
        if (acquire_result == vk::Result::eErrorOutOfDateKHR
            || acquire_result == vk::Result::eErrorSurfaceLostKHR) {
            need_rebuild = true;
            rebuild_reason = acquire_result == vk::Result::eErrorOutOfDateKHR ? "acquire: out of date" : "acquire: surface lost";
            need_surface_recreate = acquire_result == vk::Result::eErrorSurfaceLostKHR;
        } else {
            LOG_WARN("Failed to get next image. Error: {}", vk::to_string(acquire_result));
        }

        current_cmd_buffer = nullptr;
        swapchain_image_idx = 0xDEADBEAF;
        return false;
    }

    if (acquire_result == vk::Result::eSuboptimalKHR && note_size_mismatch(!surface_matches_window_size())) {
        need_rebuild = true;
        rebuild_reason = "suboptimal + persistent size mismatch";
    }

    // wait for the previous frame using this image to finish
    vk::Result result;
    do {
        result = state.device.waitForFences(fences[swapchain_image_idx], VK_TRUE, next_image_timeout);
    } while (result == vk::Result::eTimeout && !state.render_abort.load(std::memory_order_relaxed));
    if (result != vk::Result::eSuccess) {
        if (result != vk::Result::eTimeout)
            LOG_ERROR("Could not wait for fences: {}", vk::to_string(result));
        // The image was acquired and its semaphore is signaled but will never be waited on. Reusing that
        // semaphore (or the frame index) would signal it twice, so drop the whole swapchain: it is
        // rebuilt (images and semaphores recreated) before the next acquire.
        need_rebuild = true;
        rebuild_reason = "acquire: fence wait failed";
        current_cmd_buffer = nullptr;
        swapchain_image_idx = 0xDEADBEAF;
        return false;
    }
    state.device.resetFences(fences[swapchain_image_idx]);

    // begin the render command
    current_cmd_buffer = command_buffers[swapchain_image_idx];
    current_cmd_buffer.reset();

    {
        vk::CommandBufferBeginInfo begin_info{
            .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
        };
        current_cmd_buffer.begin(begin_info);
    }

    return true;
}

void ScreenRenderer::begin_default_render_pass() {
    vk::RenderPassBeginInfo pass_info{
        .renderPass = default_render_pass,
        .framebuffer = swapchain_framebuffers[swapchain_image_idx],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = extent }
    };
    vk::ClearValue clear_color{
        .color = { std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    pass_info.setClearValues(clear_color);
    current_cmd_buffer.beginRenderPass(pass_info, vk::SubpassContents::eInline);
}

void ScreenRenderer::render(vk::ImageView image_view, vk::ImageLayout layout, const Viewport &viewport,
    const emucorev::shaders::ShaderChainImage *shader_source) {
    if (swapchain_image_idx == ~0 && !acquire_swapchain_image())
        return;

    std::string shader_error;
    const bool shader_chain_ready = shader_chain.applyPending(
        static_cast<VkInstance>(state.instance),
        static_cast<VkPhysicalDevice>(state.physical_device),
        static_cast<VkDevice>(state.device),
        static_cast<VkQueue>(state.general_queue),
        VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
        [this] { state.wait_device_idle(); },
        shader_error);
    if (!shader_error.empty())
        LOG_ERROR("RetroArch shader preset could not be loaded: {}", shader_error);

    const bool source_is_full_rgba_frame = shader_source && shader_source->image
        && shader_source->format == VK_FORMAT_R8G8B8A8_UNORM
        && viewport.offset_x == 0 && viewport.offset_y == 0
        && viewport.width == viewport.texture_width && viewport.height == viewport.texture_height;
    if (shader_chain_ready && source_is_full_rgba_frame) {
        // Librashader records the complete multi-pass chain outside a render pass. Clear
        // first so aspect-preserving output retains the same black letterbox as built-in
        // filters, then provide the acquired swapchain image as the final pass target.
        begin_default_render_pass();
        current_cmd_buffer.endRenderPass();

        const vk::Image source_image{ shader_source->image };
        const vk::Image target_image = swapchain_images[swapchain_image_idx];
        const auto source_read_layout = vk::ImageLayout::eShaderReadOnlyOptimal;
        namespace sync = vkutil::sync;
        const vk::ImageSubresourceRange whole_color{ vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 };
        // the source image is owned by the caller (its layout arrives as a parameter): explicit barrier through the emitter
        if (layout != source_read_layout) {
            const bool general = layout == vk::ImageLayout::eGeneral;
            sync::explicit_barrier(current_cmd_buffer, source_image, whole_color,
                { general ? sync::S::eAllCommands : sync::Stage{}, sync::S::eFragmentShader,
                    general ? sync::A::eMemoryWrite | sync::A::eMemoryRead : sync::Access{}, sync::A::eShaderRead, layout, source_read_layout, true });
        }

        sync::explicit_barrier(current_cmd_buffer, target_image, whole_color,
            { sync::Stage{}, sync::S::eColorAttachmentOutput, sync::Access{}, sync::A::eColorAttachmentRead | sync::A::eColorAttachmentWrite,
                vk::ImageLayout::ePresentSrcKHR, vk::ImageLayout::eColorAttachmentOptimal, true });

        const vk::Viewport viewport_rect = display_viewport();
        emucorev::shaders::ShaderChainImage target{
            static_cast<VkImage>(target_image),
            static_cast<VkFormat>(surface_format.format),
            extent.width,
            extent.height
        };
        const emucorev::shaders::ShaderChainViewport output_view{
            viewport_rect.x,
            viewport_rect.y,
            static_cast<uint32_t>(std::lround(viewport_rect.width)),
            static_cast<uint32_t>(std::lround(viewport_rect.height))
        };
        const bool shader_rendered = shader_chain.frame(
            static_cast<VkCommandBuffer>(current_cmd_buffer), *shader_source, target, output_view, shader_error);

        if (layout != source_read_layout) {
            const bool general = layout == vk::ImageLayout::eGeneral;
            sync::explicit_barrier(current_cmd_buffer, source_image, whole_color,
                { sync::S::eFragmentShader, general ? sync::S::eAllCommands : sync::Stage{}, sync::A::eShaderRead,
                    general ? sync::A::eMemoryRead | sync::A::eMemoryWrite : sync::Access{}, source_read_layout, layout, true });
        }

        if (shader_rendered) {
            sync::explicit_barrier(current_cmd_buffer, target_image, whole_color,
                { sync::S::eColorAttachmentOutput, sync::S::eColorAttachmentOutput, sync::A::eColorAttachmentWrite,
                    sync::A::eColorAttachmentRead | sync::A::eColorAttachmentWrite, vk::ImageLayout::eColorAttachmentOptimal,
                    vk::ImageLayout::eGeneral, true });
            vk::RenderPassBeginInfo pass_info{
                .renderPass = post_filter_render_pass,
                .framebuffer = swapchain_framebuffers[swapchain_image_idx],
                .renderArea = { .offset = { 0, 0 }, .extent = extent }
            };
            current_cmd_buffer.beginRenderPass(pass_info, vk::SubpassContents::eInline);
        } else {
            LOG_ERROR("RetroArch shader frame failed; using the selected built-in screen filter: {}", shader_error);
            begin_default_render_pass();
            filter->render(false, image_view, layout, viewport);
        }

#ifdef __ANDROID__
        const bool keep_single_pass = state.mapping_method == MappingMethod::PageTable
            || state.mapping_method == MappingMethod::NativeBuffer;
        if (state.is_adreno_stock && !keep_single_pass) {
            current_cmd_buffer.endRenderPass();
            vk::RenderPassBeginInfo stock_pass_info{
                .renderPass = stock_adreno_pass,
                .framebuffer = swapchain_framebuffers[swapchain_image_idx],
                .renderArea = { .offset = { 0, 0 }, .extent = extent }
            };
            current_cmd_buffer.beginRenderPass(stock_pass_info, vk::SubpassContents::eInline);
        }
#endif
        return;
    }

    // we need to apply the screen filter at the right moment (before or after we start the render pass depending on it)
    filter->render(true, image_view, layout, viewport);

    const auto render_pass = filter->need_post_processing_render_pass() ? post_filter_render_pass : default_render_pass;
    vk::RenderPassBeginInfo pass_info{
        .renderPass = render_pass,
        .framebuffer = swapchain_framebuffers[swapchain_image_idx],
        .renderArea = {
            .offset = { 0, 0 },
            .extent = extent }
    };
    vk::ClearValue clear_color{
        .color = { std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f } }
    };
    pass_info.setClearValues(clear_color);
    current_cmd_buffer.beginRenderPass(pass_info, vk::SubpassContents::eInline);

    filter->render(false, image_view, layout, viewport);

#ifdef __ANDROID__
    // stock adreno driver bug
    // if there is too much load on the GPU, it just drops any render pass with ImGui graphics in it....
    // I still don't know exactly why
    // so as a partial fix, render the gui and screen in different render passes
    const bool keep_single_pass = state.mapping_method == MappingMethod::PageTable
        || state.mapping_method == MappingMethod::NativeBuffer;
    if (state.is_adreno_stock && !keep_single_pass) {
        current_cmd_buffer.endRenderPass();
        pass_info.renderPass = stock_adreno_pass;
        current_cmd_buffer.beginRenderPass(pass_info, vk::SubpassContents::eInline);
    }
#endif
}

vk::Viewport ScreenRenderer::display_viewport() const {
    vk::Viewport viewport{};
    const float window_aspect = static_cast<float>(extent.width) / extent.height;
    constexpr float vita_aspect = static_cast<float>(DEFAULT_RES_WIDTH) / DEFAULT_RES_HEIGHT;
    const bool fullscreen_hd_res_pixel_perfect_en = state.fullscreen_hd_res_pixel_perfect
        && state.fullscreen && !(extent.width % DEFAULT_RES_WIDTH) && !(extent.height % (DEFAULT_RES_HEIGHT - 4));
    if (state.stretch_the_display_area && !fullscreen_hd_res_pixel_perfect_en) {
        viewport.width = static_cast<float>(extent.width);
        viewport.height = static_cast<float>(extent.height);
        viewport.x = 0.0f;
        viewport.y = 0.0f;
    } else if (window_aspect > vita_aspect && !fullscreen_hd_res_pixel_perfect_en) {
        viewport.width = extent.height * vita_aspect;
        viewport.height = static_cast<float>(extent.height);
        viewport.x = (extent.width - viewport.width) / 2.0f;
        viewport.y = 0.0f;
    } else {
        viewport.width = static_cast<float>(extent.width);
        viewport.height = extent.width / vita_aspect;
        viewport.x = 0.0f;
        viewport.y = (extent.height - viewport.height) / 2.0f;
    }
    return viewport;
}

bool ScreenRenderer::swap_window() {
    if (!current_cmd_buffer) {
        swapchain_image_idx = ~0;
        return false;
    }

    // first submit the command buffer. The submit always happens, even when the present below is dropped
    // (rebuild pending, failed present): the frame's GPU work and its fence never depend on the present.
    current_cmd_buffer.endRenderPass();
    current_cmd_buffer.end();
    vk::SubmitInfo submit_info{};
    std::array<vk::Semaphore, 1> wait_semaphores = { image_acquired_semaphores[current_frame] };
    std::array<vk::PipelineStageFlags, 1> dst_masks
        = { vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer };
    submit_info.setWaitSemaphores(wait_semaphores);
    submit_info.setWaitDstStageMask(dst_masks);
    submit_info.setSignalSemaphores(image_ready_semaphores[swapchain_image_idx]);
    submit_info.setCommandBuffers(current_cmd_buffer);
    state.general_queue.submit(submit_info, fences[swapchain_image_idx]);

    const PresentRequest request{ swapchain, swapchain_image_idx, image_ready_semaphores[swapchain_image_idx] };
    swapchain_image_idx = ~0;
    current_cmd_buffer = nullptr;

    if (present_thread.joinable()) {
        // hand the present to the present thread; its outcome is applied on the next acquire
        std::unique_lock<std::mutex> lock(present_mutex);
        present_cv.wait(lock, [this] { return !present_pending.has_value(); });
        present_pending = request;
        lock.unlock();
        present_cv.notify_all();
        return true;
    }
    return handle_present_result(present_now(request));
}

vk::Result ScreenRenderer::present_now(const PresentRequest &request) {
    vk::PresentInfoKHR present_info{
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &request.wait_semaphore,
        .swapchainCount = 1,
        .pSwapchains = &request.swapchain,
        .pImageIndices = &request.image_index,
    };
    const std::lock_guard<std::mutex> lock(state.present_queue_mutex);
    return state.present_queue.presentKHR(&present_info);
}

bool ScreenRenderer::handle_present_result(vk::Result result) {
    if (result == vk::Result::eSuboptimalKHR) {
        if (note_size_mismatch(!surface_matches_window_size())) {
            need_rebuild = true;
            rebuild_reason = "present: suboptimal + persistent size mismatch";
        }
    } else if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eErrorSurfaceLostKHR) {
        need_rebuild = true;
        rebuild_reason = result == vk::Result::eErrorOutOfDateKHR ? "present: out of date" : "present: surface lost";
        need_surface_recreate = result == vk::Result::eErrorSurfaceLostKHR;
    } else if (result != vk::Result::eSuccess) {
        LOG_ERROR("Could not present KHR.");
        return false;
    }
    return result == vk::Result::eSuccess || result == vk::Result::eSuboptimalKHR;
}

void ScreenRenderer::start_present_thread() {
    // Capability driven: a second queue of the general family exists. With a single queue every submit and
    // present would have to be serialised against the render thread, which buys nothing.
    if (present_thread.joinable())
        return;
    if (state.present_queue == state.general_queue) {
        LOG_WARN("Present: on the render thread (the general queue family has a single queue)");
        return;
    }
    present_quit = false;
    present_thread = std::thread([this] { present_thread_main(); });
    LOG_WARN("Present: dedicated present thread on queue 1 of family {}", state.general_family_index);
}

void ScreenRenderer::stop_present_thread() {
    if (!present_thread.joinable())
        return;
    drain_present();
    {
        const std::lock_guard<std::mutex> lock(present_mutex);
        present_quit = true;
    }
    present_cv.notify_all();
    present_thread.join();
}

void ScreenRenderer::drain_present() {
    if (!present_thread.joinable())
        return;
    std::unique_lock<std::mutex> lock(present_mutex);
    present_cv.wait(lock, [this] { return !present_pending.has_value() && !present_busy; });
}

void ScreenRenderer::present_thread_main() {
#ifdef __ANDROID__
    pthread_setname_np(pthread_self(), "ecv-present");
#endif
    std::unique_lock<std::mutex> lock(present_mutex);
    for (;;) {
        present_cv.wait(lock, [this] { return present_pending.has_value() || present_quit; });
        if (!present_pending.has_value())
            return;
        const PresentRequest request = *present_pending;
        present_pending.reset();
        present_busy = true;
        lock.unlock();
        present_cv.notify_all();

        const vk::Result result = present_now(request);
        if (result != vk::Result::eSuccess)
            present_async_result.store(static_cast<int>(result), std::memory_order_release);

        lock.lock();
        present_busy = false;
        present_cv.notify_all();
    }
}

void ScreenRenderer::set_filter(const std::string_view &filter) {
    if (this->filter && filter == this->filter->get_name())
        return;

    this->filter.reset();
    if (filter == "FSR")
        this->filter = std::make_unique<FSRScreenFilter>(*this);
    else if (filter == "FXAA")
        this->filter = std::make_unique<FXAAScreenFilter>(*this);
    else if (filter == "Bicubic")
        this->filter = std::make_unique<BicubicScreenFilter>(*this);
    else if (filter == "Nearest")
        this->filter = std::make_unique<NearestScreenFilter>(*this);
    else
        this->filter = std::make_unique<BilinearScreenFilter>(*this);

    this->filter->init();
}

void ScreenRenderer::set_retroarch_shader_preset(std::string absolute_path) {
    shader_chain.requestPreset(std::move(absolute_path));
}

void ScreenRenderer::set_retroarch_shader_parameter(std::string name, float value) {
    shader_chain.setParameter(std::move(name), value);
}

void ScreenRenderer::clear_retroarch_shader_parameters() {
    shader_chain.clearParameters();
}

std::string ScreenRenderer::retroarch_shader_error() const {
    return shader_chain.lastError();
}

void ScreenRenderer::create_layout_sync() {
    if (command_buffers.size() >= swapchain_size)
        return;

    const uint32_t previous_size = command_buffers.size();
    const uint32_t to_add = swapchain_size - previous_size;

    vk::CommandBufferAllocateInfo cmd_buffer_info{
        .commandPool = state.general_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = to_add
    };
    auto new_cmd_buffers = state.device.allocateCommandBuffers(cmd_buffer_info);
    command_buffers.insert(command_buffers.end(), new_cmd_buffers.begin(), new_cmd_buffers.end());

    // create fences (in signaled state) and semaphores
    vk::FenceCreateInfo fence_info{
        .flags = vk::FenceCreateFlagBits::eSignaled
    };
    fences.resize(swapchain_size);

    // add one more semaphore for synchronisation reasons
    image_acquired_semaphores.resize(swapchain_size + 1);
    image_ready_semaphores.resize(swapchain_size + 1);
    for (uint32_t i = previous_size; i <= swapchain_size; i++) {
        if (i != swapchain_size)
            fences[i] = state.device.createFence(fence_info);

        image_acquired_semaphores[i] = state.device.createSemaphore({});
        image_ready_semaphores[i] = state.device.createSemaphore({});
    }
}

void ScreenRenderer::create_render_pass() {
    vk::AttachmentDescription color_attachment{
        .format = surface_format.format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
        .initialLayout = vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::ePresentSrcKHR
    };
    vk::AttachmentReference attachment_ref{
        .attachment = 0,
        .layout = vk::ImageLayout::eColorAttachmentOptimal
    };
    vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics
    };
    subpass.setColorAttachments(attachment_ref);

    vk::SubpassDependency dependency{
        .srcSubpass = VK_SUBPASS_EXTERNAL,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eComputeShader,
        .dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .srcAccessMask = vk::AccessFlags(),
        // don't forget blending
        .dstAccessMask = vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite
    };

    vk::RenderPassCreateInfo pass_info{};
    pass_info.setAttachments(color_attachment);
    pass_info.setSubpasses(subpass);
    pass_info.setDependencies(dependency);

    default_render_pass = state.device.createRenderPass(pass_info);

    // renderpass after post processing filter
    color_attachment
        .setLoadOp(vk::AttachmentLoadOp::eLoad)
        .setInitialLayout(vk::ImageLayout::eGeneral);
    post_filter_render_pass = state.device.createRenderPass(pass_info);

    if (state.is_adreno_stock) {
        // used to fix an adreno driver bug
        color_attachment.setInitialLayout(vk::ImageLayout::ePresentSrcKHR);
        stock_adreno_pass = state.device.createRenderPass(pass_info);
    }
}

void ScreenRenderer::create_surface_image() {
    vita_surface.resize(swapchain_size);

    vk::BufferCreateInfo buffer_info{
        // make sure it is big enough
        .size = 1024 * 720 * sizeof(uint32_t),
        .usage = vk::BufferUsageFlagBits::eTransferSrc,
        .sharingMode = vk::SharingMode::eExclusive
    };
    std::tie(vita_surface_staging, vita_surface_staging_alloc) = state.allocator.createBuffer(buffer_info, vkutil::vma_mapped_alloc, vita_surface_staging_info);
}

bool ScreenRenderer::ensure_swapchain() {
    if (!window_has_drawable_size(state))
        return false;

    // The v-sync setting can change while the game runs; the present mode is a swapchain property.
    const int pending_vsync = state.pending_vsync.exchange(-1, std::memory_order_relaxed);
    if (pending_vsync >= 0 && (pending_vsync != 0) != vsync_enabled) {
        vsync_enabled = pending_vsync != 0;
        need_rebuild = true;
        rebuild_reason = "v-sync setting changed";
    }

    if (!need_rebuild && !need_surface_recreate && swapchain) {
        // A one-frame disagreement between what Vulkan reports as the surface extent and what the window reports as its client size is not a resize
        if (!note_size_mismatch(!surface_matches_window_size()))
            return true;
        rebuild_reason = "persistent window size mismatch";
    }

    if (!rebuild_swapchain_if_visible()) {
        need_rebuild = true;
        return false;
    }

    need_rebuild = false;
    need_surface_recreate = false;
    return true;
}

bool ScreenRenderer::note_size_mismatch(bool mismatched) {
    if (!mismatched) {
        size_mismatch_frames = 0;
        return false;
    }
    return ++size_mismatch_frames >= 4;
}

bool ScreenRenderer::rebuild_swapchain_if_visible() {
    if (!window_has_drawable_size(state))
        return false;

    // This is the most expensive thing the emulator does outside loading
    // it idles the device, which stalls the vblank thread too
    drain_present();
    state.wait_device_idle();
    destroy_swapchain();

#ifdef __ANDROID__
    if (!create())
        return false;
#else
    if (need_surface_recreate && !create())
        return false;
#endif

    if (!create_swapchain())
        return false;
    size_mismatch_frames = 0;
    return static_cast<bool>(swapchain);
}

bool ScreenRenderer::surface_matches_window_size() {
    auto *frame_host = static_cast<renderer::State &>(state).frame;
    if (frame_host->drawable_width() == 0 || frame_host->drawable_height() == 0)
        return true;

    return extent.width == static_cast<uint32_t>(frame_host->drawable_width())
        && extent.height == static_cast<uint32_t>(frame_host->drawable_height());
}

} // namespace renderer::vulkan
