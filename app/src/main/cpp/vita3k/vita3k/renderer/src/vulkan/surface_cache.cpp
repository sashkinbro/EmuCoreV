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

#include <renderer/vulkan/surface_cache.h>

#include <gxm/functions.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/types.h>
#include <vkutil/vkutil.h>

#include <vulkan/vulkan_format_traits.hpp>

#include <atomic>
#include <cmath>
#include <mem/functions.h>
#include <util/align.h>
#include <util/log.h>
#include <util/vector_utils.h>

extern "C" {
#include <libswscale/swscale.h>
}

namespace renderer::vulkan {
thread_local bool surface_sync_internal_write = false;
std::atomic<uint32_t> f10_sync_count{ 0 };
std::atomic<uint32_t> f10_skip_count{ 0 };
std::atomic<uint64_t> f10_repack_us{ 0 };
} // namespace renderer::vulkan

static bool format_support_surface_sync(SceGxmColorBaseFormat format) {
    // U2F10F10F10 (emulated with RGBA16F) is converted back by pack_rgba16f_to_u2f10f10f10;
    // games CPU-read such surfaces (e.g. Sonic Transformed's 4x4 auto-exposure measurement)
    return true;
}

static bool format_support_swizzle(SceGxmColorBaseFormat format) {
    // do we support something more than the identity swizzle
    // for now we do not support any texture whose component size
    // are all not the same or not a multiple of a byte
    return format != SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10
        && format != SCE_GXM_COLOR_BASE_FORMAT_U2U10U10U10
        && format != SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4
        && format != SCE_GXM_COLOR_BASE_FORMAT_U1U5U5U5
        && format != SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9
        && format != SCE_GXM_COLOR_BASE_FORMAT_F11F11F10
        && format != SCE_GXM_COLOR_BASE_FORMAT_U5U6U5;
}

static bool format_need_additional_memory(SceGxmColorBaseFormat format) {
    // we are using 4-component surfaces to emulate them
    // so we can't simply use the allocated memory for them
    return format == SCE_GXM_COLOR_BASE_FORMAT_U8U8U8;
}

namespace renderer::vulkan {

static bool surface_sync_needs_u4u4u4u4_repack(const ColorSurfaceCacheInfo &surface) {
    return surface.format == SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4
        && (surface.texture.format == vk::Format::eR8G8B8A8Unorm
            || surface.texture.format == vk::Format::eR8G8B8A8Srgb);
}

static uint8_t unorm8_to_unorm4(uint8_t value) {
    return static_cast<uint8_t>((static_cast<uint32_t>(value) * 15 + 127) / 255);
}

static void pack_rgba8_to_r4g4b4a4(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    for (uint32_t y = 0; y < height; y++) {
        uint16_t *dst_row = reinterpret_cast<uint16_t *>(dst + y * pixel_stride * sizeof(uint16_t));
        const uint8_t *src_row = src + y * pixel_stride * 4;

        for (uint32_t x = 0; x < pixel_stride; x++) {
            const uint8_t r = unorm8_to_unorm4(src_row[x * 4 + 0]);
            const uint8_t g = unorm8_to_unorm4(src_row[x * 4 + 1]);
            const uint8_t b = unorm8_to_unorm4(src_row[x * 4 + 2]);
            const uint8_t a = unorm8_to_unorm4(src_row[x * 4 + 3]);

            dst_row[x] = static_cast<uint16_t>(r | (g << 4) | (b << 8) | (a << 12));
        }
    }
}

static bool surface_sync_needs_f10_repack(const ColorSurfaceCacheInfo &surface) {
    return surface.format == SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10 && surface.texture.format == vk::Format::eR16G16B16A16Sfloat;
}

static bool surface_sync_needs_se5_repack(const ColorSurfaceCacheInfo &surface) {
    return surface.format == SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9 && surface.texture.format == vk::Format::eR16G16B16A16Sfloat;
}

// F16 (s1e5m10) -> unsigned F10 (e5m5)
static inline uint32_t half_to_f10(uint32_t h) {
    // branchless: subtracting the sign bit yields an all-ones mask for positives, 0 for negatives
    return ((h >> 5) & 0x3FF) & (((h >> 15) & 1) - 1);
}

// F16 alpha -> 2-bit unorm, rounding to the nearest of {0, 1/3, 2/3, 1}.
static inline uint32_t half_to_unorm2(uint32_t h) {
    if (h & 0x8000)
        return 0;
    return (h >= 0x3AAB) ? 3 : (h >= 0x3800) ? 2
        : (h >= 0x3155)                      ? 1
                                             : 0;
}

static inline float half_to_float_unsigned(uint32_t h) {
    if (h & 0x8000)
        return 0.0f;
    const uint32_t exp = (h >> 10) & 0x1F;
    const uint32_t man = h & 0x3FF;
    if (exp == 31)
        return man ? 0.0f : 65504.0f;
    if (exp == 0)
        return static_cast<float>(man) * (1.0f / (1 << 24));
    const uint32_t f32 = ((exp + 112) << 23) | (man << 13);
    float result;
    memcpy(&result, &f32, sizeof(result));
    return result;
}

static inline uint32_t pack_rgb9e5(float r, float g, float b) {
    constexpr float max_rgb9e5 = 65408.0f;
    r = std::min(r, max_rgb9e5);
    g = std::min(g, max_rgb9e5);
    b = std::min(b, max_rgb9e5);
    const float maxc = std::max(r, std::max(g, b));
    if (maxc <= 0.0f)
        return 0;
    uint32_t max_bits;
    memcpy(&max_bits, &maxc, sizeof(max_bits));
    int exp_shared = std::max(-16, static_cast<int>(max_bits >> 23) - 127) + 1 + 15;
    // scale = 2^(exp_shared - 15 - 9), always a normal float here (exp_shared is in [0, 31])
    uint32_t scale_bits = static_cast<uint32_t>(exp_shared - 24 + 127) << 23;
    float scale;
    memcpy(&scale, &scale_bits, sizeof(scale));
    if (static_cast<uint32_t>(maxc / scale + 0.5f) == 512) {
        exp_shared += 1;
        scale *= 2.0f;
    }
    const uint32_t rs = std::min(511u, static_cast<uint32_t>(r / scale + 0.5f));
    const uint32_t gs = std::min(511u, static_cast<uint32_t>(g / scale + 0.5f));
    const uint32_t bs = std::min(511u, static_cast<uint32_t>(b / scale + 0.5f));
    return rs | (gs << 9) | (bs << 18) | (static_cast<uint32_t>(exp_shared) << 27);
}

static void pack_rgba16f_to_se5m9m9m9(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    const uint32_t count = pixel_stride * height;
    uint32_t *dst_px = reinterpret_cast<uint32_t *>(dst);
    const uint64_t *src_px = reinterpret_cast<const uint64_t *>(src);

    for (uint32_t i = 0; i < count; i++) {
        const uint64_t v = src_px[i];
        const float r = half_to_float_unsigned(static_cast<uint32_t>(v) & 0xFFFF);
        const float g = half_to_float_unsigned(static_cast<uint32_t>(v >> 16) & 0xFFFF);
        const float b = half_to_float_unsigned(static_cast<uint32_t>(v >> 32) & 0xFFFF);
        dst_px[i] = pack_rgb9e5(r, g, b);
    }
}

static void pack_rgba16f_to_u2f10f10f10(uint8_t *dst, const uint8_t *src, uint32_t pixel_stride, uint32_t height) {
    const uint32_t count = pixel_stride * height;
    uint32_t *dst_px = reinterpret_cast<uint32_t *>(dst);
    const uint64_t *src_px = reinterpret_cast<const uint64_t *>(src);

    for (uint32_t i = 0; i < count; i++) {
        const uint64_t v = src_px[i];
        const uint32_t r = half_to_f10(static_cast<uint32_t>(v) & 0xFFFF);
        const uint32_t g = half_to_f10(static_cast<uint32_t>(v >> 16) & 0xFFFF);
        const uint32_t b = half_to_f10(static_cast<uint32_t>(v >> 32) & 0xFFFF);
        const uint32_t a = half_to_unorm2(static_cast<uint32_t>(v >> 48));

        dst_px[i] = r | (g << 10) | (b << 20) | (a << 30);
    }
}

static void protect_surface(MemState &mem, ColorSurfaceCacheInfo &info) {
    const bool trap_reads = (info.tiling == SurfaceTiling::Linear
        && format_support_surface_sync(info.format));

    uint32_t addr_start = align(info.data.address(), KiB(4));
    uint32_t addr_end = align_down(info.data.address() + info.total_bytes, KiB(4));
    bool small_surface = addr_start >= addr_end;
    if (small_surface) {
        // we still need to protect something, even if it's not completely accurate
        addr_start = align_down(info.data.address(), KiB(4));
        addr_end = align(info.data.address() + info.total_bytes, KiB(4));
    }

    // Use MemPerm::None to trap both reads and writes for surfaces that support sync,
    // MemPerm::ReadOnly to trap only writes for other surfaces
    MemPerm perm = trap_reads ? MemPerm::None : MemPerm::ReadOnly;
    std::shared_ptr<bool> need_sync = trap_reads ? info.need_surface_sync : nullptr;
    // Don't track dirty for small surfaces to avoid false positives from unrelated writes
    std::shared_ptr<bool> dirty = small_surface ? nullptr : info.dirty;

    add_protect(mem, addr_start, addr_end - addr_start, perm,
        [dirty, need_sync](Address, bool write) {
            // ignore our own guest write-backs
            if (write && surface_sync_internal_write)
                return true;
            if (write && dirty) {
                *dirty = true;
                return true;
            }
            if (need_sync)
                *need_sync = true;
            return true;
        });
}

ColorSurfaceCacheInfo::~ColorSurfaceCacheInfo() {
    sws_freeContext(sws_context);
}

void VKSurfaceCache::destroy_framebuffers(vk::ImageView view) {
    vkutil::DestroyQueue &destroy_queue = state.frame().destroy_queue;
    for (auto it = framebuffer_array.begin(); it != framebuffer_array.end();) {
        // if the color of depth-stencil match the one of the render_target, this won't be used anymore
        if (it->first.first == view || it->first.second == view) {
            destroy_queue.add(it->second.standard);
            destroy_queue.add(it->second.shader_interlock);
            it = framebuffer_array.erase(it);
        } else {
            it = std::next(it);
        }
    }
}

void VKSurfaceCache::record_pending_cast(PendingCast &cast, VKContext &context) {
    const bool pre_scene = cast.info->last_scene_rendered == context.scene_timestamp && context.scene_has_drawn
        && !context.scene_macroblock_flushed;
    if (pre_scene) {
        cast.record(context.prerender_cmd);
        return;
    }

    if (context.in_renderpass)
        context.stop_render_pass();

    vk::ImageMemoryBarrier store_visible{
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eTransferRead | vk::AccessFlagBits::eShaderRead,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = cast.info->texture.image,
        .subresourceRange = vkutil::color_subresource_range
    };
    context.render_cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, store_visible);

    cast.record(context.render_cmd);
}

void VKSurfaceCache::perform_pending_casts(VKContext &context, uint16_t vert_texture_count, uint16_t frag_texture_count) {
    if (pending_casts.empty())
        return;

    for (size_t i = 0; i < pending_casts.size();) {
        bool used = false;
        for (uint16_t unit = 0; unit < frag_texture_count && !used; unit++) {
            const vk::ImageView bound = context.fragment_textures[unit].imageView;
            used = pending_casts[i].view == bound || (pending_casts[i].alt_view && pending_casts[i].alt_view == bound);
        }
        for (uint16_t unit = 0; unit < vert_texture_count && !used; unit++) {
            const vk::ImageView bound = context.vertex_textures[unit].imageView;
            used = pending_casts[i].view == bound || (pending_casts[i].alt_view && pending_casts[i].alt_view == bound);
        }

        if (used) {
            // move it out first: the recording can itself queue new casts
            PendingCast cast = std::move(pending_casts[i]);
            pending_casts.erase(pending_casts.begin() + i);
            record_pending_cast(cast, context);
            // start over, the vector may have changed
            i = 0;
        } else {
            i++;
        }
    }
}

void VKSurfaceCache::flush_all_pending_casts() {
    if (pending_casts.empty())
        return;
    VKContext *context = reinterpret_cast<VKContext *>(state.context);
    // the vector is swapped out first: a copy can itself trigger surface operations
    std::vector<PendingCast> casts;
    casts.swap(pending_casts);
    for (auto &cast : casts)
        record_pending_cast(cast, *context);
}

void VKSurfaceCache::destroy_surface(ColorSurfaceCacheInfo &info) {
    // queued casted copies may reference this surface: record them while everything is alive
    flush_all_pending_casts();

    vkutil::DestroyQueue &destroy_queue = state.frame().destroy_queue;

    // don't forget to destroy in the right order
    for (auto &casted : info.casted_textures) {
        destroy_queue.add_buffer(casted.transition_buffer);
        destroy_queue.add(casted.reinterpret_view);
        destroy_queue.add(casted.alt_gamma_view);
        destroy_queue.add_image(casted.texture);
    }
    info.casted_textures.clear();

    destroy_queue.add(info.alternate_view);
    destroy_queue.add(info.reinterpret_store_view);

    if (info.raw_image) {
        destroy_queue.add_image(*info.raw_image);
        info.raw_image.reset();
    }

    destroy_framebuffers(info.texture.view);
    destroy_queue.add_image(info.texture);

    if (info.blit_image) {
        destroy_queue.add_image(*info.blit_image);
        info.blit_image.reset();
    }
    if (info.copy_buffer) {
        destroy_queue.add_buffer(*info.copy_buffer);
        info.copy_buffer.reset();
    }
    if (info.upload_buffer) {
        destroy_queue.add_buffer(*info.upload_buffer);
        info.upload_buffer.reset();
    }
}

void VKSurfaceCache::destroy_surface(DepthStencilSurfaceCacheInfo &info) {
    vkutil::DestroyQueue &destroy_queue = state.frame().destroy_queue;

    for (auto &read_only : info.read_surfaces) {
        destroy_queue.add_image(read_only.stencil_view);
        destroy_queue.add_image(read_only.depth_view);
    }
    info.read_surfaces.clear();

    destroy_queue.add(info.depth_view);
    destroy_queue.add(info.stencil_view);

    destroy_framebuffers(info.texture.view);
    destroy_queue.add_image(info.texture);

    if (info.sample_rate_copy) {
        destroy_framebuffers(info.sample_rate_copy->view);
        destroy_queue.add_image(*info.sample_rate_copy);
        info.sample_rate_copy.reset();
    }
}

VKSurfaceCache::VKSurfaceCache(VKState &state)
    : state(state) {
    color_surface_queue.init(max_surfaces_allowed);
    ds_surface_queue.init(max_surfaces_allowed);
}

