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

#include <gxm/types.h>
#include <mem/ptr.h>
#include <renderer/gxm_types.h>
#include <util/containers.h>
#include <vkutil/objects.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <optional>

struct SwsContext;

namespace renderer::vulkan {

struct VKContext;
struct VKRenderTarget;
struct VKState;
struct Viewport;
using CallbackRequestFunction = std::function<void()>;

// used for in-shader texture viewport
struct TextureViewport {
    std::pair<float, float> ratio = { 1.0f, 1.0f };
    std::pair<float, float> offset = { 0.0f, 0.0f };
};

enum struct SurfaceTiling {
    Linear,
    Swizzled,
    Tiled
};

extern std::atomic<uint32_t> f10_sync_count;
extern std::atomic<uint32_t> f10_skip_count;
extern std::atomic<uint64_t> f10_repack_us;

struct SurfaceCacheInfo {
    vkutil::Image texture;
    SurfaceTiling tiling;
    // for d32s8 surfaces, this is the size of the depth part
    uint32_t total_bytes;
};

struct Framebuffer {
    // standard framebuffer, used most of the time
    vk::Framebuffer standard;
    // framebuffer used with shader interlock
    vk::Framebuffer shader_interlock;
    // base color image used by the framebuffer
    vkutil::Image *base_image;
    // framebuffer dimensions — the render area must never exceed them
    uint32_t width;
    uint32_t height;
    vkutil::Image *raw_image = nullptr;
};

struct CastedTexture {
    vkutil::Image texture;
    // only used if an image to image copy is not possible (cropped/partial reads)
    vkutil::Buffer transition_buffer;
    // storage view of the texture, used as the compute de-interleave output
    vk::ImageView reinterpret_view = nullptr;
    // view with the opposite gamma to the image's format (UNORM base <-> sRGB view)
    vk::ImageView alt_gamma_view = nullptr;
    uint64_t scene_timestamp = 0;
    uint32_t cropped_x = 0;
    uint32_t cropped_y = 0;
    uint32_t cropped_width = 0;
    uint32_t cropped_height = 0;
    SceGxmColorBaseFormat format;
};

struct ColorSurfaceCacheInfo : public SurfaceCacheInfo {
    uint16_t width;
    uint16_t height;
    uint16_t original_width;
    uint16_t original_height;
    uint32_t stride_bytes;
    uint64_t last_frame_rendered;
    uint64_t last_scene_rendered = 0;
    uint16_t rendered_w = 0;
    uint16_t rendered_h = 0;
    int32_t written_x0 = INT32_MAX;
    int32_t written_y0 = INT32_MAX;
    int32_t written_x1 = 0;
    int32_t written_y1 = 0;

    SceGxmColorBaseFormat format;
    vk::ComponentMapping swizzle;

    Ptr<void> data;
    std::vector<CastedTexture> casted_textures;
    // use a unique_ptr for the following objects as they may not be used

    // same image with a different view(swizzle) used for sampling
    vk::ImageView alternate_view = nullptr;

    // R32G32_UINT view of this surface, used as the raw-word input to the
    // typeless reinterpret compute pass. Only created if that path is taken.
    vk::ImageView reinterpret_store_view = nullptr;

    // only used when upscaling is enabled, to downscale the image first
    std::unique_ptr<vkutil::Image> blit_image;

    // parallel R16G16B16A16Uint image written by fragment shaders alongside the float image
    std::unique_ptr<vkutil::Image> raw_image;
    // whether any blending draw rendered into this surface
    bool content_is_blended = false;
    // which image the cached reinterpret_store_view was created on (it must follow the choice)
    bool reinterpret_view_is_raw = false;
    // the game has bound a cast view byte-addressed at a non-zero word of this store
    bool has_phase_view = false;

    // only used for 3-component rgb textures which can't be copied directly
    std::unique_ptr<vkutil::Buffer> copy_buffer;

    // staging buffer used to reload guest memory content into the surface image after the
    // game wrote the surface's memory with the CPU while the surface sat idle
    std::unique_ptr<vkutil::Buffer> upload_buffer;

    // pointer shared with the memory trap indicating if this surface sync is needed
    std::shared_ptr<bool> need_surface_sync;

    // pointer to decoder used for surface sync (if necessary)
    SwsContext *sws_context = nullptr;

    // do we need some CPU convert/unswizzling part for surface sync
    bool need_post_surface_sync = false;

    // repack-format surfaces (CPU-converted writeback) sync at most once per throttle window
    std::chrono::steady_clock::time_point last_repack_sync_time{};

    // only for double buffer, do we need to sync the two views?
    bool need_buffer_sync = false;

    // the surface is synced only for GPU raw buffer reads: keep the data in the mapped
    // GPU buffer but skip the write-back to guest RAM (a CPU-side write-back would trip the
    // surface's own dirty trap and force a full re-upload of the surface every frame)
    bool gpu_read_sync_only = false;
    std::shared_ptr<bool> dirty = std::make_shared<bool>(false);

