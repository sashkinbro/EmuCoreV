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

#include <renderer/vulkan/yuv_convert.h>

#include <renderer/vulkan/state.h>
#include <renderer/vulkan/types.h>

#include <gxm/functions.h>
#include <util/align.h>
#include <util/log.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace renderer::vulkan {

#include "yuv_shaders/yuv_shaders.inc"

namespace {

struct PlanarPush {
    float yc[4];
    float coef[4];
    int32_t info[4];
};
static_assert(sizeof(PlanarPush) == 48);

struct YcbcrPush {
    int32_t size[2];
};

constexpr uint32_t group_size = 8;

// The YCbCr route is only used for packed 4:2:2 (single plane formats). The two plane 4:2:0 formats were tried and
// crash lavapipe in vkCmdCopyBufferToImage on the second plane, so 4:2:0 uses the (bit exact) shader route.
// YUYV / YVYU use the G8B8G8R8 byte order (Y0 U Y1 V), UYVY / VYUY the B8G8R8G8 one (U Y0 V Y1)
vk::Format ycbcr_format(uint32_t swizzle) {
    return (swizzle & 3) >= 2 ? vk::Format::eB8G8R8G8422Unorm : vk::Format::eG8B8G8R8422Unorm;
}

// the chroma order is swapped compared to the Vulkan format (V before U)
bool ycbcr_chroma_swapped(uint32_t swizzle) {
    return (swizzle & 3) == 1 || (swizzle & 3) == 3;
}

uint32_t ycbcr_key(uint8_t profile, uint32_t swizzle) {
    return static_cast<uint32_t>(profile) | ((swizzle & 3) << 2);
}

bool features_ok(vk::PhysicalDevice device, vk::Format format, vk::FormatFeatureFlags needed) {
    return (device.getFormatProperties(format).optimalTilingFeatures & needed) == needed;
}

vk::Format rgba8_plane_format(yuv::Kind kind, uint32_t plane) {
    if (kind == yuv::Kind::Packed422)
        return vk::Format::eR8G8B8A8Unorm;
    return (kind == yuv::Kind::P2 && plane == 1) ? vk::Format::eR8G8Unorm : vk::Format::eR8Unorm;
}

} // namespace

void YuvConverter::init() {
    if (probed)
        return;
    probed = true;

    forced_route = -1;
#if !defined(NDEBUG) || ECV_ORACLE || defined(ECV_YUV_DEBUG)
    // debug switch: ECV_YUV_ROUTE=cpu|shader|ycbcr tries that route first (falls back like the automatic choice)
    if (const char *forced = std::getenv("ECV_YUV_ROUTE")) {
        if (std::strcmp(forced, "cpu") == 0)
            forced_route = static_cast<int>(yuv::Route::Cpu);
        else if (std::strcmp(forced, "shader") == 0)
            forced_route = static_cast<int>(yuv::Route::Shader);
        else if (std::strcmp(forced, "ycbcr") == 0)
            forced_route = static_cast<int>(yuv::Route::Ycbcr);
        if (forced_route >= 0)
            LOG_WARN("YUV route forced to '{}' (ECV_YUV_ROUTE)", forced);
    }
#endif

    const vk::PhysicalDevice device = state.physical_device;

    // shader route: compute on the general queue, rgba8 storage image, sampled R8 / RG8 / RGBA8
    const bool compute = state.general_family_index < state.physical_device_queue_families.size()
        && (state.physical_device_queue_families[state.general_family_index].queueFlags & vk::QueueFlagBits::eCompute);
    constexpr vk::FormatFeatureFlags plane_features = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst;
    const bool formats = features_ok(device, vk::Format::eR8Unorm, plane_features)
        && features_ok(device, vk::Format::eR8G8Unorm, plane_features)
        && features_ok(device, vk::Format::eR8G8B8A8Unorm, plane_features)
        && features_ok(device, vk::Format::eR8G8B8A8Unorm, vk::FormatFeatureFlagBits::eStorageImage | vk::FormatFeatureFlagBits::eSampledImage);
    const bool invocations = state.physical_device_properties.limits.maxComputeWorkGroupInvocations >= group_size * group_size
        && state.physical_device_properties.limits.maxComputeWorkGroupSize[0] >= group_size
        && state.physical_device_properties.limits.maxComputeWorkGroupSize[1] >= group_size;
    caps.shader = compute && formats && invocations;

    // ycbcr route: the feature is enabled and the multi-planar formats can be sampled with a linear chroma filter
    const bool ycbcr_usable = state.sampler_device_support.ycbcr_conversion
        && VULKAN_HPP_DEFAULT_DISPATCHER.vkCreateSamplerYcbcrConversion != nullptr
        && caps.shader;
    if (ycbcr_usable) {
        constexpr vk::FormatFeatureFlags needed = vk::FormatFeatureFlagBits::eSampledImage | vk::FormatFeatureFlagBits::eTransferDst
            | vk::FormatFeatureFlagBits::eSampledImageYcbcrConversionLinearFilter;
        const auto usable = [&](vk::Format format) {
            return features_ok(device, format, needed)
                && (device.getFormatProperties(format).optimalTilingFeatures
                    & (vk::FormatFeatureFlagBits::eMidpointChromaSamples | vk::FormatFeatureFlagBits::eCositedChromaSamples));
        };
        ycbcr_format_ok[static_cast<uint32_t>(yuv::Kind::Packed422)] = usable(vk::Format::eG8B8G8R8422Unorm) && usable(vk::Format::eB8G8R8G8422Unorm);
    }

    LOG_INFO("YUV conversion: shader route={} ycbcr route (4:2:2)={}", caps.shader, ycbcr_format_ok[static_cast<uint32_t>(yuv::Kind::Packed422)]);
}

