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
#include <renderer/vulkan/gpu_resource_cleanup.h>
#include <renderer/transfer_model.h>
#include <renderer/vulkan/colour_format_model.h>
#include <renderer/vulkan/surface_pack.h>
#include <renderer/vulkan/surface_sync.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/types.h>
#include <renderer/vulkan/render_pass_sync.h>
#include <vkutil/vkutil.h>
#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#endif

#include <vulkan/vulkan_format_traits.hpp>

#define XXH_INLINE_ALL
#include <xxhash.h>

#if ECV_ORACLE
#include <ecv_frame_capture.h> // oracle: GXM frame capture (debug builds only)
#endif

#include <atomic>
#include <cmath>
#include <shared_mutex>
#include <variant>
#include <limits>
#include <mem/functions.h>
#include <util/align.h>
#include <util/log.h>
#include <util/vector_utils.h>

extern "C" {
#include <libswscale/swscale.h>
}

namespace renderer::vulkan {
thread_local bool surface_sync_internal_write = false;
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

static bool is_small_linear_writeback_surface(const ColorSurfaceCacheInfo &surface) {
    constexpr uint32_t small_surface_limit = 512;
    return surface.tiling == SurfaceTiling::Linear
        && surface.original_width <= small_surface_limit
        && surface.original_height <= small_surface_limit;
}

static bool surface_sync_needs_u4u4u4u4_repack(const ColorSurfaceCacheInfo &surface) {
    return surface.format == SCE_GXM_COLOR_BASE_FORMAT_U4U4U4U4
        && (surface.texture.format == vk::Format::eR8G8B8A8Unorm
            || surface.texture.format == vk::Format::eR8G8B8A8Srgb);
}

static bool surface_sync_needs_f10_repack(const ColorSurfaceCacheInfo &surface) {
    return surface.format == SCE_GXM_COLOR_BASE_FORMAT_U2F10F10F10 && surface.texture.format == vk::Format::eR16G16B16A16Sfloat;
}

static bool surface_sync_needs_se5_repack(const ColorSurfaceCacheInfo &surface) {
    return surface.format == SCE_GXM_COLOR_BASE_FORMAT_SE5M9M9M9 && surface.texture.format == vk::Format::eR16G16B16A16Sfloat;
}

// host format != guest layout for the fallback formats of the colour format model (P7), see colourmodel::Repack
static colourmodel::Repack surface_fallback_repack(const ColorSurfaceCacheInfo &surface) {
    const colourmodel::Repack r = colourmodel::repack_for(surface.format, surface.texture.format);
    switch (r) {
    case colourmodel::Repack::None:
    case colourmodel::Repack::U4444_RGBA8:
    case colourmodel::Repack::U2F10F10F10_F16:
    case colourmodel::Repack::SE5M9M9M9_F16:
        return colourmodel::Repack::None; // the dedicated paths above
    default:
        return r;
    }
}

static bool surface_sync_needs_cpu_repack(const ColorSurfaceCacheInfo &surface) {
    return surface_sync_needs_f10_repack(surface) || surface_sync_needs_se5_repack(surface)
        || surface_fallback_repack(surface) != colourmodel::Repack::None;
}

static bool protect_surface(MemState &mem, ColorSurfaceCacheInfo &info) {
    const bool trap_reads = (info.tiling == SurfaceTiling::Linear
        && format_support_surface_sync(info.format));

    // Every surface, whatever its size, is trapped over the whole page span that covers its bytes
    // (B131: small surfaces used to be untracked and big ones skipped their first/last partial page).
    // Faults from unrelated data sharing those pages are not guessed away: the fired trap is judged by
    // refresh_guest_trap (exact write address, then a hash of the surface bytes).
    const uint32_t addr_start = align_down(info.data.address(), KiB(4));
    const uint32_t addr_end = align(info.data.address() + info.total_bytes, KiB(4));
    const uint32_t surface_begin = info.data.address();
    const uint32_t surface_end = info.data.address() + info.total_bytes;

    if (!info.guest_trap)
        info.guest_trap = std::make_shared<GuestTrapState>();

    // Use MemPerm::None to trap both reads and writes for surfaces that support sync,
    // MemPerm::ReadOnly to trap only writes for other surfaces
    MemPerm perm = trap_reads ? MemPerm::None : MemPerm::ReadOnly;
    std::shared_ptr<bool> need_sync = trap_reads ? info.need_surface_sync : nullptr;
    std::shared_ptr<bool> dirty = info.dirty;
    std::shared_ptr<GuestTrapState> trap = info.guest_trap;

    return add_protect(mem, addr_start, addr_end - addr_start, perm,
        [dirty, need_sync, trap, surface_begin, surface_end](Address fault, bool write) {
            // the segment is one-shot: whatever happens below, the surface is unarmed from now on
            trap->fired.store(true);
#if ECV_ORACLE
            ecv::oracle::fcap::note_memory_write(fault, 1, surface_sync_internal_write);
#endif
            // ignore our own guest write-backs (the hash baseline is not used for synced surfaces)
            if (write && surface_sync_internal_write) {
                trap->emulator_wrote.store(true);
                return true;
            }
            if (write) {
                if (fault >= surface_begin && fault < surface_end) {
                    trap->write_inside.store(true);
                    // with an arm-time hash the bytes decide (refresh_guest_trap); without one the fault is the proof
                    if (!trap->verify_by_hash.load())
                        *dirty = true;
                }
                // else: a write to data sharing the page. It may still sweep into the surface; judged by hash
                return true;
            }
            if (need_sync)
                *need_sync = true;
            return true;
        });
}

bool VKSurfaceCache::arm_guest_trap(MemState &mem, ColorSurfaceCacheInfo &info) {
    if (!info.guest_trap)
        info.guest_trap = std::make_shared<GuestTrapState>();
    info.guest_trap->fired.store(false);
    info.guest_trap->write_inside.store(false);
    info.guest_trap->verify_by_hash.store(false);
    info.guest_trap->emulator_wrote.store(false);

    // Baseline first: while the surface is unarmed its bytes are readable without a trap (a linear
    // surface's trap also catches reads). A CPU write in the few instructions before add_protect is
    // not trapped; it is only missed if it also hits no later fault, and such a write targets memory
    // the GPU image already superseded.
    info.guest_hash_valid = false;
    if (info.total_bytes != 0 && info.total_bytes <= surface_policy::verify_hash_limit_bytes
        && is_valid_addr_range(mem, info.data.address(), info.data.address() + info.total_bytes)) {
        info.guest_hash = XXH3_64bits(info.data.get(mem), info.total_bytes);
        info.guest_hash_valid = true;
        info.guest_trap->verify_by_hash.store(!(info.need_surface_sync && *info.need_surface_sync));
    }
    return protect_surface(mem, info);
}

void VKSurfaceCache::refresh_guest_trap(MemState &mem, ColorSurfaceCacheInfo &info) {
    if (!info.data || !info.guest_trap || !info.guest_trap->fired.load())
        return;
    // the fault handler flags the trap before it opens the pages: wait until it has finished
    { const std::lock_guard<std::mutex> handler_done(mem.protect_mutex); }

    surface_policy::TrapObservation observation;
    observation.fired = true;
    observation.write_inside_range = info.guest_trap->write_inside.load();
    observation.baseline_valid = info.guest_hash_valid;
    observation.gpu_writes_memory = info.need_surface_sync && *info.need_surface_sync;
    observation.emulator_wrote = info.guest_trap->emulator_wrote.load();
    if (observation.baseline_valid && !observation.gpu_writes_memory) {
        observation.hash_differs = !is_valid_addr_range(mem, info.data.address(), info.data.address() + info.total_bytes)
            || XXH3_64bits(info.data.get(mem), info.total_bytes) != info.guest_hash;
    }
    if (surface_policy::judge_trap(observation) == surface_policy::TrapVerdict::CpuWrote)
        *info.dirty = true;
    arm_guest_trap(mem, info);
}

bool VKSurfaceCache::gpu_writes_guest_range(Address begin, Address end) const {
    for (const auto &[address, surface] : color_address_lookup) {
        if (surface && surface->need_surface_sync && *surface->need_surface_sync
            && address < end && begin < static_cast<uint64_t>(address) + surface->total_bytes)
            return true;
    }
    return false;
}

bool VKSurfaceCache::protect_cached_surface(MemState &mem, ColorSurfaceCacheInfo &info) {
    return arm_guest_trap(mem, info);
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
            if (it->second.shell_fetch_snapshot)
                destroy_queue.add_image(*it->second.shell_fetch_snapshot);
            it = framebuffer_array.erase(it);
        } else {
            it = std::next(it);
        }
    }
}

