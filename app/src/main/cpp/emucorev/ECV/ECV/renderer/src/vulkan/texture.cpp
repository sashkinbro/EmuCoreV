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

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cpu/functions.h>
#include <renderer/vulkan/functions.h>

#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/yuv.h>

#include <vulkan/vulkan_format_traits.hpp>

#include <gxm/functions.h>
#include <gxm/types.h>
#include <renderer/functions.h>
#include <util/align.h>
#include <vkutil/vkutil.h>
#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#endif

namespace renderer::vulkan {

// return if this format can be used to read a depth stencil buffer
// Only return the formats we support and make sense for now
// (technically we can read a D24S8 or D32 as R8R8R8R8, but it is not implemented
// yet and no game I am aware of does it)
static bool is_depth_stencil_compatible_format(SceGxmTextureBaseFormat format, bool &can_be_depth) {
    switch (format) {
        // D16 format
    case SCE_GXM_TEXTURE_BASE_FORMAT_U16:
        // D32 format
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32:
        // D32M format
    case SCE_GXM_TEXTURE_BASE_FORMAT_F32M:
        // D24S8 format
    case SCE_GXM_TEXTURE_BASE_FORMAT_X8U24:
        can_be_depth = true;
        [[fallthrough]];
        // 8bit stencil
    case SCE_GXM_TEXTURE_BASE_FORMAT_U8:
    case SCE_GXM_TEXTURE_BASE_FORMAT_S8:
        return true;
    default:
        return false;
    }
}

VKTextureCache::VKTextureCache(VKState &state)
    : state(state)
    , yuv_converter(state) {}

uint64_t VKTextureCache::release_all_cached_textures() {
    uint64_t freed = 0;
    for (auto &item : texture_queue.items) {
        TextureCacheInfo &info = item.content;
        if (info.texture_size == 0)
            continue;

        texture_lookup.erase(std::bit_cast<TextureGxmDataRepr>(info.texture));
        info.texture_size = 0;
        info.dirty = false;
        info.use_hash = false;
        info.last_hash_scene = 0;

        TextureCacheEntry &entry = textures[info.index];
        if (entry.texture.image) {
            freed += entry.memory_needed;
            entry.texture.destroy();
        }
        if (entry.yuv) {
            yuv_converter.destroy_entry_now(*entry.yuv);
            entry.yuv.reset();
        }
        entry.memory_needed = 0;
    }
    current_texture = nullptr;
    gxm_texture = nullptr;
    return freed;
}

void VKTextureCache::cleanup() {
    for (auto &entry : textures) {
        if (entry.texture.image)
            entry.texture.destroy();
        if (entry.yuv) {
            yuv_converter.destroy_entry_now(*entry.yuv);
            entry.yuv.reset();
        }
    }
    yuv_converter.cleanup();

    for (auto &sampler : samplers)
        state.device.destroy(sampler);
    samplers.clear();

    for (auto &staging : staging_buffers) {
        staging.buffer.destroy();
        // Buffer::destroy clears its handle, but retains the old capacity and
        // mapped pointer. They must not advertise a reusable freed allocation.
        staging.buffer.allocation = nullptr;
        staging.buffer.mapped_data = nullptr;
        staging.buffer.size = 0;
        staging.used_so_far = 0;
        staging.waiting_fence = nullptr;
        staging.scene_timestamp = ~0;
        staging.frame_timestamp = ~0;
    }

    staging_idx = 0;
    last_waited_scene = 0;
    current_texture = nullptr;
    gxm_texture = nullptr;
    cmd_buffer = nullptr;
    is_texture_transfer_ready = false;
}

void VKTextureCache::reset() {
    const size_t sampler_cache_size = sampler_queue.items.size();
    // cleanup() destroys views, images and samplers immediately: wait for submitted work using them
    state.device.waitIdle();
    cleanup();
    texture_lookup.clear();
    texture_queue.init(TextureCacheSize);
    for (size_t i = 0; i < TextureCacheSize; i++)
        texture_queue.items[i].content.index = static_cast<int>(i);
    sampler_lookup.clear();
    // cleanup releases the old handles and clears storage. Queue slots remain
    // usable after reset, so recreate matching empty handle slots too.
    samplers.resize(sampler_cache_size);
    if (sampler_cache_size > 0) {
        sampler_queue.init(sampler_cache_size);
        for (size_t i = 0; i < sampler_cache_size; i++)
            sampler_queue.items[i].content.index = static_cast<int>(i);
    }
    available_textures_hash.clear();
    exported_textures_hash.clear();
    current_info = nullptr;
}

void sync_texture(VKContext &context, MemState &mem, std::size_t index, SceGxmTexture texture, const Config &config) {
    // A save-state load can resume mid-scene: there is no render target until
    // the guest starts its next scene, so skip the texture sync.
    if (!context.render_target)
        return;

    // why are we doing this here?
    // well textures are synced right before the draw
    // in particular, we know that the scissor is the correct one for the upcoming draw
    // and the texture copy needs to be in the correct prerender command for it to render fine
    // so start a new recording right now if the macroblock has changed
    context.check_for_macroblock_change(false);

#if ECV_ORACLE
    ecv::oracle::gpu::tex_begin();
#endif
    bool is_vertex = index >= SCE_GXM_MAX_TEXTURE_UNITS;

    const SceGxmTextureFormat format = gxm::get_format(texture);
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(format);
    if (gxm::is_paletted_format(base_format) && texture.palette_addr == 0) {
        LOG_WARN("Ignoring null palette texture");
        return;
    }

    if (index >= SCE_GXM_MAX_TEXTURE_UNITS) {
        // Vertex textures
        context.shader_hints.vertex_textures[index - SCE_GXM_MAX_TEXTURE_UNITS] = format;
    } else {
        context.shader_hints.fragment_textures[index] = format;
    }

    std::optional<TextureLookupResult> lookup_result = std::nullopt;

    SceGxmColorBaseFormat format_target_of_texture;

    TextureViewport texture_viewport{};

    // surface-cache views are always 2D, so a cube texture must skip it or the shader samples a cube sampler through a 2D view
    const bool texture_is_cube = texture.texture_type() == SCE_GXM_TEXTURE_CUBE || texture.texture_type() == SCE_GXM_TEXTURE_CUBE_ARBITRARY;

    bool color_convertible = renderer::texture::convert_base_texture_format_to_base_color_format(base_format, format_target_of_texture);
    if (color_convertible && !texture_is_cube) {
        // try to retrieve it from the color surface cache
        lookup_result = context.state.surface_cache.retrieve_color_surface_as_texture(texture, format_target_of_texture, &texture_viewport);
    }

#if ECV_ORACLE
    const bool oracle_color_hit = lookup_result.has_value();
#endif
    bool is_depth_surface = false;
    if (!lookup_result.has_value() && !texture_is_cube && is_depth_stencil_compatible_format(base_format, is_depth_surface)) {
        // Try to retrieve depth/stencil cache
        lookup_result = context.state.surface_cache.retrieve_depth_stencil_as_texture(texture, &texture_viewport);
    }

#if ECV_ORACLE
    const int oracle_source = oracle_color_hit ? 2 : (lookup_result.has_value() ? 3 : 1);
#endif
    if (lookup_result.has_value()) {
        const bool needs_nearest = !context.state.texture_cache.format_supports_linear_filter(lookup_result->format);
        context.state.texture_cache.cache_and_bind_sampler(texture, needs_nearest);
    } else {
        context.state.texture_cache.current_scene = context.scene_timestamp;
        context.state.texture_cache.cache_and_bind_texture(texture, mem);
        auto &image = context.state.texture_cache.current_texture->texture;
        lookup_result = TextureLookupResult{
            image.view,
            image.layout,
            image.format
        };
    }

    const vk::ImageLayout layout = vkutil::get_underlying_layout(lookup_result->layout);
    const vk::Sampler sampler = context.state.texture_cache.get_retrieved_sampler();

    vk::DescriptorImageInfo &image_info = is_vertex
        ? context.vertex_textures[index - SCE_GXM_MAX_TEXTURE_UNITS]
        : context.fragment_textures[index];
    if (image_info.sampler != sampler || image_info.imageView != lookup_result->view) {
        image_info = vk::DescriptorImageInfo{
            .sampler = sampler,
            .imageView = lookup_result->view,
            .imageLayout = layout
        };
        // invalidate last descriptor set
        if (is_vertex)
            context.last_vert_texture_count = ~0;
        else
            context.last_frag_texture_count = ~0;
    }

    if (context.state.features.use_texture_viewport) {
        if (!texture.normalize_mode) {
            // Unnormalized texture: the guest samples with texel unit coordinates, the shader turns them into
            // normalized ones through the same scale it uses for surface views (see configure_sampler)
            texture_viewport.ratio.first /= static_cast<float>(std::max(gxm::get_width(texture), 1U));
            texture_viewport.ratio.second /= static_cast<float>(std::max(gxm::get_height(texture), 1U));
        }

        if (is_vertex) {
            context.curr_vert_ublock.set_viewport_ratio(index - SCE_GXM_MAX_TEXTURE_UNITS, texture_viewport.ratio);
            context.curr_vert_ublock.set_viewport_offset(index - SCE_GXM_MAX_TEXTURE_UNITS, texture_viewport.offset);
        } else {
            context.curr_frag_ublock.set_viewport_ratio(index, texture_viewport.ratio);
            context.curr_frag_ublock.set_viewport_offset(index, texture_viewport.offset);
        }
    }

    if (!is_vertex) {
        context.curr_frag_ublock.set_cast_sampler_bit(index, lookup_result->is_typeless_cast, lookup_result->cast_phase_hi);
        context.curr_frag_ublock.set_raw_cast_bit(index, lookup_result->is_raw_bits);
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::enabled())
        ecv::oracle::gpu::tex_bind(static_cast<uint32_t>(index), texture, oracle_source,
            oracle_source != 1 && context.state.features.use_texture_viewport,
            texture_viewport.ratio.first, texture_viewport.ratio.second, texture_viewport.offset.first, texture_viewport.offset.second);
#endif
}

void VKTextureCache::prepare_staging_buffer(bool is_configure) {
    assert(!is_texture_transfer_ready);
    VKContext *context = reinterpret_cast<VKContext *>(state.context);

    TextureStagingBuffer *staging_buffer = &staging_buffers[staging_idx];
    // some textures must be 16-bytes aligned, and staging_buffer->buffer.size is 16-bytes aligned
    staging_buffer->used_so_far = align(staging_buffer->used_so_far, 16);
    // we can keep using the same buffer as before if we are in the same scene and there is enough memory left
    bool use_previous_buffer = staging_buffer->buffer.buffer && staging_buffer->buffer.mapped_data
        && (current_scene_timestamp == staging_buffer->scene_timestamp)
        && staging_buffer->used_so_far <= staging_buffer->buffer.size
        && (staging_buffer->buffer.size - staging_buffer->used_so_far) >= current_texture->memory_needed;

    if (!use_previous_buffer) {
        staging_idx = (staging_idx + 1) % NB_TEXTURE_STAGING_BUFFERS;
        staging_buffer = &staging_buffers[staging_idx];
    }

    // if we are not using the previous buffer, we wait if the buffer was used at least once,
    // less than MAX_FRAMES_RENDERING frames ago and we have not yet waited for its fence
    const bool need_wait = !use_previous_buffer
        && is_frame_timestamp_in_flight(staging_buffer->frame_timestamp, context->frame_timestamp)
        && staging_buffer->scene_timestamp > last_waited_scene;
    const vk::Fence current_fence = context->next_fence;

    // Bounded wait: re-check the abort flag every 100 ms so a lost device or a shutdown cannot hang the
    // render thread. A real failure raises a fatal renderer error instead of leaving the texture empty.
    // Returns false when the caller must give up (aborted or failed).
    auto wait_for_fence = [&](vk::Fence fence) {
        vk::Result result;
        do {
            result = state.device.waitForFences(fence, VK_TRUE, 100'000'000ULL);
            if (result == vk::Result::eTimeout && state.render_abort.load(std::memory_order_relaxed))
                return false;
        } while (result == vk::Result::eTimeout);
        if (result != vk::Result::eSuccess) {
            LOG_ERROR("Could not wait for fences: {}", vk::to_string(result));
            state.signal_fatal_error("texture staging waitForFences failed: " + vk::to_string(result), result == vk::Result::eErrorDeviceLost);
            return false;
        }
        return true;
    };

    if (need_wait) {
        if (staging_buffer->scene_timestamp == current_scene_timestamp) {
            assert(current_fence == staging_buffer->waiting_fence);
            // special case, all the staging buffer are occupied by the current scene
            // submit the command buffer and wait for it
            context->prerender_cmd.end();
            context->cmdbuffers_to_submit.push_back(context->prerender_cmd);

            vk::SubmitInfo submit_info{};
            submit_info.setCommandBuffers(context->cmdbuffers_to_submit);
            state.general_queue.submit(submit_info, current_fence);
            context->cmdbuffers_to_submit.clear();

            if (!wait_for_fence(current_fence))
                return;
            state.device.resetFences(current_fence);

            // also call begin again on the prerender command
            vk::CommandBufferBeginInfo begin_info{
                .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit
            };
            context->prerender_cmd.begin(begin_info);

            // then set all buffers as unused
            for (auto &buffer : staging_buffers) {
                buffer.frame_timestamp = ~0;
                buffer.scene_timestamp = ~0;
            }
        } else {
            // wait for the fence, but don't reset it
            if (!wait_for_fence(staging_buffer->waiting_fence))
                return;
            last_waited_scene = staging_buffer->scene_timestamp;
        }
    }

    // then we can use the buffer
    cmd_buffer = context->prerender_cmd;
    if (!use_previous_buffer) {
        staging_buffer->scene_timestamp = current_scene_timestamp;
        staging_buffer->frame_timestamp = context->frame_timestamp;
        staging_buffer->waiting_fence = current_fence;
        staging_buffer->used_so_far = 0;

        if (!staging_buffer->buffer.buffer || !staging_buffer->buffer.mapped_data
            || staging_buffer->buffer.size < current_texture->memory_needed) {
            // we need to create a bigger buffer
            // destroy the previous one if there is, no need to defer destroy it as we know it is no longer being used
            staging_buffer->buffer.destroy();

            staging_buffer->buffer.size = current_texture->memory_needed;
            staging_buffer->buffer.init_buffer(vk::BufferUsageFlagBits::eTransferSrc, vkutil::vma_mapped_alloc);
        }
    }

    // now the transition
    vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = current_texture->mip_count,
        .baseArrayLayer = 0,
        .layerCount = current_texture->is_cube ? 6U : 1U
    };