bool YuvConverter::ensure_pipelines() {
    if (pipelines_ready)
        return true;
    if (pipelines_failed || !caps.shader)
        return false;

    const vk::Device device = state.device;
    try {
        planar_module = vkutil::load_shader(device, yuv_planar_spv, sizeof(yuv_planar_spv));

        std::array<vk::DescriptorSetLayoutBinding, 4> bindings;
        for (uint32_t i = 0; i < 3; i++) {
            bindings[i] = vk::DescriptorSetLayoutBinding{
                .binding = i,
                .descriptorType = vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eCompute
            };
        }
        bindings[3] = vk::DescriptorSetLayoutBinding{
            .binding = 3,
            .descriptorType = vk::DescriptorType::eStorageImage,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eCompute
        };
        vk::DescriptorSetLayoutCreateInfo set_info{};
        set_info.setBindings(bindings);
        planar_set_layout = device.createDescriptorSetLayout(set_info);

        const vk::PushConstantRange push{
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .offset = 0,
            .size = sizeof(PlanarPush)
        };
        vk::PipelineLayoutCreateInfo layout_info{};
        layout_info.setSetLayouts(planar_set_layout);
        layout_info.setPushConstantRanges(push);
        planar_pipeline_layout = device.createPipelineLayout(layout_info);

        const vk::ComputePipelineCreateInfo pipeline_info{
            .stage = {
                .stage = vk::ShaderStageFlagBits::eCompute,
                .module = planar_module,
                .pName = "main" },
            .layout = planar_pipeline_layout
        };
        const auto result = device.createComputePipeline(nullptr, pipeline_info);
        if (result.result != vk::Result::eSuccess)
            throw std::runtime_error("compute pipeline");
        planar_pipeline = result.value;

        ycbcr_module = vkutil::load_shader(device, yuv_ycbcr_spv, sizeof(yuv_ycbcr_spv));
    } catch (const std::exception &e) {
        LOG_ERROR("YUV compute pipeline creation failed ({}), GPU YUV conversion is disabled", e.what());
        pipelines_failed = true;
        caps.shader = false;
        for (bool &ok : ycbcr_format_ok)
            ok = false;
        cleanup();
        return false;
    }

    pipelines_ready = true;
    return true;
}