void VKSurfaceCache::record_pending_cast(PendingCast &cast, VKContext &context) {
    const bool pre_scene = cast.info->last_scene_rendered == context.scene_timestamp && context.scene_has_drawn
        && !context.scene_macroblock_flushed;
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::cast(pre_scene ? "RECORD_PRE" : "RECORD_MID", cast.info->data.address(), 0, 0, 0, 0, false, cast.info->last_scene_rendered);
#endif
    if (pre_scene) {
        cast.record(context.prerender_cmd);
        return;
    }

#if ECV_ORACLE
    ecv::oracle::gpu::rp_reason("cast_copy");
#endif
    if (context.in_renderpass)
        context.stop_render_pass();

    {
        // the surface was just rendered: make it visible to the transfer / compute cast
        vkutil::sync::Batch batch;
        cast.info->texture.use_in_place(batch, vkutil::sync::S::eTransfer | vkutil::sync::S::eComputeShader,
            vkutil::sync::A::eTransferRead | vkutil::sync::A::eShaderRead);
        batch.flush(context.render_cmd);
    }

    cast.record(context.render_cmd);
}

ColorSurfaceCacheInfo *VKSurfaceCache::find_transfer_surface(MemState &mem, Address address) {
    auto it = color_address_lookup.upper_bound(address);
    if (it == color_address_lookup.begin())
        return nullptr;
    --it;
    ColorSurfaceCacheInfo *info = it->second;
    if (info == nullptr || !info->texture.image || address >= it->first + info->total_bytes)
        return nullptr;
    // a surface the guest wrote to since its last upload has stale image content: the CPU transfer handles it
    refresh_guest_trap(mem, *info);
    if (*info->dirty)
        return nullptr;
    // the image must hold the 8888 bytes of the guest in order: identity swizzle, plain RGBA8, no raw alias
    if (info->tiling != SurfaceTiling::Linear || info->format != SCE_GXM_COLOR_BASE_FORMAT_U8U8U8U8
        || info->swizzle.r != vk::ComponentSwizzle::eR || info->texture.format != vk::Format::eR8G8B8A8Unorm || info->raw_image)
        return nullptr;
    return info;
}

bool VKSurfaceCache::try_transfer_color_gpu(MemState &mem, const ColorTransferKind kind, const SceGxmTransferImage &src, const SceGxmTransferImage &dst,
    const SceGxmTransferType src_type, const SceGxmTransferType dst_type, const uint32_t fill_color) {
    // the surface sync writes the destination back to guest memory (the DoubleBuffer mapping forwards its mirror like a scene end does)
    if (!state.features.enable_memory_mapping || state.disable_surface_sync)
        return false;
    constexpr SceGxmTransferFormat rgba8 = SCE_GXM_TRANSFER_FORMAT_U8U8U8U8_ABGR;
    if (dst.format != rgba8 || dst_type != SCE_GXM_TRANSFER_LINEAR || (kind != ColorTransferKind::Fill && (src.format != rgba8 || src_type != SCE_GXM_TRANSFER_LINEAR)))
        return false;
    VKContext &context = *static_cast<VKContext *>(state.context);

    const uint32_t src_w = src.width, src_h = src.height;
    const uint32_t dst_w = kind == ColorTransferKind::Copy ? src.width : dst.width;
    const uint32_t dst_h = kind == ColorTransferKind::Copy ? src.height : dst.height;
    if (kind == ColorTransferKind::Downscale && !transfer::is_exact_half(src_w, src_h, dst_w, dst_h))
        return false;
    if (dst.stride <= 0 || (kind != ColorTransferKind::Fill && src.stride <= 0))
        return false;

    ColorSurfaceCacheInfo *dst_info = find_transfer_surface(mem, dst.address.address());
    if (dst_info == nullptr)
        return false;
    const auto geometry = [](const ColorSurfaceCacheInfo &i) {
        return transfer::SurfaceGeometry{ i.data.address(), i.stride_bytes, i.original_width, i.original_height, 4 };
    };
    const auto dst_rect = transfer::locate(geometry(*dst_info), dst.address.address(), static_cast<uint32_t>(dst.stride), dst.x, dst.y, dst_w, dst_h);
    if (!dst_rect)
        return false;

    ColorSurfaceCacheInfo *src_info = nullptr;
    std::optional<transfer::PixelRect> src_rect;
    if (kind != ColorTransferKind::Fill) {
        src_info = find_transfer_surface(mem, src.address.address());
        if (src_info == nullptr)
            return false;
        src_rect = transfer::locate(geometry(*src_info), src.address.address(), static_cast<uint32_t>(src.stride), src.x, src.y, src_w, src_h);
        if (!src_rect || (src_info == dst_info && transfer::rects_overlap(*src_rect, *dst_rect)))
            return false;
    }

    const float m = state.res_multiplier;
    const auto scaled = [&](const ColorSurfaceCacheInfo &i, const transfer::PixelRect &r) {
        const auto clampi = [](float v, uint32_t limit) { return static_cast<int32_t>(std::min<float>(std::max(v, 0.0f), static_cast<float>(limit))); };
        return std::array<vk::Offset3D, 2>{ vk::Offset3D{ clampi(std::floor(r.x * m), i.texture.width), clampi(std::floor(r.y * m), i.texture.height), 0 },
            vk::Offset3D{ clampi(std::floor((r.x + r.w) * m), i.texture.width), clampi(std::floor((r.y + r.h) * m), i.texture.height), 1 } };
    };

    // The blit is recorded into the scene command stream, in order with the draws that wrote the surfaces and the scenes that read the
    // destination (a separate submission cannot be ordered against a scene that is still being recorded). Between two scenes a recording
    // is opened for it. All barriers are the vkutil sync tracker's (transition_to / the surface sync's own use_in_place).
    if (!context.render_target)
        return false;
    std::unique_ptr<vkutil::Image> temp;
    if (kind == ColorTransferKind::Fill) {
        temp = std::make_unique<vkutil::Image>();
        temp->width = 1;
        temp->height = 1;
        temp->format = vk::Format::eR8G8B8A8Unorm;
        temp->init_image(vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst);
    }
    if (!context.is_recording)
        context.start_recording();
    if (context.in_renderpass)
        context.stop_render_pass();
    {
        vk::CommandBuffer transfer_cmd = context.render_cmd;
        vkutil::Image *source_image = nullptr;
        if (kind == ColorTransferKind::Fill) {
            temp->transition_to(transfer_cmd, vkutil::ImageLayout::TransferDst);
            // unorm8 of the four bytes of the fill value (little endian: R G B A), exact
            const vk::ClearColorValue color{ std::array<float, 4>{ static_cast<float>(fill_color & 0xFF) / 255.0f, static_cast<float>((fill_color >> 8) & 0xFF) / 255.0f,
                static_cast<float>((fill_color >> 16) & 0xFF) / 255.0f, static_cast<float>((fill_color >> 24) & 0xFF) / 255.0f } };
            transfer_cmd.clearColorImage(temp->image, vk::ImageLayout::eTransferDstOptimal, color, vkutil::color_subresource_range);
            temp->transition_to(transfer_cmd, vkutil::ImageLayout::TransferSrc);
            source_image = temp.get();
        } else {
            src_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::TransferSrc);
            source_image = &src_info->texture;
        }
        dst_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::TransferDst);

        const std::array<vk::Offset3D, 2> src_offsets = kind == ColorTransferKind::Fill
            ? std::array<vk::Offset3D, 2>{ vk::Offset3D{ 0, 0, 0 }, vk::Offset3D{ 1, 1, 1 } }
            : scaled(*src_info, *src_rect);
        const vk::ImageBlit blit{
            .srcSubresource = vkutil::color_subresource_layer,
            .srcOffsets = src_offsets,
            .dstSubresource = vkutil::color_subresource_layer,
            .dstOffsets = scaled(*dst_info, *dst_rect)
        };
        // a copy and a fill replicate texels; the exact 2:1 downscale is the average of the four texels under each output texel
        const vk::Filter filter = kind == ColorTransferKind::Downscale ? vk::Filter::eLinear : vk::Filter::eNearest;
        transfer_cmd.blitImage(source_image->image, vk::ImageLayout::eTransferSrcOptimal, dst_info->texture.image, vk::ImageLayout::eTransferDstOptimal, blit, filter);

        if (src_info != nullptr)
            src_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::ColorAttachmentReadWrite);
        dst_info->texture.transition_to(transfer_cmd, vkutil::ImageLayout::ColorAttachmentReadWrite);
    }
    if (temp)
        state.frame().destroy_queue.add_image(*temp);

    // the destination now holds newer content than guest memory: mark what was written (write-back bounds) and write it
    // back like the end of a scene would, so that guest reads of the destination see the transfer
    dst_info->last_frame_rendered = context.frame_timestamp;
    const int32_t t0x = static_cast<int32_t>(dst_rect->x), t0y = static_cast<int32_t>(dst_rect->y);
    const int32_t t1x = static_cast<int32_t>(dst_rect->x + dst_rect->w), t1y = static_cast<int32_t>(dst_rect->y + dst_rect->h);
    dst_info->scene_x0 = t0x;
    dst_info->scene_y0 = t0y;
    dst_info->scene_x1 = t1x;
    dst_info->scene_y1 = t1y;
    const auto align_down32 = [](int32_t v) { return (v / 32) * 32; };
    const auto align_up32 = [&](int32_t v, int32_t limit) { return std::min(((v + 31) / 32) * 32, limit); };
    dst_info->written_x0 = std::min(dst_info->written_x0, align_down32(t0x));
    dst_info->written_y0 = std::min(dst_info->written_y0, align_down32(t0y));
    dst_info->written_x1 = std::max(dst_info->written_x1, align_up32(t1x, dst_info->original_width));
    dst_info->written_y1 = std::max(dst_info->written_y1, align_up32(t1y, dst_info->original_height));
    {
        // written back to guest memory by the scene stream itself (an identity RGBA8 surface needs no CPU post step)
        ColorSurfaceCacheInfo *prev_last_written = last_written_surface;
        *dst_info->need_surface_sync = true;
        last_written_surface = dst_info;
        ColorSurfaceCacheInfo *synced = perform_surface_sync();
        last_written_surface = prev_last_written;
        // DoubleBuffer: the sync wrote the mirror buffer; the forward to guest memory is queued behind the submit that carries this blit
        if (synced != nullptr && state.mapping_method == MappingMethod::DoubleBuffer && synced->need_buffer_sync) {
            if (const auto request = context.make_buffer_sync_request(*synced))
                context.pending_buffer_syncs.push_back(*request);
        }
    }

    LOG_INFO_ONCE("Colour transfer ({}) done on the GPU: {}x{} RGBA8", kind == ColorTransferKind::Copy ? "copy" : kind == ColorTransferKind::Downscale ? "downscale" : "fill", dst_rect->w, dst_rect->h);
    return true;
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