    // if this is done during configure, layout is undefined, otherwise it is shader read only
    if (is_configure)
        vkutil::transition_image_layout(cmd_buffer, current_texture->texture.image, vkutil::ImageLayout::Undefined, vkutil::ImageLayout::TransferDst, range);
    else
        vkutil::transition_image_layout_discard(cmd_buffer, current_texture->texture.image, vkutil::ImageLayout::SampledImage, vkutil::ImageLayout::TransferDst, range);

    is_texture_transfer_ready = true;
}

bool VKTextureCache::init(const bool hashless_texture_cache, const fs::path &texture_folder, const std::string_view game_id) {
    // set a limit to the number of samplers which can be allocated at the same time
    const size_t max_sampler_used = std::min(state.physical_device_properties.limits.maxSamplerAllocationCount / 2, 512U);

    TextureCache::init(hashless_texture_cache, texture_folder, game_id, max_sampler_used);
    backend = Backend::Vulkan;

    samplers.resize(max_sampler_used);

    // check for linear filtering on depth support
    // (depth surfaces use the host format of their GXM class: linear filtering must work on every depth format in use)
    bool depth_linear_ok = true;
    for (const vk::Format ds_format : state.ds_class_format) {
        if (dsmodel::host_has_depth(ds_format))
            depth_linear_ok = depth_linear_ok && static_cast<bool>(state.physical_device.getFormatProperties(ds_format).optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
    }
    const vk::FormatProperties x8d24_support = state.physical_device.getFormatProperties(vk::Format::eX8D24UnormPack32);
    support_depth_linear_filtering = depth_linear_ok;
    support_x8d24 = static_cast<bool>(x8d24_support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eDepthStencilAttachment);

    // other format
    const vk::FormatProperties e5rgb9_support = state.physical_device.getFormatProperties(vk::Format::eE5B9G9R9UfloatPack32);
    const vk::FormatProperties a2rgb10_support = state.physical_device.getFormatProperties(vk::Format::eA2R10G10B10UnormPack32);
    support_e5rgb9 = static_cast<bool>(e5rgb9_support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);
    support_a2rgb10 = static_cast<bool>(a2rgb10_support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);

    // PVRTC is mapped to RGBA8, so keep software decoding until native PVRTC formats are mapped.
    support_pvrt = false;

    // check for dxt support
    const vk::FormatProperties dxt_support = state.physical_device.getFormatProperties(vk::Format::eBc1RgbaSrgbBlock);
    // support_dxt might have already been set by the bcn patch on android
    support_dxt |= static_cast<bool>(dxt_support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);

    // check for astc support
    const vk::FormatProperties astc_support = state.physical_device.getFormatProperties(vk::Format::eAstc4x4SrgbBlock);
    support_astc = static_cast<bool>(astc_support.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImage);

    yuv_converter.init();

    return true;
}

void VKTextureCache::select(size_t index, const SceGxmTexture &texture) {
    current_texture = &textures[index];
    is_texture_transfer_ready = false;
}

static vk::Format linear_to_srgb(const vk::Format format) {
    switch (format) {
    case vk::Format::eR8Unorm:
        return vk::Format::eR8Srgb;
    case vk::Format::eR8G8Unorm:
        return vk::Format::eR8G8Srgb;
    case vk::Format::eR8G8B8A8Unorm:
        return vk::Format::eR8G8B8A8Srgb;
    case vk::Format::eBc1RgbaUnormBlock:
        return vk::Format::eBc1RgbaSrgbBlock;
    case vk::Format::eBc2UnormBlock:
        return vk::Format::eBc2SrgbBlock;
    case vk::Format::eBc3UnormBlock:
        return vk::Format::eBc3SrgbBlock;
    case vk::Format::eBc7UnormBlock:
        return vk::Format::eBc7SrgbBlock;
    default: {
        LOG_WARN_ONCE("Trying to use gamma correction with non-compatible format {}", vk::to_string(format));
        return format;
    }
    }
}

static vk::Format bcn_to_rgba8(const vk::Format format) {
    switch (format) {
    case vk::Format::eBc4UnormBlock:
        return vk::Format::eR8Unorm;
    case vk::Format::eBc4SnormBlock:
        return vk::Format::eR8Snorm;
    case vk::Format::eBc5UnormBlock:
        return vk::Format::eR8G8Unorm;
    case vk::Format::eBc5SnormBlock:
        return vk::Format::eR8G8Snorm;
    default:
        // BC1/2/3
        return vk::Format::eR8G8B8A8Unorm;
    }
}

// get an upper bound on the amount of memory needed to upload this texture
// the bound is only for one face and the base mip
static uint32_t get_image_memory_upper_bound(const SceGxmTexture &gxm_texture, const vk::Format vk_format, const SceGxmTextureBaseFormat base_format) {
    // we only want an upper bound, it doesn't matter if because of some conversions we later get the width instead of the stride
    // aligning to 8 takes care of the default stride values
    uint32_t stride = std::max(next_power_of_two(gxm::get_width(gxm_texture)), 8U);
    uint32_t height = std::max(next_power_of_two(gxm::get_height(gxm_texture)), 8U);

    if (gxm_texture.texture_type() == SCE_GXM_TEXTURE_LINEAR_STRIDED) {
        const uint32_t bpp = (gxm::bits_per_pixel(base_format) + 7) / 8;
        // the max is to handle the case of P4 textures
        stride = std::max<uint32_t>(stride, gxm::get_stride_in_bytes(gxm_texture) / bpp);
    }

    return ((stride * height) / vk::texelsPerBlock(vk_format)) * vk::blockSize(vk_format);
}

void VKTextureCache::configure_texture(const SceGxmTexture &gxm_texture) {
    const SceGxmTextureFormat format = gxm::get_format(gxm_texture);
    const SceGxmTextureBaseFormat base_format = gxm::get_base_format(format);

    const vk::ComponentMapping swizzle = texture::translate_swizzle(format);

    const bool is_cube = (gxm_texture.texture_type() == SCE_GXM_TEXTURE_CUBE || gxm_texture.texture_type() == SCE_GXM_TEXTURE_CUBE_ARBITRARY);

    uint32_t width = gxm::get_width(gxm_texture);
    uint32_t height = gxm::get_height(gxm_texture);

    uint16_t mip_count = renderer::texture::get_upload_mip(gxm_texture.true_mip_count(), width, height);

    vk::Format vk_format = texture::translate_format(base_format);
    if (gxm::is_bcn_format(base_format) && !support_dxt)
        // texture will be decompressed
        vk_format = bcn_to_rgba8(vk_format);
    if (gxm_texture.gamma_mode)
        vk_format = linear_to_srgb(vk_format);

    if (vk_format != vk::Format::eUndefined && !format_supports_sampled_image(vk_format)) {
        vk::Format fallback = texture::translate_format(base_format);
        if (gxm::is_bcn_format(base_format) && !support_dxt)
            fallback = bcn_to_rgba8(fallback);
        if (fallback != vk_format && format_supports_sampled_image(fallback)) {
            LOG_WARN_ONCE("Texture format {} is not sampleable on this GPU - using {} instead (sRGB decode dropped)", vk::to_string(vk_format), vk::to_string(fallback));
            vk_format = fallback;
        } else {
            LOG_ERROR("Texture format {} (base 0x{:X}) is not sampleable on this GPU and has no direct fallback - substituting RGBA8", vk::to_string(vk_format), fmt::underlying(base_format));
            vk_format = gxm_texture.gamma_mode ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        }
    }

    // Refuse to hand the gfx driver anything out of range and instead substitute safe values
    {
        const uint32_t max_dim = 16384; // conservative floor of maxImageDimension2D across our GPUs
        const bool format_invalid = (vk_format == vk::Format::eUndefined);
        const bool dims_invalid = (width == 0) || (height == 0) || (width > max_dim) || (height > max_dim);
        const uint32_t max_mips_allowed = static_cast<uint32_t>(std::bit_width(std::max(std::max(width, 1u), std::max(height, 1u))));
        const bool mips_invalid = (mip_count == 0) || (mip_count > max_mips_allowed);
        if (format_invalid || dims_invalid || mips_invalid) {
            LOG_ERROR("Refusing degenerate texture image: {}x{} mips {} format {} (base 0x{:X} type 0x{:X} cube {}) - substituting a safe placeholder",
                width, height, mip_count, vk::to_string(vk_format), fmt::underlying(base_format),
                fmt::underlying(gxm_texture.texture_type()), is_cube);
            if (format_invalid)
                vk_format = vk::Format::eR8G8B8A8Unorm;
            width = std::clamp(width, 1u, max_dim);
            height = std::clamp(height, 1u, max_dim);
            const uint32_t max_mips_fixed = static_cast<uint32_t>(std::bit_width(std::max(width, height)));
            mip_count = static_cast<uint16_t>(std::clamp<uint32_t>(mip_count, 1u, max_mips_fixed));
        }
    }

    // YUV textures are converted on the GPU into the RGBA8 image whenever the device has a route for it
    if (current_texture->yuv) {
        yuv_converter.destroy_entry(*current_texture->yuv);
        current_texture->yuv.reset();
    }
    if (gxm::is_yuv_format(base_format) && !is_cube) {
        const yuv::Kind kind = (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_YUV422) ? yuv::Kind::Packed422
            : (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_YUV420P3 ? yuv::Kind::P3 : yuv::Kind::P2);
        const SceGxmTextureType type = gxm_texture.texture_type();
        // packed 4:2:2 is only converted on the GPU when linear, the other layouts are unswizzled on the CPU first
        const bool layout_ok = kind != yuv::Kind::Packed422 || type == SCE_GXM_TEXTURE_LINEAR || type == SCE_GXM_TEXTURE_LINEAR_STRIDED;
        // the profile is stored in unk2 by renderer::set_texture
        const uint8_t profile = static_cast<uint8_t>(gxm_texture.unk2);
        const uint8_t swizzle = static_cast<uint8_t>(gxm_texture.swizzle_format);
        const yuv::Route route = layout_ok ? yuv_converter.select_route(kind, profile, swizzle) : yuv::Route::Cpu;
        if (route != yuv::Route::Cpu) {
            auto data = std::make_unique<YuvEntryData>();
            if (yuv_converter.create_entry(*data, route, kind, profile, swizzle, width, height)) {
                current_texture->yuv = std::move(data);
                // the converted image is written by a compute pass: single mip, no sRGB view of the storage image
                vk_format = vk::Format::eR8G8B8A8Unorm;
                mip_count = 1;
            }
        }
    }

    current_texture->mip_count = mip_count;
    current_texture->is_cube = is_cube;
    uint32_t memory_needed = get_image_memory_upper_bound(gxm_texture, vk_format, base_format);
    if (mip_count > 1)
        // using mips, the overall memory needed will be 4/3 of the base memory
        // round up to 3/2
        memory_needed += memory_needed / 2;
    if (is_cube)
        memory_needed *= 6;
    current_texture->memory_needed = align(memory_needed, 16);
    vkutil::Image &image = current_texture->texture;

    // In case the cache is full, no need to put the previous image in the destroy queue
    // because of texture importation, we must be careful when destroying an image
    if (image.image)
        state.frame().destroy_queue.add_image(image);

    // manually initialize the image
    image.width = width;
    image.height = height;
    image.format = vk_format;

    // create image
    vk::ImageCreateInfo image_info{
        .flags = is_cube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags(),
        .imageType = vk::ImageType::e2D,
        .format = vk_format,
        .extent = vk::Extent3D{
            .width = width,
            .height = height,
            .depth = 1 },
        .mipLevels = mip_count,
        .arrayLayers = is_cube ? 6U : 1U,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
            | (current_texture->yuv ? vk::ImageUsageFlagBits::eStorage : vk::ImageUsageFlags()),
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    std::tie(image.image, image.allocation) = state.allocator.createImage(image_info, vkutil::vma_auto_alloc);

    // create image view
    vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = mip_count,
        .baseArrayLayer = 0,
        .layerCount = is_cube ? 6U : 1U
    };

    vk::ImageViewCreateInfo view_info{
        .image = image.image,
        .viewType = is_cube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D,
        .format = vk_format,
        .components = swizzle,
        .subresourceRange = range
    };
    image.view = state.device.createImageView(view_info);

    prepare_staging_buffer(true);
}

// add an alpha channel to u8u8u8 textures
static void *add_alpha_channel(const void *pixels, const uint32_t width, const uint32_t height, std::vector<uint8_t> &data) {
    data.resize(width * height * 4);

    const uint8_t *src = static_cast<const uint8_t *>(pixels);
    uint8_t *dst = data.data();
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            // set 1.0 as the alpha channel
            dst[3] = 255;

            src += 3;
            dst += 4;
        }
    }

    return data.data();
}