const YcbcrObjects *YuvConverter::get_ycbcr_objects(yuv::Kind kind, uint8_t profile, uint8_t swizzle) {
    const uint32_t key = ycbcr_key(profile, swizzle);
    if (const auto it = ycbcr_objects.find(key); it != ycbcr_objects.end())
        return it->second.get();
    if (ycbcr_failed.count(key))
        return nullptr;

    const vk::Device device = state.device;
    auto objects = std::make_unique<YcbcrObjects>();
    objects->format = ycbcr_format(swizzle);
    try {
        const vk::FormatFeatureFlags features = state.physical_device.getFormatProperties(objects->format).optimalTilingFeatures;
        const bool cosited = static_cast<bool>(features & vk::FormatFeatureFlagBits::eCositedChromaSamples);
        const bool midpoint = static_cast<bool>(features & vk::FormatFeatureFlagBits::eMidpointChromaSamples);
        // the siting of the shader route: co-sited with the even luma sample horizontally, midpoint vertically.
        // A format supporting a single kind of siting has to use it for both axes.
        const vk::ChromaLocation x_offset = cosited ? vk::ChromaLocation::eCositedEven : vk::ChromaLocation::eMidpoint;
        const vk::ChromaLocation y_offset = midpoint ? vk::ChromaLocation::eMidpoint : vk::ChromaLocation::eCositedEven;

        const bool bt709 = profile == yuv::BT709_STANDARD || profile == yuv::BT709_FULL_RANGE;
        const bool full = profile == yuv::BT601_FULL_RANGE || profile == yuv::BT709_FULL_RANGE;
        const bool swap = ycbcr_chroma_swapped(swizzle);

        const vk::SamplerYcbcrConversionCreateInfo conversion_info{
            .format = objects->format,
            .ycbcrModel = bt709 ? vk::SamplerYcbcrModelConversion::eYcbcr709 : vk::SamplerYcbcrModelConversion::eYcbcr601,
            .ycbcrRange = full ? vk::SamplerYcbcrRange::eItuFull : vk::SamplerYcbcrRange::eItuNarrow,
            .components = {
                swap ? vk::ComponentSwizzle::eB : vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity,
                swap ? vk::ComponentSwizzle::eR : vk::ComponentSwizzle::eIdentity,
                vk::ComponentSwizzle::eIdentity },
            .xChromaOffset = x_offset,
            .yChromaOffset = y_offset,
            .chromaFilter = vk::Filter::eLinear,
            .forceExplicitReconstruction = VK_FALSE
        };
        objects->conversion = device.createSamplerYcbcrConversion(conversion_info);

        const vk::SamplerYcbcrConversionInfo conversion_ref{ .conversion = objects->conversion };
        const vk::SamplerCreateInfo sampler_info{
            .pNext = &conversion_ref,
            .magFilter = vk::Filter::eLinear,
            .minFilter = vk::Filter::eLinear,
            .mipmapMode = vk::SamplerMipmapMode::eNearest,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxAnisotropy = 1.0f,
            .minLod = 0.0f,
            .maxLod = 0.0f,
            .borderColor = vk::BorderColor::eFloatTransparentBlack,
            .unnormalizedCoordinates = VK_FALSE
        };
        objects->sampler = device.createSampler(sampler_info);

        const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = { {
            { .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute, .pImmutableSamplers = &objects->sampler },
            { .binding = 1, .descriptorType = vk::DescriptorType::eStorageImage, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eCompute },
        } };
        vk::DescriptorSetLayoutCreateInfo set_info{};
        set_info.setBindings(bindings);
        objects->set_layout = device.createDescriptorSetLayout(set_info);

        const vk::PushConstantRange push{
            .stageFlags = vk::ShaderStageFlagBits::eCompute,
            .offset = 0,
            .size = sizeof(YcbcrPush)
        };
        vk::PipelineLayoutCreateInfo layout_info{};
        layout_info.setSetLayouts(objects->set_layout);
        layout_info.setPushConstantRanges(push);
        objects->pipeline_layout = device.createPipelineLayout(layout_info);

        const vk::ComputePipelineCreateInfo pipeline_info{
            .stage = {
                .stage = vk::ShaderStageFlagBits::eCompute,
                .module = ycbcr_module,
                .pName = "main" },
            .layout = objects->pipeline_layout
        };
        const auto result = device.createComputePipeline(nullptr, pipeline_info);
        if (result.result != vk::Result::eSuccess)
            throw std::runtime_error("compute pipeline");
        objects->pipeline = result.value;
    } catch (const std::exception &e) {
        LOG_WARN("YCbCr conversion objects for {} could not be created ({}), using the shader route", vk::to_string(objects->format), e.what());
        if (objects->pipeline)
            device.destroy(objects->pipeline);
        if (objects->pipeline_layout)
            device.destroy(objects->pipeline_layout);
        if (objects->set_layout)
            device.destroy(objects->set_layout);
        if (objects->sampler)
            device.destroy(objects->sampler);
        if (objects->conversion)
            device.destroy(objects->conversion);
        ycbcr_failed[key] = true;
        return nullptr;
    }

    const YcbcrObjects *result = objects.get();
    ycbcr_objects[key] = std::move(objects);
    return result;
}