void VKSurfaceCache::cleanup() {
    for (auto &[key, fb] : framebuffer_array) {
        state.device.destroy(fb.standard);
        state.device.destroy(fb.shader_interlock);
    }
    framebuffer_array.clear();

    for (auto &item : color_surface_queue.items) {
        auto &info = item.content;
        for (auto &casted : info.casted_textures) {
            casted.transition_buffer.destroy();
            if (casted.reinterpret_view) {
                state.device.destroy(casted.reinterpret_view);
                casted.reinterpret_view = nullptr;
            }
            if (casted.alt_gamma_view) {
                state.device.destroy(casted.alt_gamma_view);
                casted.alt_gamma_view = nullptr;
            }
            casted.texture.destroy();
        }
        info.casted_textures.clear();

        if (info.alternate_view) {
            state.device.destroy(info.alternate_view);
            info.alternate_view = nullptr;
        }

        if (info.reinterpret_store_view) {
            state.device.destroy(info.reinterpret_store_view);
            info.reinterpret_store_view = nullptr;
        }

        if (info.blit_image)
            info.blit_image->destroy();
        if (info.copy_buffer)
            info.copy_buffer->destroy();
        if (info.upload_buffer)
            info.upload_buffer->destroy();

        info.texture.destroy();
    }

    for (auto &item : ds_surface_queue.items) {
        auto &info = item.content;
        for (auto &read_surface : info.read_surfaces) {
            read_surface.depth_view.destroy();
            read_surface.stencil_view.destroy();
        }
        info.read_surfaces.clear();

        if (info.depth_view) {
            state.device.destroy(info.depth_view);
            info.depth_view = nullptr;
        }
        if (info.stencil_view) {
            state.device.destroy(info.stencil_view);
            info.stencil_view = nullptr;
        }

        info.texture.destroy();
    }

    if (reinterpret_pipeline) {
        state.device.destroy(reinterpret_pipeline);
        state.device.destroy(reinterpret_pipeline_layout);
        state.device.destroy(reinterpret_desc_layout);
        state.device.destroy(reinterpret_desc_pool);
        state.device.destroy(reinterpret_shader);
        if (reinterpret_sampler) {
            state.device.destroy(reinterpret_sampler);
            reinterpret_sampler = nullptr;
        }
        reinterpret_pipeline = nullptr;
        reinterpret_desc_sets.clear();
    }

    color_address_lookup.clear();
    depth_address_lookup.clear();
    stencil_address_lookup.clear();
    cpu_surfaces_changed.clear();
    target = nullptr;
    last_written_surface = nullptr;
}

// On real hardware the surface IS its memory: a game may freely mix CPU writes and GPU
// rendering into the same buffer. Returns whether the upload was recorded.
bool VKSurfaceCache::try_upload_guest_content(ColorSurfaceCacheInfo &info, MemState &mem) {
    const bool upload_supported = state.features.enable_memory_mapping
        && info.tiling == SurfaceTiling::Linear
        && format_support_surface_sync(info.format)
        // guest U2F10F10F10/SE5M9M9M9 -> RGBA16F upload conversion is not implemented (sync is one-way)
        && info.format != SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10
        && info.format != SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9
        && info.swizzle.r == vk::ComponentSwizzle::eR
        && !info.raw_image
        && vk::blockSize(info.texture.format) > 0
        && (info.stride_bytes % vk::blockSize(info.texture.format)) == 0;
    if (!upload_supported) {
        return false;
    }

    // never read past the end of valid guest memory (surface descriptors can cover
    // more than the underlying allocation)
    if (!is_valid_addr_range(mem, info.data.address(), info.data.address() + static_cast<uint32_t>(info.stride_bytes) * info.original_height)) {
        return false;
    }

    VKContext *context = reinterpret_cast<VKContext *>(state.context);

    const uint32_t bytes_pp = static_cast<uint32_t>(vk::blockSize(info.texture.format));
    const uint32_t pixel_stride = info.stride_bytes / bytes_pp;
    const vk::DeviceSize upload_size = static_cast<vk::DeviceSize>(info.stride_bytes) * info.original_height;

    if (info.upload_buffer && info.upload_buffer->size < upload_size) {
        state.frame().destroy_queue.add_buffer(*info.upload_buffer);
        info.upload_buffer.reset();
    }
    if (!info.upload_buffer)
        info.upload_buffer = std::make_unique<vkutil::Buffer>();
    if (!info.upload_buffer->buffer) {
        info.upload_buffer->size = upload_size;
        info.upload_buffer->init_buffer(vk::BufferUsageFlagBits::eTransferSrc, vkutil::vma_mapped_alloc);
    }
    memcpy(info.upload_buffer->mapped_data, info.data.get(mem), upload_size);

    vk::CommandBuffer pre_cmd = context->prerender_cmd;
    const vk::BufferImageCopy upload_copy{
        .bufferOffset = 0,
        .bufferRowLength = pixel_stride,
        .bufferImageHeight = info.original_height,
        .imageSubresource = vkutil::color_subresource_layer,
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { info.original_width, info.original_height, 1 }
    };
    if (state.res_multiplier == 1.0f) {
        info.texture.transition_to(pre_cmd, vkutil::ImageLayout::TransferDst);
        pre_cmd.copyBufferToImage(info.upload_buffer->buffer, info.texture.image, vk::ImageLayout::eTransferDstOptimal, upload_copy);
        info.texture.transition_to(pre_cmd, vkutil::ImageLayout::ColorAttachmentReadWrite);
    } else {
        // upscaled surface: stage at original size then blit up
        if (info.blit_image && info.blit_image->image && info.blit_image->format != info.texture.format) {
            state.frame().destroy_queue.add_image(*info.blit_image);
            *info.blit_image = vkutil::Image();
        }
        if (!info.blit_image)
            info.blit_image = std::make_unique<vkutil::Image>();
        if (!info.blit_image->image) {
            info.blit_image->format = info.texture.format;
            info.blit_image->width = info.original_width;
            info.blit_image->height = info.original_height;
            info.blit_image->init_image(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
            info.blit_image->transition_to(pre_cmd, vkutil::ImageLayout::TransferDst);
        } else {
            info.blit_image->transition_to_discard(pre_cmd, vkutil::ImageLayout::TransferDst);
        }
        pre_cmd.copyBufferToImage(info.upload_buffer->buffer, info.blit_image->image, vk::ImageLayout::eTransferDstOptimal, upload_copy);
        info.blit_image->transition_to(pre_cmd, vkutil::ImageLayout::TransferSrc);
        info.texture.transition_to(pre_cmd, vkutil::ImageLayout::TransferDst);
        const vk::ImageBlit upload_blit{
            .srcSubresource = vkutil::color_subresource_layer,
            .srcOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(info.original_width), static_cast<int32_t>(info.original_height), 1 } },
            .dstSubresource = vkutil::color_subresource_layer,
            .dstOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(info.width), static_cast<int32_t>(info.height), 1 } }
        };
        pre_cmd.blitImage(info.blit_image->image, vk::ImageLayout::eTransferSrcOptimal, info.texture.image, vk::ImageLayout::eTransferDstOptimal, upload_blit, vk::Filter::eNearest);
        info.texture.transition_to(pre_cmd, vkutil::ImageLayout::ColorAttachmentReadWrite);
    }
    return true;
}

void VKSurfaceCache::note_scene_draw_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (!last_written_surface || x1 <= x0 || y1 <= y0)
        return;
    ColorSurfaceCacheInfo &info = *last_written_surface;
    // scaled -> unscaled, rounded outward to the 32px tile the hardware writes back as a whole
    const float inv = 1.0f / state.res_multiplier;
    int32_t ux0 = static_cast<int32_t>(std::floor(x0 * inv / 32.0f)) * 32;
    int32_t uy0 = static_cast<int32_t>(std::floor(y0 * inv / 32.0f)) * 32;
    int32_t ux1 = static_cast<int32_t>(std::ceil(x1 * inv / 32.0f)) * 32;
    int32_t uy1 = static_cast<int32_t>(std::ceil(y1 * inv / 32.0f)) * 32;
    ux0 = std::clamp<int32_t>(ux0, 0, info.original_width);
    uy0 = std::clamp<int32_t>(uy0, 0, info.original_height);
    ux1 = std::clamp<int32_t>(ux1, 0, info.original_width);
    uy1 = std::clamp<int32_t>(uy1, 0, info.original_height);
    if (ux1 <= ux0 || uy1 <= uy0)
        return;
    info.written_x0 = std::min(info.written_x0, ux0);
    info.written_y0 = std::min(info.written_y0, uy0);
    info.written_x1 = std::max(info.written_x1, ux1);
    info.written_y1 = std::max(info.written_y1, uy1);
}

SurfaceRetrieveResult VKSurfaceCache::retrieve_color_surface_for_framebuffer(MemState &mem, SceGxmColorSurface *color) {
    // Create the key to access the cache struct
    const uint32_t address = color->data.address();

    const uint32_t original_width = color->width;
    const uint32_t original_height = color->height;

    uint32_t width = static_cast<uint32_t>(original_width * state.res_multiplier);
    uint32_t height = static_cast<uint32_t>(original_height * state.res_multiplier);

    bool overlap = true;

    // Of course, this works under the assumption that range must be unique :D
    auto ite = color_address_lookup.upper_bound(address);
    if (ite == color_address_lookup.begin())
        // no match
        overlap = false;
    else
        --ite;
    // ite is now the first item with an address lower or equal to key

    overlap = (overlap && (ite->first + ite->second->total_bytes) > address);

    if (!overlap && ite != color_address_lookup.begin()) {
        --ite;
        overlap = (ite->first + ite->second->total_bytes) > address;
    }

    const SceGxmColorBaseFormat base_format = gxm::get_base_format(color->colorFormat);
    vk::Format vk_format = color::translate_surface_format(base_format);

    SurfaceTiling tiling;
    if (color->surfaceType == SCE_GXM_COLOR_SURFACE_LINEAR)
        tiling = SurfaceTiling::Linear;
    else if (color->surfaceType == SCE_GXM_COLOR_SURFACE_SWIZZLED)
        tiling = SurfaceTiling::Swizzled;
    else
        tiling = SurfaceTiling::Tiled;

    const bool is_srgb = color->gamma != 0;
    if (is_srgb) {
        if (vk_format == vk::Format::eR8G8B8A8Unorm) {
            vk_format = vk::Format::eR8G8B8A8Srgb;
        } else {
            LOG_WARN_ONCE("Trying to use gamma correction with non-compatible format {}", vk::to_string(vk_format));
        }
    }

    uint32_t bytes_per_stride = color->strideInPixels * gxm::bits_per_pixel(base_format) / 8;
    uint32_t total_surface_size = bytes_per_stride * original_height;

    VKContext *context = reinterpret_cast<VKContext *>(state.context);

    if (overlap) {
        ColorSurfaceCacheInfo &info = *ite->second;

        // There are four situations I think of:
        // 1. Different base address, lookup for write, in this case, if the cached surface range contains the given address, then
        // probably this cached surface has already been freed GPU-wise. So erase.
        // 2. Same base address, but width and height change to be larger, or format change if write. Remake a new one for both read and write situation.
        // 3. Out of cache range. In write case, create a new one, in read case, lul
        // 4. Read situation with smaller width and height, probably need to extract the needed region out.
        // 5. the surface is a gbuffer and we are currently trying to read the 2nd component, in this case key == ite->first + 4
        const bool addr_in_range_of_cache = ((address + total_surface_size) <= (ite->first + info.total_bytes + 4));
        const bool cache_probably_freed = (ite->first != address) && addr_in_range_of_cache;
        const bool surface_extent_changed = info.height < height || bytes_per_stride != info.stride_bytes || tiling != info.tiling;
        bool surface_stat_changed = false;

        if (ite->first == address)
            surface_stat_changed = surface_extent_changed || info.width < width || base_format != info.format;

        const bool invalidated = cache_probably_freed || surface_stat_changed || !addr_in_range_of_cache;
        if (invalidated) {
            destroy_surface(info);
            color_address_lookup.erase(ite);
            color_surface_queue.set_as_lru(&info);
        } else {
            color_surface_queue.set_as_mru(&info);
            if (context->render_target) {
                const uint32_t rt_w = static_cast<uint32_t>(std::lround(context->render_target->width / state.res_multiplier));
                const uint32_t rt_h = static_cast<uint32_t>(std::lround(context->render_target->height / state.res_multiplier));
                info.rendered_w = static_cast<uint16_t>(std::max<uint32_t>(info.rendered_w, std::min<uint32_t>(info.original_width, rt_w)));
                info.rendered_h = static_cast<uint16_t>(std::max<uint32_t>(info.rendered_h, std::min<uint32_t>(info.original_height, rt_h)));
            }

            if (info.data && *info.dirty) {
                try_upload_guest_content(info, mem);
                protect_surface(mem, info);
            }
            *info.dirty = false;

            last_written_surface = &info;

            // if this surface has not been rendered to for the last 60 frames, consider it is not safe not to render all shaders to it
            constexpr uint64_t big_delay_between_frames = 60;
            state.pipeline_cache.can_use_deferred_compilation = context->frame_timestamp - info.last_frame_rendered < big_delay_between_frames;
            info.last_frame_rendered = context->frame_timestamp;
            info.last_scene_rendered = context->scene_timestamp;

            if (vk_format == info.texture.format) {
                return { info.texture.view, &info.texture, info.raw_image.get() };
            } else {
                // using both srgb/linear
                if (!info.alternate_view) {
                    vk::ImageViewCreateInfo view_info{
                        .image = info.texture.image,
                        .viewType = vk::ImageViewType::e2D,
                        .format = vk_format,
                        .components = vkutil::default_comp_mapping,
                        .subresourceRange = vkutil::color_subresource_range
                    };
                    info.alternate_view = state.device.createImageView(view_info);
                }

                return { info.alternate_view, &info.texture, info.raw_image.get() };
            }
        }
    }

    // get the least recently used (probably unused) color surface
    ColorSurfaceCacheInfo &info_added = *color_surface_queue.get_lru();
    if (info_added.texture.image) {
        // deferred destruction of the existing surface
        destroy_surface(info_added);
    }
    const bool reused_for_other_store = info_added.data && info_added.data.address() != address;
    if (info_added.data)
        color_address_lookup.erase(info_added.data.address());
    if (reused_for_other_store)
        info_added.has_phase_view = false;

    color_surface_queue.set_as_mru(&info_added);
    info_added.last_frame_rendered = context->frame_timestamp;
    info_added.last_scene_rendered = context->scene_timestamp;
    info_added.rendered_w = 0;
    info_added.rendered_h = 0;
    info_added.written_x0 = INT32_MAX;
    info_added.written_y0 = INT32_MAX;
    info_added.written_x1 = 0;
    info_added.written_y1 = 0;
    if (context->render_target) {
        const uint32_t rt_w = static_cast<uint32_t>(std::lround(context->render_target->width / state.res_multiplier));
        const uint32_t rt_h = static_cast<uint32_t>(std::lround(context->render_target->height / state.res_multiplier));
        info_added.rendered_w = static_cast<uint16_t>(std::min<uint32_t>(original_width, rt_w));
        info_added.rendered_h = static_cast<uint16_t>(std::min<uint32_t>(original_height, rt_h));
    }

    color_address_lookup[address] = &info_added;

    info_added.width = width;
    info_added.height = height;
    info_added.original_width = original_width;
    info_added.original_height = original_height;
    info_added.stride_bytes = bytes_per_stride;
    info_added.data = color->data;
    info_added.total_bytes = total_surface_size;
    info_added.format = base_format;
    info_added.tiling = tiling;
    // only remember the swizzle here, it will be useful if we get to present or sample from this image with a different swizzle
    info_added.swizzle = color::translate_swizzle(color->colorFormat);

    vkutil::Image &image = info_added.texture;
    image.width = width;
    image.height = height;
    image.format = vk_format;
    image.layout = vkutil::ImageLayout::Undefined;

    // we might have to create a non-srgb/linear view later if this surface is used for presentation
    const bool need_mutable_rgba8 = (vk_format == vk::Format::eR8G8B8A8Unorm || vk_format == vk::Format::eR8G8B8A8Srgb);
    // 64-bit surfaces may be read back through an R32G32_UINT view by the typeless
    // reinterpret compute pass (any 64-bit format is in the same compatibility class),
    // which also needs a mutable format.
    const bool need_mutable_64bit = (vk::blockSize(vk_format) == 8);
    const bool need_mutable = need_mutable_rgba8 || need_mutable_64bit;
    const vk::ImageCreateFlags image_create_flags = need_mutable ? vk::ImageCreateFlagBits::eMutableFormat : vk::ImageCreateFlags();
    const void *image_info_pNext = nullptr;
    if (support_image_format_specifier && need_mutable_rgba8) {
        static const vk::Format view_formats[] = { vk::Format::eR8G8B8A8Unorm, vk::Format::eR8G8B8A8Srgb };
        static const vk::ImageFormatListCreateInfoKHR image_info_formats{
            .viewFormatCount = 2,
            .pViewFormats = view_formats
        };
        image_info_pNext = &image_info_formats;
    }

    vk::ImageUsageFlags surface_usages = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eInputAttachment;
    if (state.features.support_shader_interlock)
        surface_usages |= vk::ImageUsageFlagBits::eStorage;
    image.init_image(surface_usages, vkutil::default_comp_mapping, image_create_flags, image_info_pNext);

    // do it in the prerender if we read from this texture in the same scene (although this would be useless)
    vk::CommandBuffer cmd_buffer = context->prerender_cmd;
    // must do a first transition to draw the placeholder color
    image.transition_to(cmd_buffer, vkutil::ImageLayout::TransferDst);

    vk::ClearColorValue clear_color{ std::array<float, 4>({ 0.0f, 0.0f, 0.0f, 0.0f }) };
    cmd_buffer.clearColorImage(image.image, vk::ImageLayout::eTransferDstOptimal, clear_color, vkutil::color_subresource_range);
    image.transition_to(cmd_buffer, vkutil::ImageLayout::ColorAttachmentReadWrite);

    // on real hardware the new surface already "contains" whatever the game put in its
    // memory — load it so CPU-prepared content isn't lost. Restricted to atlas-class
    // surfaces (the streamed-tile pattern this addresses); ordinary scene targets keep
    // the clear-to-black behavior.
    if (info_added.data && original_width >= 1024 && original_height >= 1024) {
        try_upload_guest_content(info_added, mem);
    }

    if (state.features.preserve_f16_nan_as_u16 && base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16) {
        info_added.raw_image = std::make_unique<vkutil::Image>(width, height, vk::Format::eR16G16B16A16Uint);
        vkutil::Image &raw = *info_added.raw_image;
        raw.layout = vkutil::ImageLayout::Undefined;
        raw.init_image(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
            vkutil::default_comp_mapping, vk::ImageCreateFlagBits::eMutableFormat);
        raw.transition_to(cmd_buffer, vkutil::ImageLayout::TransferDst);
        vk::ClearColorValue clear_zero{};
        clear_zero.setUint32({ 0, 0, 0, 0 });
        cmd_buffer.clearColorImage(raw.image, vk::ImageLayout::eTransferDstOptimal, clear_zero, vkutil::color_subresource_range);
        raw.transition_to(cmd_buffer, vkutil::ImageLayout::StorageImage);
    }

    last_written_surface = &info_added;
    info_added.need_surface_sync.reset();
    info_added.need_surface_sync = std::make_shared<bool>(false);
    info_added.dirty = std::make_shared<bool>(false);
    info_added.gpu_read_sync_only = false;
    info_added.content_is_blended = false;
    info_added.reinterpret_view_is_raw = false;

    // we only support surface sync of linear surfaces for now
    if (!can_mprotect_mapped_memory) {
        // perform surface sync on everything
        // it is slow but well... we can't mprotect the buffer
        *info_added.need_surface_sync = color->surfaceType == SCE_GXM_COLOR_SURFACE_LINEAR;
    } else {
        protect_surface(mem, info_added);
    }

    // it's not impossible that this surface will be rendered once and only used after, so do not skip any shader on it
    state.pipeline_cache.can_use_deferred_compilation = false;

    return { info_added.texture.view, &info_added.texture, info_added.raw_image.get() };
}