void VKTextureCache::upload_texture_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height,
    uint32_t mip_index, const void *pixels, int face, uint32_t pixels_per_stride) {
    if (!is_texture_transfer_ready)
        prepare_staging_buffer();

    vkutil::Image &image = current_texture->texture;
    TextureStagingBuffer &staging_buffer = staging_buffers[staging_idx];

    if (face > 0)
        face--;

    if (pixels_per_stride == 0)
        pixels_per_stride = width;

    const void *text_data = pixels;
    std::vector<uint8_t> temp_data;
    if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8 || base_format == SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8) {
        text_data = add_alpha_channel(pixels, pixels_per_stride, height, temp_data);
        base_format = (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8) ? SCE_GXM_TEXTURE_BASE_FORMAT_U8U8U8U8 : SCE_GXM_TEXTURE_BASE_FORMAT_S8S8S8S8;
    }

    vk::DeviceSize upload_size;
    uint32_t buffer_height = height;
    if (gxm::is_bcn_format(base_format)) {
        upload_size = renderer::texture::get_compressed_size(base_format, pixels_per_stride, height);
        pixels_per_stride = align(pixels_per_stride, 4);
        buffer_height = align(buffer_height, 4);
    } else if (renderer::texture::is_astc_format(base_format)) {
        upload_size = renderer::texture::get_compressed_size(base_format, pixels_per_stride, height);
        const uint32_t block_height = gxm::get_block_size(base_format).second;
        // align can only be used with powers of 2 (not necessarily the case here)
        buffer_height = (buffer_height + block_height - 1) / block_height * block_height;
    } else {
        size_t bpp = gxm::bits_per_pixel(base_format);
        size_t bytes_per_pixel = (bpp + 7) >> 3;
        upload_size = pixels_per_stride * height * bytes_per_pixel;
    }

    if (staging_buffer.used_so_far + upload_size > staging_buffer.buffer.size) {
        LOG_ERROR("Staging buffer size left ({}) is too small for texture size {}!", staging_buffer.buffer.size - staging_buffer.used_so_far, upload_size);
#if ECV_ORACLE
        ecv::oracle::gpu::tex_upload_fail(static_cast<uint32_t>(staging_buffer.buffer.size - staging_buffer.used_so_far), upload_size, mip_index, width, height);
#endif
        return;
    }

    memcpy(static_cast<uint8_t *>(staging_buffer.buffer.mapped_data) + staging_buffer.used_so_far, text_data, upload_size);

    vk::ImageSubresourceLayers layer{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = mip_index,
        .baseArrayLayer = static_cast<uint32_t>(face),
        .layerCount = 1
    };
    vk::BufferImageCopy region{
        .bufferOffset = staging_buffer.used_so_far,
        .bufferRowLength = pixels_per_stride,
        .bufferImageHeight = buffer_height,
        .imageSubresource = layer,
        .imageOffset = { 0, 0, 0 },
        .imageExtent = { width, height, 1 }
    };
    cmd_buffer.copyBufferToImage(staging_buffer.buffer.buffer, image.image, vk::ImageLayout::eTransferDstOptimal, region);
    staging_buffer.used_so_far += upload_size;
}