void YuvConverter::destroy_ycbcr_objects() {
    const vk::Device device = state.device;
    for (auto &[key, objects] : ycbcr_objects) {
        device.destroy(objects->pipeline);
        device.destroy(objects->pipeline_layout);
        device.destroy(objects->set_layout);
        device.destroy(objects->sampler);
        device.destroy(objects->conversion);
    }
    ycbcr_objects.clear();
    ycbcr_failed.clear();
}

void YuvConverter::cleanup() {
    if (!state.device)
        return;
    const vk::Device device = state.device;

    destroy_ycbcr_objects();
    if (planar_pipeline)
        device.destroy(planar_pipeline);
    if (planar_pipeline_layout)
        device.destroy(planar_pipeline_layout);
    if (planar_set_layout)
        device.destroy(planar_set_layout);
    if (planar_module)
        device.destroy(planar_module);
    if (ycbcr_module)
        device.destroy(ycbcr_module);
    planar_pipeline = nullptr;
    planar_pipeline_layout = nullptr;
    planar_set_layout = nullptr;
    planar_module = nullptr;
    ycbcr_module = nullptr;
    pipelines_ready = false;

    for (size_t i = 0; i < pools.size(); i++) {
        if (pools[i])
            device.destroy(pools[i]);
        pools[i] = nullptr;
        pool_frame[i] = ~0ULL;
    }
}

yuv::Route YuvConverter::select_route(yuv::Kind kind, uint8_t profile, uint8_t swizzle) {
    init();

    yuv::RouteCaps current;
    current.shader = ensure_pipelines();
    if (current.shader && ycbcr_format_ok[static_cast<uint32_t>(kind)]
        && (forced_route < 0 || forced_route == static_cast<int>(yuv::Route::Ycbcr)))
        current.ycbcr[static_cast<uint32_t>(kind)] = get_ycbcr_objects(kind, profile, swizzle) != nullptr;
    return yuv::select_route(current, kind, forced_route);
}