std::optional<TextureLookupResult> VKSurfaceCache::retrieve_color_surface_as_texture(const SceGxmTexture &texture, const SceGxmColorBaseFormat base_format, TextureViewport *texture_viewport) {
    // Create the key to access the cache struct
    const uint32_t address = (texture.data_addr << 2);

    const uint32_t original_width = gxm::get_width(texture);
    const uint32_t original_height = gxm::get_height(texture);

    const uint32_t width = static_cast<uint32_t>(original_width * state.res_multiplier);
    const uint32_t height = static_cast<uint32_t>(original_height * state.res_multiplier);

    uint32_t stride_bytes = 0;
    SurfaceTiling tiling = SurfaceTiling::Swizzled;
    if (texture.texture_type() == SCE_GXM_TEXTURE_LINEAR_STRIDED) {
        stride_bytes = gxm::get_stride_in_bytes(texture);
        tiling = SurfaceTiling::Linear;
    } else {
        uint32_t pixel_stride = original_width;
        switch (texture.texture_type()) {
        case SCE_GXM_TEXTURE_LINEAR:
            tiling = SurfaceTiling::Linear;
            pixel_stride = align(pixel_stride, 8);
            break;
        case SCE_GXM_TEXTURE_TILED:
            tiling = SurfaceTiling::Tiled;
            pixel_stride = align(pixel_stride, 32);
            break;
        case SCE_GXM_TEXTURE_SWIZZLED_ARBITRARY:
            pixel_stride = next_power_of_two(pixel_stride);
            break;
        default:
            break;
        }
        stride_bytes = pixel_stride * gxm::bits_per_pixel(base_format) / 8;
    }
    uint32_t total_surface_size = stride_bytes * original_height;

    // Walk backward through surfaces to find one that overlaps AND matches stride/tiling.
    // Multiple surfaces can overlap the same address; pick the one with the right layout.
    auto ite = color_address_lookup.upper_bound(address);
    bool found = false;
    while (ite != color_address_lookup.begin()) {
        --ite;
        if ((ite->first + ite->second->total_bytes) > address
            && ite->second->tiling == tiling
            && ite->second->stride_bytes == stride_bytes) {
            found = true;
            break;
        }
    }

    if (!found) {
        return std::nullopt;
    }

    if (*ite->second->dirty && ite->second->last_frame_rendered + 2 <= reinterpret_cast<VKContext *>(state.context)->frame_timestamp) {
        return std::nullopt;
    }

    const vk::ComponentMapping swizzle = texture::translate_swizzle(gxm::get_format(texture));
    vk::Format vk_format = color::translate_surface_format(base_format);

    const bool is_srgb = texture.gamma_mode != 0;
    if (is_srgb) {
        if (vk_format == vk::Format::eR8G8B8A8Unorm) {
            vk_format = vk::Format::eR8G8B8A8Srgb;
        } else {
            LOG_WARN_ONCE("Trying to use gamma correction with non-compatible format {}", vk::to_string(vk_format));
        }
    }

    ColorSurfaceCacheInfo &info = *ite->second;

    if ((base_format == SCE_GXM_COLOR_BASE_FORMAT_U8U8U8 || info.format == SCE_GXM_COLOR_BASE_FORMAT_U8U8U8)
        && base_format != info.format) {
        return std::nullopt;
    }

    // Check if we can use this surface
    bool addr_in_range_of_cache = ((address + total_surface_size) <= (ite->first + info.total_bytes + 4));

    if (ite->first != address && !addr_in_range_of_cache) {
        return std::nullopt;
    }

    uint32_t bytes_per_pixel_requested = gxm::bits_per_pixel(base_format) / 8;
    uint32_t bytes_per_pixel_in_store = gxm::bits_per_pixel(info.format) / 8;

    if (std::max(bytes_per_pixel_requested, bytes_per_pixel_in_store) % std::min(bytes_per_pixel_requested, bytes_per_pixel_in_store) != 0) {
        return std::nullopt;
    }

    const bool is_typeless_cast = bytes_per_pixel_requested != bytes_per_pixel_in_store;

    // A same-size cast to a different format is a reinterpretation - the game wants the store's bytes.
    // Those bytes cannot survive a float image: any 32-bit word whose exponent field is 0xFF is a NaN and
    // the sampler canonicalises it. For a 64-bit F16 store we therefore hand the shader the bytes in a
    // R16G16B16A16_UNORM image (no NaN encodings, and k/65535 round-trips exactly through float32) and let
    // do_fetch_texture rebuild the words. See [raw cast] in shader/src/translator/texture.cpp.
    const bool store_is_f16_format = info.format == SCE_GXM_COLOR_BASE_FORMAT_F16
        || info.format == SCE_GXM_COLOR_BASE_FORMAT_F16F16
        || info.format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16;
    constexpr bool carry_raw_cast_in_unorm = true;
    const bool raw_bits_cast = carry_raw_cast_in_unorm && !is_typeless_cast
        && bytes_per_pixel_in_store == 8 && store_is_f16_format
        && vk_format != info.texture.format;

    // TODO: this is true only for linear textures (and also kind of for tiled textures) (and in this case start_x = 0),
    // for swizzled textures this is different
    const uint32_t data_delta = address - ite->first;
    uint32_t start_sourced_line = static_cast<uint32_t>((data_delta / stride_bytes) * state.res_multiplier);
    uint32_t start_x = static_cast<uint32_t>((data_delta % stride_bytes) / bytes_per_pixel_requested * state.res_multiplier);

    const bool cast_phase_hi = is_typeless_cast && bytes_per_pixel_in_store != 0 && bytes_per_pixel_requested != 0
        && (((data_delta % stride_bytes) % bytes_per_pixel_in_store) / bytes_per_pixel_requested) != 0;
    if (cast_phase_hi && !info.has_phase_view) {
        info.has_phase_view = true;
        for (CastedTexture &casted_texture : info.casted_textures)
            casted_texture.scene_timestamp = 0;
    }

    if (static_cast<uint16_t>(start_sourced_line + height) > info.height)
        LOG_WARN_ONCE("Trying to use texture partially in the surface cache");

    // The compute de-interleave below rewrites the cast's byte layout, so it must only engage for the exact pattern it is correct for
    const uint32_t guard_native_byte_offset = data_delta % stride_bytes;
    const uint32_t guard_sub_texel_byte = bytes_per_pixel_in_store ? (guard_native_byte_offset % bytes_per_pixel_in_store) : 0u;
    const uint32_t guard_native_store_col = bytes_per_pixel_in_store ? (guard_native_byte_offset / bytes_per_pixel_in_store) : 0u;
    const uint32_t guard_ratio = bytes_per_pixel_requested ? (bytes_per_pixel_in_store / bytes_per_pixel_requested) : 0u;

    const bool use_compute_deinterleave = state.res_multiplier != 1.0f
        && bytes_per_pixel_in_store == 8 && bytes_per_pixel_requested == 4 && guard_ratio == 2
        && guard_native_store_col == 0 && start_sourced_line == 0
        && (guard_sub_texel_byte % bytes_per_pixel_requested) == 0
        && info.original_width > 0 && info.original_height > 0;
    // ----------------------------------------------------------------------------

    // We should be able to use this texture, so set it as mru
    color_surface_queue.set_as_mru(&info);

    const vk::ImageView color_handle_view = reinterpret_cast<VKContext *>(state.context)->current_color_view;
    const bool is_same_image = (color_handle_view == info.texture.view) || (color_handle_view == info.alternate_view);

    if (state.features.use_texture_viewport && base_format == info.format) {
        // use a texture viewport
        *texture_viewport = {
            .ratio = {
                original_width / static_cast<float>(info.original_width),
                original_height / static_cast<float>(info.original_height) },
            .offset = { start_x / static_cast<float>(info.width), start_sourced_line / static_cast<float>(info.height) }
        };

        // if everything matches
        if (vk_format == info.texture.format && swizzle == info.swizzle)
            return TextureLookupResult{
                info.texture.view,
                info.texture.layout,
                info.texture.format
            };

        // use the other view with the correct swizzle / gamma correction
        if (!info.alternate_view) {
            vk::ComponentMapping resulting_mapping = vkutil::color_to_texture_swizzle(info.swizzle, swizzle);

            vk::ImageViewCreateInfo view_info{
                .image = info.texture.image,
                .viewType = vk::ImageViewType::e2D,
                .format = vk_format,
                .components = resulting_mapping,
                .subresourceRange = vkutil::color_subresource_range
            };
            info.alternate_view = state.device.createImageView(view_info);
        }

        return TextureLookupResult{
            info.alternate_view,
            info.texture.layout,
            info.texture.format
        };
    }

    // At non-integer resolution multipliers, upscaled surfaces have different texel
    // boundaries than native.  Bilinear filtering at the same UV produces different blend
    // weights, corrupting data textures with discrete values (e.g. tile-index maps in LBP).
    // Force the cast path to downsample back to native resolution so filtering matches hardware.
    const bool non_integer_downsample = state.res_multiplier != 1.0f
        && state.res_multiplier != std::floor(state.res_multiplier)
        && original_width <= 256 && original_height <= 256
        && texture.min_filter == SCE_GXM_TEXTURE_FILTER_POINT
        && texture.mag_filter == SCE_GXM_TEXTURE_FILTER_POINT;

    if (is_same_image || (start_sourced_line != 0) || (start_x != 0) || (info.width != width) || (info.height != height) || (info.format != base_format) || non_integer_downsample) {
        const uint64_t scene_timestamp = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

        std::vector<CastedTexture> &casted_vec = info.casted_textures;

        CastedTexture *casted = nullptr;

        // Look in cast cache and grab one. The cache really does not store immediate grab on now, but rather to reduce the synchronization in the pipeline (use different texture)
        for (size_t i = 0; i < casted_vec.size();) {
            if ((casted_vec[i].cropped_height == height) && (casted_vec[i].cropped_width == width) && (casted_vec[i].cropped_y == start_sourced_line) && (casted_vec[i].cropped_x == start_x) && (casted_vec[i].format == base_format)) {
                casted = &casted_vec[i];

                if (casted->scene_timestamp == scene_timestamp) {
                    // already copied (or copy pending) for this scene, don't do it again
                    const bool base_is_srgb = casted->texture.format == vk::Format::eR8G8B8A8Srgb;
                    const bool use_alt_view = casted->alt_gamma_view && (is_srgb != base_is_srgb);
                    return TextureLookupResult{
                        use_alt_view ? casted->alt_gamma_view : casted->texture.view,
                        vkutil::ImageLayout::SampledImage,
                        use_alt_view ? (base_is_srgb ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb) : casted->texture.format,
                        is_typeless_cast && !info.has_phase_view,
                        cast_phase_hi,
                        raw_bits_cast
                    };
                }

                break;
            } else {
                i++;
            }
        }

        const bool casted_is_new = casted == nullptr;

        if (casted == nullptr) {
            // Try to crop + cast
            casted_vec.resize(casted_vec.size() + 1);
            casted = &casted_vec[casted_vec.size() - 1];
            *casted = CastedTexture{
                .cropped_x = start_x,
                .cropped_y = start_sourced_line,
                .cropped_width = width,
                .cropped_height = height,
                .format = base_format
            };
            if (bytes_per_pixel_requested == bytes_per_pixel_in_store) {
                const bool full_width_read = (start_x == 0) && (width == info.width);
                if (full_width_read && !non_integer_downsample) {
                    casted->texture.width = width;
                    casted->texture.height = height;
                } else {
                    casted->texture.width = original_width;
                    casted->texture.height = original_height;
                }
            } else {
                casted->texture.width = width;
                casted->texture.height = height;
            }
            auto store_is_f16 = [](SceGxmColorBaseFormat f) {
                return f == SCE_GXM_COLOR_BASE_FORMAT_F16
                    || f == SCE_GXM_COLOR_BASE_FORMAT_F16F16
                    || f == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16;
            };

            auto force_unsigned_reinterpret_format = [](vk::Format fmt) {
                switch (fmt) {
                case vk::Format::eR8Snorm: return vk::Format::eR8Unorm;
                case vk::Format::eR8G8Snorm: return vk::Format::eR8G8Unorm;
                case vk::Format::eR8G8B8A8Snorm: return vk::Format::eR8G8B8A8Unorm;
                case vk::Format::eR16Snorm: return vk::Format::eR16Unorm;
                case vk::Format::eR16G16Snorm: return vk::Format::eR16G16Unorm;
                case vk::Format::eR16G16B16A16Snorm: return vk::Format::eR16G16B16A16Unorm;
                case vk::Format::eR8Sint: return vk::Format::eR8Uint;
                case vk::Format::eR8G8Sint: return vk::Format::eR8G8Uint;
                case vk::Format::eR8G8B8A8Sint: return vk::Format::eR8G8B8A8Uint;
                default: return fmt;
                }
            };

            casted->texture.format = (bytes_per_pixel_requested != bytes_per_pixel_in_store && store_is_f16(info.format)) ? force_unsigned_reinterpret_format(vk_format) : vk_format;

            if (raw_bits_cast) {
                // carry the bytes in a NaN-free format; the shader rebuilds the words after sampling
                casted->texture.format = vk::Format::eR16G16B16A16Unorm;
                LOG_INFO_ONCE("Raw-bit cast of a 64-bit F16 store: carrying the bytes through a UNORM image");
            }

            const bool casted_is_rgba8 = casted->texture.format == vk::Format::eR8G8B8A8Srgb
                || casted->texture.format == vk::Format::eR8G8B8A8Unorm;
            if (casted_is_rgba8)
                casted->texture.format = (bytes_per_pixel_requested == bytes_per_pixel_in_store && info.texture.format == vk::Format::eR8G8B8A8Srgb)
                    ? vk::Format::eR8G8B8A8Srgb
                    : vk::Format::eR8G8B8A8Unorm;

            // find the swizzle we need to apply
            const std::uint8_t components_in_store = vk::componentCount(info.texture.format);
            const std::uint8_t components_requested = vk::componentCount(vk_format);
            vk::ComponentMapping resulting_swizzle;
            // Only take into consideration the current swizzle when it makes sense
            // (Not perfect but better than doing this all the time)
            if (bytes_per_pixel_requested == bytes_per_pixel_in_store && components_in_store == components_requested)
                resulting_swizzle = vkutil::color_to_texture_swizzle(info.swizzle, swizzle);
            else
                resulting_swizzle = swizzle;

            if (raw_bits_cast) {
                // The carrier holds the store's four 16-bit halves, not colour channels. The guest
                // texture's swizzle describes the REINTERPRETED result (an R32G32 texture asks for
                // b = 0, a = 1), so applying it to the carrier destroys halves 2 and 3 - which is how
                // word1 came back as 0xFFFF0000 and painted a flat blue over Ragnarok Odyssey ACE.
                // The halves must arrive untouched; do_fetch_texture rebuilds the words from them.
                resulting_swizzle = vk::ComponentMapping{};
            }

            if (use_compute_deinterleave) {
                // The compute pass writes this image through an R32_UINT storage view (created lazily at dispatch)
                // the consumer still samples it through its real format view
                casted->texture.init_image(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage, resulting_swizzle, vk::ImageCreateFlagBits::eMutableFormat);
            } else {
                casted->texture.init_image(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, resulting_swizzle, casted_is_rgba8 ? vk::ImageCreateFlagBits::eMutableFormat : vk::ImageCreateFlags());
            }

            if (casted_is_rgba8) {
                vk::ImageViewCreateInfo alt_view_info{
                    .image = casted->texture.image,
                    .viewType = vk::ImageViewType::e2D,
                    .format = (casted->texture.format == vk::Format::eR8G8B8A8Srgb) ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb,
                    .components = resulting_swizzle,
                    .subresourceRange = vkutil::color_subresource_range
                };
                casted->alt_gamma_view = state.device.createImageView(alt_view_info);
            }
        }

        casted->scene_timestamp = scene_timestamp;

        const size_t casted_index = static_cast<size_t>(casted - casted_vec.data());
        ColorSurfaceCacheInfo *info_ptr = &info;

        auto record_cast = [this, info_ptr, casted_index, casted_is_new, use_compute_deinterleave,
                               bytes_per_pixel_requested, bytes_per_pixel_in_store, width, height,
                               start_x, start_sourced_line, data_delta, stride_bytes](vk::CommandBuffer cmd_buffer) {
            ColorSurfaceCacheInfo &info = *info_ptr;
            CastedTexture *casted = &info.casted_textures[casted_index];
            if (casted_is_new)
                casted->texture.transition_to(cmd_buffer, use_compute_deinterleave ? vkutil::ImageLayout::StorageImage : vkutil::ImageLayout::TransferDst);
            else
                casted->texture.transition_to_discard(cmd_buffer, use_compute_deinterleave ? vkutil::ImageLayout::StorageImage : vkutil::ImageLayout::TransferDst);

            if (bytes_per_pixel_requested == bytes_per_pixel_in_store) {
                const int32_t src_w = static_cast<int32_t>(std::min<uint32_t>(width, info.width - start_x));
                const int32_t src_h = static_cast<int32_t>(std::min<uint32_t>(height, info.height - start_sourced_line));
                constexpr bool clamp_casted_dst_to_source = true;
                const int32_t dst_w = clamp_casted_dst_to_source
                    ? std::max<int32_t>(1, static_cast<int32_t>(casted->texture.width * static_cast<uint32_t>(src_w) / width))
                    : static_cast<int32_t>(casted->texture.width);
                const int32_t dst_h = clamp_casted_dst_to_source
                    ? std::max<int32_t>(1, static_cast<int32_t>(casted->texture.height * static_cast<uint32_t>(src_h) / height))
                    : static_cast<int32_t>(casted->texture.height);
                if (dst_w != static_cast<int32_t>(casted->texture.width) || dst_h != static_cast<int32_t>(casted->texture.height))
                    LOG_INFO_ONCE("Casted texture sourced from a smaller surface: {}x{} of a requested {}x{} "
                                  "written to the first {}x{} of a {}x{} image",
                        src_w, src_h, width, height, dst_w, dst_h, casted->texture.width, casted->texture.height);

                vk::ImageBlit blit{
                    .srcSubresource = vkutil::color_subresource_layer,
                    .srcOffsets = std::array<vk::Offset3D, 2>{
                        vk::Offset3D{ static_cast<int32_t>(start_x), static_cast<int32_t>(start_sourced_line), 0 },
                        vk::Offset3D{ static_cast<int32_t>(start_x) + src_w, static_cast<int32_t>(start_sourced_line) + src_h, 1 } },
                    .dstSubresource = vkutil::color_subresource_layer,
                    .dstOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ dst_w, dst_h, 1 } }
                };
                // A cast to a DIFFERENT format of the same texel size is a reinterpretation: the game wants
                // the bytes, not the values. vkCmdBlitImage converts between formats, so it silently
                // rewrites them. Ragnarok Odyssey ACE renders packed integers (its shaders read them back
                // with unpack2xU16 / unpack4xU8) into what we allocate as R16G16B16A16_FLOAT and then reads
                // the surface through an R32G32_FLOAT view.
                //
                // The bytes must also come from the right image. An F16 store cannot hold every bit pattern
                // through the float attachment - NaN payloads get canonicalised and out-of-range values
                // clamp - which is exactly why `raw_image`, the parallel R16G16B16A16_UINT attachment the
                // fragment shaders write alongside it, exists. The two typeless paths below already prefer
                // it on the same condition (`raw_image && !content_is_blended`; blending can only be done
                // on the float attachment, so a blended surface has no trustworthy raw copy).
                constexpr bool preserve_bits_on_equal_size_cast = true;
                const bool is_reinterpretation = info.texture.format != casted->texture.format;
                const bool size_compatible = vk::blockSize(info.texture.format) == vk::blockSize(casted->texture.format);
                const bool extents_match = (src_w == dst_w) && (src_h == dst_h);
                const bool bit_copy = preserve_bits_on_equal_size_cast && is_reinterpretation && size_compatible && extents_match;

                const bool cast_use_raw = bit_copy && info.raw_image && !info.content_is_blended
                    && vk::blockSize(info.raw_image->format) == vk::blockSize(casted->texture.format);
                const vk::Image cast_src_image = cast_use_raw ? info.raw_image->image : info.texture.image;

                vk::ImageMemoryBarrier src_barrier{
                    .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderWrite,
                    .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                    .oldLayout = vk::ImageLayout::eGeneral,
                    .newLayout = vk::ImageLayout::eGeneral,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = cast_src_image,
                    .subresourceRange = vkutil::color_subresource_range
                };
                cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, src_barrier);

                if (bit_copy) {
                    const vk::ImageCopy copy{
                        .srcSubresource = vkutil::color_subresource_layer,
                        .srcOffset = vk::Offset3D{ static_cast<int32_t>(start_x), static_cast<int32_t>(start_sourced_line), 0 },
                        .dstSubresource = vkutil::color_subresource_layer,
                        .dstOffset = vk::Offset3D{ 0, 0, 0 },
                        .extent = vk::Extent3D{ static_cast<uint32_t>(src_w), static_cast<uint32_t>(src_h), 1 }
                    };
                    cmd_buffer.copyImage(cast_src_image, vk::ImageLayout::eGeneral, casted->texture.image, vk::ImageLayout::eTransferDstOptimal, copy);
                    LOG_INFO_ONCE("Reinterpreting a surface as a different format of the same size: copying the "
                                  "raw bytes from the {} image (raw_image={}, content_is_blended={})",
                        cast_use_raw ? "RAW" : "float", static_cast<bool>(info.raw_image), info.content_is_blended);
                } else {
                    // Filtering between texels of packed data blends unrelated bit patterns, so a cast that
                    // has to rescale at least picks whole texels.
                    const vk::Filter filter = is_reinterpretation ? vk::Filter::eNearest : vk::Filter::eLinear;
                    if (is_reinterpretation)
                        LOG_WARN_ONCE("Reinterpreting a surface as a different format while rescaling it "
                                      "({}x{} -> {}x{}); the bytes cannot be preserved through the resize",
                            src_w, src_h, dst_w, dst_h);
                    cmd_buffer.blitImage(info.texture.image, vk::ImageLayout::eGeneral, casted->texture.image, vk::ImageLayout::eTransferDstOptimal, blit, filter);
                }
            } else {
                LOG_INFO_ONCE("Game is doing typeless copies");

                const uint32_t ratio = bytes_per_pixel_in_store / bytes_per_pixel_requested;

                const uint32_t native_byte_offset = data_delta % stride_bytes;
                const uint32_t sub_texel_byte = native_byte_offset % bytes_per_pixel_in_store;

                if (use_compute_deinterleave) {
                    ensure_reinterpret_pipeline();

                    const uint32_t half_index = sub_texel_byte / bytes_per_pixel_requested;

                    if (!casted->reinterpret_view) {
                        vk::ImageViewCreateInfo reinterpret_view_info{
                            .image = casted->texture.image,
                            .viewType = vk::ImageViewType::e2D,
                            .format = vk::Format::eR32Uint,
                            .components = {},
                            .subresourceRange = vkutil::color_subresource_range
                        };
                        casted->reinterpret_view = state.device.createImageView(reinterpret_view_info);
                    }

                    // Read the 64-bit store as raw 32-bit word pairs through an R32G32_UINT
                    const bool want_raw_src = info.raw_image && !info.content_is_blended;
                    const vk::Image reinterpret_src = want_raw_src ? info.raw_image->image : info.texture.image;
                    if (info.reinterpret_store_view && info.reinterpret_view_is_raw != want_raw_src) {
                        state.frame().destroy_queue.add(info.reinterpret_store_view);
                        info.reinterpret_store_view = nullptr;
                    }
                    if (!info.reinterpret_store_view) {
                        vk::ImageViewCreateInfo store_view_info{
                            .image = reinterpret_src,
                            .viewType = vk::ImageViewType::e2D,
                            .format = vk::Format::eR32G32Uint,
                            .components = {},
                            .subresourceRange = vkutil::color_subresource_range
                        };
                        info.reinterpret_store_view = state.device.createImageView(store_view_info);
                        info.reinterpret_view_is_raw = want_raw_src;
                    }

                    // Make the freshly-rendered store visible to the compute read.
                    vk::ImageMemoryBarrier store_to_compute{
                        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderWrite,
                        .dstAccessMask = vk::AccessFlagBits::eShaderRead,
                        .oldLayout = vk::ImageLayout::eGeneral,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = reinterpret_src,
                        .subresourceRange = vkutil::color_subresource_range
                    };
                    cmd_buffer.pipelineBarrier(
                        vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader,
                        vk::PipelineStageFlagBits::eComputeShader, {}, {}, {}, store_to_compute);

                    vk::DescriptorSet dset = reinterpret_desc_sets[reinterpret_desc_idx];
                    reinterpret_desc_idx = (reinterpret_desc_idx + 1) % static_cast<uint32_t>(reinterpret_desc_sets.size());

                    vk::DescriptorImageInfo store_ii{ reinterpret_sampler, info.reinterpret_store_view, vk::ImageLayout::eGeneral };
                    vk::DescriptorImageInfo cast_ii{ nullptr, casted->reinterpret_view, vk::ImageLayout::eGeneral };
                    std::array<vk::WriteDescriptorSet, 2> writes;
                    writes[0] = vk::WriteDescriptorSet{ .dstSet = dset, .dstBinding = 0, .dstArrayElement = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler };
                    writes[0].setImageInfo(store_ii);
                    writes[1] = vk::WriteDescriptorSet{ .dstSet = dset, .dstBinding = 1, .dstArrayElement = 0, .descriptorType = vk::DescriptorType::eStorageImage };
                    writes[1].setImageInfo(cast_ii);
                    state.device.updateDescriptorSets(writes, {});

                    ReinterpretPushConstants pc{
                        .out_width = width,
                        .out_height = height,
                        .scaled_store_w = info.texture.width,
                        .scaled_store_h = info.texture.height,
                        .ratio = ratio,
                        .half_index = half_index,
                        .interleave = (!info.has_phase_view && width == ratio * info.texture.width) ? 1u : 0u
                    };
                    cmd_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, reinterpret_pipeline);
                    cmd_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, reinterpret_pipeline_layout, 0, dset, {});
                    cmd_buffer.pushConstants(reinterpret_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(pc), &pc);
                    // 2D dispatch keeps the per-axis workgroup count well under the limit at high multipliers.
                    cmd_buffer.dispatch((width + 7u) / 8u, (height + 7u) / 8u, 1);
                } else {
                    // Direct byte reinterpret (cropped / partial reads)
                    const uint32_t src_pixel_stride = static_cast<uint32_t>((info.stride_bytes / bytes_per_pixel_in_store) * state.res_multiplier);
                    const uint32_t scaled_store_col = static_cast<uint32_t>((native_byte_offset / bytes_per_pixel_in_store) * state.res_multiplier);
                    const uint32_t src_byte_offset = scaled_store_col * bytes_per_pixel_in_store + sub_texel_byte;
                    const uint32_t dst_pixel_stride = src_pixel_stride * ratio;

                    const vk::DeviceSize buffer_size = static_cast<vk::DeviceSize>(src_pixel_stride) * bytes_per_pixel_in_store * align(height, 4) + src_byte_offset + bytes_per_pixel_in_store;

                    if (!casted->transition_buffer.buffer || casted->transition_buffer.size < buffer_size) {
                        state.frame().destroy_queue.add_buffer(casted->transition_buffer);
                        casted->transition_buffer = vkutil::Buffer(buffer_size);
                        casted->transition_buffer.init_buffer(vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc);
                    }

                    vk::BufferImageCopy copy_image_buffer{
                        .bufferOffset = 0,
                        .bufferRowLength = src_pixel_stride,
                        .bufferImageHeight = height,
                        .imageSubresource = vkutil::color_subresource_layer,
                        .imageOffset = { 0, static_cast<int32_t>(start_sourced_line), 0 },
                        .imageExtent = { info.width, height, 1 }
                    };
                    const bool byte_use_raw = info.raw_image && !info.content_is_blended;
                    const vk::Image byte_src = byte_use_raw ? info.raw_image->image : info.texture.image;
                    vk::ImageMemoryBarrier img_barrier{
                        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderWrite,
                        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                        .oldLayout = vk::ImageLayout::eGeneral,
                        .newLayout = vk::ImageLayout::eGeneral,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = byte_src,
                        .subresourceRange = vkutil::color_subresource_range
                    };
                    cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, img_barrier);
                    cmd_buffer.copyImageToBuffer(byte_src, vk::ImageLayout::eGeneral, casted->transition_buffer.buffer, copy_image_buffer);

                    vk::BufferMemoryBarrier buf_barrier{
                        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
                        .dstAccessMask = vk::AccessFlagBits::eTransferRead,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .buffer = casted->transition_buffer.buffer,
                        .offset = 0,
                        .size = VK_WHOLE_SIZE
                    };
                    cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer, {}, {}, buf_barrier, {});

                    copy_image_buffer
                        .setBufferOffset(src_byte_offset)
                        .setBufferRowLength(dst_pixel_stride)
                        .setImageOffset({ 0, 0, 0 })
                        .setImageExtent({ width, height, 1 });
                    cmd_buffer.copyBufferToImage(casted->transition_buffer.buffer, casted->texture.image, vk::ImageLayout::eTransferDstOptimal, copy_image_buffer);
                }
            }
            casted->texture.transition_to(cmd_buffer, vkutil::ImageLayout::SampledImage);
        };

        pending_casts.push_back({ casted->texture.view, casted->alt_gamma_view, &info, std::move(record_cast) });

        const bool base_is_srgb = casted->texture.format == vk::Format::eR8G8B8A8Srgb;
        const bool use_alt_view = casted->alt_gamma_view && (is_srgb != base_is_srgb);
        return TextureLookupResult{
            use_alt_view ? casted->alt_gamma_view : casted->texture.view,
            vkutil::ImageLayout::SampledImage,
            use_alt_view ? (base_is_srgb ? vk::Format::eR8G8B8A8Unorm : vk::Format::eR8G8B8A8Srgb) : casted->texture.format,
            is_typeless_cast && !info.has_phase_view,
            cast_phase_hi,
            raw_bits_cast
        };
    } else {
        // the renderpass external dependencies should take care of the barrier
        if (swizzle == info.swizzle && vk_format == info.texture.format)
            // we can use the same texture view
            return TextureLookupResult{
                info.texture.view,
                info.texture.layout,
                info.texture.format
            };

        if (!info.alternate_view) {
            vk::ComponentMapping resulting_mapping = vkutil::color_to_texture_swizzle(info.swizzle, swizzle);

            vk::ImageViewCreateInfo view_info{
                .image = info.texture.image,
                .viewType = vk::ImageViewType::e2D,
                .format = vk_format,
                .components = resulting_mapping,
                .subresourceRange = vkutil::color_subresource_range
            };
            info.alternate_view = state.device.createImageView(view_info);
        }

        return TextureLookupResult{
            info.alternate_view,
            vkutil::ImageLayout::ColorAttachmentReadWrite,
            vk_format
        };
    }
}