void VKTextureCache::upload_done() {
    if (!is_texture_transfer_ready) {
        LOG_ERROR_ONCE("Texture upload produced no data (see preceding texture errors) - skipping layout transition");
#if ECV_ORACLE
        ecv::oracle::gpu::tex_upload_nodata();
#endif
        return;
    }

    if (current_texture->yuv && current_texture->yuv->pending) {
        // the planes are in place: convert them into the texture (leaves it ready for sampling)
        yuv_converter.finish_upload(*this, *current_texture);
        current_texture->texture.layout = vkutil::ImageLayout::SampledImage;
        cmd_buffer = nullptr;
        is_texture_transfer_ready = false;
        return;
    }

    // transition the texture back to read only
    vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = current_texture->mip_count,
        .baseArrayLayer = 0,
        .layerCount = current_texture->is_cube ? 6U : 1U
    };
    vkutil::transition_image_layout(cmd_buffer, current_texture->texture.image, vkutil::ImageLayout::TransferDst, vkutil::ImageLayout::SampledImage, range);
    current_texture->texture.layout = vkutil::ImageLayout::SampledImage;
    // this should not be necessary
    cmd_buffer = nullptr;
    is_texture_transfer_ready = false;
}

bool VKTextureCache::upload_yuv_gpu(const SceGxmTexture &texture, SceGxmTextureBaseFormat base_format, const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride_pixels, uint32_t layout_width, uint32_t layout_height) {
    TextureCacheEntry &entry = *current_texture;
    if (!entry.yuv || entry.yuv->route == yuv::Route::Cpu)
        return false;

    if (!is_texture_transfer_ready)
        prepare_staging_buffer();
    if (!is_texture_transfer_ready)
        return false;

    return yuv_converter.upload(*this, entry, texture, base_format, src, width, height, stride_pixels, layout_width, layout_height);
}