bool YuvConverter::create_entry(YuvEntryData &data, yuv::Route route, yuv::Kind kind, uint8_t profile, uint8_t swizzle, uint32_t width, uint32_t height) {
    data.route = yuv::Route::Cpu;
    data.kind = kind;
    data.profile = profile;
    data.swizzle = swizzle;
    data.width = width;
    data.height = height;
    data.plane_count = 0;
    data.ycbcr = nullptr;
    data.pending = false;
    if (route == yuv::Route::Cpu)
        return false;

    const vk::Device device = state.device;
    const auto make_image = [&](vkutil::Image &image, uint32_t w, uint32_t h, vk::Format format, const void *view_pnext) {
        const vk::ImageCreateInfo image_info{
            .imageType = vk::ImageType::e2D,
            .format = format,
            .extent = vk::Extent3D{ .width = w, .height = h, .depth = 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = vk::SampleCountFlagBits::e1,
            .tiling = vk::ImageTiling::eOptimal,
            .usage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst,
            .sharingMode = vk::SharingMode::eExclusive,
            .initialLayout = vk::ImageLayout::eUndefined,
        };
        std::tie(image.image, image.allocation) = state.allocator.createImage(image_info, vkutil::vma_auto_alloc);
        image.width = w;
        image.height = h;
        image.format = format;
        image.layout = vkutil::ImageLayout::Undefined;

        const vk::ImageViewCreateInfo view_info{
            .pNext = view_pnext,
            .image = image.image,
            .viewType = vk::ImageViewType::e2D,
            .format = format,
            .components = vkutil::default_comp_mapping,
            .subresourceRange = vkutil::color_subresource_range
        };
        image.view = device.createImageView(view_info);
    };

    try {
        if (route == yuv::Route::Shader) {
            const uint32_t cw = (width + 1) / 2;
            const uint32_t ch = (height + 1) / 2;
            if (kind == yuv::Kind::Packed422) {
                make_image(data.planes[0], cw, height, vk::Format::eR8G8B8A8Unorm, nullptr);
                data.plane_count = 1;
            } else {
                make_image(data.planes[0], width, height, vk::Format::eR8Unorm, nullptr);
                make_image(data.planes[1], cw, ch, rgba8_plane_format(kind, 1), nullptr);
                data.plane_count = 2;
                if (kind == yuv::Kind::P3) {
                    make_image(data.planes[2], cw, ch, vk::Format::eR8Unorm, nullptr);
                    data.plane_count = 3;
                }
            }
        } else {
            const YcbcrObjects *objects = get_ycbcr_objects(kind, profile, swizzle);
            if (!objects)
                return false;
            // subsampled formats need even dimensions, the extra column / row is never sampled for the output
            const vk::SamplerYcbcrConversionInfo conversion_ref{ .conversion = objects->conversion };
            make_image(data.planes[0], (width + 1) & ~1U, (kind == yuv::Kind::Packed422) ? height : ((height + 1) & ~1U), objects->format, &conversion_ref);
            data.plane_count = 1;
            data.ycbcr = objects;
        }
    } catch (const std::exception &e) {
        LOG_ERROR("YUV plane image creation failed ({}), using the CPU conversion", e.what());
        destroy_entry_now(data);
        return false;
    }

    data.route = route;
    return true;
}

void YuvConverter::destroy_entry(YuvEntryData &data) {
    FrameObject &frame = state.frame();
    for (vkutil::Image &plane : data.planes)
        frame.destroy_queue.add_image(plane);
    data.plane_count = 0;
    data.ycbcr = nullptr;
    data.pending = false;
    data.route = yuv::Route::Cpu;
}

void YuvConverter::destroy_entry_now(YuvEntryData &data) {
    for (vkutil::Image &plane : data.planes)
        plane.destroy();
    data.plane_count = 0;
    data.ycbcr = nullptr;
    data.pending = false;
    data.route = yuv::Route::Cpu;
}

vk::DescriptorSet YuvConverter::allocate_set(vk::DescriptorSetLayout layout) {
    const vk::Device device = state.device;
    const size_t slot = static_cast<size_t>(state.current_frame_idx) % pools.size();
    const VKContext *context = reinterpret_cast<const VKContext *>(state.context);
    if (!context)
        return {};

    try {
        if (!pools[slot]) {
            constexpr uint32_t max_sets = 32;
            const std::array<vk::DescriptorPoolSize, 3> sizes = { {
                { vk::DescriptorType::eSampledImage, max_sets * 3 },
                { vk::DescriptorType::eStorageImage, max_sets },
                // a combined image sampler with a YCbCr conversion may take several descriptors
                { vk::DescriptorType::eCombinedImageSampler, max_sets * 4 },
            } };
            vk::DescriptorPoolCreateInfo pool_info{ .maxSets = max_sets };
            pool_info.setPoolSizes(sizes);
            pools[slot] = device.createDescriptorPool(pool_info);
            pool_frame[slot] = ~0ULL;
        }
        if (pool_frame[slot] != context->frame_timestamp) {
            // the slot was last used by a frame that has finished (the renderer waited for its fence)
            device.resetDescriptorPool(pools[slot]);
            pool_frame[slot] = context->frame_timestamp;
        }

        vk::DescriptorSetAllocateInfo allocate_info{ .descriptorPool = pools[slot] };
        allocate_info.setSetLayouts(layout);
        return device.allocateDescriptorSets(allocate_info).front();
    } catch (const std::exception &e) {
        LOG_WARN_ONCE("YUV descriptor set allocation failed ({}), using the CPU conversion", e.what());
        return {};
    }
}

bool YuvConverter::upload(VKTextureCache &cache, TextureCacheEntry &entry, const SceGxmTexture &texture, SceGxmTextureBaseFormat base_format,
    const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride_pixels, uint32_t layout_width, uint32_t layout_height) {
    YuvEntryData &data = *entry.yuv;
    if (data.route == yuv::Route::Cpu || !ensure_pipelines() || width != data.width || height != data.height)
        return false;

    struct PlaneCopy {
        const uint8_t *src;
        uint64_t bytes;
        uint32_t row_texels;
        uint32_t rows;
        uint32_t plane; // index in data.planes
        vk::ImageAspectFlags aspect;
        vk::Extent3D extent;
    };
    std::array<PlaneCopy, 3> copies;
    uint32_t copy_count = 0;

    const bool ycbcr = data.route == yuv::Route::Ycbcr;
    if (data.kind == yuv::Kind::Packed422) {
        const uint32_t macro_count = (width + 1) / 2;
        const uint32_t row_texels = ycbcr ? ((std::max(stride_pixels, width) + 1) & ~1U) : std::max(stride_pixels / 2, macro_count);
        const uint32_t extent_width = ycbcr ? ((width + 1) & ~1U) : macro_count;
        copies[copy_count++] = { src, static_cast<uint64_t>(stride_pixels) * 2 * height, row_texels, height, 0, vk::ImageAspectFlagBits::eColor, { extent_width, height, 1 } };
    } else {
        const bool is_p3 = data.kind == yuv::Kind::P3;
        const yuv::PlaneLayout420 layout = yuv::plane_layout_420(is_p3, width, height, stride_pixels, layout_width, layout_height);
        const vk::ImageAspectFlags color = vk::ImageAspectFlagBits::eColor;
        copies[copy_count++] = { src, static_cast<uint64_t>(layout.y_stride) * height, layout.y_stride, height, 0, color, { width, height, 1 } };
        if (is_p3) {
            copies[copy_count++] = { src + layout.c0_offset, static_cast<uint64_t>(layout.c_stride) * layout.chroma_height, layout.c_stride, layout.chroma_height, 1,
                color, { layout.chroma_width, layout.chroma_height, 1 } };
            copies[copy_count++] = { src + layout.c1_offset, static_cast<uint64_t>(layout.c_stride) * layout.chroma_height, layout.c_stride, layout.chroma_height, 2,
                color, { layout.chroma_width, layout.chroma_height, 1 } };
        } else {
            // interleaved chroma plane: R8G8 texels, two bytes each
            copies[copy_count++] = { src + layout.c0_offset, static_cast<uint64_t>(layout.c_stride) * layout.chroma_height, layout.c_stride / 2, layout.chroma_height, 1,
                color, { layout.chroma_width, layout.chroma_height, 1 } };
        }
    }

    TextureStagingBuffer &staging = cache.staging_buffers[cache.staging_idx];
    uint64_t needed = 0;
    for (uint32_t i = 0; i < copy_count; i++)
        needed += align(copies[i].bytes, 16);
    if (staging.used_so_far + needed > staging.buffer.size) {
        LOG_ERROR("Staging buffer size left ({}) is too small for YUV texture size {}!", staging.buffer.size - staging.used_so_far, needed);
        return false;
    }

    // the descriptor set is allocated before anything is recorded so that the CPU path can still take over
    const vk::DescriptorSet set = allocate_set(ycbcr ? data.ycbcr->set_layout : planar_set_layout);
    if (!set)
        return false;

    const vk::Device device = state.device;
    const vk::CommandBuffer cmd = cache.cmd_buffer;

    // descriptors: the planes as inputs, the converted texture as output
    const vk::ImageView dst_view = entry.texture.view;
    if (ycbcr) {
        const vk::DescriptorImageInfo input{ .imageView = data.planes[0].view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        const vk::DescriptorImageInfo output{ .imageView = dst_view, .imageLayout = vk::ImageLayout::eGeneral };
        const std::array<vk::WriteDescriptorSet, 2> writes = { {
            { .dstSet = set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &input },
            { .dstSet = set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eStorageImage, .pImageInfo = &output },
        } };
        device.updateDescriptorSets(writes, {});
    } else {
        std::array<vk::DescriptorImageInfo, 4> infos;
        for (uint32_t i = 0; i < 3; i++) {
            // unused bindings reference an existing plane
            infos[i] = { .imageView = data.planes[i < data.plane_count ? i : 0].view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal };
        }
        infos[3] = { .imageView = dst_view, .imageLayout = vk::ImageLayout::eGeneral };
        std::array<vk::WriteDescriptorSet, 4> writes;
        for (uint32_t i = 0; i < 4; i++) {
            writes[i] = vk::WriteDescriptorSet{ .dstSet = set, .dstBinding = i, .descriptorCount = 1,
                .descriptorType = i < 3 ? vk::DescriptorType::eSampledImage : vk::DescriptorType::eStorageImage, .pImageInfo = &infos[i] };
        }
        device.updateDescriptorSets(writes, {});
    }

    // copy the planes
    for (uint32_t i = 0; i < data.plane_count; i++)
        data.planes[i].transition_to_discard(cmd, vkutil::ImageLayout::TransferDst);

    for (uint32_t i = 0; i < copy_count; i++) {
        const PlaneCopy &copy = copies[i];
        std::memcpy(static_cast<uint8_t *>(staging.buffer.mapped_data) + staging.used_so_far, copy.src, copy.bytes);
        const vk::BufferImageCopy region{
            .bufferOffset = staging.used_so_far,
            .bufferRowLength = copy.row_texels,
            .bufferImageHeight = 0,
            .imageSubresource = { .aspectMask = copy.aspect, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = copy.extent
        };
        cmd.copyBufferToImage(staging.buffer.buffer, data.planes[copy.plane].image, vk::ImageLayout::eTransferDstOptimal, region);
        staging.used_so_far += static_cast<uint32_t>(align(copy.bytes, 16));
    }

    for (uint32_t i = 0; i < data.plane_count; i++)
        data.planes[i].transition_to(cmd, vkutil::ImageLayout::SampledImage);

    data.pending = true;
    data.pending_set = set;
    return true;
}

void YuvConverter::finish_upload(VKTextureCache &cache, TextureCacheEntry &entry) {
    YuvEntryData &data = *entry.yuv;
    const vk::CommandBuffer cmd = cache.cmd_buffer;
    vkutil::Image &dst = entry.texture;
    data.pending = false;

    // the destination is in TransferDst since the staging buffer was prepared
    vkutil::transition_image_layout(cmd, dst.image, vkutil::ImageLayout::TransferDst, vkutil::ImageLayout::StorageImage);

    const uint32_t groups_x = (data.width + group_size - 1) / group_size;
    const uint32_t groups_y = (data.height + group_size - 1) / group_size;
    if (data.route == yuv::Route::Ycbcr) {
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, data.ycbcr->pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, data.ycbcr->pipeline_layout, 0, data.pending_set, {});
        // the multi-planar image has even dimensions, the pass samples it as a whole
        const YcbcrPush push{ { static_cast<int32_t>(data.planes[0].width), static_cast<int32_t>(data.planes[0].height) } };
        cmd.pushConstants(data.ycbcr->pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
        cmd.dispatch(groups_x, groups_y, 1);
    } else {
        const yuv::Matrix m = yuv::make_matrix(data.profile);
        PlanarPush push{};
        push.yc[0] = m.y_bias;
        push.yc[1] = m.y_scale;
        push.yc[2] = m.c_bias;
        push.yc[3] = m.c_scale;
        push.coef[0] = m.r_v;
        push.coef[1] = m.g_u;
        push.coef[2] = m.g_v;
        push.coef[3] = m.b_u;
        push.info[0] = static_cast<int32_t>(data.width);
        push.info[1] = static_cast<int32_t>(data.height);
        push.info[2] = data.kind == yuv::Kind::Packed422 ? 2 : (data.kind == yuv::Kind::P3 ? 1 : 0);
        push.info[3] = data.kind == yuv::Kind::Packed422 ? (data.swizzle & 3) : (data.swizzle & 1);
        cmd.bindPipeline(vk::PipelineBindPoint::eCompute, planar_pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, planar_pipeline_layout, 0, data.pending_set, {});
        cmd.pushConstants(planar_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
        cmd.dispatch(groups_x, groups_y, 1);
    }

    vkutil::transition_image_layout(cmd, dst.image, vkutil::ImageLayout::StorageImage, vkutil::ImageLayout::SampledImage);
    data.pending_set = nullptr;
}

} // namespace renderer::vulkan