bool VKSurfaceCache::begin_ds_scene_depth_check(const SceGxmDepthStencilSurface &depth_stencil, bool this_scene_stores, Address scene_color_addr) {
    DepthStencilSurfaceCacheInfo *cached_info = nullptr;
    if (depth_stencil.depth_data) {
        auto it = depth_address_lookup.find(depth_stencil.depth_data.address());
        if (it != depth_address_lookup.end())
            cached_info = it->second;
    } else if (depth_stencil.stencil_data) {
        auto it = stencil_address_lookup.find(depth_stencil.stencil_data.address());
        if (it != stencil_address_lookup.end())
            cached_info = it->second;
    }

    pending_ds_scene = cached_info;
    pending_ds_scene_stores = this_scene_stores;

    if (cached_info == nullptr)
        // surface not created yet: it will be created cleared, loading that is fine
        return true;

    const bool is_continuation = cached_info->last_scene_color_addr == scene_color_addr;
    cached_info->last_scene_color_addr = scene_color_addr;

    return cached_info->depth_content_stored || is_continuation;
}

bool VKSurfaceCache::try_transfer_depth_gpu(Address src_address, Address dst_address, uint32_t width, uint32_t height) {
    if (src_address == dst_address)
        return false;

    auto src_it = depth_address_lookup.find(src_address);
    auto dst_it = depth_address_lookup.find(dst_address);
    if (src_it == depth_address_lookup.end() || dst_it == depth_address_lookup.end())
        return false;
    if (color_address_lookup.contains(src_address) || color_address_lookup.contains(dst_address))
        return false;

    DepthStencilSurfaceCacheInfo *src_info = src_it->second;
    DepthStencilSurfaceCacheInfo *dst_info = dst_it->second;
    if (src_info == nullptr || dst_info == nullptr || src_info == dst_info)
        return false;
    if (!src_info->texture.image || !dst_info->texture.image)
        return false;

    const uint32_t scaled_width = static_cast<uint32_t>(width * state.res_multiplier);
    const uint32_t scaled_height = static_cast<uint32_t>(height * state.res_multiplier);
    const uint32_t copy_width = std::min({ scaled_width, src_info->texture.width, dst_info->texture.width });
    const uint32_t copy_height = std::min({ scaled_height, src_info->texture.height, dst_info->texture.height });
    if (copy_width == 0 || copy_height == 0)
        return false;

    vk::CommandBuffer transfer_cmd = nullptr;
    vk::Fence fence = state.device.createFence({});
    {
        std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
        transfer_cmd = vkutil::create_single_time_command(state.device, state.multithread_command_pool);

        src_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::TransferSrc, vkutil::ds_subresource_range);
        dst_info->texture.transition_to_discard(transfer_cmd, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);

        vk::ImageSubresourceLayers layers = vkutil::color_subresource_layer;
        layers.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        vk::ImageCopy image_copy{
            .srcSubresource = layers,
            .srcOffset = { 0, 0, 0 },
            .dstSubresource = layers,
            .dstOffset = { 0, 0, 0 },
            .extent = { copy_width, copy_height, 1U }
        };
        transfer_cmd.copyImage(src_info->texture.image, vk::ImageLayout::eTransferSrcOptimal, dst_info->texture.image, vk::ImageLayout::eTransferDstOptimal, image_copy);

        src_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
        dst_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);

        transfer_cmd.end();
    }

    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(transfer_cmd);
    state.general_queue.submit(submit_info, fence);

    dst_info->depth_content_stored = true;

    CallbackRequestFunction vk_callback = [&state = this->state, fence, transfer_cmd]() {
        const auto result = state.device.waitForFences(fence, vk::True, std::numeric_limits<uint64_t>::max());
        if (result != vk::Result::eSuccess)
            LOG_ERROR("Could not wait for the depth transfer fence.");

        state.device.destroyFence(fence);

        std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
        state.device.freeCommandBuffers(state.multithread_command_pool, transfer_cmd);
    };
    state.request_queue.push(CallbackRequest{ new CallbackRequestFunction(std::move(vk_callback)) });

    LOG_INFO_ONCE("Depth transfer done on the GPU: 0x{:08X} -> 0x{:08X} {}x{} (scaled {}x{})", src_address, dst_address, width, height, copy_width, copy_height);

    return true;
}

