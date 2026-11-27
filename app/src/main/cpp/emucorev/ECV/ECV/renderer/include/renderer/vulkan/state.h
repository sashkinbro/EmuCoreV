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

#include <renderer/state.h>
#include <renderer/types.h>

#include <renderer/vulkan/colour_format_model.h>
#include <renderer/vulkan/depth_stencil_model.h>
#include <renderer/vulkan/gpu_caps.h>
#include <renderer/vulkan/msaa.h>
#include <renderer/vulkan/overlay_renderer.h>
#include <renderer/vulkan/pipeline_cache.h>
#include <renderer/vulkan/sampler_support.h>
#include <renderer/vulkan/screen_renderer.h>
#include <renderer/vulkan/surface_cache.h>
#include <renderer/vulkan/types.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>

struct Config;

namespace renderer::vulkan {

enum class LinuxSurfaceType {
    Unknown,
    Wayland,
    Xlib,
    Xcb
};

struct Viewport {
    uint32_t offset_x;
    uint32_t offset_y;
    uint32_t width;
    uint32_t height;
    uint32_t texture_width;
    uint32_t texture_height;
};

struct VKState : public renderer::State {
    MemState *mem;

    // 0 = automatic, > 0 = order in instance.enumeratePhysicalDevices
    int gpu_idx;

    VKSurfaceCache surface_cache;
    MsaaState msaa_state;
    PipelineCache pipeline_cache;
    VKTextureCache texture_cache;

    vk::Instance instance;
    vk::Device device;
    bool support_device_fault = false;
    // capability layer + the path table chosen from it at device creation (gpu_caps.h)
    GpuCaps gpu_caps;
    Strategy gpu_strategy;
    // RenderPath::DynamicRendering: scenes use vkCmdBeginRendering (+ local read), no VkRenderPass/VkFramebuffer objects
    bool use_dynamic_rendering = false;
    // StrategyL::extended_dynamic_state: the cull mode is dynamic state set per draw (pipeline_cache.cpp, scene.cpp)
    bool use_extended_dynamic_state = false;
    // FbFetchPath::CopySnapshot: framebuffer-fetch draws read a snapshot copy of the attachment (set from the strategy or the start-up self-test verdict)
    bool use_copy_snapshot = false;
    // SubmitTracking::TimelineSemaphore: every scene submit signals ++submit_counter (render thread only); null when fences are used
    vk::Semaphore submit_timeline = nullptr;
    uint64_t submit_counter = 0;

    ScreenRenderer screen_renderer;
    OverlayRenderer overlay_renderer;

    // Used for memory allocation and general query later.
    vk::PhysicalDevice physical_device;
    vk::PhysicalDeviceProperties physical_device_properties;
    vk::PhysicalDeviceFeatures physical_device_features;
    vk::PhysicalDeviceMemoryProperties physical_device_memory;
    std::vector<vk::QueueFamilyProperties> physical_device_queue_families;
    // combined depth+stencil format for attachments with no guest surface behind them (and the fallback of everything else)
    vk::Format deep_stencil_use;
    // host format of a GXM colour base format as probed at start up (GpuCaps::colour_format, P7); nothing for a format the renderer does not render to,
    // the legacy mapping while the probe has not run
    std::optional<colourmodel::SurfaceFormatTable::Entry> colour_entry(const SceGxmColorBaseFormat base) const {
        const colourmodel::Fmt f = colourmodel::fmt_of(base);
        if (f == colourmodel::Fmt::Count)
            return std::nullopt;
        const GpuCaps::ColourFormatCap &cap = gpu_caps.colour_format[static_cast<size_t>(f)];
        if (cap.host_format == 0)
            return colourmodel::SurfaceFormatTable()[f];
        return colourmodel::SurfaceFormatTable::Entry{ static_cast<vk::Format>(cap.host_format), static_cast<colourmodel::Repack>(cap.repack), cap.blend_ok, cap.lossy, cap.forced };
    }
    // host format of each GXM depth/stencil class, chosen from the probed format features (screen_renderer.cpp setup)
    std::array<vk::Format, static_cast<size_t>(dsmodel::DsClass::Count)> ds_class_format{};

    vk::Format ds_format_for(dsmodel::DsClass c) const {
        const vk::Format f = ds_class_format[static_cast<size_t>(c)];
        return f == vk::Format::eUndefined ? deep_stencil_use : f;
    }

    vma::Allocator allocator;

    uint32_t general_family_index = 0;
    uint32_t transfer_family_index = 0;
    uint32_t general_queue_last = 0;
    uint32_t transfer_queue_last = 0;
    vk::Queue general_queue;
    vk::Queue transfer_queue;
    // Second queue of the general family when the device exposes one: the present thread presents on it
    // so a blocking vkQueuePresentKHR never holds up submissions. Equals general_queue when the family has a
    // single queue (the screen renderer then presents on the render thread).
    vk::Queue present_queue;
    // Held by the present thread around vkQueuePresentKHR and by wait_device_idle(): vkDeviceWaitIdle
    // needs every queue externally synchronised.
    std::mutex present_queue_mutex;
    void wait_device_idle();