bool VKTextureCache::format_supports_sampled_image(vk::Format format) {
    auto it = sampled_image_support_cache.find(static_cast<VkFormat>(format));
    if (it != sampled_image_support_cache.end())
        return it->second;
    const vk::FormatProperties props = state.physical_device.getFormatProperties(format);
    constexpr vk::FormatFeatureFlags needed = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
    const bool supported = (props.optimalTilingFeatures & needed) == needed;
    sampled_image_support_cache[static_cast<VkFormat>(format)] = supported;
    return supported;
}

bool VKTextureCache::format_supports_linear_filter(vk::Format format) {
    auto it = linear_filter_support_cache.find(static_cast<VkFormat>(format));
    if (it != linear_filter_support_cache.end())
        return it->second;
    const vk::FormatProperties props = state.physical_device.getFormatProperties(format);
    const bool supported = static_cast<bool>(props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eSampledImageFilterLinear);
    linear_filter_support_cache[static_cast<VkFormat>(format)] = supported;
    if (!supported)
        LOG_INFO("format {} lacks linear-filter support: samplers for it will use nearest", vk::to_string(format));
    return supported;
}

void VKTextureCache::configure_sampler(size_t index, const SceGxmTexture &texture, bool no_linear) {
    vk::Sampler &sampler = samplers[index];
    if (sampler) {
        // the previous one has not been used for a while, we can destroy it
        state.device.destroy(sampler);
    }

    // Strided textures reuse the mip/LOD fields for their byte stride.
    // Their sampler key excludes those fields, so do not interpret them as LOD state.
    const bool is_linear_strided = texture.texture_type() == SCE_GXM_TEXTURE_LINEAR_STRIDED;

    // Unnormalized (texel unit) coordinates: the shader scales the coordinates when it has the texture viewport
    // (see sync_texture), otherwise the sampler does it with unnormalizedCoordinates
    renderer::SamplerSupport support = state.sampler_support;
    support.unnormalized_in_shader = state.features.use_texture_viewport;
    const bool unnormalized = !texture.normalize_mode;
    const bool unnormalized_sampler = unnormalized && !support.unnormalized_in_shader;

    const SceGxmTextureAddrMode uaddr = static_cast<SceGxmTextureAddrMode>(texture.uaddr_mode);
    const SceGxmTextureAddrMode vaddr = static_cast<SceGxmTextureAddrMode>(texture.vaddr_mode);
    // Note: I don't know what to do with the MIPMAP version of SceGxmTextureFilter
    SceGxmTextureFilter mag_filter = static_cast<SceGxmTextureFilter>(texture.mag_filter);
    SceGxmTextureFilter min_filter = is_linear_strided ? mag_filter : static_cast<SceGxmTextureFilter>(texture.min_filter);

    if (no_linear || unnormalized_sampler) {
        // an unnormalizedCoordinates sampler needs the same min and mag filter
        if (no_linear) {
            min_filter = SCE_GXM_TEXTURE_FILTER_POINT;
            mag_filter = SCE_GXM_TEXTURE_FILTER_POINT;
        } else {
            min_filter = mag_filter;
        }
    }

    // Unnormalized textures have no mip chain to select from
    const bool no_mips = is_linear_strided || unnormalized;

    vk::SamplerAddressMode address_u = texture::translate_address_mode(uaddr, support.mirror_clamp_to_edge);
    vk::SamplerAddressMode address_v = texture::translate_address_mode(vaddr, support.mirror_clamp_to_edge);
    if (unnormalized_sampler) {
        // the only address modes allowed with unnormalizedCoordinates
        const auto clamp_only = [](vk::SamplerAddressMode mode) {
            return mode == vk::SamplerAddressMode::eClampToBorder ? mode : vk::SamplerAddressMode::eClampToEdge;
        };
        address_u = clamp_only(address_u);
        address_v = clamp_only(address_v);
    }

    // create sampler
    vk::SamplerCreateInfo sampler_info{
        .magFilter = texture::translate_filter(mag_filter),
        .minFilter = texture::translate_filter(min_filter),
        .mipmapMode = !no_mips && texture.mip_filter && !no_linear ? vk::SamplerMipmapMode::eLinear : vk::SamplerMipmapMode::eNearest,
        .addressModeU = address_u,
        .addressModeV = address_v,
        .addressModeW = vk::SamplerAddressMode::eRepeat,
        // the bias must stay inside the device limit
        .mipLodBias = no_mips ? 0.f : renderer::translate_lod_bias(texture.lod_bias, support.max_lod_bias),
        .maxAnisotropy = 1.f,
        .compareEnable = VK_FALSE,
        .minLod = no_mips ? 0.f : static_cast<float>(texture.true_lod_min()),
        .maxLod = unnormalized ? 0.f : VK_LOD_CLAMP_NONE,
        // GXM has no programmable border colour: the border modes sample transparent black (see sampler_state.h)
        .borderColor = vk::BorderColor::eFloatTransparentBlack,
        .unnormalizedCoordinates = unnormalized_sampler ? VK_TRUE : VK_FALSE,
    };

    // when using nearest filter, disable anisotropy as the pixels can contain data other than color
    const renderer::AnisotropyPlan anisotropy = renderer::plan_anisotropy(anisotropic_filtering, support,
        sampler_info.magFilter != vk::Filter::eNearest, sampler_info.minFilter != vk::Filter::eNearest, unnormalized_sampler);
    sampler_info.anisotropyEnable = anisotropy.enable;
    sampler_info.maxAnisotropy = anisotropy.max_anisotropy;

    sampler = state.device.createSampler(sampler_info);
}