void VKSurfaceCache::resolve_ds_scene_end(bool scene_wrote_depth) {
    if (pending_ds_scene != nullptr && scene_wrote_depth)
        pending_ds_scene->depth_content_stored = pending_ds_scene_stores;
    pending_ds_scene = nullptr;
}

SurfaceRetrieveResult VKSurfaceCache::retrieve_depth_stencil_for_framebuffer(SceGxmDepthStencilSurface *depth_stencil, const uint32_t width, const uint32_t height) {
    // when writing we use the render target size which is already upscaled
    int32_t memory_width = static_cast<int32_t>(width / state.res_multiplier);
    int32_t memory_height = static_cast<int32_t>(height / state.res_multiplier);

    const SurfaceTiling tiling = (depth_stencil->get_type() == SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR) ? SurfaceTiling::Linear : SurfaceTiling::Tiled;

    // check if MSAA is used, the depth buffer is never downscaled
    if (target->multisample_mode != SCE_GXM_MULTISAMPLE_NONE)
        memory_height *= 2;
    if (target->multisample_mode == SCE_GXM_MULTISAMPLE_4X)
        memory_width *= 2;

    const bool is_stencil_only = depth_stencil->depth_data.address() == 0;
    DepthStencilSurfaceCacheInfo *cached_info = nullptr;

    if (!is_stencil_only) {
        auto it = depth_address_lookup.find(depth_stencil->depth_data.address());
        if (it != depth_address_lookup.end())
            cached_info = it->second;
    } else {
        auto it = stencil_address_lookup.find(depth_stencil->stencil_data.address());
        if (it != stencil_address_lookup.end())
            cached_info = it->second;
    }

    if (cached_info != nullptr) {
        // this the most recently used depth-stencil surface
        ds_surface_queue.set_as_mru(cached_info);

        const bool need_remake = cached_info->texture.width < width
            || cached_info->texture.height < height
            || cached_info->stride_samples != depth_stencil->get_stride()
            || cached_info->tiling != tiling;

        if (!need_remake) {
            // MSAA passes rasterise at the sample rate, so a pass whose colour surface downscales
            // draws through a viewport half the size of the pass that filled the shared depth/stencil
            constexpr bool disable_ds_resample = false;

            VKContext *ctx = reinterpret_cast<VKContext *>(state.context);
            uint32_t sx = 1, sy = 1;
            if (!disable_ds_resample
                && target->multisample_mode != SCE_GXM_MULTISAMPLE_NONE && ctx && ctx->record.color_surface.downscale
                && !depth_stencil->force_store) {
                sy = 2;
                if (target->multisample_mode == SCE_GXM_MULTISAMPLE_4X)
                    sx = 2;
            }
            vkutil::Image &full = cached_info->texture;
            const uint32_t src_w = width * sx;
            const uint32_t src_h = height * sy;
            const vk::FormatFeatureFlags blit_feats = vk::FormatFeatureFlagBits::eBlitSrc | vk::FormatFeatureFlagBits::eBlitDst;
            if ((sx > 1 || sy > 1) && full.image && full.width >= src_w && full.height >= src_h
                && (state.physical_device.getFormatProperties(full.format).optimalTilingFeatures & blit_feats) == blit_feats) {
                if (!cached_info->sample_rate_copy || cached_info->sample_rate_copy->width != width || cached_info->sample_rate_copy->height != height) {
                    if (cached_info->sample_rate_copy) {
                        destroy_framebuffers(cached_info->sample_rate_copy->view);
                        state.frame().destroy_queue.add_image(*cached_info->sample_rate_copy);
                        cached_info->sample_rate_copy.reset();
                    }
                    cached_info->sample_rate_copy = std::make_unique<vkutil::Image>(width, height, full.format);
                    cached_info->sample_rate_copy->init_image(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);
                }
                vkutil::Image &resampled = *cached_info->sample_rate_copy;
                vk::CommandBuffer cmd_buffer = reinterpret_cast<VKContext *>(state.context)->prerender_cmd;
                full.transition_to(cmd_buffer, vkutil::ImageLayout::TransferSrc, vkutil::ds_subresource_range);
                resampled.transition_to_discard(cmd_buffer, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);
                const std::array<vk::Offset3D, 2> src_bounds{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(src_w - (sx - 1)), static_cast<int32_t>(src_h - (sy - 1)), 1 } };
                const std::array<vk::Offset3D, 2> dst_bounds{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ static_cast<int32_t>(width), static_cast<int32_t>(height), 1 } };
                const auto region_for = [&](vk::ImageAspectFlagBits aspect) {
                    const vk::ImageSubresourceLayers layers{ aspect, 0, 0, 1 };
                    return vk::ImageBlit{
                        .srcSubresource = layers,
                        .srcOffsets = src_bounds,
                        .dstSubresource = layers,
                        .dstOffsets = dst_bounds
                    };
                };
                const std::array<vk::ImageBlit, 2> blit_regions{
                    region_for(vk::ImageAspectFlagBits::eDepth),
                    region_for(vk::ImageAspectFlagBits::eStencil)
                };
                cmd_buffer.blitImage(full.image, vk::ImageLayout::eTransferSrcOptimal, resampled.image, vk::ImageLayout::eTransferDstOptimal, blit_regions, vk::Filter::eNearest);
                full.transition_to(cmd_buffer, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
                resampled.transition_to(cmd_buffer, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
                LOG_INFO_ONCE("Depth/stencil resampled to the MSAA sample rate for a downscaling pass: "
                              "{}x{} region of {}x{} -> {}x{}",
                    src_w, src_h, full.width, full.height, width, height);
                return { resampled.view, &resampled };
            }

            return {
                cached_info->texture.view,
                &cached_info->texture
            };
        }
    } else {
        // retrieve a new depth stencil
        cached_info = ds_surface_queue.get_lru();
    }

    // erase it if it was used previously
    if (cached_info->surface.depth_data)
        depth_address_lookup.erase(cached_info->surface.depth_data.address());
    if (cached_info->surface.stencil_data)
        stencil_address_lookup.erase(cached_info->surface.stencil_data.address());
    if (cached_info->texture.image)
        destroy_surface(*cached_info);

    // update the lookup info
    ds_surface_queue.set_as_mru(cached_info);
    if (depth_stencil->depth_data)
        depth_address_lookup[depth_stencil->depth_data.address()] = cached_info;
    if (depth_stencil->stencil_data)
        stencil_address_lookup[depth_stencil->stencil_data.address()] = cached_info;

    cached_info->surface = *depth_stencil;
    cached_info->memory_width = memory_width;
    cached_info->memory_height = memory_height;
    cached_info->multisample_mode = target->multisample_mode;
    cached_info->stride_samples = depth_stencil->get_stride();
    cached_info->tiling = tiling;

    uint32_t bytes_per_sample;
    switch (depth_stencil->get_format()) {
    case SCE_GXM_DEPTH_STENCIL_FORMAT_S8:
        bytes_per_sample = 1;
        break;
    case SCE_GXM_DEPTH_STENCIL_FORMAT_D16:
        bytes_per_sample = 2;
        break;
    default:
        bytes_per_sample = 4;
        break;
    }
    cached_info->total_bytes = bytes_per_sample * depth_stencil->get_stride() * memory_height;

    vkutil::Image &image = cached_info->texture;

    // use prerender cmd in case we read from the depth buffer (although I really doubt this could happen)
    VKContext *context = reinterpret_cast<VKContext *>(state.context);
    vk::CommandBuffer cmd_buffer = context->prerender_cmd;

    image.width = width;
    image.height = height;
    image.format = state.deep_stencil_use;
    image.layout = vkutil::ImageLayout::Undefined;
    image.init_image(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);

    image.transition_to(cmd_buffer, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);
    vk::ClearDepthStencilValue clear_value{
        .depth = 1.0,
        .stencil = 0
    };
    cmd_buffer.clearDepthStencilImage(image.image, vk::ImageLayout::eTransferDstOptimal, clear_value, vkutil::ds_subresource_range);
    image.transition_to(cmd_buffer, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);

    return {
        image.view,
        &image
    };
}