    // These might be merged into one queue, but for now they are different.
    vk::CommandPool general_command_pool;
    // Transfer pool has transient bit set.
    vk::CommandPool transfer_command_pool;
    // command pool which can be used from multiple thread
    vk::CommandPool multithread_command_pool;
    std::mutex multithread_pool_mutex;

    // objects for which one copy is needed for every frame being rendered at the same time
    std::array<FrameObject, MAX_FRAMES_RENDERING> frames;
    // start at 1 because last_frame_waited is set to 0
    int current_frame_idx = 1;
    // Monotonic epoch for shared frame-slot pool resets; context frame counters are local.
    uint64_t frame_epoch = 1;

    // vector of descriptor pools used for the frame descriptor, they are not really used anywhere
    // but it's better to keep a reference to them somewhere
    std::deque<vk::DescriptorPool> frame_descriptor_pools;

    // only used when memory mapping is enabled
    std::map<Address, MappedMemory, std::greater<Address>> mapped_memories;

#ifdef __ANDROID__
    struct CachedNativeBuffer {
        MappedMemory mapping;
        void *mapped_location;
    };
    std::unordered_map<uint64_t, CachedNativeBuffer> native_buffer_cache;
    uint64_t native_buffer_cache_bytes = 0;
    void release_cached_native_buffer(CachedNativeBuffer &cached);
    void trim_native_buffer_cache(uint64_t budget);
#endif
    // used with double buffer memory trapping
    BufferTrapping buffer_trapping;
    // modify the behavior of trapping on vertex buffers if there are shader stores
    bool has_shader_store = false;

    // queue where we put requests that need to wait for the GPU
    Queue<WaitThreadRequest> request_queue;
    std::atomic<size_t> gpu_request_reader_count{ 0 };

    // fatal GPU error: stop accepting GPU wait requests so waiters are released
    void abort_backend_queues() override {
        request_queue.abort();
    }

    vkutil::Image default_image;
    vkutil::Image default_raw_image;
    vkutil::Buffer default_buffer;

    // mirror-clamp / YCbCr capabilities and sampler limits chosen at device creation (P4/P6)
    SamplerDeviceSupport sampler_device_support;
    renderer::SamplerSupport sampler_support;

    bool support_fsr = false;
    // support for the VK_KHR_uniform_buffer_standard_layout extension, needed for memory mapping and texture viewport
    bool support_standard_layout = false;
    bool support_rasterized_order_access = false;
    // bound guest write-backs to the render-target extent the surface was rendered with
    bool surface_sync_clamp_rt = true;
    bool eager_surface_sync = false;
    LinuxSurfaceType linux_surface_type = LinuxSurfaceType::Unknown;

#ifdef __ANDROID__
    bool support_android_buffer_import = false;
    bool support_unix_fd_import = false;
#endif

    VKState(int gpu_idx);

    bool init() override;
    bool create(std::unique_ptr<renderer::State> &state, const Config &config);
    void late_init(const Config &cfg, const std::string_view game_id, MemState &mem) override;
    void cleanup() override;

    TextureCache *get_texture_cache() override {
        return &texture_cache;
    }

    void render_frame(DisplayState &display, const GxmState &gxm, MemState &mem) override;
    void swap_window() override;
    FirstPresentReadiness::Ticket restore_presentation_ticket;
    std::vector<uint32_t> dump_frame(DisplayState &display, uint32_t &width, uint32_t &height) override;

    uint32_t get_features_mask() override;
    int get_supported_filters() override;
    void set_screen_filter(const std::string_view &filter) override;
    int get_max_anisotropic_filtering() override;
    void set_anisotropic_filtering(int anisotropic_filtering) override;
    int get_max_2d_texture_width() override;
    void set_async_compilation(bool enable) override;

    bool map_memory(MemState &mem, Ptr<void> address, uint32_t size) override;
    bool map_memory_page_table_fallback(MemState &mem, Ptr<void> address, uint32_t size);
    void unmap_memory(MemState &mem, Ptr<void> address) override;
    void wait_gpu_idle() override;
    void reset_caches() override;
    // Save-state teardown/reconstruction; no new GPU work may be submitted.
    bool prepare_state_restore_teardown(std::string &error);
    bool reset_frame_runtime_for_restore(std::string &error);
    void flush_surfaces(MemState &mem) override;
    // return the matching buffer and offset for the memory location
    std::tuple<vk::Buffer, uint32_t> get_matching_mapping(const Ptr<void> address);
    // return the GPU buffer device address matching this one
    uint64_t get_matching_device_address(const Address address);
#ifdef __ANDROID__
    bool support_custom_drivers() override;
    void set_turbo_mode(bool set) override;
#endif
    std::string_view get_gpu_name() override;
    uint32_t get_gpu_version() override;

    void precompile_shader(const ShadersHash &hash) override;
    void preclose_action() override;

    // dumps the resolved device/driver/feature configuration to the log, once, from late_init
    void log_gpu_configuration(const Config &cfg);
    void log_device_fault();

    inline FrameObject &frame() {
        return frames[current_frame_idx];
    }
};
} // namespace renderer::vulkan