void VKTextureCache::import_configure_impl(SceGxmTextureBaseFormat base_format, uint32_t width, uint32_t height, bool is_srgb, uint16_t nb_components, uint16_t mipcount, bool swap_rb) {
    uint32_t texture_size;
    if (renderer::texture::is_astc_format(base_format)) {
        texture_size = renderer::texture::get_compressed_size(base_format, width, height);
    } else {
        const size_t bpp = gxm::bits_per_pixel(base_format);
        texture_size = static_cast<uint32_t>((align(width, 4) * align(height, 4) * bpp) / 8);
    }
    current_texture->memory_needed = align(texture_size, 16);

    current_texture->mip_count = mipcount;
    if (mipcount > 1)
        current_texture->memory_needed += current_texture->memory_needed / 2;

    const bool is_cube = current_info->texture.texture_type() == SCE_GXM_TEXTURE_CUBE || current_info->texture.texture_type() == SCE_GXM_TEXTURE_CUBE_ARBITRARY;
    current_texture->is_cube = is_cube;
    if (is_cube)
        current_texture->memory_needed *= 6;

    vkutil::Image &image = current_texture->texture;
    // In case the cache is full, no need to put the previous image in the destroy queue
    // because of texture importation, we must be careful when destroying an image
    if (image.image)
        state.frame().destroy_queue.add_image(image);

    vk::Format vk_format = texture::translate_format(base_format);
    if (is_srgb)
        vk_format = linear_to_srgb(vk_format);

    // manually initialize the image
    image.width = width;
    image.height = height;
    image.format = vk_format;

    // create image
    vk::ImageCreateInfo image_info{
        .flags = is_cube ? vk::ImageCreateFlagBits::eCubeCompatible : vk::ImageCreateFlags(),
        .imageType = vk::ImageType::e2D,
        .format = vk_format,
        .extent = vk::Extent3D{
            .width = width,
            .height = height,
            .depth = 1 },
        .mipLevels = mipcount,
        .arrayLayers = is_cube ? 6U : 1U,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
        .sharingMode = vk::SharingMode::eExclusive,
        .initialLayout = vk::ImageLayout::eUndefined,
    };

    std::tie(image.image, image.allocation) = state.allocator.createImage(image_info, vkutil::vma_auto_alloc);

    // create image view
    vk::ImageSubresourceRange range{
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = mipcount,
        .baseArrayLayer = 0,
        .layerCount = is_cube ? 6U : 1U
    };

    vk::ComponentMapping swizzle{ vk::ComponentSwizzle::eR, vk::ComponentSwizzle::eG, vk::ComponentSwizzle::eB, vk::ComponentSwizzle::eA };
    if (nb_components == 3) {
        swizzle.a = vk::ComponentSwizzle::eOne;
    } else if (nb_components <= 2) {
        // use the real swizzle
        swizzle = texture::translate_swizzle(gxm::get_format(current_info->texture));
    }

    // u5u6u5 is stored as bgr in vulkan
    if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U5U6U5) {
        swap_rb = !swap_rb;
    }

    // this format is stored as abgr in vulkan
    if (base_format == SCE_GXM_TEXTURE_BASE_FORMAT_U4U4U4U4) {
        std::swap(swizzle.r, swizzle.a);
        std::swap(swizzle.g, swizzle.b);
    }

    if (swap_rb)
        std::swap(swizzle.r, swizzle.b);

    vk::ImageViewCreateInfo view_info{
        .image = image.image,
        .viewType = is_cube ? vk::ImageViewType::eCube : vk::ImageViewType::e2D,
        .format = vk_format,
        .components = swizzle,
        .subresourceRange = range
    };
    image.view = state.device.createImageView(view_info);

    prepare_staging_buffer(true);
}

} // namespace renderer::vulkan