std::optional<TextureLookupResult> VKSurfaceCache::retrieve_depth_stencil_as_texture(const SceGxmTexture &texture, TextureViewport *texture_viewport) {
    SceGxmTextureBaseFormat base_format = gxm::get_base_format(gxm::get_format(texture));
    bool can_be_depth = false;
    bool can_be_stencil = false;

    uint32_t bytes_per_sample = 4;
    switch (base_format) {
        // 8bit stencil
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8:
        bytes_per_sample = 1;
        can_be_stencil = true;
        break;
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16:
        bytes_per_sample = 2;
        [[fallthrough]];
    case SCE_GXM_TEXTURE_BASE_FORMAT_X8U24:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32:
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32M:
        can_be_depth = true;
        break;
    default:
        break;
    }
    int32_t memory_width = gxm::get_width(texture);
    int32_t memory_height = gxm::get_height(texture);

    SurfaceTiling tiling;
    uint32_t stride_samples;

    switch (texture.texture_type()) {
    case SCE_GXM_TEXTURE_LINEAR:
        tiling = SurfaceTiling::Linear;
        stride_samples = align(memory_width, 8);
        break;
    case SCE_GXM_TEXTURE_LINEAR_STRIDED:
        tiling = SurfaceTiling::Linear;
        stride_samples = (gxm::get_stride_in_bytes(texture) * 8) / gxm::bits_per_pixel(base_format);
        break;
    case SCE_GXM_TEXTURE_TILED:
        tiling = SurfaceTiling::Tiled;
        stride_samples = align(memory_width, 32);
        break;
    default:
        // a depth/stencil is never swizzled
        return std::nullopt;
    }

    if (stride_samples % 32 != 0)
        // a depth/stencil always has a stride which is a multiple of the tile size
        return std::nullopt;

    // take upscaling into account
    uint32_t width = static_cast<uint32_t>(memory_width * state.res_multiplier);
    uint32_t height = static_cast<uint32_t>(memory_height * state.res_multiplier);
    uint32_t total_bytes = bytes_per_sample * stride_samples * memory_height;

    const uint32_t address = texture.data_addr << 2;
    uint32_t surface_address = 0;
    DepthStencilSurfaceCacheInfo *found_info = nullptr;

    auto surface_rows_allocated = [](const DepthStencilSurfaceCacheInfo *info) -> uint32_t {
        if (info->memory_height <= 0)
            return 0;
        const uint32_t rows = static_cast<uint32_t>(info->memory_height);
        return (info->tiling == SurfaceTiling::Tiled) ? align(rows, 32u) : rows;
    };

    if (can_be_depth) {
        // get the first depth surface with an address lower or equal to address
        auto it = depth_address_lookup.upper_bound(address);
        if (it != depth_address_lookup.begin()) {
            --it;

            uint32_t surface_bytes = it->second->total_bytes;
            if (it->second->memory_height > 0)
                surface_bytes = (surface_bytes / static_cast<uint32_t>(it->second->memory_height)) * surface_rows_allocated(it->second);

            // the texture must be contained entirely in the depth surface
            if (address + total_bytes <= it->first + surface_bytes) {
                surface_address = it->first;
                found_info = it->second;
            }
        }
    }
    if (!found_info && can_be_stencil) {
        // get the first stencil surface with an address lower or equal to address
        auto it = stencil_address_lookup.upper_bound(address);
        if (it != stencil_address_lookup.begin()) {
            --it;

            // note: we don't support sampling the stencil from a D24S8 depth-stencil
            // so we can assume any stencil uses only 1 byte per sample
            uint32_t surface_bytes = it->second->stride_samples * surface_rows_allocated(it->second) * 1;

            // the texture must be contained entirely in the stencil surface
            if (address + total_bytes <= it->first + surface_bytes) {
                surface_address = it->first;
                found_info = it->second;
            }
        }
    }

    if (found_info == nullptr)
        return std::nullopt;

    DepthStencilSurfaceCacheInfo &cached_info = *found_info;
    if (tiling != cached_info.tiling || stride_samples != cached_info.stride_samples)
        return std::nullopt;

    // we sample from it, set the surface as most recently used
    ds_surface_queue.set_as_mru(found_info);

    // take MSAA into account
    if (cached_info.multisample_mode != SCE_GXM_MULTISAMPLE_NONE)
        height /= 2;
    if (cached_info.multisample_mode == SCE_GXM_MULTISAMPLE_4X)
        width /= 2;

    const bool is_stencil = can_be_stencil;

    const uint32_t delta_samples = (address - surface_address) / bytes_per_sample;
    uint32_t delta_col_samples = delta_samples % stride_samples;
    uint32_t delta_row_samples = delta_samples / stride_samples;

    vk::ImageView ds_attachment = reinterpret_cast<VKContext *>(state.context)->current_ds_view;
    const bool reading_ds_attachment = cached_info.texture.view == ds_attachment;
    const bool same_dimension = memory_width == cached_info.memory_width
        && memory_height == cached_info.memory_height
        && delta_col_samples == 0
        && delta_row_samples == 0;

    if (!reading_ds_attachment && (state.features.use_texture_viewport || same_dimension)) {
        // we can just sample from the surface itself

        // we must create a new read-only view if it is not already present
        vk::ImageView &img_view = is_stencil ? cached_info.stencil_view : cached_info.depth_view;
        if (!img_view) {
            vk::ImageSubresourceRange range = vkutil::ds_subresource_range;
            range.aspectMask = is_stencil ? vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eDepth;
            vk::ImageViewCreateInfo view_info{
                .image = cached_info.texture.image,
                .viewType = vk::ImageViewType::e2D,
                .format = state.deep_stencil_use,
                .components = {},
                .subresourceRange = range
            };
            img_view = state.device.createImageView(view_info);
        }

        const float inv_surface_width = 1 / static_cast<float>(cached_info.memory_width);
        const float inv_surface_height = 1 / static_cast<float>(cached_info.memory_height);
        if (state.features.use_texture_viewport) {
            texture_viewport->offset = {
                delta_col_samples * inv_surface_width,
                delta_row_samples * inv_surface_height
            };
            texture_viewport->ratio = {
                memory_width * inv_surface_width,
                memory_height * inv_surface_height
            };
        }

        return TextureLookupResult{
            img_view,
            vkutil::ImageLayout::DepthStencilReadOnly,
            state.deep_stencil_use
        };
    }

    const uint64_t scene_timestamp = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

    int read_surface_idx = -1;
    for (int i = 0; i < cached_info.read_surfaces.size(); i++) {
        auto &read_surface = cached_info.read_surfaces[i];
        if (read_surface.depth_view.width == width
            && read_surface.depth_view.height == height
            && read_surface.delta_row == delta_row_samples
            && read_surface.delta_col == delta_col_samples) {
            read_surface_idx = i;
            break;
        }
    }

    if (read_surface_idx == -1) {
        // no compatible read surface found

        DepthSurfaceView read_only{
            .depth_view = vkutil::Image(width, height, state.deep_stencil_use),
            .scene_timestamp = 0,
            .delta_col = delta_col_samples,
            .delta_row = delta_row_samples,
        };
        read_only.depth_view.init_image(vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
        // we want a texture view with only the depth or stencil aspect bit
        // TODO: not efficient
        state.device.destroy(read_only.depth_view.view);
        read_only.depth_view.view = nullptr;

        read_surface_idx = cached_info.read_surfaces.size();
        cached_info.read_surfaces.emplace_back(std::move(read_only));
    }

    DepthSurfaceView &read_only = cached_info.read_surfaces[read_surface_idx];
    vkutil::Image &img_view = is_stencil ? read_only.stencil_view : read_only.depth_view;

    if (!img_view.view) {
        vk::ImageSubresourceRange range = vkutil::ds_subresource_range;
        range.aspectMask = is_stencil ? vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eDepth;
        vk::ImageViewCreateInfo view_info{
            .image = read_only.depth_view.image,
            .viewType = vk::ImageViewType::e2D,
            .format = state.deep_stencil_use,
            .components = {},
            .subresourceRange = range
        };
        img_view.view = state.device.createImageView(view_info);
        img_view.layout = vkutil::ImageLayout::SampledImage;
    }

    // copy the depth stencil only once per scene
    if (read_only.scene_timestamp == scene_timestamp)
        return TextureLookupResult{
            img_view.view,
            img_view.layout,
            img_view.format
        };

    read_only.scene_timestamp = scene_timestamp;

    // use prerender cmd as we can't copy an image or use pipeline barriers in a render pass
    VKContext *context = reinterpret_cast<VKContext *>(state.context);
    vk::CommandBuffer cmd_buffer = context->prerender_cmd;

    delta_row_samples *= state.res_multiplier;
    delta_col_samples *= state.res_multiplier;

    read_only.depth_view.transition_to_discard(cmd_buffer, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);

    cached_info.texture.transition_to(cmd_buffer, vkutil::ImageLayout::TransferSrc, vkutil::ds_subresource_range);
    vk::ImageSubresourceLayers layers = vkutil::color_subresource_layer;
    layers.aspectMask = vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
    vk::ImageCopy image_copy{
        .srcSubresource = layers,
        .srcOffset = { static_cast<int>(delta_col_samples), static_cast<int>(delta_row_samples), 0 },
        .dstSubresource = layers,
        .dstOffset = { 0, 0, 0 },
        .extent = { std::min(width, cached_info.texture.width - delta_col_samples), std::min(height, cached_info.texture.height - delta_row_samples), 1U }
    };
    cmd_buffer.copyImage(cached_info.texture.image, vk::ImageLayout::eTransferSrcOptimal, read_only.depth_view.image, vk::ImageLayout::eTransferDstOptimal, image_copy);

    // transition back
    cached_info.texture.transition_to(cmd_buffer, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
    read_only.depth_view.transition_to(cmd_buffer, vkutil::ImageLayout::SampledImage, vkutil::ds_subresource_range);

    return TextureLookupResult{
        img_view.view,
        img_view.layout,
        img_view.format
    };
}

static Framebuffer empty_framebuffer{};
Framebuffer &VKSurfaceCache::retrieve_framebuffer_handle(MemState &mem, SceGxmColorSurface *color, SceGxmDepthStencilSurface *depth_stencil,
    vk::RenderPass standard_render_pass, vk::RenderPass interlock_render_pass, vk::ImageView &color_view, vk::ImageView &ds_view) {
    if (!target) {
        LOG_ERROR("Unable to retrieve framebuffer with no active render target!");
        return empty_framebuffer;
    }

    if (!color && !depth_stencil)
        LOG_ERROR_ONCE("Depth stencil and color surface are both null!");

    // might get modified by retrieve_color_surface_for_framebuffer
    state.pipeline_cache.can_use_deferred_compilation = true;

    // First retrieve separately the color surface and ds surface
    SurfaceRetrieveResult color_result;
    SurfaceRetrieveResult ds_result;

    if (color) {
        color_result = retrieve_color_surface_for_framebuffer(mem, color);
    } else {
        color_result.view = target->color.view;
        color_result.base_image = &target->color;
    }

    if (depth_stencil) {
        ds_result = retrieve_depth_stencil_for_framebuffer(depth_stencil, target->width, target->height);
    } else {
        ds_result.view = target->depthstencil.view;
        ds_result.base_image = &target->depthstencil;
    }

    color_view = color_result.view;
    ds_view = ds_result.view;

    std::pair<vk::ImageView, vk::ImageView> key = { color_view, ds_view };
    auto it = framebuffer_array.find(key);

    if (it != framebuffer_array.end()) {
        // we already created a framebuffer for this pair
        return it->second;
    }

    // make the framebuffer as big as possible
    const uint32_t framebuffer_width = std::min(color_result.base_image->width, ds_result.base_image->width);
    const uint32_t framebuffer_height = std::min(color_result.base_image->height, ds_result.base_image->height);

    vk::FramebufferCreateInfo fb_info{
        .renderPass = standard_render_pass,
        .width = framebuffer_width,
        .height = framebuffer_height,
        .layers = 1
    };
    vk::ImageView attachments[] = { color_result.view, color_result.raw_image ? color_result.raw_image->view : ds_result.view, ds_result.view };
    fb_info.setAttachments(attachments);
    fb_info.attachmentCount = color_result.raw_image ? 3 : 2;
    vk::Framebuffer fb_standard = state.device.createFramebuffer(fb_info);

    vk::Framebuffer fb_interlock = nullptr;
    if (state.features.support_shader_interlock) {
        // we also need to create the framebuffer for shader interlock
        fb_info.renderPass = interlock_render_pass;
        fb_info.pAttachments = &attachments[2];
        fb_info.attachmentCount = 1;
        fb_interlock = state.device.createFramebuffer(fb_info);
    }

    return (framebuffer_array[key] = { fb_standard, fb_interlock, color_result.base_image, framebuffer_width, framebuffer_height, color_result.raw_image });
}

bool VKSurfaceCache::check_for_surface(MemState &mem, Address source_address, CallbackRequestFunction &callback, Address target_address) {
    if (!state.features.enable_memory_mapping || state.disable_surface_sync)
        return false;

    if (vector_utils::find_index(cpu_surfaces_changed, source_address) != -1) {
        // there is a transfer operation pending on this surface, just add the callback after and we are done
        state.request_queue.push(CallbackRequest{ new CallbackRequestFunction(std::move(callback)) });

        if (target_address)
            cpu_surfaces_changed.push_back(target_address);
        return true;
    }

    // for now, only look if the address matches exactly a color surface
    auto it = color_address_lookup.find(source_address);
    if (it == color_address_lookup.end())
        return false;

    auto &surface = *it->second;
    VKContext &context = *static_cast<VKContext *>(state.context);
    // if the frame is already rendered skip
    // Note: that's not the best behavior but it should be fine
    // also it prevents invalidated surfaces from causing issues
    if (surface.last_frame_rendered + MAX_FRAMES_RENDERING <= context.frame_timestamp)
        return false;

    // we found something
    if (!*surface.need_surface_sync) {
        // first send the command to sync the surface with the GPU
        *surface.need_surface_sync = true;

        // we shouldn't have a command buffer being used, but just in case
        vk::CommandBuffer prev_cmd = context.render_cmd;

        // for the time being, just create a temp command buffer / fence
        // That's not the best approach but I guess it works
        vk::CommandBuffer surface_cmd = nullptr;
        vk::Fence fence = state.device.createFence({});
        ColorSurfaceCacheInfo *returned_info = nullptr;
        {
            std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
            surface_cmd = vkutil::create_single_time_command(state.device, state.multithread_command_pool);

            context.render_cmd = surface_cmd;
            last_written_surface = &surface;
            returned_info = perform_surface_sync();
            context.render_cmd = prev_cmd;

            surface_cmd.end();
        }
        // submit this command
        vk::SubmitInfo submit_info{};
        submit_info.setCommandBuffers(surface_cmd);
        state.general_queue.submit(submit_info, fence);

        // now we need to wait for the fence, then destroy it along with the command buffer
        // to prevent memory leaks
        CallbackRequestFunction vk_callback = [&state = this->state, fence, surface_cmd]() {
            auto result = state.device.waitForFences(fence, vk::True, std::numeric_limits<uint64_t>::max());
            if (result != vk::Result::eSuccess)
                LOG_ERROR("Could not wait for fences.");

            // destroy the objects
            state.device.destroyFence(fence);

            std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
            state.device.freeCommandBuffers(state.multithread_command_pool, surface_cmd);
        };
        state.request_queue.push(CallbackRequest{ new CallbackRequestFunction(std::move(vk_callback)) });

        if (returned_info)
            state.request_queue.push(PostSurfaceSyncRequest{ returned_info });
    }

    // now push the callback
    state.request_queue.push(CallbackRequest{ new CallbackRequestFunction(std::move(callback)) });

    if (target_address)
        cpu_surfaces_changed.push_back(target_address);

    return true;
}

void VKSurfaceCache::submit_immediate_surface_sync(ColorSurfaceCacheInfo &surface, MemState *mem, Address sync_addr, uint32_t sync_size) {
    VKContext &context = *static_cast<VKContext *>(state.context);

    // first send the command to sync the surface with the GPU
    *surface.need_surface_sync = true;

    // we shouldn't have a command buffer being used, but just in case
    vk::CommandBuffer prev_cmd = context.render_cmd;
    // this can be called mid-scene, so preserve the scene's last written surface
    ColorSurfaceCacheInfo *prev_last_written = last_written_surface;

    // for the time being, just create a temp command buffer / fence
    // That's not the best approach but I guess it works
    vk::CommandBuffer surface_cmd = nullptr;
    vk::Fence fence = state.device.createFence({});
    ColorSurfaceCacheInfo *returned_info = nullptr;
    {
        std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
        surface_cmd = vkutil::create_single_time_command(state.device, state.multithread_command_pool);

        context.render_cmd = surface_cmd;
        last_written_surface = &surface;
        returned_info = perform_surface_sync();
        context.render_cmd = prev_cmd;
        last_written_surface = (prev_last_written == &surface) ? nullptr : prev_last_written;

        surface_cmd.end();
    }
    // submit this command
    vk::SubmitInfo submit_info{};
    submit_info.setCommandBuffers(surface_cmd);
    state.general_queue.submit(submit_info, fence);

    if (mem != nullptr) {
        // synchronous completion: the caller (a CPU operation like sceGxmTransfer) needs the
        // guest memory content to be correct and ordered exactly like the hardware transfer
        // unit would � wait for the copy and write guest RAM before returning
        auto result = state.device.waitForFences(fence, vk::True, std::numeric_limits<uint64_t>::max());
        if (result != vk::Result::eSuccess)
            LOG_ERROR("Could not wait for fences.");
        state.device.destroyFence(fence);
        {
            std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
            state.device.freeCommandBuffers(state.multithread_command_pool, surface_cmd);
        }

        if (returned_info) {
            surface_sync_internal_write = true;
            perform_post_surface_sync(*mem, returned_info);
            surface_sync_internal_write = false;
        }
        return;
    }

    // asynchronous completion: wait for the fence on the wait thread, then destroy it along
    // with the command buffer to prevent memory leaks
    CallbackRequestFunction vk_callback = [&state = this->state, fence, surface_cmd]() {
        auto result = state.device.waitForFences(fence, vk::True, std::numeric_limits<uint64_t>::max());
        if (result != vk::Result::eSuccess)
            LOG_ERROR("Could not wait for fences.");

        // destroy the objects
        state.device.destroyFence(fence);

        std::lock_guard<std::mutex> lock(state.multithread_pool_mutex);
        state.device.freeCommandBuffers(state.multithread_command_pool, surface_cmd);
    };
    state.request_queue.push(CallbackRequest{ new CallbackRequestFunction(std::move(vk_callback)) });

    if (returned_info) {
        state.request_queue.push(PostSurfaceSyncRequest{ returned_info });
    }
}

bool VKSurfaceCache::sync_surface_for_gpu_read(Address address, uint32_t size) {
    if (!state.features.enable_memory_mapping || state.disable_surface_sync)
        return false;

    // Range-based lookup: find the surface whose base address is <= address
    auto it = color_address_lookup.upper_bound(address);
    if (it == color_address_lookup.begin())
        return false;
    --it;

    // A smaller surface can shadow a larger one; check one entry back if needed.
    if (it->first != address) {
        const bool first_contained = address >= it->first
            && static_cast<uint64_t>(address) + size <= static_cast<uint64_t>(it->first) + it->second->total_bytes;
        if (!first_contained && it != color_address_lookup.begin()) {
            --it;
        }
    }

    auto &surface = *it->second;
    if (it->first != address) {
        constexpr uint32_t min_surface_read_size = KiB(16);
        const bool contained = address >= it->first
            && static_cast<uint64_t>(address) + size <= static_cast<uint64_t>(it->first) + surface.total_bytes;
        const bool engaged = contained && (size == 0 || size >= min_surface_read_size);
        if (!engaged)
            return false;
    }

    VKContext &context = *static_cast<VKContext *>(state.context);
    // if the surface has not been rendered recently, its memory content is as up to date as it gets
    if (surface.last_frame_rendered + MAX_FRAMES_RENDERING <= context.frame_timestamp)
        return false;

    // once need_surface_sync is set, the end-of-scene sync in perform_surface_sync keeps the
    // memory up to date every time the surface is rendered, so only the first read needs this
    if (!*surface.need_surface_sync) {
        // GPU-only readers don't need the data written back to guest RAM
        surface.gpu_read_sync_only = true;
        submit_immediate_surface_sync(surface, nullptr);
    }

    return true;
}

ColorSurfaceCacheInfo *VKSurfaceCache::perform_surface_sync() {
    // surface sync is supported only if memory mapping is enabled
    if (!state.features.enable_memory_mapping)
        return nullptr;

    if (last_written_surface == nullptr || !*last_written_surface->need_surface_sync)
        return nullptr;

    // repack-format surfaces (CPU-converted writeback) sync at most once per 25ms
    if (surface_sync_needs_f10_repack(*last_written_surface) || surface_sync_needs_se5_repack(*last_written_surface)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_written_surface->last_repack_sync_time < std::chrono::milliseconds(25)) {
            f10_skip_count.fetch_add(1, std::memory_order_relaxed);
            return nullptr;
        }
        last_written_surface->last_repack_sync_time = now;
        f10_sync_count.fetch_add(1, std::memory_order_relaxed);
    }

    VKContext *context = reinterpret_cast<VKContext *>(state.context);
    vk::CommandBuffer cmd_buffer = context->render_cmd;

    const bool sync_from_raw = last_written_surface->raw_image && !last_written_surface->content_is_blended;
    vk::Image image_to_copy = sync_from_raw ? last_written_surface->raw_image->image : last_written_surface->texture.image;
    const vk::Format sync_format = sync_from_raw ? vk::Format::eR16G16B16A16Uint : last_written_surface->texture.format;
    vk::ImageLayout image_layout = vk::ImageLayout::eGeneral;

    {
        vk::ImageMemoryBarrier sync_src_barrier{
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite | vk::AccessFlagBits::eShaderWrite,
            .dstAccessMask = vk::AccessFlagBits::eTransferRead,
            .oldLayout = vk::ImageLayout::eGeneral,
            .newLayout = vk::ImageLayout::eGeneral,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image_to_copy,
            .subresourceRange = vkutil::color_subresource_range
        };
        cmd_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eFragmentShader, vk::PipelineStageFlagBits::eTransfer, {}, {}, {}, sync_src_barrier);
    }

    // this works for surface swizzles
    bool is_swizzle_identity = last_written_surface->swizzle.r == vk::ComponentSwizzle::eR;
    if (!is_swizzle_identity && !format_support_swizzle(last_written_surface->format)) {
        LOG_WARN_ONCE("Surface sync with swizzle not support on {}", vk::to_string(last_written_surface->texture.format));

        is_swizzle_identity = true;
    }

    const uint32_t pixel_stride = (last_written_surface->stride_bytes * 8) / gxm::bits_per_pixel(last_written_surface->format);
    const bool needs_copy_buffer = format_need_additional_memory(last_written_surface->format) || surface_sync_needs_u4u4u4u4_repack(*last_written_surface) || surface_sync_needs_f10_repack(*last_written_surface) || surface_sync_needs_se5_repack(*last_written_surface);

    // For macrotile-sync surfaces at non-integer scale factors, clamp the sync
    // to only the rendered macroblocks.
    bool clamp_sync = false;
    int32_t sync_x0 = 0, sync_y0 = 0;
    uint32_t sync_w = last_written_surface->original_width;
    uint32_t sync_h = last_written_surface->original_height;

    if (state.res_multiplier != 1.0f
        && context->render_target && context->render_target->has_macroblock_sync
        && !sync_from_raw && !needs_copy_buffer
        && context->rendered_rect_x1 > context->rendered_rect_x0
        && context->rendered_rect_y1 > context->rendered_rect_y0) {
        int32_t nx0 = static_cast<int32_t>(context->rendered_rect_x0 / state.res_multiplier);
        int32_t ny0 = static_cast<int32_t>(context->rendered_rect_y0 / state.res_multiplier);
        int32_t nx1 = static_cast<int32_t>(context->rendered_rect_x1 / state.res_multiplier);
        int32_t ny1 = static_cast<int32_t>(context->rendered_rect_y1 / state.res_multiplier);

        if (nx0 > 0 || ny0 > 0
            || nx1 < static_cast<int32_t>(last_written_surface->original_width)
            || ny1 < static_cast<int32_t>(last_written_surface->original_height)) {
            clamp_sync = true;
            sync_x0 = nx0;
            sync_y0 = ny0;
            sync_w = static_cast<uint32_t>(nx1 - nx0);
            sync_h = static_cast<uint32_t>(ny1 - ny0);
        }
    }

    bool rt_clamped = false;
    if (state.surface_sync_clamp_rt && !sync_from_raw && !needs_copy_buffer
        && last_written_surface->rendered_w > 0 && last_written_surface->rendered_h > 0
        && (last_written_surface->rendered_w < last_written_surface->original_width || last_written_surface->rendered_h < last_written_surface->original_height)) {
        const int32_t lim_x1 = static_cast<int32_t>(last_written_surface->rendered_w);
        const int32_t lim_y1 = static_cast<int32_t>(last_written_surface->rendered_h);
        const int32_t cur_x1 = sync_x0 + static_cast<int32_t>(sync_w);
        const int32_t cur_y1 = sync_y0 + static_cast<int32_t>(sync_h);
        const int32_t new_x1 = std::min(cur_x1, lim_x1);
        const int32_t new_y1 = std::min(cur_y1, lim_y1);
        if (new_x1 < cur_x1 || new_y1 < cur_y1) {
            sync_w = static_cast<uint32_t>(std::max(0, new_x1 - sync_x0));
            sync_h = static_cast<uint32_t>(std::max(0, new_y1 - sync_y0));
            clamp_sync = true;
            rt_clamped = true;
        }
    }
    bool skip_writeback = false;
    if (state.surface_sync_clamp_rt && !sync_from_raw && !needs_copy_buffer) {
        const ColorSurfaceCacheInfo &ws = *last_written_surface;
        if (ws.written_x1 <= ws.written_x0 || ws.written_y1 <= ws.written_y0) {
            // nothing was ever drawn into it: the GPU changed no memory, there is nothing to write back
            skip_writeback = true;
        } else {
            const int32_t cur_x0 = sync_x0, cur_y0 = sync_y0;
            const int32_t cur_x1 = sync_x0 + static_cast<int32_t>(sync_w), cur_y1 = sync_y0 + static_cast<int32_t>(sync_h);
            const int32_t nx0 = std::max(cur_x0, ws.written_x0), ny0 = std::max(cur_y0, ws.written_y0);
            const int32_t nx1 = std::min(cur_x1, ws.written_x1), ny1 = std::min(cur_y1, ws.written_y1);
            if (nx1 <= nx0 || ny1 <= ny0) {
                skip_writeback = true;
            } else if (nx0 != cur_x0 || ny0 != cur_y0 || nx1 != cur_x1 || ny1 != cur_y1) {
                sync_x0 = nx0;
                sync_y0 = ny0;
                sync_w = static_cast<uint32_t>(nx1 - nx0);
                sync_h = static_cast<uint32_t>(ny1 - ny0);
                clamp_sync = true;
                rt_clamped = true;
            }
        }
    }
    if (skip_writeback || sync_w == 0 || sync_h == 0)
        return nullptr;

    if (state.res_multiplier != 1.0f) {
        // scale back the image using a blit command first

        if (!last_written_surface->blit_image)
            last_written_surface->blit_image = std::make_unique<vkutil::Image>();

        vkutil::Image &blit_image = *last_written_surface->blit_image;

        if (blit_image.image && blit_image.format != sync_format) {
            state.frame().destroy_queue.add_image(blit_image);
            blit_image = vkutil::Image();
        }

        if (!blit_image.image) {
            blit_image.format = sync_format;
            blit_image.width = last_written_surface->original_width;
            blit_image.height = last_written_surface->original_height;

            blit_image.init_image(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
            blit_image.transition_to(cmd_buffer, vkutil::ImageLayout::TransferDst);
        } else {
            if (clamp_sync)
                blit_image.transition_to(cmd_buffer, vkutil::ImageLayout::TransferDst);
            else
                blit_image.transition_to_discard(cmd_buffer, vkutil::ImageLayout::TransferDst);
        }

        int32_t src_x0 = 0, src_y0 = 0;
        int32_t src_x1 = last_written_surface->width, src_y1 = last_written_surface->height;
        int32_t dst_x0 = 0, dst_y0 = 0;
        int32_t dst_x1 = last_written_surface->original_width, dst_y1 = last_written_surface->original_height;

        if (clamp_sync) {
            if (rt_clamped) {
                src_x0 = static_cast<int32_t>(sync_x0 * state.res_multiplier);
                src_y0 = static_cast<int32_t>(sync_y0 * state.res_multiplier);
                src_x1 = static_cast<int32_t>((sync_x0 + static_cast<int32_t>(sync_w)) * state.res_multiplier);
                src_y1 = static_cast<int32_t>((sync_y0 + static_cast<int32_t>(sync_h)) * state.res_multiplier);
            } else {
                src_x0 = context->rendered_rect_x0;
                src_y0 = context->rendered_rect_y0;
                src_x1 = context->rendered_rect_x1;
                src_y1 = context->rendered_rect_y1;
            }
            dst_x0 = sync_x0;
            dst_y0 = sync_y0;
            dst_x1 = sync_x0 + static_cast<int32_t>(sync_w);
            dst_y1 = sync_y0 + static_cast<int32_t>(sync_h);
        }

        vk::ImageBlit blit{
            .srcSubresource = vkutil::color_subresource_layer,
            .srcOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ src_x0, src_y0, 0 }, vk::Offset3D{ src_x1, src_y1, 1 } },
            .dstSubresource = vkutil::color_subresource_layer,
            .dstOffsets = std::array<vk::Offset3D, 2>{ vk::Offset3D{ dst_x0, dst_y0, 0 }, vk::Offset3D{ dst_x1, dst_y1, 1 } },
        };
        const vk::Filter sync_filter = sync_from_raw ? vk::Filter::eNearest : vk::Filter::eLinear;
        cmd_buffer.blitImage(image_to_copy, image_layout, blit_image.image, vk::ImageLayout::eTransferDstOptimal, blit, sync_filter);

        blit_image.transition_to(cmd_buffer, vkutil::ImageLayout::TransferSrc);
        image_to_copy = blit_image.image;
        image_layout = vk::ImageLayout::eTransferSrcOptimal;
    }

    vk::Buffer buffer;
    uint32_t offset;

    if (needs_copy_buffer) {
        if (!last_written_surface->copy_buffer)
            last_written_surface->copy_buffer = std::make_unique<vkutil::Buffer>();

        vkutil::Buffer &copy_buffer = *last_written_surface->copy_buffer;

        if (!copy_buffer.buffer) {
            copy_buffer.size = static_cast<vk::DeviceSize>(pixel_stride) * last_written_surface->original_height * vk::blockSize(last_written_surface->texture.format);
            // the CPU repack reads this whole buffer back so it must be host-cached. Reading write-combined memory costs ~60ms for a 4MB surface.
            copy_buffer.init_buffer(vk::BufferUsageFlagBits::eTransferDst, vkutil::vma_readback_alloc);
        }

        buffer = copy_buffer.buffer;
        offset = 0;

        last_written_surface->need_buffer_sync = false;
        last_written_surface->need_post_surface_sync = true;
    } else {
        last_written_surface->need_buffer_sync = !last_written_surface->gpu_read_sync_only;
        last_written_surface->need_post_surface_sync = !is_swizzle_identity;
        std::tie(buffer, offset) = state.get_matching_mapping(last_written_surface->data);
    }

    vk::BufferImageCopy copy{
        .bufferOffset = offset,
        .bufferRowLength = pixel_stride,
        .bufferImageHeight = last_written_surface->original_height,
        .imageSubresource = vkutil::color_subresource_layer,
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { last_written_surface->original_width, last_written_surface->original_height, 1 }
    };

    if (clamp_sync) {
        const uint32_t block_size = static_cast<uint32_t>(vk::blockSize(sync_format));
        copy.bufferOffset = offset + (static_cast<uint32_t>(sync_y0) * pixel_stride + static_cast<uint32_t>(sync_x0)) * block_size;
        copy.imageOffset = { sync_x0, sync_y0, 0 };
        copy.imageExtent = { sync_w, sync_h, 1 };
    }

    cmd_buffer.copyImageToBuffer(image_to_copy, image_layout, buffer, copy);

    ColorSurfaceCacheInfo *return_value = last_written_surface;
    last_written_surface = nullptr;

    return return_value;
}