    ColorSurfaceCacheInfo() = default;
    ~ColorSurfaceCacheInfo();
};

// set while the emulator itself writes surface data back to guest memory, so the
// surface write traps can tell emulator write-backs apart from genuine guest writes
extern thread_local bool surface_sync_internal_write;

struct DepthSurfaceView {
    vkutil::Image depth_view;
    // only contains an image view with the stencil aspect
    vkutil::Image stencil_view;
    // used so that we copy the depth stencil at most once per scene
    uint64_t scene_timestamp;
    uint32_t delta_col;
    uint32_t delta_row;
};

struct DepthStencilSurfaceCacheInfo : public SurfaceCacheInfo {
    SceGxmDepthStencilSurface surface;
    // dimensions of the depth buffer in memory
    int32_t memory_width;
    int32_t memory_height;
    // stride in samples
    uint32_t stride_samples;
    SceGxmMultisampleMode multisample_mode;

    bool depth_content_stored = true;
    Address last_scene_color_addr = 0;

    // used when reading from this depth stencil in a shader with texture viewport enabled
    vk::ImageView depth_view = nullptr;
    vk::ImageView stencil_view = nullptr;

    // used when texture viewport is not enabled
    std::vector<DepthSurfaceView> read_surfaces;

    // Resampled view of this surface for a pass that rasterises at a lower rate than the pass which filled it
    std::unique_ptr<vkutil::Image> sample_rate_copy;
};

// result when looking in the surface cache for a texture
struct TextureLookupResult {
    vk::ImageView view;
    vkutil::ImageLayout layout;
    vk::Format format;
    bool is_typeless_cast = false;
    bool cast_phase_hi = false;
    bool is_raw_bits = false;
};

// result when trying to retrieve a surface from the surface cache
struct SurfaceRetrieveResult {
    vk::ImageView view;
    vkutil::Image *base_image;
    vkutil::Image *raw_image = nullptr;
};

// for use with the surface_cast_reinterpret shader
struct ReinterpretPushConstants {
    uint32_t out_width;
    uint32_t out_height;
    uint32_t scaled_store_w;
    uint32_t scaled_store_h;
    uint32_t ratio;
    uint32_t half_index;
    uint32_t interleave;
};

class VKSurfaceCache {
private:
    VKState &state;

    // only have 20 color surfaces and 20 depth surfaces allocated at most at a given time
    static constexpr uint32_t max_surfaces_allowed = 20;

    std::map<Address, ColorSurfaceCacheInfo *> color_address_lookup;

    std::map<Address, DepthStencilSurfaceCacheInfo *> depth_address_lookup;
    std::map<Address, DepthStencilSurfaceCacheInfo *> stencil_address_lookup;

    // structure allowing to set the lru surface with a good complexity
    lru::Queue<ColorSurfaceCacheInfo> color_surface_queue;
    lru::Queue<DepthStencilSurfaceCacheInfo> ds_surface_queue;

    std::map<std::pair<vk::ImageView, vk::ImageView>, Framebuffer> framebuffer_array;

    // used with check_for_surface
    // contains the addresses of the surfaces that are the target
    // of a transfer operation from a surface in the GPU in the current frame
    // use a vector instead of a set because expect it to be always quite small
    std::vector<Address> cpu_surfaces_changed;

    VKRenderTarget *target = nullptr;
    ColorSurfaceCacheInfo *last_written_surface = nullptr;

    DepthStencilSurfaceCacheInfo *pending_ds_scene = nullptr;
    bool pending_ds_scene_stores = false;

    struct PendingCast {
        vk::ImageView view;
        vk::ImageView alt_view;
        ColorSurfaceCacheInfo *info;
        std::function<void(vk::CommandBuffer)> record;
    };
    std::vector<PendingCast> pending_casts;

    void record_pending_cast(PendingCast &cast, VKContext &context);

    // destroy all framebuffers using view as their color or depth-stencil
    void destroy_framebuffers(vk::ImageView view);

    void destroy_surface(ColorSurfaceCacheInfo &info);
    void destroy_surface(DepthStencilSurfaceCacheInfo &info);

    // reload a surface's guest-memory content into its Vulkan image (recorded in prerender_cmd)
    bool try_upload_guest_content(ColorSurfaceCacheInfo &info, MemState &mem);

    // compute pipeline that re-groups a typeless byte-reinterpret so the wanted
    // 32-bit half is written coherently at fully upscaled resolution, instead of
    // being interleaved every column (which misaligns the sampling)
    vk::ShaderModule reinterpret_shader = nullptr;
    vk::DescriptorSetLayout reinterpret_desc_layout = nullptr;
    vk::PipelineLayout reinterpret_pipeline_layout = nullptr;
    vk::Pipeline reinterpret_pipeline = nullptr;
    vk::DescriptorPool reinterpret_desc_pool = nullptr;
    std::vector<vk::DescriptorSet> reinterpret_desc_sets;
    uint32_t reinterpret_desc_idx = 0;
    // point sampler used to texelFetch the store inside the reinterpret shader
    vk::Sampler reinterpret_sampler = nullptr;

    // lazily build the reinterpret compute pipeline (no-op once built)
    void ensure_reinterpret_pipeline();

