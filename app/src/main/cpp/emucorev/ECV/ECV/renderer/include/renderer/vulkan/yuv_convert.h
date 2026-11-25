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

// GPU conversion of GXM YUV textures (YUV420P2/P3, YUV422) to the RGBA8 image the shaders sample.
// The guest planes are uploaded as they are (R8 / RG8 / RGBA8, or one multi-planar image) and a compute pass writes
// the converted image. Route selection and the maths are described in renderer/yuv.h.

#include <renderer/yuv.h>

#include <vkutil/objects.h>

#include <array>
#include <map>
#include <memory>

struct SceGxmTexture;
enum SceGxmTextureBaseFormat : uint32_t;

namespace renderer::vulkan {

struct VKState;
struct VKTextureCache;
struct TextureCacheEntry;

// Pipeline objects of the YCbCr route (one per multi-planar format, profile and chroma order, the conversion is
// baked into an immutable sampler).
struct YcbcrObjects {
    vk::Format format = vk::Format::eUndefined;
    vk::SamplerYcbcrConversion conversion;
    vk::Sampler sampler;
    vk::DescriptorSetLayout set_layout;
    vk::PipelineLayout pipeline_layout;
    vk::Pipeline pipeline;
};

// GPU side of one cache entry holding a YUV texture
struct YuvEntryData {
    yuv::Route route = yuv::Route::Cpu;
    yuv::Kind kind = yuv::Kind::P2;
    uint8_t profile = 0;
    uint8_t swizzle = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    // Shader route: [0] luma R8 / packed macro pixels RGBA8, [1] chroma RG8 (P2) or R8 (P3), [2] second chroma plane (P3)
    // Ycbcr route: [0] the multi-planar image
    std::array<vkutil::Image, 3> planes;
    uint32_t plane_count = 0;
    const YcbcrObjects *ycbcr = nullptr;
    // conversion recorded by upload(), executed by finish_upload()
    bool pending = false;
    vk::DescriptorSet pending_set;
};

class YuvConverter {
public:
    explicit YuvConverter(VKState &state)
        : state(state) {}

    // probes what the device can do, once the logical device exists
    void init();
    // destroys pipelines and pools, the device must be idle (the converter recreates them when it is used again)
    void cleanup();

    // route for a texture, from the probed capabilities (creates the pipeline objects on first use)
    yuv::Route select_route(yuv::Kind kind, uint8_t profile, uint8_t swizzle);

    // creates the GPU resources of an entry whose RGBA8 image already exists. Returns false when the route cannot
    // be used, in which case data.route is Cpu.
    bool create_entry(YuvEntryData &data, yuv::Route route, yuv::Kind kind, uint8_t profile, uint8_t swizzle, uint32_t width, uint32_t height);
    // hands the GPU objects to the frame destroy queue
    void destroy_entry(YuvEntryData &data);
    // destroys the GPU objects right away, the device must be idle
    void destroy_entry_now(YuvEntryData &data);

    // copies the guest planes into the staging buffer and the plane images (recorded in cache.cmd_buffer, the
    // texture transfer must be ready). Returns false, with nothing recorded, when the CPU path must be used.
    bool upload(VKTextureCache &cache, TextureCacheEntry &entry, const SceGxmTexture &texture, SceGxmTextureBaseFormat base_format,
        const uint8_t *src, uint32_t width, uint32_t height, uint32_t stride_pixels, uint32_t layout_width, uint32_t layout_height);
    // records the conversion pass of the last upload() and leaves the RGBA8 image ready for sampling
    void finish_upload(VKTextureCache &cache, TextureCacheEntry &entry);

private:
    VKState &state;

    bool probed = false;
    bool pipelines_ready = false;
    bool pipelines_failed = false;
    int forced_route = -1;
    yuv::RouteCaps caps;
    // YCbCr route: the format features were verified for this kind (the objects are made on use)
    bool ycbcr_format_ok[yuv::kind_count] = { false, false, false };

    // shader route
    vk::ShaderModule planar_module;
    vk::DescriptorSetLayout planar_set_layout;
    vk::PipelineLayout planar_pipeline_layout;
    vk::Pipeline planar_pipeline;
    vk::ShaderModule ycbcr_module;

    std::map<uint32_t, std::unique_ptr<YcbcrObjects>> ycbcr_objects;
    std::map<uint32_t, bool> ycbcr_failed;

    // descriptor sets live one frame: a pool per frame slot, reset when the slot is reused
    std::array<vk::DescriptorPool, 3> pools;
    std::array<uint64_t, 3> pool_frame = { ~0ULL, ~0ULL, ~0ULL };

    bool ensure_pipelines();
    const YcbcrObjects *get_ycbcr_objects(yuv::Kind kind, uint8_t profile, uint8_t swizzle);
    vk::DescriptorSet allocate_set(vk::DescriptorSetLayout layout);
    void destroy_ycbcr_objects();
};

} // namespace renderer::vulkan