template <typename T>
static void swizzle_text_T_2(T *pixels, uint32_t nb_pixel) {
    for (uint32_t i = 0; i < nb_pixel; i++) {
        std::swap(pixels[2 * i], pixels[2 * i + 1]);
    }
}

template <typename T, size_t type>
static void swizzle_text_T_4(T *pixels, uint32_t nb_pixel) {
    for (uint32_t i = 0; i < nb_pixel; i++) {
        if constexpr (type == 0) {
            // BGRA
            std::swap(pixels[4 * i], pixels[4 * i + 2]);
        } else if constexpr (type == 1) {
            // ABGR
            std::swap(pixels[4 * i], pixels[4 * i + 3]);
            std::swap(pixels[4 * i + 1], pixels[4 * i + 2]);
        } else {
            // ARGB
            T copy[] = { pixels[4 * i],
                pixels[4 * i + 1],
                pixels[4 * i + 2],
                pixels[4 * i + 3] };
            pixels[4 * i] = copy[3];
            pixels[4 * i + 1] = copy[0];
            pixels[4 * i + 2] = copy[1];
            pixels[4 * i + 3] = copy[2];
        }
    }
}

template <typename T>
static void swizzle_text_T(T *pixels, uint32_t nb_pixel, ColorSurfaceCacheInfo *surface) {
    // there can only be 2 or 4 component textures here
    if (vk::componentCount(surface->texture.format) == 2) {
        swizzle_text_T_2<T>(pixels, nb_pixel);
    } else {
        // find the swizzle
        // swizzles are inversed
        switch (surface->swizzle.r) {
        case vk::ComponentSwizzle::eB:
            // BGRA
            swizzle_text_T_4<T, 0>(pixels, nb_pixel);
            break;
        case vk::ComponentSwizzle::eA:
            // ABGR
            swizzle_text_T_4<T, 1>(pixels, nb_pixel);
            break;
        case vk::ComponentSwizzle::eG:
            // ARGB
            swizzle_text_T_4<T, 2>(pixels, nb_pixel);
            break;
        }
    }
}