    // record and submit a one-off command buffer that copies the surface image into the
    // mapped memory buffer at its guest address (shared by check_for_surface and
    // sync_surface_for_gpu_read). If mem is non-null, the whole sync (fence wait, guest RAM
    // write-back, post sync) completes synchronously before returning, and only
    // [sync_addr, sync_addr + sync_size) is written to guest RAM: a surface's address range
    // can be a memory pool that also holds CPU-written data (e.g. transfer destinations),
    // which a full-range write-back would clobber with stale image content.
    void submit_immediate_surface_sync(ColorSurfaceCacheInfo &surface, MemState *mem, Address sync_addr = 0, uint32_t sync_size = 0);

public:
    // fold the scene's drawn rect into the current colour surface's written region
    void note_scene_draw_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1);

    // when creating a mutable image, can we pass as an argument
    // the possible format used for an image view to improve performance ?
    bool support_image_format_specifier = false;

    // can we protect mapped memory ?
    // On Windows this causes no issue, but according to my test
    // It only works with Nvidia drivers on Linux...
    bool can_mprotect_mapped_memory = true;

    explicit VKSurfaceCache(VKState &state);
    void cleanup();

    SurfaceRetrieveResult retrieve_color_surface_for_framebuffer(MemState &mem, SceGxmColorSurface *color);
    std::optional<TextureLookupResult> retrieve_color_surface_as_texture(const SceGxmTexture &texture, const SceGxmColorBaseFormat base_format, TextureViewport *texture_viewport);

    SurfaceRetrieveResult retrieve_depth_stencil_for_framebuffer(SceGxmDepthStencilSurface *depth_stencil, const uint32_t width, const uint32_t height);

    bool begin_ds_scene_depth_check(const SceGxmDepthStencilSurface &depth_stencil, bool this_scene_stores, Address scene_color_addr);
    void resolve_ds_scene_end(bool scene_wrote_depth);

    bool try_transfer_depth_gpu(Address src_address, Address dst_address, uint32_t width, uint32_t height);

    void perform_pending_casts(VKContext &context, uint16_t vert_texture_count, uint16_t frag_texture_count);
    void flush_all_pending_casts();

    void mark_current_surface_blended() {
        if (last_written_surface)
            last_written_surface->content_is_blended = true;
    }

    bool current_surface_raw_is_valid() const {
        return last_written_surface && last_written_surface->raw_image && !last_written_surface->content_is_blended;
    }

    std::optional<TextureLookupResult> retrieve_depth_stencil_as_texture(const SceGxmTexture &texture, TextureViewport *texture_viewport);

    Framebuffer &retrieve_framebuffer_handle(MemState &mem, SceGxmColorSurface *color, SceGxmDepthStencilSurface *depth_stencil,
        vk::RenderPass standard_render_pass, vk::RenderPass interlock_render_pass, vk::ImageView &color_view, vk::ImageView &ds_view);

    // Check if the address is one of a used surface
    // If it is the case, this function returns true, moves the callback
    // synchronize the surface back to the RAM then only call the callback
    // if this call is used for a copy or similar operation set the changed address to the destination
    // so that subsequent calls to check_for_surface with the target destination also get delayed
    bool check_for_surface(MemState &mem, Address source_address, CallbackRequestFunction &callback, Address target_address);
    // Called when a guest address is about to be read by the GPU through raw buffer
    // accesses (e.g. a uniform buffer aliasing a rendered surface, like Killzone Mercenary's
    // bloom chain reading its HDR render target). If the address lies inside a
    // recently-rendered color surface, make sure the surface content is synced into the
    // mapped memory buffer so the shader reads up-to-date data. Returns true if the address
    // belongs to such a surface.
    bool sync_surface_for_gpu_read(Address address, uint32_t size);

    // If non-null, the return value must be sent as a PostSurfaceSyncRequest
    ColorSurfaceCacheInfo *perform_surface_sync();

    // Called after the render has been done
    void perform_post_surface_sync(const MemState &mem, ColorSurfaceCacheInfo *surface);

    // destroy all framebuffers associated with render_target
    // (meaning their color or depth-stencil surface is not backed by memory)
    void destroy_associated_framebuffers(const VKRenderTarget *render_target);

    // Return the image along with the viewport to be displayed on the screen
    // Viewport should already have its fields width and height filled
    struct PresentSurfaceInfo {
        vk::Image image;
        bool plain_rgba8;
    };
    vk::ImageView sourcing_color_surface_for_presentation(Ptr<const void> address, uint32_t pitch, Viewport &viewport, PresentSurfaceInfo *present_info = nullptr);

    // Dump an rgba8 frame with the given properties to the returned vector
    // if this function fails, the vector will be empty
    std::vector<uint32_t> dump_frame(Ptr<const void> address, uint32_t width, uint32_t height, uint32_t pitch);

    void set_render_target(VKRenderTarget *new_target) {
        target = new_target;
    }

    void clear_surfaces_changed() {
        cpu_surfaces_changed.clear();
    }
};
} // namespace renderer::vulkan