uint64_t VKSurfaceCache::color_surface_gpu_bytes(const ColorSurfaceCacheInfo &info) const {
    const auto image_bytes = [](const vkutil::Image &image) -> uint64_t {
        if (!image.image || image.format == vk::Format::eUndefined)
            return 0;
        return static_cast<uint64_t>(image.width) * image.height * vk::blockSize(image.format);
    };
    uint64_t bytes = image_bytes(info.texture);
    if (info.raw_image)
        bytes += image_bytes(*info.raw_image);
    if (info.blit_image)
        bytes += image_bytes(*info.blit_image);
    return bytes;
}

uint64_t VKSurfaceCache::ensure_surface_budget() {
    if (surface_budget == 0) {
        uint64_t heap = 0;
        for (uint32_t i = 0; i < state.physical_device_memory.memoryHeapCount; i++) {
            const auto &h = state.physical_device_memory.memoryHeaps[i];
            if (h.flags & vk::MemoryHeapFlagBits::eDeviceLocal)
                heap = std::max<uint64_t>(heap, h.size);
        }
        surface_budget = surface_policy::surface_budget_bytes(heap);
        LOG_INFO("Surface cache budget: {} MiB (device-local heap {} MiB)", surface_budget >> 20, heap >> 20);
    }
    return surface_budget;
}

bool VKSurfaceCache::write_back_before_destroy(ColorSurfaceCacheInfo &info, MemState &mem) {
    if (!info.texture.image || !info.data || info.total_bytes == 0)
        return false;
    refresh_guest_trap(mem, info);

    surface_policy::WritebackInput input;
    input.linear = info.tiling == SurfaceTiling::Linear;
    input.sync_available = state.features.enable_memory_mapping && !state.disable_surface_sync
        && format_support_surface_sync(info.format)
        && is_valid_addr_range(mem, info.data.address(), info.data.address() + info.total_bytes);
    input.cpu_newer = info.dirty && *info.dirty;
    input.kept_in_sync = info.need_surface_sync && *info.need_surface_sync && !info.gpu_read_sync_only;
    for (const auto &[address, other] : color_address_lookup) {
        if (other == &info || !other->texture.image)
            continue;
        if (surface_policy::is_superseded(info.last_scene_rendered, other->last_scene_rendered,
                surface_policy::ranges_overlap(info.data.address(), info.data.address() + info.total_bytes, address, address + other->total_bytes))) {
            input.superseded = true;
            break;
        }
    }

    const surface_policy::Writeback decision = surface_policy::decide_writeback(input);
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::rt_surface("WRITEBACK", info.data.address(), info.total_bytes, info.original_width, info.original_height,
            static_cast<uint32_t>(info.format), info.stride_bytes, static_cast<uint32_t>(info.tiling), input.cpu_newer,
            decision == surface_policy::Writeback::Do, static_cast<uint64_t>(decision));
#endif
    if (decision == surface_policy::Writeback::Do) {
        info.gpu_read_sync_only = false;
        sync_for_eviction = true;
        submit_immediate_surface_sync(info, &mem);
        sync_for_eviction = false;

        if (state.mapping_method == MappingMethod::DoubleBuffer && info.need_buffer_sync) {
            // the scene-end path forwards the mirror buffer to guest memory through a BufferSyncRequest;
            // a retired surface has no scene end, so do the same rows here, synchronously
            info.need_buffer_sync = false;
            const uint32_t bpp = gxm::bits_per_pixel(info.format) / 8;
            const uint64_t x0 = static_cast<uint32_t>(std::max(info.post_sync_x0, 0));
            const uint64_t y0 = static_cast<uint32_t>(std::max(info.post_sync_y0, 0));
            const uint64_t row_bytes = static_cast<uint64_t>(info.post_sync_width) * bpp;
            const uint64_t first = static_cast<uint64_t>(info.data.address()) + y0 * info.stride_bytes + x0 * bpp;
            const auto span = surface_sync_rows_span(info.stride_bytes, row_bytes, info.post_sync_height);
            auto mem_it = state.mapped_memories.lower_bound(static_cast<Address>(first));
            if (bpp != 0 && span && *span != 0 && first + *span <= static_cast<uint64_t>(info.data.address()) + info.total_bytes
                && mem_it != state.mapped_memories.end() && static_cast<Address>(first) >= mem_it->first
                && std::holds_alternative<vkutil::Buffer>(mem_it->second.buffer_impl)) {
                const uint64_t offset = first - mem_it->first;
                if (offset <= mem_it->second.size && *span <= mem_it->second.size - offset) {
                    const std::shared_lock<std::shared_mutex> transition_lock(mem.external_transition_mutex);
                    const uint8_t *src = static_cast<const uint8_t *>(std::get<vkutil::Buffer>(mem_it->second.buffer_impl).mapped_data) + offset;
                    uint8_t *dst = Ptr<void>(static_cast<Address>(first)).cast<uint8_t>().get(mem);
                    surface_sync_internal_write = true;
#if ECV_ORACLE
                    ecv::oracle::fcap::note_memory_write(static_cast<uint32_t>(first), *span, true);
#endif
                    copy_surface_sync_rows(dst, *span, src, *span, info.stride_bytes, row_bytes, info.post_sync_height);
                    surface_sync_internal_write = false;
                }
            }
        }
    }

    const bool guest_authoritative = decision == surface_policy::Writeback::Do
        || decision == surface_policy::Writeback::SkipAlreadySynced
        || decision == surface_policy::Writeback::SkipCpuNewer;
    if (guest_authoritative) {
        VKContext &context = *static_cast<VKContext *>(state.context);
        surface_policy::remember_preserved(preserved_ranges,
            { info.data.address(), info.data.address() + info.total_bytes, context.frame_timestamp });
    }
    return decision == surface_policy::Writeback::Do;
}

void VKSurfaceCache::evict_color_surface(ColorSurfaceCacheInfo &info, MemState &mem) {
    if (info.texture.image) {
        write_back_before_destroy(info, mem);
        destroy_surface(info);
    }
    if (info.data) {
        auto it = color_address_lookup.find(info.data.address());
        if (it != color_address_lookup.end() && it->second == &info)
            color_address_lookup.erase(it);
    }
    if (last_written_surface == &info)
        last_written_surface = nullptr;
}