void VKSurfaceCache::perform_post_surface_sync(const MemState &mem, ColorSurfaceCacheInfo *surface) {
    if (surface == nullptr)
        return;

    const uint32_t pixel_stride = (surface->stride_bytes * 8) / gxm::bits_per_pixel(surface->format);
    const uint32_t nb_pixels = pixel_stride * surface->original_height;
    uint8_t *pixels = surface->data.cast<uint8_t>().get(mem);

    if (surface_sync_needs_u4u4u4u4_repack(*surface)) {
        pack_rgba8_to_r4g4b4a4(pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride, surface->original_height);
        return;
    }

    if (surface_sync_needs_f10_repack(*surface)) {
        const auto t0 = std::chrono::steady_clock::now();
        pack_rgba16f_to_u2f10f10f10(pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride, surface->original_height);
        f10_repack_us.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count(), std::memory_order_relaxed);

        return;
    }

    if (surface_sync_needs_se5_repack(*surface)) {
        pack_rgba16f_to_se5m9m9m9(pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride, surface->original_height);
        return;
    }

    if (format_need_additional_memory(surface->format)) {
        // special case, use a custom function
        const bool is_swizzle_identity = surface->swizzle.r == vk::ComponentSwizzle::eR;
        if (!surface->sws_context) {
            const AVPixelFormat dst_fmt = is_swizzle_identity ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_BGR24;
            surface->sws_context = sws_getContext(surface->original_width, surface->original_height, AV_PIX_FMT_RGB0, surface->original_width, surface->original_height, dst_fmt, 0, nullptr, nullptr, nullptr);
            assert(surface->sws_context != NULL);
        }

        int src_stride = pixel_stride * 4;
        int dst_stride = pixel_stride * 3;
        sws_scale(surface->sws_context, reinterpret_cast<const uint8_t *const *>(&surface->copy_buffer->mapped_data), &src_stride, 0, surface->original_height, &pixels, &dst_stride);
        return;
    }

    switch (vk::componentBits(surface->texture.format, 0)) {
    case 8:
        swizzle_text_T<uint8_t>(pixels, nb_pixels, surface);
        break;
    case 16:
        swizzle_text_T<uint16_t>(reinterpret_cast<uint16_t *>(pixels), nb_pixels, surface);
        break;
    case 32:
        swizzle_text_T<uint32_t>(reinterpret_cast<uint32_t *>(pixels), nb_pixels, surface);
        break;
    }
}

void VKSurfaceCache::destroy_associated_framebuffers(const VKRenderTarget *render_target) {
    if (!render_target)
        return;

    destroy_framebuffers(render_target->color.view);
    destroy_framebuffers(render_target->depthstencil.view);
}

vk::ImageView VKSurfaceCache::sourcing_color_surface_for_presentation(Ptr<const void> address, uint32_t pitch, Viewport &viewport, PresentSurfaceInfo *present_info) {
    // get closest surface with an address below address
    auto ite = color_address_lookup.upper_bound(address.address());
    if (ite == color_address_lookup.begin()) {
        return nullptr;
    }
    --ite;

    ColorSurfaceCacheInfo &info = *ite->second;
    if (info.data.address() + info.total_bytes <= address.address())
        // they do not overlap
        return nullptr;

    if (info.stride_bytes == pitch * 4) {
        // In assumption the format is RGBA8
        const size_t data_delta = address.address() - ite->first;
        uint32_t limited_height = viewport.height;
        if ((data_delta % (pitch * 4)) == 0) {
            uint32_t start_sourced_line = static_cast<uint32_t>((data_delta / (pitch * 4)) * state.res_multiplier);
            if ((start_sourced_line + viewport.height) > info.height) {
                // Sometimes the surface is just missing a little bit of lines
                if (start_sourced_line < info.height) {
                    // Just limit the height and display it
                    limited_height = info.height - start_sourced_line;
                } else {
                    LOG_ERROR("Trying to present non-existent segment in cached color surface!");
                    return nullptr;
                }
            }

            // Compute position in texture
            viewport.offset_x = 0;
            viewport.offset_y = start_sourced_line;
            viewport.width = std::min(viewport.width, static_cast<uint32_t>(info.width));
            viewport.height = limited_height;
            viewport.texture_width = info.width;
            viewport.texture_height = info.height;

            if (present_info) {
                present_info->image = info.texture.image;
                present_info->plain_rgba8 = (info.swizzle == vkutil::rgba_mapping && info.texture.format == vk::Format::eR8G8B8A8Unorm);
            }

            if (info.swizzle == vkutil::rgba_mapping && info.texture.format == vk::Format::eR8G8B8A8Unorm)
                return info.texture.view;

            if (!info.alternate_view) {
                // create a view with the right swizzle and without gamma correction
                vk::ImageViewCreateInfo view_info{
                    .image = info.texture.image,
                    .viewType = vk::ImageViewType::e2D,
                    .format = vk::Format::eR8G8B8A8Unorm,
                    .components = vkutil::color_to_texture_swizzle(info.swizzle, vkutil::rgba_mapping),
                    .subresourceRange = vkutil::color_subresource_range
                };
                info.alternate_view = state.device.createImageView(view_info);
            }

            return info.alternate_view;
        }
    }

    return nullptr;
}

std::vector<uint32_t> VKSurfaceCache::dump_frame(Ptr<const void> address, uint32_t width, uint32_t height, uint32_t pitch) {
    // get closest surface with an address below address
    auto ite = color_address_lookup.upper_bound(address.address());
    if (ite == color_address_lookup.begin()) {
        return {};
    }
    --ite;

    const ColorSurfaceCacheInfo &info = *ite->second;

    const uint32_t data_delta = address.address() - ite->first;
    const uint32_t pitch_byte = pitch * 4;
    if (info.stride_bytes != pitch_byte || data_delta % pitch_byte != 0)
        return {};

    const uint32_t line_delta = static_cast<uint32_t>((data_delta / pitch_byte) * state.res_multiplier);
    if (line_delta >= info.height)
        return {};

    const uint32_t real_height = std::min(height, info.height - line_delta);

    std::vector<uint32_t> frame(width * height, 0);

    // we need a temporary buffer and command buffer for this
    // this is a raii buffer, it will be destroyed at the end of this function
    vkutil::Buffer temp_buff(width * height * 4);
    temp_buff.init_buffer(vk::BufferUsageFlagBits::eTransferDst, vkutil::vma_mapped_alloc);
    vk::CommandBuffer cmd_buffer = vkutil::create_single_time_command(state.device, state.general_command_pool);

    // layout is general, we can directly copy from it
    vk::BufferImageCopy image_copy{
        .bufferOffset = 0,
        .bufferRowLength = width,
        .bufferImageHeight = height,
        .imageSubresource = vkutil::color_subresource_layer,
        .imageOffset = { 0, static_cast<int>(line_delta), 0 },
        .imageExtent = { width, real_height, 1 }
    };
    cmd_buffer.copyImageToBuffer(info.texture.image, vk::ImageLayout::eGeneral, temp_buff.buffer, image_copy);

    // this will cause a waitIdle, not an issue
    vkutil::end_single_time_command(state.device, state.general_queue, state.general_command_pool, cmd_buffer);

    // on non-coherent staging memory the CPU would read stale data without an explicit invalidate
    const vk::MemoryPropertyFlags dump_mem_props = state.allocator.getAllocationMemoryProperties(temp_buff.allocation);
    if (!(dump_mem_props & vk::MemoryPropertyFlagBits::eHostCoherent))
        state.allocator.invalidateAllocation(temp_buff.allocation, 0, VK_WHOLE_SIZE);

    memcpy(frame.data(), temp_buff.mapped_data, frame.size() * 4);

    return frame;
}

void VKSurfaceCache::ensure_reinterpret_pipeline() {
    if (reinterpret_pipeline)
        return;

    const fs::path shader_path = state.static_assets / "shaders-builtin/vulkan" / "surface_cast_reinterpret.comp.spv";
    reinterpret_shader = vkutil::load_shader(state.device, shader_path);

    // point sampler used only so the shader can texelFetch the store
    vk::SamplerCreateInfo sampler_info{
        .magFilter = vk::Filter::eNearest,
        .minFilter = vk::Filter::eNearest,
        .mipmapMode = vk::SamplerMipmapMode::eNearest,
        .addressModeU = vk::SamplerAddressMode::eClampToEdge,
        .addressModeV = vk::SamplerAddressMode::eClampToEdge,
        .addressModeW = vk::SamplerAddressMode::eClampToEdge
    };
    reinterpret_sampler = state.device.createSampler(sampler_info);

    // set 0: binding 0 = store (combined image sampler), binding 1 = cast (storage image)
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings{};
    bindings[0] = vk::DescriptorSetLayoutBinding{
        .binding = 0,
        .descriptorType = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    };
    bindings[1] = vk::DescriptorSetLayoutBinding{
        .binding = 1,
        .descriptorType = vk::DescriptorType::eStorageImage,
        .descriptorCount = 1,
        .stageFlags = vk::ShaderStageFlagBits::eCompute
    };
    vk::DescriptorSetLayoutCreateInfo layout_info{};
    layout_info.setBindings(bindings);
    reinterpret_desc_layout = state.device.createDescriptorSetLayout(layout_info);

    vk::PushConstantRange push_range{
        .stageFlags = vk::ShaderStageFlagBits::eCompute,
        .offset = 0,
        .size = sizeof(ReinterpretPushConstants)
    };
    vk::PipelineLayoutCreateInfo pl_info{};
    pl_info.setSetLayouts(reinterpret_desc_layout);
    pl_info.setPushConstantRanges(push_range);
    reinterpret_pipeline_layout = state.device.createPipelineLayout(pl_info);

    vk::PipelineShaderStageCreateInfo stage{
        .stage = vk::ShaderStageFlagBits::eCompute,
        .module = reinterpret_shader,
        .pName = "main"
    };
    vk::ComputePipelineCreateInfo pipeline_info{
        .stage = stage,
        .layout = reinterpret_pipeline_layout
    };
    reinterpret_pipeline = state.device.createComputePipeline(nullptr, pipeline_info).value;

    constexpr uint32_t NB_SETS = 256;
    std::array<vk::DescriptorPoolSize, 2> pool_sizes{
        vk::DescriptorPoolSize{ vk::DescriptorType::eCombinedImageSampler, NB_SETS },
        vk::DescriptorPoolSize{ vk::DescriptorType::eStorageImage, NB_SETS }
    };
    vk::DescriptorPoolCreateInfo pool_info{ .maxSets = NB_SETS };
    pool_info.setPoolSizes(pool_sizes);
    reinterpret_desc_pool = state.device.createDescriptorPool(pool_info);

    std::vector<vk::DescriptorSetLayout> layouts(NB_SETS, reinterpret_desc_layout);
    vk::DescriptorSetAllocateInfo alloc_info{ .descriptorPool = reinterpret_desc_pool };
    alloc_info.setSetLayouts(layouts);
    reinterpret_desc_sets = state.device.allocateDescriptorSets(alloc_info);
    reinterpret_desc_idx = 0;
}

} // namespace renderer::vulkan