ColorSurfaceCacheInfo &VKSurfaceCache::claim_color_slot(MemState &mem, uint64_t incoming_bytes) {
    VKContext &context = *static_cast<VKContext *>(state.context);

    // slots in LRU order (least recently used first); the queue links point from the LRU end towards the head
    std::vector<ColorSurfaceCacheInfo *> order;
    std::vector<surface_policy::SlotInfo> slots;
    order.reserve(color_surface_queue.items.size());
    const auto *item = color_surface_queue.head->prev;
    for (size_t i = 0; i < color_surface_queue.items.size(); i++, item = item->prev) {
        ColorSurfaceCacheInfo &c = const_cast<ColorSurfaceCacheInfo &>(item->content);
        order.push_back(&c);
        surface_policy::SlotInfo slot;
        slot.occupied = c.texture.image != nullptr;
        slot.bytes = slot.occupied ? color_surface_gpu_bytes(c) : 0;
        const uint64_t scene = std::max(c.last_used_scene, c.last_scene_rendered);
        const uint64_t frame = std::max(c.last_used_frame, c.last_frame_rendered);
        slot.used_this_scene = slot.occupied && scene == context.scene_timestamp;
        slot.used_this_frame = slot.occupied && frame == context.frame_timestamp;
        slots.push_back(slot);
    }

    const surface_policy::SlotChoice choice = surface_policy::choose_slot(slots, incoming_bytes, ensure_surface_budget());
    if (choice.forced)
        LOG_WARN_ONCE("Surface cache: all {} colour surfaces are used by the current scene, reusing one", slots.size());

    for (const int extra : choice.also_evict) {
        LOG_INFO_ONCE("Surface cache over its memory budget: evicting {}x{} surface at 0x{:X}",
            order[extra]->original_width, order[extra]->original_height, order[extra]->data.address());
        evict_color_surface(*order[extra], mem);
        color_surface_queue.set_as_lru(order[extra]);
    }
    ColorSurfaceCacheInfo &chosen = *order[choice.slot];
    if (choice.slot_occupied) {
        LOG_INFO_ONCE("Surface cache full: evicting {}x{} surface at 0x{:X}",
            chosen.original_width, chosen.original_height, chosen.data.address());
#if ECV_ORACLE
        if (ecv::oracle::gpu::enabled())
            ecv::oracle::gpu::rt_surface("EVICT", chosen.data.address(), chosen.total_bytes, chosen.original_width, chosen.original_height,
                static_cast<uint32_t>(chosen.format), chosen.stride_bytes, static_cast<uint32_t>(chosen.tiling), chosen.dirty && *chosen.dirty, choice.forced, chosen.last_frame_rendered);
#endif
        evict_color_surface(chosen, mem);
    }
    return chosen;
}

DepthStencilSurfaceCacheInfo &VKSurfaceCache::claim_depth_slot() {
    VKContext &context = *static_cast<VKContext *>(state.context);
    std::vector<DepthStencilSurfaceCacheInfo *> order;
    std::vector<surface_policy::SlotInfo> slots;
    const auto *item = ds_surface_queue.head->prev;
    for (size_t i = 0; i < ds_surface_queue.items.size(); i++, item = item->prev) {
        DepthStencilSurfaceCacheInfo &d = const_cast<DepthStencilSurfaceCacheInfo &>(item->content);
        order.push_back(&d);
        surface_policy::SlotInfo slot;
        slot.occupied = d.texture.image != nullptr;
        slot.used_this_scene = slot.occupied && d.last_used_scene == context.scene_timestamp;
        slot.used_this_frame = slot.occupied && d.last_used_frame == context.frame_timestamp;
        slots.push_back(slot);
    }
    // depth surfaces carry no guest-visible content (no write-back exists) so only the slot rules apply
    const surface_policy::SlotChoice choice = surface_policy::choose_slot(slots, 0, ~uint64_t{ 0 });
    return *order[choice.slot];
}

VKSurfaceCache::VKSurfaceCache(VKState &state)
    : state(state) {
    color_surface_queue.init(max_surfaces_allowed);
    ds_surface_queue.init(max_surfaces_allowed);
}

void VKSurfaceCache::reset() {
    // cleanup() destroys views, images and framebuffers immediately; the render thread may
    // still have submitted work (and descriptor sets) referencing them
    state.device.waitIdle();
    cleanup();
    color_address_lookup.clear();
    depth_address_lookup.clear();
    stencil_address_lookup.clear();
    color_surface_queue.init(max_surfaces_allowed);
    ds_surface_queue.init(max_surfaces_allowed);
    cpu_surfaces_changed.clear();
    preserved_ranges.clear();
    target = nullptr;
    last_written_surface = nullptr;
    pending_ds_scene = nullptr;
    pending_ds_scene_stores = false;
    pending_casts.clear();
}

void VKSurfaceCache::flush_all_surfaces(MemState &mem) {
    if (!state.features.enable_memory_mapping)
        return;

    for (auto &item : color_surface_queue.items) {
        ColorSurfaceCacheInfo &info = item.content;
        if (!info.texture.image || info.total_bytes == 0)
            continue;
        submit_immediate_surface_sync(info, &mem);
    }
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
        // The cache outlives the allocator during renderer shutdown.
        info.raw_image.reset();

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

        info.sample_rate_copy.reset();
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
    // a host image that is not byte identical to guest memory (emulated or fallback formats) is converted on the CPU
    // (colourmodel::guest_to_host), the inverse of the surface sync
    const colourmodel::Repack repack = colourmodel::repack_for(info.format, info.texture.format);
    const uint32_t guest_bytes_pp = static_cast<uint32_t>(gxm::bits_per_pixel(info.format) / 8);
    const uint32_t host_bytes_pp = static_cast<uint32_t>(vk::blockSize(info.texture.format));
    bool upload_supported = state.features.enable_memory_mapping
        && info.tiling == SurfaceTiling::Linear
        && format_support_surface_sync(info.format)
        // 24 bit packed RGB is expanded to RGBA8 by the sync only
        && info.format != SCE_GXM_COLOR_BASE_FORMAT_U8U8U8
        && info.swizzle.r == vk::ComponentSwizzle::eR
        && !info.raw_image
        && host_bytes_pp > 0 && guest_bytes_pp > 0;
    if (upload_supported) {
        if (repack == colourmodel::Repack::None)
            upload_supported = (info.stride_bytes % host_bytes_pp) == 0;
        else
            upload_supported = (info.stride_bytes % guest_bytes_pp) == 0;
    }
    if (!upload_supported) {
        return false;
    }

    // never read past the end of valid guest memory (surface descriptors can cover
    // more than the underlying allocation)
    if (!is_valid_addr_range(mem, info.data.address(), info.data.address() + static_cast<uint32_t>(info.stride_bytes) * info.original_height)) {
        return false;
    }

    VKContext *context = reinterpret_cast<VKContext *>(state.context);

    const uint32_t pixel_stride = repack == colourmodel::Repack::None ? info.stride_bytes / host_bytes_pp : info.stride_bytes / guest_bytes_pp;
    const vk::DeviceSize upload_size = repack == colourmodel::Repack::None
        ? static_cast<vk::DeviceSize>(info.stride_bytes) * info.original_height
        : static_cast<vk::DeviceSize>(pixel_stride) * host_bytes_pp * info.original_height;

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
    if (repack == colourmodel::Repack::None) {
        memcpy(info.upload_buffer->mapped_data, info.data.get(mem), upload_size);
    } else if (!colourmodel::guest_to_host(repack, static_cast<uint8_t *>(info.upload_buffer->mapped_data), info.data.cast<uint8_t>().get(mem),
                   pixel_stride * info.original_height, guest_bytes_pp)) {
        return false;
    }

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
    if (!last_written_surface)
        return;
    ColorSurfaceCacheInfo &info = *last_written_surface;
    info.scene_x0 = info.scene_y0 = info.scene_x1 = info.scene_y1 = 0;
    if (x1 <= x0 || y1 <= y0)
        return;
    const float inv = 1.0f / state.res_multiplier;
    info.scene_x0 = std::clamp<int32_t>(static_cast<int32_t>(std::floor(x0 * inv)), 0, info.original_width);
    info.scene_y0 = std::clamp<int32_t>(static_cast<int32_t>(std::floor(y0 * inv)), 0, info.original_height);
    info.scene_x1 = std::clamp<int32_t>(static_cast<int32_t>(std::ceil(x1 * inv)), 0, info.original_width);
    info.scene_y1 = std::clamp<int32_t>(static_cast<int32_t>(std::ceil(y1 * inv)), 0, info.original_height);
    if (info.scene_x1 <= info.scene_x0 || info.scene_y1 <= info.scene_y0)
        return;
    // scaled -> unscaled, rounded outward to the 32px tile the hardware writes back as a whole
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
    const auto colour_entry = state.colour_entry(base_format);
    vk::Format vk_format = colour_entry ? colour_entry->format : color::translate_surface_format(base_format);

    SurfaceTiling tiling;
    if (color->surfaceType == SCE_GXM_COLOR_SURFACE_LINEAR)
        tiling = SurfaceTiling::Linear;
    else if (color->surfaceType == SCE_GXM_COLOR_SURFACE_SWIZZLED)
        tiling = SurfaceTiling::Swizzled;
    else
        tiling = SurfaceTiling::Tiled;

    const bool is_srgb = color->gamma != 0 && colourmodel::gamma_applies(base_format);
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

        (void)cache_probably_freed;
        (void)surface_stat_changed;
        // B131: the decision is a pure rule (surface_policy::framebuffer_action); both replace actions
        // save the old content to guest memory before the image goes away (evict_color_surface)
        surface_policy::FramebufferRequest request;
        request.same_base = ite->first == address;
        request.new_range_inside_old = addr_in_range_of_cache;
        request.extent_or_layout_changed = surface_extent_changed;
        request.wider_or_other_format = info.width < width || base_format != info.format;
        const bool invalidated = surface_policy::framebuffer_action(request) != surface_policy::FramebufferAction::Reuse;
#if ECV_ORACLE
        if (ecv::oracle::gpu::enabled())
            ecv::oracle::gpu::rt_surface("INVALIDATE", ite->first, info.total_bytes, info.original_width, info.original_height,
                static_cast<uint32_t>(info.format), info.stride_bytes, static_cast<uint32_t>(info.tiling), *info.dirty, false, info.last_frame_rendered);
#endif
        if (invalidated) {
            evict_color_surface(info, mem);
            color_surface_queue.set_as_lru(&info);
        } else {
            color_surface_queue.set_as_mru(&info);
            info.last_used_frame = context->frame_timestamp;
            info.last_used_scene = context->scene_timestamp;
            if (context->render_target) {
                const uint32_t rt_w = static_cast<uint32_t>(std::lround(context->render_target->width / state.res_multiplier));
                const uint32_t rt_h = static_cast<uint32_t>(std::lround(context->render_target->height / state.res_multiplier));
                info.rendered_w = static_cast<uint16_t>(std::max<uint32_t>(info.rendered_w, std::min<uint32_t>(info.original_width, rt_w)));
                info.rendered_h = static_cast<uint16_t>(std::max<uint32_t>(info.rendered_h, std::min<uint32_t>(info.original_height, rt_h)));
            }

#if ECV_ORACLE
            const bool oracle_was_dirty = info.data && *info.dirty;
            const uint64_t oracle_prev_lfr = info.last_frame_rendered;
#endif
            refresh_guest_trap(mem, info);
            if (info.data && *info.dirty) {
                try_upload_guest_content(info, mem);
                arm_guest_trap(mem, info);
            }
            *info.dirty = false;

            last_written_surface = &info;

#if ECV_ORACLE
            if (ecv::oracle::gpu::enabled())
                ecv::oracle::gpu::rt_surface("REUSE", ite->first, info.total_bytes, info.original_width, info.original_height,
                    static_cast<uint32_t>(info.format), info.stride_bytes, static_cast<uint32_t>(info.tiling), oracle_was_dirty, oracle_was_dirty, oracle_prev_lfr);
#endif
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

    // B130: take a free slot, else evict by the byte budget / slot rules (never a surface the recording
    // scene uses); an evicted surface is written back to guest memory first
    const bool carries_raw_image = state.features.preserve_f16_nan_as_u16 && base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16;
    const uint64_t incoming_bytes = static_cast<uint64_t>(width) * height * vk::blockSize(vk_format) * (carries_raw_image ? 2 : 1);
    ColorSurfaceCacheInfo &info_added = claim_color_slot(mem, incoming_bytes);
    const bool reused_for_other_store = info_added.data && info_added.data.address() != address;
    if (reused_for_other_store)
        info_added.has_phase_view = false;

    color_surface_queue.set_as_mru(&info_added);
    info_added.last_frame_rendered = context->frame_timestamp;
    info_added.last_scene_rendered = context->scene_timestamp;
    info_added.last_used_frame = context->frame_timestamp;
    info_added.last_used_scene = context->scene_timestamp;
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
    // known content: every texel is the clear colour (a following pass can CLEAR instead of LOAD)
    image.sync.content = vkutil::sync::Content::Cleared;
    image.sync.cleared_to = {};

    // on real hardware the new surface already "contains" whatever the game put in its
    // memory — load it so CPU-prepared content isn't lost. Restricted to atlas-class
    // surfaces (the streamed-tile pattern this addresses); ordinary scene targets keep
    // the clear-to-black behavior.
    // A range whose previous surface was retired with a write-back (or was already in sync) is
    // authoritative in guest memory, so the new surface starts from it (B131).
    const bool restore_preserved = surface_policy::consume_preserved(preserved_ranges, address, address + total_surface_size, context->frame_timestamp);
    if (info_added.data && ((original_width >= 1024 && original_height >= 1024) || restore_preserved)) {
        try_upload_guest_content(info_added, mem);
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::rt_surface("NEW", address, total_surface_size, original_width, original_height, static_cast<uint32_t>(base_format),
            bytes_per_stride, static_cast<uint32_t>(tiling), false, info_added.data && ((original_width >= 1024 && original_height >= 1024) || restore_preserved), 0);
#endif

    if (state.features.preserve_f16_nan_as_u16 && base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16) {
        info_added.raw_image = std::make_unique<vkutil::Image>(width, height, vk::Format::eR16G16B16A16Uint);
        vkutil::Image &raw = *info_added.raw_image;
        raw.layout = vkutil::ImageLayout::Undefined;
        raw.init_image(vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment
                | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc,
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
    info_added.guest_trap = std::make_shared<GuestTrapState>();
    info_added.gpu_read_sync_only = false;
    info_added.content_is_blended = false;
    info_added.reinterpret_view_is_raw = false;

    // we only support surface sync of linear surfaces for now
    if (!can_mprotect_mapped_memory) {
        // Without protection, write back every linear surface.
        *info_added.need_surface_sync = color->surfaceType == SCE_GXM_COLOR_SURFACE_LINEAR;
    } else {
        arm_guest_trap(mem, info_added);
    }
    // A first-read trap only requests future writeback. Games that CPU-copy a
    // one-shot render target need its initial scene written before notification.
    // Retain protection above so subsequent CPU writes still invalidate the cache.
    if (state.eager_surface_sync && color->surfaceType == SCE_GXM_COLOR_SURFACE_LINEAR)
        *info_added.need_surface_sync = true;

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

#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled()) {
        const ColorSurfaceCacheInfo &oracle_info = *ite->second;
        ecv::oracle::gpu::tex_surface_candidate(ite->first, oracle_info.total_bytes, oracle_info.width, oracle_info.height,
            oracle_info.original_width, oracle_info.original_height, static_cast<uint32_t>(oracle_info.format), oracle_info.stride_bytes,
            static_cast<uint32_t>(oracle_info.tiling), *oracle_info.dirty, oracle_info.last_frame_rendered, oracle_info.last_scene_rendered,
            oracle_info.need_surface_sync && *oracle_info.need_surface_sync, address - ite->first);
    }
#endif
    // B131: the GPU image stands in for guest memory only while the GPU is the last writer. A CPU write
    // seen by the trap (refresh_guest_trap proves it, also for small surfaces and after a trap that was
    // consumed by a read) hands the address to the texture cache at once; the old 2-frame grace sampled
    // the stale image and then flipped to the new data.
    refresh_guest_trap(reinterpret_cast<VKContext *>(state.context)->mem, *ite->second);
    if (surface_policy::texture_source(*ite->second->dirty) == surface_policy::TextureSource::TextureCache) {
        return std::nullopt;
    }

    const vk::ComponentMapping swizzle = texture::translate_swizzle(gxm::get_format(texture));
    const auto colour_entry = state.colour_entry(base_format);
    vk::Format vk_format = colour_entry ? colour_entry->format : color::translate_surface_format(base_format);

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
    bool addr_in_range_of_cache = surface_policy::texture_fits_surface(ite->first, info.total_bytes, address, total_surface_size);

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
    info.last_used_frame = reinterpret_cast<VKContext *>(state.context)->frame_timestamp;
    info.last_used_scene = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

    const vk::ImageView color_handle_view = reinterpret_cast<VKContext *>(state.context)->current_color_view;
    const bool is_same_image = (color_handle_view == info.texture.view) || (color_handle_view == info.alternate_view);

    bool oracle_force_color_copy = false;
#if ECV_ORACLE
    oracle_force_color_copy = ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_COLOR_COPY);
#endif
    if (state.features.use_texture_viewport && base_format == info.format && !oracle_force_color_copy) {
        // use a texture viewport
        *texture_viewport = {
            .ratio = {
                original_width / static_cast<float>(info.original_width),
                original_height / static_cast<float>(info.original_height) },
            .offset = { start_x / static_cast<float>(info.width), start_sourced_line / static_cast<float>(info.height) }
        };

        // sampled directly by the draws of this scene: the pass dependencies must already cover it (counted if not)
        observe_sampled_color(info.texture);
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

    if (oracle_force_color_copy || is_same_image || (start_sourced_line != 0) || (start_x != 0) || (info.width != width) || (info.height != height) || (info.format != base_format) || non_integer_downsample) {
        const uint64_t scene_timestamp = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

        std::vector<CastedTexture> &casted_vec = info.casted_textures;

        CastedTexture *casted = nullptr;

        // Look in cast cache and grab one. The cache really does not store immediate grab on now, but rather to reduce the synchronization in the pipeline (use different texture)
        for (size_t i = 0; i < casted_vec.size();) {
            if ((casted_vec[i].cropped_height == height) && (casted_vec[i].cropped_width == width) && (casted_vec[i].cropped_y == start_sourced_line) && (casted_vec[i].cropped_x == start_x) && (casted_vec[i].format == base_format)) {
                casted = &casted_vec[i];

                if (casted->scene_timestamp == scene_timestamp) {
#if ECV_ORACLE
                    if (ecv::oracle::gpu::enabled())
                        ecv::oracle::gpu::cast("REUSE_SCENE", info.data.address(), start_x, start_sourced_line, width, height, is_same_image, casted->scene_timestamp);
#endif
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

#if ECV_ORACLE
        if (ecv::oracle::gpu::enabled())
            ecv::oracle::gpu::cast(casted_is_new ? "NEW" : "PENDING", info.data.address(), start_x, start_sourced_line, width, height, is_same_image, scene_timestamp);
#endif
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

                {
                    vkutil::Image &cast_src = cast_use_raw ? *info.raw_image : info.texture;
                    vkutil::sync::Batch batch;
                    cast_src.use_in_place(batch, vkutil::sync::S::eTransfer, vkutil::sync::A::eTransferRead);
                    batch.flush(cmd_buffer);
                }

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
                    {
                        vkutil::Image &store_src = want_raw_src ? *info.raw_image : info.texture;
                        vkutil::sync::Batch batch;
                        store_src.use_in_place(batch, vkutil::sync::S::eComputeShader, vkutil::sync::A::eShaderRead);
                        batch.flush(cmd_buffer);
                    }

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
                    {
                        vkutil::Image &byte_src_img = byte_use_raw ? *info.raw_image : info.texture;
                        vkutil::sync::Batch batch;
                        byte_src_img.use_in_place(batch, vkutil::sync::S::eTransfer, vkutil::sync::A::eTransferRead);
                        // the staging buffer is written by the copy below and read by the buffer->image copy after it
                        casted->transition_buffer.use(batch, vkutil::sync::S::eTransfer, vkutil::sync::A::eTransferWrite);
                        batch.flush(cmd_buffer);
                    }
                    cmd_buffer.copyImageToBuffer(byte_src, vk::ImageLayout::eGeneral, casted->transition_buffer.buffer, copy_image_buffer);
                    {
                        vkutil::sync::Batch batch;
                        casted->transition_buffer.use(batch, vkutil::sync::S::eTransfer, vkutil::sync::A::eTransferRead);
                        batch.flush(cmd_buffer);
                    }

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
        // the renderpass external dependencies should take care of the barrier (the tracker verifies that and counts a miss)
        observe_sampled_color(info.texture);
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
    pending_ds_scene_stores = cached_info != nullptr && this_scene_stores;

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
    // an image copy needs identical formats (surfaces of different GXM formats get different host formats)
    if (src_info->texture.format != dst_info->texture.format)
        return false;

    const uint32_t scaled_width = static_cast<uint32_t>(width * state.res_multiplier);
    const uint32_t scaled_height = static_cast<uint32_t>(height * state.res_multiplier);
    const uint32_t copy_width = std::min({ scaled_width, src_info->texture.width, dst_info->texture.width });
    const uint32_t copy_height = std::min({ scaled_height, src_info->texture.height, dst_info->texture.height });
    if (copy_width == 0 || copy_height == 0)
        return false;

    const auto record_copy = [&](vk::CommandBuffer cmd) {
        src_info->texture.transition_to(cmd, vkutil::ImageLayout::TransferSrc, vkutil::ds_subresource_range);
        dst_info->texture.transition_to_discard(cmd, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);

        vk::ImageSubresourceLayers layers = vkutil::color_subresource_layer;
        layers.aspectMask = dsmodel::host_aspects(src_info->texture.format);
        vk::ImageCopy image_copy{
            .srcSubresource = layers,
            .srcOffset = { 0, 0, 0 },
            .dstSubresource = layers,
            .dstOffset = { 0, 0, 0 },
            .extent = { copy_width, copy_height, 1U }
        };
        cmd.copyImage(src_info->texture.image, vk::ImageLayout::eTransferSrcOptimal, dst_info->texture.image, vk::ImageLayout::eTransferDstOptimal, image_copy);

        src_info->texture.transition_to(cmd, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
        dst_info->texture.transition_to(cmd, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
    };

    // The copy is recorded into the scene command stream, in order with the draws that wrote the depth and the scenes that read the destination
    // (a separate submission cannot be ordered against a scene that is still being recorded). Between two scenes a recording is opened for it:
    // it is submitted with the next scene. Without a render target there is nothing to order against: the CPU transfer path runs.
    VKContext *recording_context = reinterpret_cast<VKContext *>(state.context);
    if (!recording_context || !recording_context->render_target)
        return false;
    if (!recording_context->is_recording)
        recording_context->start_recording();
    if (recording_context->in_renderpass)
        recording_context->stop_render_pass();
    record_copy(recording_context->render_cmd);
    dst_info->depth_content_stored = true;
    LOG_INFO_ONCE("Depth transfer on the GPU: 0x{:08X} -> 0x{:08X} {}x{} (scaled {}x{})", src_address, dst_address, width, height, copy_width, copy_height);
    return true;
}

void VKSurfaceCache::resolve_ds_scene_end(bool scene_wrote_depth) {
    if (pending_ds_scene != nullptr && scene_wrote_depth)
        pending_ds_scene->depth_content_stored = pending_ds_scene_stores;
    pending_ds_scene = nullptr;
    pending_ds_scene_stores = false;
}

DepthStencilSurfaceCacheInfo *VKSurfaceCache::find_depth_stencil_entry(const SceGxmDepthStencilSurface &depth_stencil, uint32_t width, uint32_t height, bool &would_remake) const {
    would_remake = false;
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
    if (cached_info == nullptr)
        return nullptr;

    // same test as retrieve_depth_stencil_for_framebuffer
    const SurfaceTiling tiling = (depth_stencil.get_type() == SCE_GXM_DEPTH_STENCIL_SURFACE_LINEAR) ? SurfaceTiling::Linear : SurfaceTiling::Tiled;
    would_remake = cached_info->texture.width < width || cached_info->texture.height < height
        || cached_info->stride_samples != depth_stencil.get_stride() || cached_info->tiling != tiling;
    return cached_info;
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
        cached_info->last_used_frame = reinterpret_cast<VKContext *>(state.context)->frame_timestamp;
        cached_info->last_used_scene = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

        // the host format follows the GXM format of the surface (depth only, stencil only, D16, ... have different host images)
        const bool need_remake = cached_info->texture.width < width
            || cached_info->texture.height < height
            || cached_info->stride_samples != depth_stencil->get_stride()
            || cached_info->tiling != tiling
            || cached_info->texture.format != state.ds_format_for(dsmodel::ds_class(depth_stencil->get_format()));

        if (!need_remake) {
            // MSAA passes rasterise at the sample rate, so a pass whose colour surface downscales
            // draws through a viewport half the size of the pass that filled the shared depth/stencil
            constexpr bool disable_ds_resample = false;

            VKContext *ctx = reinterpret_cast<VKContext *>(state.context);
            uint32_t sx = 1, sy = 1;
            if (!disable_ds_resample && !msaa_skip_ds_resample
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
                // only the aspects the host format has (a depth-only or stencil-only surface has one)
                std::vector<vk::ImageBlit> blit_regions;
                if (dsmodel::host_has_depth(full.format))
                    blit_regions.push_back(region_for(vk::ImageAspectFlagBits::eDepth));
                if (dsmodel::host_has_stencil(full.format))
                    blit_regions.push_back(region_for(vk::ImageAspectFlagBits::eStencil));
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
        // retrieve a new depth stencil (B130: a free slot, else the oldest one the recording scene does not use)
        cached_info = &claim_depth_slot();
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
    cached_info->last_used_frame = reinterpret_cast<VKContext *>(state.context)->frame_timestamp;
    cached_info->last_used_scene = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;
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
    image.format = state.ds_format_for(dsmodel::ds_class(depth_stencil->get_format()));
    image.layout = vkutil::ImageLayout::Undefined;
    image.init_image(vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled);

    image.transition_to(cmd_buffer, vkutil::ImageLayout::TransferDst, vkutil::ds_subresource_range);
    vk::ClearDepthStencilValue clear_value{
        .depth = 1.0,
        .stencil = 0
    };
    vk::ImageSubresourceRange clear_range = vkutil::ds_subresource_range;
    clear_range.aspectMask = dsmodel::host_aspects(image.format);
    cmd_buffer.clearDepthStencilImage(image.image, vk::ImageLayout::eTransferDstOptimal, clear_value, clear_range);
    image.transition_to(cmd_buffer, vkutil::ImageLayout::DepthStencilReadOnly, vkutil::ds_subresource_range);
    image.sync.content = vkutil::sync::Content::Cleared;
    image.sync.cleared_to = { { 0, 0, 0, 0 }, 1.0f, 0 };

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
    found_info->last_used_frame = reinterpret_cast<VKContext *>(state.context)->frame_timestamp;
    found_info->last_used_scene = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

    // depth sampled as a texture by this scene's draws (Killzone depth of field): verify the pass dependencies cover it
    observe_sampled_depth(cached_info.texture);

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

    bool oracle_force_ds_copy = false;
#if ECV_ORACLE
    oracle_force_ds_copy = ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_DS_COPY);
#endif
    if (!reading_ds_attachment && !oracle_force_ds_copy && (state.features.use_texture_viewport || same_dimension)) {
        // we can just sample from the surface itself

        // we must create a new read-only view if it is not already present
        vk::ImageView &img_view = is_stencil ? cached_info.stencil_view : cached_info.depth_view;
        if (!img_view) {
            vk::ImageSubresourceRange range = vkutil::ds_subresource_range;
            range.aspectMask = is_stencil ? vk::ImageAspectFlagBits::eStencil : vk::ImageAspectFlagBits::eDepth;
            vk::ImageViewCreateInfo view_info{
                .image = cached_info.texture.image,
                .viewType = vk::ImageViewType::e2D,
                .format = cached_info.texture.format,
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
            cached_info.texture.format
        };
    }

    const uint64_t scene_timestamp = reinterpret_cast<VKContext *>(state.context)->scene_timestamp;

    int read_surface_idx = -1;
    for (int i = 0; i < cached_info.read_surfaces.size(); i++) {
        auto &read_surface = cached_info.read_surfaces[i];
        if (read_surface.depth_view.width == width
            && read_surface.depth_view.height == height
            && read_surface.depth_view.format == cached_info.texture.format
            && read_surface.delta_row == delta_row_samples
            && read_surface.delta_col == delta_col_samples) {
            read_surface_idx = i;
            break;
        }
    }

    if (read_surface_idx == -1) {
        // no compatible read surface found

        DepthSurfaceView read_only{
            .depth_view = vkutil::Image(width, height, cached_info.texture.format),
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
            .format = read_only.depth_view.format,
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
    layers.aspectMask = dsmodel::host_aspects(cached_info.texture.format);
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

// A surface sampled while it is also an attachment of the running pass is a feedback loop (the game reads what it is drawing): that is
// ordered by the attachment access itself (rasterisation order / the in-pass self-dependency), not by a barrier, so it is not a hazard.
void VKSurfaceCache::observe_sampled_color(vkutil::Image &image) {
    if (!vkutil::sync::config().observe_samples)
        return;
    const VKContext *ctx = reinterpret_cast<VKContext *>(state.context);
    if (ctx && (&image == ctx->current_color_base_image || &image == ctx->current_color_raw_image)) {
        vkutil::sync::stats().feedback_reads++;
        return;
    }
    vkutil::sync::Batch::observe(image.sync, vkutil::sync::S::eVertexShader | vkutil::sync::S::eFragmentShader, vkutil::sync::A::eShaderRead,
        image.sync.layout == vk::ImageLayout::eUndefined ? vk::ImageLayout::eGeneral : vk::ImageLayout::eGeneral);
}

void VKSurfaceCache::observe_sampled_depth(vkutil::Image &image) {
    if (!vkutil::sync::config().observe_samples)
        return;
    const VKContext *ctx = reinterpret_cast<VKContext *>(state.context);
    if (ctx && &image == ctx->current_ds_image) {
        vkutil::sync::stats().feedback_reads++;
        return;
    }
    vkutil::sync::Batch::observe(image.sync, vkutil::sync::S::eVertexShader | vkutil::sync::S::eFragmentShader, vkutil::sync::A::eShaderRead,
        vk::ImageLayout::eDepthStencilReadOnlyOptimal);
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

#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled() && color)
        ecv::oracle::gpu::rt_begin(color->data.address(), color->width, color->height, color->strideInPixels,
            static_cast<uint32_t>(color->colorFormat), static_cast<uint32_t>(color->surfaceType), depth_stencil ? depth_stencil->depth_data.address() : 0);
#endif
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
    std::shared_ptr<vkutil::Image> shell_snapshot;
    // (the dynamic rendering path has neither framebuffers nor the shell input-attachment snapshot: local read does the job)
    if (!state.use_dynamic_rendering && color && !color_result.raw_image
        && (state.use_copy_snapshot || (state.current_title_id == "SceShell" && color_result.base_image->format == vk::Format::eR8G8B8A8Unorm))) {
        // same format as the pass attachment it replaces (a gamma-corrected surface is rendered through an sRGB view of its UNORM image; the copy is bit-exact between the two)
        shell_snapshot = std::make_shared<vkutil::Image>(framebuffer_width, framebuffer_height, static_cast<VKContext *>(state.context)->current_color_format);
        LOG_INFO_ONCE("Fbfetch snapshot image: {} {}x{} (base image {})", vk::to_string(shell_snapshot->format), framebuffer_width, framebuffer_height, vk::to_string(color_result.base_image->format));
        shell_snapshot->init_image(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eColorAttachment);
        auto &context = *static_cast<VKContext *>(state.context);
        shell_snapshot->transition_to(context.prerender_cmd, vkutil::ImageLayout::StorageImage);
        attachments[2] = shell_snapshot->view;
    }
    fb_info.setAttachments(attachments);
    fb_info.attachmentCount = color_result.raw_image ? 3 : 2;
    if (shell_snapshot)
        fb_info.attachmentCount = 3;
    vk::Framebuffer fb_standard = nullptr;
    if (!state.use_dynamic_rendering)
        fb_standard = state.device.createFramebuffer(fb_info);

    vk::Framebuffer fb_interlock = nullptr;
    if (state.features.support_shader_interlock && !state.use_dynamic_rendering) {
        // we also need to create the framebuffer for shader interlock
        fb_info.renderPass = interlock_render_pass;
        fb_info.pAttachments = &attachments[2];
        fb_info.attachmentCount = 1;
        fb_interlock = state.device.createFramebuffer(fb_info);
    }

    auto &result = (framebuffer_array[key] = { fb_standard, fb_interlock, color_result.base_image, framebuffer_width, framebuffer_height, color_result.raw_image });
    result.ds_image = ds_result.base_image;
    result.shell_fetch_snapshot = std::move(shell_snapshot);
    return result;
}

std::map<Address, ColorSurfaceCacheInfo *>::iterator VKSurfaceCache::find_color_surface_containing(Address address, uint32_t size) {
    // the guard keeps small transfers on their existing exact-match path
    constexpr uint32_t min_surface_read_size = KiB(16);
    if (size < min_surface_read_size)
        return color_address_lookup.end();

    auto it = color_address_lookup.upper_bound(address);
    if (it == color_address_lookup.begin())
        return color_address_lookup.end();
    --it;

    const auto contains = [&](const std::map<Address, ColorSurfaceCacheInfo *>::iterator &i) {
        return address >= i->first
            && static_cast<uint64_t>(address) + size <= static_cast<uint64_t>(i->first) + i->second->total_bytes;
    };

    if (contains(it))
        return it;
    if (it == color_address_lookup.begin())
        return color_address_lookup.end();
    --it;
    return contains(it) ? it : color_address_lookup.end();
}

void VKSurfaceCache::mark_guest_range_written(Address first, Address end) {
    for (auto &[address, info] : color_address_lookup) {
        if (address >= end)
            break;
        if (info->dirty && static_cast<uint64_t>(address) + info->total_bytes > first)
            *info->dirty = true;
    }
}

bool VKSurfaceCache::check_for_surface(MemState &mem, Address source_address, CallbackRequestFunction &callback, Address target_address, uint32_t source_size) {
    if (!state.features.enable_memory_mapping || state.disable_surface_sync)
        return false;

    if (vector_utils::find_index(cpu_surfaces_changed, source_address) != -1) {
        // there is a transfer operation pending on this surface, just add the callback after and we are done
        state.request_queue.push(CallbackRequest{ new CallbackRequestFunction(std::move(callback)) });

        if (target_address)
            cpu_surfaces_changed.push_back(target_address);
        return true;
    }

    auto it = color_address_lookup.find(source_address);
    if (it == color_address_lookup.end()) {
        it = find_color_surface_containing(source_address, source_size);
        if (it == color_address_lookup.end())
            return false;
    }

    auto &surface = *it->second;
    VKContext &context = *static_cast<VKContext *>(state.context);
    // if the frame is already rendered skip
    // Note: that's not the best behavior but it should be fine
    // also it prevents invalidated surfaces from causing issues
    if (surface.last_frame_rendered + MAX_FRAMES_RENDERING <= context.frame_timestamp)
        return false;

#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::surf_sync_request("cpu_read", surface.data.address(), source_size);
#endif
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

#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::surf_sync_request(mem ? "immediate_cpu" : "immediate_gpu", surface.data.address(), sync_size);
#endif
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
#if ECV_ORACLE
            ecv::oracle::fcap::note_memory_write(returned_info->data.address(), returned_info->total_bytes, true);
#endif
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

Address VKSurfaceCache::color_surface_limit(const Address address) const {
    auto it = color_address_lookup.upper_bound(address);
    const Address next_surface = it == color_address_lookup.end()
        ? std::numeric_limits<Address>::max()
        : it->first;

    for (auto previous = it; previous != color_address_lookup.begin();) {
        --previous;
        const uint64_t surface_begin = previous->first;
        const uint64_t surface_end = surface_begin + previous->second->total_bytes;
        if (static_cast<uint64_t>(address) >= surface_begin && static_cast<uint64_t>(address) < surface_end)
            return address;
    }
    return next_surface;
}

ColorSurfaceCacheInfo *VKSurfaceCache::perform_surface_sync() {
    // surface sync is supported only if memory mapping is enabled
    if (!state.features.enable_memory_mapping)
        return nullptr;

    if (last_written_surface == nullptr || !*last_written_surface->need_surface_sync)
        return nullptr;

    // repack-format surfaces (CPU-converted writeback) sync at most once per 25ms
    if (surface_sync_needs_cpu_repack(*last_written_surface)) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_written_surface->last_repack_sync_time < std::chrono::milliseconds(25)) {
            return nullptr;
        }
        last_written_surface->last_repack_sync_time = now;
    }

    VKContext *context = reinterpret_cast<VKContext *>(state.context);
    vk::CommandBuffer cmd_buffer = context->render_cmd;

    const bool sync_from_raw = last_written_surface->raw_image && !last_written_surface->content_is_blended;
    vk::Image image_to_copy = sync_from_raw ? last_written_surface->raw_image->image : last_written_surface->texture.image;
    const vk::Format sync_format = sync_from_raw ? vk::Format::eR16G16B16A16Uint : last_written_surface->texture.format;
    vk::ImageLayout image_layout = vk::ImageLayout::eGeneral;

    {
        vkutil::Image &sync_src_image = sync_from_raw ? *last_written_surface->raw_image : last_written_surface->texture;
        vkutil::sync::Batch batch;
        sync_src_image.use_in_place(batch, vkutil::sync::S::eTransfer, vkutil::sync::A::eTransferRead);
        batch.flush(cmd_buffer);
    }

    // this works for surface swizzles
    bool is_swizzle_identity = last_written_surface->swizzle.r == vk::ComponentSwizzle::eR;
    if (!is_swizzle_identity && !format_support_swizzle(last_written_surface->format)) {
        LOG_WARN_ONCE("Surface sync with swizzle not support on {}", vk::to_string(last_written_surface->texture.format));

        is_swizzle_identity = true;
    }

    const uint32_t pixel_stride = (last_written_surface->stride_bytes * 8) / gxm::bits_per_pixel(last_written_surface->format);
    const bool needs_copy_buffer = format_need_additional_memory(last_written_surface->format) || surface_sync_needs_u4u4u4u4_repack(*last_written_surface) || surface_sync_needs_cpu_repack(*last_written_surface);

    // For macrotile-sync surfaces at non-integer scale factors, clamp the sync
    // to only the rendered macroblocks.
    bool clamp_sync = false;
    int32_t sync_x0 = 0, sync_y0 = 0;
    uint32_t sync_w = last_written_surface->original_width;
    uint32_t sync_h = last_written_surface->original_height;

    if (!sync_for_eviction && state.res_multiplier != 1.0f
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
    bool partial_scene_sync = false;
    if (!sync_for_eviction && is_small_linear_writeback_surface(*last_written_surface)) {
        const ColorSurfaceCacheInfo &surface = *last_written_surface;
        const bool covers_surface = surface.written_x0 <= 0 && surface.written_y0 <= 0
            && surface.written_x1 >= static_cast<int32_t>(surface.original_width)
            && surface.written_y1 >= static_cast<int32_t>(surface.original_height);
        const bool identity_swizzle = surface.swizzle.r == vk::ComponentSwizzle::eR;
        if (surface_partial_writeback_eligible(true, needs_copy_buffer, covers_surface, identity_swizzle)) {
            const auto rect = intersect_surface_rects(
                { sync_x0, sync_y0, sync_x0 + static_cast<int32_t>(sync_w), sync_y0 + static_cast<int32_t>(sync_h) },
                { surface.scene_x0, surface.scene_y0, surface.scene_x1, surface.scene_y1 },
                surface.original_width,
                surface.original_height);
            if (!rect)
                return nullptr;
            sync_x0 = rect->x0;
            sync_y0 = rect->y0;
            sync_w = static_cast<uint32_t>(rect->x1 - rect->x0);
            sync_h = static_cast<uint32_t>(rect->y1 - rect->y0);
            clamp_sync = true;
            rt_clamped = true;
            partial_scene_sync = true;
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

    last_written_surface->post_sync_x0 = sync_x0;
    last_written_surface->post_sync_y0 = sync_y0;
    last_written_surface->post_sync_width = sync_w;
    last_written_surface->post_sync_height = sync_h;
    last_written_surface->partial_write_back = partial_scene_sync;
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::surf_sync_gpu(last_written_surface->data.address(), last_written_surface->total_bytes,
            static_cast<uint32_t>(last_written_surface->format), sync_x0, sync_y0, sync_w, sync_h, partial_scene_sync);
#endif

    {
        // consecutive scenes write the same mirror buffer: WAW dependency (sync validation: WRITE_AFTER_WRITE between two vkCmdCopyImageToBuffer)
        vkutil::sync::Batch mirror_batch;
        mirror_batch.use_buffer(mirror_buffer_sync[reinterpret_cast<uint64_t>(static_cast<VkBuffer>(buffer))], buffer, vkutil::sync::S::eTransfer, vkutil::sync::A::eTransferWrite);
        mirror_batch.flush(cmd_buffer);
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
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::surf_writeback(surface->data.address(), surface->total_bytes, static_cast<uint32_t>(surface->format),
            surface->post_sync_x0, surface->post_sync_y0, surface->post_sync_width, surface->post_sync_height);
#endif

    const uint32_t pixel_stride = (surface->stride_bytes * 8) / gxm::bits_per_pixel(surface->format);
    const uint32_t nb_pixels = pixel_stride * surface->original_height;
    uint8_t *pixels = surface->data.cast<uint8_t>().get(mem);

    if (surface_sync_needs_u4u4u4u4_repack(*surface)) {
        pack_rgba8_to_r4g4b4a4(pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride, surface->original_height);
        return;
    }

    if (surface_sync_needs_f10_repack(*surface)) {
        pack_rgba16f_to_u2f10f10f10(pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride, surface->original_height);

        return;
    }

    if (surface_sync_needs_se5_repack(*surface)) {
        pack_rgba16f_to_se5m9m9m9(pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride, surface->original_height);
        return;
    }

    if (const colourmodel::Repack fallback = surface_fallback_repack(*surface); fallback != colourmodel::Repack::None) {
        const uint32_t guest_bpp = gxm::bits_per_pixel(surface->format) / 8;
        if (colourmodel::host_to_guest_extra(fallback, pixels, static_cast<const uint8_t *>(surface->copy_buffer->mapped_data), pixel_stride * surface->original_height, guest_bpp)) {
            // component swizzles of the plain component formats are applied on the guest data after the repack
            // (only two and four component surfaces have a component swizzle to apply)
            const uint32_t host_components = vk::componentCount(surface->texture.format);
            const bool identity = surface->swizzle.r == vk::ComponentSwizzle::eR || (host_components != 2 && host_components != 4);
            if (!identity && (fallback == colourmodel::Repack::Unorm16_F32 || fallback == colourmodel::Repack::Snorm16_F32))
                swizzle_text_T<uint16_t>(reinterpret_cast<uint16_t *>(pixels), nb_pixels, surface);
            else if (!identity && fallback == colourmodel::Repack::Snorm8_F16)
                swizzle_text_T<uint8_t>(pixels, nb_pixels, surface);
            else if (!identity && fallback == colourmodel::Repack::F32_F16)
                swizzle_text_T<uint32_t>(reinterpret_cast<uint32_t *>(pixels), nb_pixels, surface);
        }
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
