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

#include <renderer/program_source.h>
#include <renderer/vulkan/functions.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <span>
#include <vector>

#include <gxm/functions.h>
#include <renderer/functions.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/uniform_slack.h>
#include <renderer/vulkan/vertex_stream.h>

#include <config/state.h>
#include <spdlog/fmt/bin_to_hex.h>

#include <util/log.h>
#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#endif

namespace renderer::vulkan {

// Some shaders index a uniform buffer beyond its declared span. Mirror a small,
// bounded amount of adjacent guest data only for buffers identified by analysis.
constexpr uint32_t uniform_slack_window = 16 * 1024;

static void copy_uniform_slack(VKContext &context, MemState &mem, const Address address, const uint32_t declared) {
#if ECV_ORACLE
    if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_NO_SLACK))
        return;
#endif
    VKState &state = context.state;
    const auto mapping = state.mapped_memories.lower_bound(address);
    if (mapping == state.mapped_memories.end() || address < mapping->first)
        return;

    const vkutil::Buffer *buffer = std::get_if<vkutil::Buffer>(&mapping->second.buffer_impl);
    const uint8_t *guest = Ptr<const uint8_t>(address).get(mem);
    if (!buffer || !buffer->mapped_data || !guest)
        return;

    const uint64_t mapping_offset = static_cast<uint64_t>(address) - mapping->first;
    if (mapping_offset > mapping->second.size)
        return;

    const auto copy_range = uniform_slack_copy_range(address, declared,
        mapping->second.size - mapping_offset, state.surface_cache.color_surface_limit(address), uniform_slack_window);
    if (!copy_range)
        return;

    uint8_t *mirror = reinterpret_cast<uint8_t *>(buffer->mapped_data) + mapping_offset;
    std::memcpy(mirror + copy_range->offset, guest + copy_range->offset, static_cast<size_t>(copy_range->size));
}

void set_uniform_buffer(VKContext &context, MemState &mem, const ShaderProgram *program, const bool vertex_shader, const int block_num, const int size, Ptr<uint8_t> data) {
    auto offset = program->uniform_buffer_data_offsets.at(block_num);
    if (offset == static_cast<std::uint32_t>(-1)) {
        return;
    }

    const uint32_t data_size_upload = std::min<uint32_t>(size, program->uniform_buffer_sizes.at(block_num) * 4);
#if ECV_ORACLE
    if (ecv::oracle::gpu::draws_enabled())
        ecv::oracle::gpu::ub_bind(vertex_shader, block_num, data.address(), std::min<uint32_t>(size, program->uniform_buffer_sizes.at(block_num) * 4));
    if (ecv::oracle::gpu::uniforms_enabled() && data.address())
        ecv::oracle::gpu::ub_data(vertex_shader, block_num, data.address(), std::min<uint32_t>(size, program->uniform_buffer_sizes.at(block_num) * 4), data.get(mem));
#endif
    if (context.state.features.enable_memory_mapping) {
        const bool aliases_surface = context.state.surface_cache.sync_surface_for_gpu_read(data.address(), data_size_upload);

        if (!aliases_surface && context.state.mapping_method == MappingMethod::DoubleBuffer) {
            TrappedBuffer *oracle_ub = context.state.buffer_trapping.access_buffer(data.address(), data_size_upload, mem, false, true);
#if ECV_ORACLE
            if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_UB_CHECK) && oracle_ub && oracle_ub->mapped_location && oracle_ub->size)
                ecv::oracle::gpu::mirror_check(vertex_shader ? "uv" : "uf", data.address(), std::min<uint32_t>(oracle_ub->size, data_size_upload),
                    oracle_ub->mapped_location, data.get(mem));
            if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_MIRROR_VERIFY) && oracle_ub && oracle_ub->size)
                ecv::oracle::gpu::mirror_note(vertex_shader ? "uv" : "uf", data.address(), std::min<uint32_t>(oracle_ub->size, data_size_upload), oracle_ub->mapped_location);
#else
            (void)oracle_ub;
#endif
            if (block_num >= 0 && block_num < 32
                && (program->dynamic_uniform_buffers & (1u << block_num)) && !context.state.has_shader_store)
                copy_uniform_slack(context, mem, data.address(), data_size_upload);
        }

        uint64_t buffer_address = context.state.get_matching_device_address(data.address());
#if ECV_ORACLE
        if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_UB_SNAPSHOT) && data.address()) {
            // debug A/B: give the GPU a private per-draw snapshot of the uniform data (+16 KiB slack)
            static vkutil::Buffer *oracle_ring = nullptr;
            static uint64_t oracle_ring_addr = 0;
            static uint32_t oracle_cursor = 0;
            constexpr uint32_t oracle_cap = 96u * 1024 * 1024;
            if (!oracle_ring) {
                oracle_ring = new vkutil::Buffer(oracle_cap + 64 * 1024);
                constexpr vma::AllocationCreateInfo oracle_alloc = {
                    .flags = vma::AllocationCreateFlagBits::eHostAccessRandom | vma::AllocationCreateFlagBits::eMapped,
                    .usage = vma::MemoryUsage::eAutoPreferHost,
                    .requiredFlags = vk::MemoryPropertyFlagBits::eHostCoherent,
                };
                oracle_ring->init_buffer(vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress, oracle_alloc);
                oracle_ring_addr = context.state.device.getBufferAddress(vk::BufferDeviceAddressInfo{ .buffer = oracle_ring->buffer });
            }
            auto oracle_map = context.state.mapped_memories.lower_bound(data.address());
            if (oracle_map != context.state.mapped_memories.end() && data.address() < oracle_map->first + oracle_map->second.size) {
                const uint32_t remaining = oracle_map->first + oracle_map->second.size - data.address();
                const uint32_t copy_size = std::min<uint32_t>(remaining, data_size_upload + 16 * 1024);
                if (oracle_cursor + copy_size > oracle_cap)
                    oracle_cursor = 0;
                std::memcpy(reinterpret_cast<uint8_t *>(oracle_ring->mapped_data) + oracle_cursor, data.get(mem), copy_size);
                buffer_address = oracle_ring_addr + oracle_cursor;
                oracle_cursor = (oracle_cursor + copy_size + 255u) & ~255u;
            }
        }
#endif

        if (vertex_shader) {
            context.curr_vert_ublock.set_buffer_address(block_num, buffer_address);
        } else {
            context.curr_frag_ublock.set_buffer_address(block_num, buffer_address);
        }
    } else {
        const uint32_t offset_start_upload = offset * 4;

        if (vertex_shader) {
            if (!context.vertex_uniform_storage_allocated) {
                // Allocate a region for it. Don't worry though, when the shader program is changed
                context.vertex_uniform_stream_ring_buffer.allocate(program->max_total_uniform_buffer_storage * 4);
                context.vertex_uniform_storage_allocated = true;
            }

            context.vertex_uniform_stream_ring_buffer.copy(context.prerender_cmd, data_size_upload, data.get(mem), offset_start_upload);
        } else {
            if (!context.fragment_uniform_storage_allocated) {
                // Allocate a region for it. Don't worry though, when the shader program is changed
                context.fragment_uniform_stream_ring_buffer.allocate(program->max_total_uniform_buffer_storage * 4);
                context.fragment_uniform_storage_allocated = true;
            }

            context.fragment_uniform_stream_ring_buffer.copy(context.prerender_cmd, data_size_upload, data.get(mem), offset_start_upload);
        }
    }
}

void mid_scene_flush(VKContext &context, const SceGxmNotification notification) {
    // two cases :
    // notification.addr is 0: this means that the mid scene flush must be used as a barrier in the renderpass
    // notification.addr is not 0: this means the app is waiting for this part to be finished to re-use the resources

    // Note: however, when testing, the barrier inside a pipeline does not work (or not entirely, depending on the GPU)
    // maybe because I'm writing using buffer device addresses, not sure...
    // so for the time being always restart the render pass
    // const bool restart_render_pass = notification.address.address() != 0;
    const bool restart_render_pass = true;

#if ECV_ORACLE
    ecv::oracle::gpu::rp_reason("mid_scene_flush");
#endif
    if (restart_render_pass && context.in_renderpass)
        context.stop_render_pass();

    // in case there is no notification, this will happen in the render pass
    {
        // shader stores through device addresses cannot be attributed to a tracked buffer: explicit global dependency
        vkutil::sync::Batch batch;
        batch.global(vkutil::sync::S::eVertexShader, vkutil::sync::A::eShaderWrite,
            vkutil::sync::S::eVertexInput, vkutil::sync::A::eVertexAttributeRead);
        batch.flush(context.render_cmd);
    }

    if (restart_render_pass) {
        SceGxmNotification empty_notification = { Ptr<uint32_t>(0), 0 };
        const bool submit = notification.address.address() != 0;
        context.stop_recording(notification, empty_notification, submit);
        context.start_recording();
        context.scene_timestamp++;
        context.scene_has_drawn = false;
    }
}

// when needed, how many descriptor of the given size we allocate for each frame at once
static constexpr uint32_t DESCRIPTOR_PACK_SIZE = 64;

static vk::DescriptorSet retrieve_descriptor(VKContext &context, bool is_vertex, uint16_t textures_count) {
    if (textures_count == 0)
        return context.empty_set;

    VKState &state = context.state;
    FrameDescriptor &frame_descriptor = is_vertex ? state.frame().vert_descriptors[textures_count - 1] : state.frame().frag_descriptors[textures_count - 1];
    if (frame_descriptor.descriptors_idx < frame_descriptor.sets.size())
        return frame_descriptor.sets[frame_descriptor.descriptors_idx++];

    // we have no more frame descriptor available, create a bunch of new one for this specific layout
    vk::DescriptorPoolSize pool_size{
        .type = vk::DescriptorType::eCombinedImageSampler,
        .descriptorCount = textures_count * DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING
    };

    vk::DescriptorPoolCreateInfo descriptor_pool_info{
        .maxSets = DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING
    };
    descriptor_pool_info.setPoolSizes(pool_size);

    vk::DescriptorPool descriptor_pool = state.device.createDescriptorPool(descriptor_pool_info);
    state.frame_descriptor_pools.push_back(descriptor_pool);

    // allocate all the descriptor sets
    const vk::DescriptorSetLayout set_layout = is_vertex ? state.pipeline_cache.vertex_textures_layout[textures_count] : state.pipeline_cache.fragment_textures_layout[textures_count];
    std::vector<vk::DescriptorSetLayout> layouts(DESCRIPTOR_PACK_SIZE * MAX_FRAMES_RENDERING, set_layout);
    vk::DescriptorSetAllocateInfo descr_set_info{
        .descriptorPool = descriptor_pool
    };
    descr_set_info.setSetLayouts(layouts);
    auto descriptor_sets = state.device.allocateDescriptorSets(descr_set_info);

    // distribute them among all frames
    for (int frame_idx = 0; frame_idx < MAX_FRAMES_RENDERING; frame_idx++) {
        FrameObject &frame_object = state.frames[frame_idx];
        FrameDescriptor &frame_descr = is_vertex ? frame_object.vert_descriptors[textures_count - 1] : frame_object.frag_descriptors[textures_count - 1];

        // insert DESCRIPTOR_PACK_SIZE in each frame descriptor
        auto descr_it = descriptor_sets.begin() + frame_idx * DESCRIPTOR_PACK_SIZE;
        frame_descr.sets.insert(frame_descr.sets.end(), descr_it, descr_it + DESCRIPTOR_PACK_SIZE);
    }

    return frame_descriptor.sets[frame_descriptor.descriptors_idx++];
}

static void draw_bind_descriptors(VKContext &context, MemState &mem) {
    VKState &state = context.state;

    std::array<vk::DescriptorSet, 4> descriptors;
    descriptors[0] = context.global_set;
    descriptors[1] = context.rendertarget_set;

    const uint16_t vertex_textures_count = context.record.vertex_program.get(mem)->renderer_data->texture_count;
    const uint16_t fragment_texture_count = context.record.fragment_program.get(mem)->renderer_data->texture_count;

    vk::PipelineLayout pipeline_layout = state.pipeline_cache.pipeline_layouts[vertex_textures_count][fragment_texture_count];

    // try to use last descriptor if it still matches
    bool need_vert_descr = (vertex_textures_count != context.last_vert_texture_count);
    bool need_frag_descr = (fragment_texture_count != context.last_frag_texture_count);

    context.last_vert_texture_count = vertex_textures_count;
    context.last_frag_texture_count = fragment_texture_count;

    {
        if (need_vert_descr) {
            context.last_vert_texture_descriptor = retrieve_descriptor(context, true, vertex_textures_count);
        }
        descriptors[2] = context.last_vert_texture_descriptor;

        if (need_frag_descr) {
            context.last_frag_texture_descriptor = retrieve_descriptor(context, false, fragment_texture_count);
        }
        descriptors[3] = context.last_frag_texture_descriptor;
    }

    // bind textures
    std::array<vk::WriteDescriptorSet, 16> write_descrs;
    // some default sampler in case a slot has never been set and we read a slot with higher idx
    vk::DescriptorImageInfo default_image_info{
        .sampler = context.state.default_image.sampler,
        .imageView = context.state.default_image.view,
        .imageLayout = vk::ImageLayout::eGeneral
    };

    // vertex
    if (need_vert_descr) {
        for (uint32_t i = 0; i < vertex_textures_count; i++) {
            write_descrs[i] = vk::WriteDescriptorSet{
                .dstSet = descriptors[2],
                .dstBinding = i,
                .dstArrayElement = 0,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            };
            write_descrs[i].setImageInfo(context.vertex_textures[i].sampler ? context.vertex_textures[i] : default_image_info);
        }
        state.device.updateDescriptorSets(vertex_textures_count, write_descrs.data(), 0, nullptr);
    }

    // fragment
    if (need_frag_descr) {
        for (uint32_t i = 0; i < fragment_texture_count; i++) {
            write_descrs[i] = vk::WriteDescriptorSet{
                .dstSet = descriptors[3],
                .dstBinding = i,
                .dstArrayElement = 0,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
            };
            write_descrs[i].setImageInfo(context.fragment_textures[i].sampler ? context.fragment_textures[i] : default_image_info);
        }
        state.device.updateDescriptorSets(fragment_texture_count, write_descrs.data(), 0, nullptr);
    }

    const uint32_t dynamic_offset_count = state.features.enable_memory_mapping ? 2U : 4U;
    const uint32_t dynamic_offsets[] = {
        // GXMRenderVertUniformBlock
        context.vertex_info_uniform_buffer.data_offset,
        // GXMRenderFragUniformBlock
        context.fragment_info_uniform_buffer.data_offset,
        // vertex ssbo
        context.vertex_uniform_stream_ring_buffer.data_offset,
        // fragment ssbo
        context.fragment_uniform_stream_ring_buffer.data_offset
    };

    context.render_cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pipeline_layout, 0,
        descriptors.size(), descriptors.data(), dynamic_offset_count, dynamic_offsets);
}

// vertex count is only used with double buffer mapping
static bool bind_vertex_streams(VKContext &context, MemState &mem, uint32_t instance_count, uint32_t max_index) {
    GxmRecordState &state = context.record;
    const SceGxmVertexProgram &vertex_program = *state.vertex_program.get(mem);
    VertexProgram *vkvert = vertex_program.renderer_data.get();

    int max_stream_idx = -1;
    std::array<std::vector<VertexAttributeRange>, SCE_GXM_MAX_VERTEX_STREAMS> stream_attribute_ranges;

    for (const SceGxmVertexAttribute &attribute : vertex_program.attributes) {
        if (!vkvert->attribute_infos.contains(attribute.regIndex))
            continue;
        max_stream_idx = std::max<int>(max_stream_idx, attribute.streamIndex);
        stream_attribute_ranges[attribute.streamIndex].push_back({
            attribute.offset,
            gxm::attribute_format_size(static_cast<SceGxmAttributeFormat>(attribute.format)) * attribute.componentCount });
    }
    max_stream_idx++;

    if (context.state.mapping_method == MappingMethod::DoubleBuffer) {
        for (int i = 0; i < max_stream_idx; i++)
            state.vertex_streams[i].size = 0;

        // same as in SceGxm.cpp
        for (const SceGxmVertexAttribute &attribute : vertex_program.attributes) {
            const SceGxmAttributeFormat attribute_format = static_cast<SceGxmAttributeFormat>(attribute.format);
            const size_t attribute_size = gxm::attribute_format_size(attribute_format) * attribute.componentCount;
            const SceGxmVertexStream &stream = vertex_program.streams[attribute.streamIndex];
            const SceGxmIndexSource index_source = static_cast<SceGxmIndexSource>(stream.indexSource);
            const size_t data_passed_length = gxm::is_stream_instancing(index_source) ? ((instance_count - 1) * stream.stride) : (max_index * stream.stride);
            const size_t data_length = attribute.offset + data_passed_length + attribute_size;
            state.vertex_streams[attribute.streamIndex].size = std::max<size_t>(state.vertex_streams[attribute.streamIndex].size, data_length);
        }

        for (int i = 0; i < max_stream_idx; i++) {
            if (state.vertex_streams[i].data) {
                // on the PS Vita, shader stores are used most of the time to write to a vertex buffer
                TrappedBuffer *oracle_tb = context.state.buffer_trapping.access_buffer(state.vertex_streams[i].data.address(), static_cast<uint32_t>(state.vertex_streams[i].size), mem, context.state.has_shader_store);
#if ECV_ORACLE
                if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_UB_CHECK) && oracle_tb && oracle_tb->mapped_location && oracle_tb->size)
                    ecv::oracle::gpu::mirror_check("vb", state.vertex_streams[i].data.address(), std::min<uint32_t>(oracle_tb->size, static_cast<uint32_t>(state.vertex_streams[i].size)),
                        oracle_tb->mapped_location, state.vertex_streams[i].data.cast<const uint8_t>().get(mem));
                if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_MIRROR_VERIFY) && oracle_tb && oracle_tb->size)
                    ecv::oracle::gpu::mirror_note("vb", state.vertex_streams[i].data.address(), std::min<uint32_t>(oracle_tb->size, static_cast<uint32_t>(state.vertex_streams[i].size)), oracle_tb->mapped_location);
#else
                (void)oracle_tb;
#endif
            }
        }
    }

    if (max_stream_idx == 0)
        return true;

    bool mapped = context.state.features.enable_memory_mapping;
#if ECV_ORACLE
    if (mapped && ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_VB_COPY))
        mapped = false;
#endif
    std::array<bool, SCE_GXM_MAX_VERTEX_STREAMS> restride_streams{};
    std::array<size_t, SCE_GXM_MAX_VERTEX_STREAMS> batch_stream_sizes{};
    size_t batch_stream_count = 0;
    for (int i = 0; i < max_stream_idx; i++) {
        if (!state.vertex_streams[i].data || mapped)
            continue;

        const uint32_t stride = vertex_program.streams[i].stride;
        restride_streams[i] = should_restride_vertex_stream(mapped, stride, stream_attribute_ranges[i]);
        const size_t upload_size = restride_streams[i]
            ? restrided_vertex_stream_size(state.vertex_streams[i].size, stride)
            : state.vertex_streams[i].size;
        batch_stream_sizes[batch_stream_count++] = upload_size;
    }

    if (!mapped && !vertex_stream_batch_fits(
                       context.vertex_stream_ring_buffer.cursor_bytes(),
                       context.vertex_stream_ring_buffer.capacity_bytes(),
                       context.vertex_stream_ring_buffer.alignment,
                       std::span<const size_t>(batch_stream_sizes.data(), batch_stream_count))) {
        LOG_WARN("Skipping draw: vertex stream allocations exceed available ring capacity");
        return false;
    }

    for (int i = 0; i < max_stream_idx; i++) {
        if (state.vertex_streams[i].data) {
            const uint32_t stride = vertex_program.streams[i].stride;
            if (mapped) {
                auto [buffer, offset] = context.state.get_matching_mapping(state.vertex_streams[i].data.cast<void>());

                context.vertex_stream_offsets[i] = offset;
                context.vertex_stream_buffers[i] = buffer;
            } else {
                const uint8_t *stream = state.vertex_streams[i].data.get(mem);
                uint32_t stream_size = state.vertex_streams[i].size;
                std::vector<uint8_t> repacked;
                if (restride_streams[i]) {
                    repacked = restride_vertex_stream(
                        std::span<const uint8_t>(stream, stream_size), stride);
                    stream = repacked.data();
                    stream_size = static_cast<uint32_t>(repacked.size());
                }
                context.vertex_stream_ring_buffer.allocate(context.prerender_cmd, stream_size, stream);
                context.vertex_stream_offsets[i] = context.vertex_stream_ring_buffer.data_offset;
                context.vertex_stream_buffers[i] = context.vertex_stream_ring_buffer.handle();
            }

            state.vertex_streams[i].data = nullptr;
            state.vertex_streams[i].size = 0;
        }
    }

    context.render_cmd.bindVertexBuffers(0, max_stream_idx, context.vertex_stream_buffers, context.vertex_stream_offsets);
    return true;
}

void draw(VKContext &context, SceGxmPrimitiveType type, SceGxmIndexFormat format,
    Ptr<void> indices, size_t count, uint32_t instance_count, MemState &mem, const Config &config) {
#if ECV_ORACLE
    ecv::oracle::gpu::dbg_stall_once(ecv::oracle::gpu::DBG_STALL_DRAW, "draw start");
#endif
    // A save-state load can leave the record pointing at a program whose guest
    // memory was freed: skip such draws instead of dereferencing garbage.
    const SceGxmFragmentProgram *frag_program = context.record.fragment_program.get(mem);
    const SceGxmVertexProgram *vert_program = context.record.vertex_program.get(mem);
    if (!frag_program || !frag_program->renderer_data || !vert_program || !vert_program->renderer_data) {
        LOG_CRITICAL_ONCE("Dropped draw with invalid shader programs");
        return;
    }

    // the mask bit is not emulated here, so a mask-update program would just paint writing_mask over the whole target (gl/draw.cpp skips these too)
    if (!context.state.features.use_mask_bit && frag_program->is_maskupdate)
        return;

    // DOA5 black clothes fix:
    {
        const SceGxmVertexProgram *gxm_vp = context.record.vertex_program.get(mem);
        if (gxm_vp && gxm_vp->renderer_data) {
            VertexProgram *vp_data = gxm_vp->renderer_data.get();
            if (vp_data->varying_location_mask == 0xFFFFFFFFu) {
                uint32_t written_mask = 0;
                const SceGxmProgram *vp_gxp = renderer::get_program(*gxm_vp, mem);
                if (vp_gxp) {
                    const SceGxmVertexProgramOutputs vo_mask = gxp::get_vertex_outputs(*vp_gxp);
                    static constexpr struct {
                        SceGxmVertexProgramOutputs vo;
                        int location;
                    } vo_locations[] = {
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_POSITION, 0 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_COLOR0, 1 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_COLOR1, 2 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_FOG, 3 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD0, 4 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD1, 5 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD2, 6 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD3, 7 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD4, 8 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD5, 9 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD6, 10 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD7, 11 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD8, 12 }, { SCE_GXM_VERTEX_PROGRAM_OUTPUT_TEXCOORD9, 13 },
                        { SCE_GXM_VERTEX_PROGRAM_OUTPUT_PSIZE, 14 }
                    };
                    for (const auto &e : vo_locations)
                        if (vo_mask & e.vo)
                            written_mask |= 1u << e.location;
                }
                vp_data->varying_location_mask = written_mask;
            }
            context.curr_frag_ublock.set_iterator_written_mask(vp_data->varying_location_mask);
        }
    }

    void *indices_ptr = indices.get(mem);

    if (context.record.front_depth_write_mode == SCE_GXM_DEPTH_WRITE_ENABLED
        || (context.record.two_sided == SCE_GXM_TWO_SIDED_ENABLED && context.record.back_depth_write_mode == SCE_GXM_DEPTH_WRITE_ENABLED))
        context.scene_wrote_depth = true;

    // record queued casted-texture copies BEFORE marking the draw, so the placement decision
    // sees whether this scene had drawn prior to this draw
    {
        const uint16_t vert_texture_count = context.record.vertex_program.get(mem)->renderer_data->texture_count;
        const uint16_t frag_texture_count = context.record.fragment_program.get(mem)->renderer_data->texture_count;
        context.state.surface_cache.perform_pending_casts(context, vert_texture_count, frag_texture_count);
    }
    {
        // scissor x viewport bounds what this draw can rasterize
        const int32_t sx0 = context.scissor.offset.x, sy0 = context.scissor.offset.y;
        const int32_t sx1 = sx0 + static_cast<int32_t>(context.scissor.extent.width), sy1 = sy0 + static_cast<int32_t>(context.scissor.extent.height);
        const int32_t vx0 = static_cast<int32_t>(std::floor(std::min(context.viewport.x, context.viewport.x + context.viewport.width)));
        const int32_t vy0 = static_cast<int32_t>(std::floor(std::min(context.viewport.y, context.viewport.y + context.viewport.height)));
        const int32_t vx1 = static_cast<int32_t>(std::ceil(std::max(context.viewport.x, context.viewport.x + context.viewport.width)));
        const int32_t vy1 = static_cast<int32_t>(std::ceil(std::max(context.viewport.y, context.viewport.y + context.viewport.height)));
        const int32_t x0 = std::max(sx0, vx0), y0 = std::max(sy0, vy0);
        const int32_t x1 = std::min(sx1, vx1), y1 = std::min(sy1, vy1);
        if (x1 > x0 && y1 > y0) {
            context.draw_rect_x0 = std::min(context.draw_rect_x0, x0);
            context.draw_rect_y0 = std::min(context.draw_rect_y0, y0);
            context.draw_rect_x1 = std::max(context.draw_rect_x1, x1);
            context.draw_rect_y1 = std::max(context.draw_rect_y1, y1);
            context.inst_rect.add({ x0, y0, x1, y1 });
        }
    }
    context.scene_has_drawn = true;

    context.check_for_macroblock_change(true);

    // B140 test hook (debug builds): end the multisampled pass instance before its 4th draw so the restart path runs
    if (context.in_renderpass && msaa::debug_force_restart(context)) {
        const auto scene_dyn_pass = context.dyn_pass;
        context.dyn_pass.depth_load = context.dyn_pass.stencil_load = true;
        context.stop_render_pass();
        context.start_render_pass();
        context.dyn_pass = scene_dyn_pass;
    }

    if (!context.in_renderpass)
        context.start_render_pass();

    // when the render pass restarts within one scene (shader interlock or slow macroblock
    // multi-pass, or a mid-scene interruption such as a casted-texture copy), the re-begin
    // must load the depth-stencil, never re-run the scene-begin clear
    if (context.is_first_scene_draw && (context.state.features.support_shader_interlock || context.ignore_macroblock || !context.render_target->has_macroblock_sync)) {
        // the following instances load the depth and stencil
        context.dyn_pass.depth_load = context.dyn_pass.stencil_load = true;
#if ECV_ORACLE
        if (ecv::oracle::gpu::enabled())
            ecv::oracle::gpu::rp_loads(reinterpret_cast<uint64_t>(static_cast<VkRenderPass>(context.current_render_pass)), "first_draw_upgrade");
#endif
        context.is_first_scene_draw = false;
    }

    const SceGxmFragmentProgram &gxm_fragment_program = *context.record.fragment_program.get(mem);
    const SceGxmProgram &fragment_program_gxp = *renderer::get_program(gxm_fragment_program, mem);
#if ECV_ORACLE
    if (fragment_program_gxp.is_frag_color_used()) {
        ecv::oracle::gpu::fbfetch_note(vert_program->renderer_data->hash.data(), frag_program->renderer_data->hash.data());
        if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_SKIP_FBFETCH))
            return;
    }
#endif
    if (context.shell_fetch_snapshot && !context.ms_scene.active && fragment_program_gxp.is_frag_color_used()) {
#if ECV_ORACLE
    ecv::oracle::gpu::rp_reason("shell_fetch_snapshot");
#endif
        // PAF's glow must sample completed pixels, not a simultaneous write to
        // the same attachment on a non-coherent mobile framebuffer-fetch path.
        // The instance that follows the copy continues the scene: it loads the depth-stencil (the scene-begin clear must not run again; a
        // macroblock scene keeps its clear for the next macroblock, so the ops are restored afterwards).
        const auto scene_dyn_pass = context.dyn_pass;
        context.dyn_pass.depth_load = context.dyn_pass.stencil_load = true;
        context.stop_render_pass();
        auto &source = *context.current_color_base_image;
        auto &snapshot = *context.shell_fetch_snapshot;
        source.transition_to(context.render_cmd, vkutil::ImageLayout::TransferSrc);
        snapshot.transition_to(context.render_cmd, vkutil::ImageLayout::TransferDst);
        vk::ImageCopy region{
            .srcSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
            .dstSubresource = { vk::ImageAspectFlagBits::eColor, 0, 0, 1 },
            .extent = { snapshot.width, snapshot.height, 1 }
        };
        context.render_cmd.copyImage(source.image, vk::ImageLayout::eTransferSrcOptimal,
            snapshot.image, vk::ImageLayout::eTransferDstOptimal, region);
        {
            // back to the attachment layout; the snapshot becomes the pass' input attachment
            vkutil::sync::Batch batch;
            source.use(batch, vkutil::sync::S::eColorAttachmentOutput, vkutil::sync::A::eColorAttachmentRead | vkutil::sync::A::eColorAttachmentWrite,
                vk::ImageLayout::eGeneral);
            snapshot.use(batch, vkutil::sync::S::eFragmentShader, vkutil::sync::A::eInputAttachmentRead | vkutil::sync::A::eShaderRead,
                vk::ImageLayout::eGeneral);
            batch.flush(context.render_cmd);
            source.layout = vkutil::ImageLayout::ColorAttachmentReadWrite;
            snapshot.layout = vkutil::ImageLayout::StorageImage;
        }
        context.start_render_pass();
        context.dyn_pass = scene_dyn_pass;
    }

    if (context.state.features.preserve_f16_nan_as_u16) {
        const VKFragmentProgram &vk_frag_program = *reinterpret_cast<VKFragmentProgram *>(gxm_fragment_program.renderer_data.get());
        if (vk_frag_program.blending.blendEnable)
            context.state.surface_cache.mark_current_surface_blended();
    }
    if (context.state.features.direct_fragcolor && fragment_program_gxp.is_frag_color_used() && context.state.features.split_fbfetch_between_draws
        && context.in_renderpass && context.prev_draw_fbfetch) {
        // Two consecutive fetching draws inside ONE render pass instance read wrong tile data on some drivers (Adreno 840 stock, B025; proven
        // at start-up by gpu_caps_selftest.h, never guessed from a vendor name): restart the instance (attachments stored, then loaded again).
        // The restarted instance must load the depth-stencil (the scene-begin clear must not run twice) without changing the scene's render
        // pass for the instances that follow (a macroblock change still starts from the scene-begin clear).
        const auto scene_dyn_pass = context.dyn_pass;
        context.dyn_pass.depth_load = context.dyn_pass.stencil_load = true;
        context.stop_render_pass();
        context.start_render_pass();
        context.dyn_pass = scene_dyn_pass;
    }
    context.prev_draw_fbfetch = context.state.features.direct_fragcolor && fragment_program_gxp.is_frag_color_used();
#if ECV_ORACLE
    if (context.state.features.direct_fragcolor && fragment_program_gxp.is_frag_color_used()
        && ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_FB_RESTART) && context.in_renderpass) {
        // debug A/B: every framebuffer-fetch draw starts a fresh render pass instance (attachment LOADed)
        context.stop_render_pass();
        context.start_render_pass();
    }
#endif
    if (context.state.features.direct_fragcolor && fragment_program_gxp.is_frag_color_used()) {
        // the fragment shader is using programmable blending with a subpass input
        {
            // render pass self-dependency (dependencies[3] in pipeline_cache.cpp): pixels written by earlier draws -> input attachment read.
            // Recorded inside the pass, so it is an explicit by-region barrier and does not change the tracked image state.
            namespace sync = vkutil::sync;
            const sync::Barrier self_dep{ sync::S::eColorAttachmentOutput, sync::S::eFragmentShader, sync::A::eColorAttachmentWrite,
                sync::A::eInputAttachmentRead, vk::ImageLayout::eGeneral, vk::ImageLayout::eGeneral, true };
            sync::Batch batch;
            batch.set_by_region(true);
            const bool ms = context.ms_scene.active;
            batch.explicit_image(ms ? context.ms_scene.color_ms->image : context.current_color_base_image->image, vkutil::color_subresource_range, self_dep);
            if (ms ? context.ms_scene.raw_ms != nullptr : context.current_color_raw_image != nullptr)
                batch.explicit_image(ms ? context.ms_scene.raw_ms->image : context.current_color_raw_image->image, vkutil::color_subresource_range, self_dep);
#if ECV_ORACLE
            if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_FB_NOBARRIER)) {
                // debug A/B: no barrier at all (batch dropped)
            } else {
                if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_FB_NOREGION))
                    batch.set_by_region(false);
                batch.flush(context.render_cmd);
            }
#else
            batch.flush(context.render_cmd);
#endif
        }
    } else if (context.state.features.support_shader_interlock
        && fragment_program_gxp.is_frag_color_used() != context.last_draw_was_framebuffer_fetch) {
        // restart the render pass to act as a barrier, in the other pass variant (the restarted instance loads the depth stencil)
        const auto scene_dyn_pass = context.dyn_pass;
        context.dyn_pass.depth_load = context.dyn_pass.stencil_load = true;
        context.stop_render_pass();
        context.interlock_pass_active = fragment_program_gxp.is_frag_color_used();
        context.start_render_pass(false);
        context.dyn_pass = scene_dyn_pass;
        context.last_draw_was_framebuffer_fetch = fragment_program_gxp.is_frag_color_used();
    }

    if (context.current_visibility_buffer != nullptr && context.current_query_idx != -1 && !context.is_in_query) {
        if (context.current_visibility_buffer->queries_used[context.current_query_idx]) {
            LOG_WARN_ONCE("Visibility buffer entry is used more than once in a scene");
            // still let this happen, this is a validation error but I think most GPUs should be fine with it
        }
        context.current_visibility_buffer->queries_used[context.current_query_idx] = true;

        context.visibility_max_used_idx = std::max(context.visibility_max_used_idx, context.current_query_idx);

        const vk::QueryControlFlags control_flags = (context.is_query_op_increment && context.state.physical_device_features.occlusionQueryPrecise) ? vk::QueryControlFlagBits::ePrecise : vk::QueryControlFlags();
        context.render_cmd.beginQuery(context.current_visibility_buffer->query_pool, context.current_query_idx, control_flags);
        context.is_in_query = true;
        context.instance_queries++;
    }

    // do we need to check for a pipeline change?
    if (context.refresh_pipeline || type != context.last_primitive) {
        context.refresh_pipeline = false;
        context.last_primitive = type;

        // We don't want to defer cases where we draw a whole quad over the screen as these draws could be necessary
        // to be able to see anything
        bool can_be_whole_quad = instance_count == 1 && count <= 6;

        // The Vita uses the front or the back depth state of the face a primitive hits. Vulkan can not do that per face (stencil
        // aside): if the guest gives the faces different state, draw once per facing with the other facing culled.
        const GxmRecordState &rec = context.record;
        const dsmodel::FaceState front_face{ rec.front_depth_func, rec.front_depth_write_mode, rec.front_polygon_mode,
            rec.front_side_fragment_program_mode, dsmodel::DepthBias{ rec.depth_bias_slope, rec.depth_bias_unit } };
        const dsmodel::FaceState back_face{ rec.back_depth_func, rec.back_depth_write_mode, rec.back_polygon_mode,
            rec.back_side_fragment_program_mode, context.depth_bias.back };
        const dsmodel::FacePlan plan = dsmodel::plan_faces(rec.two_sided == SCE_GXM_TWO_SIDED_ENABLED, rec.cull_mode, front_face, back_face);
        const dsmodel::PassList passes = dsmodel::passes_for(plan);
        if (plan == dsmodel::FacePlan::Split)
            LOG_INFO_ONCE("Two-sided draw with different front/back depth state: drawn once per facing");

        vk::Pipeline new_pipeline = context.state.pipeline_cache.retrieve_pipeline(context, type, !can_be_whole_quad, mem, passes.variant[0]);
        vk::Pipeline new_split = nullptr;
        if (passes.count == 2 && new_pipeline != nullptr) {
            new_split = context.state.pipeline_cache.retrieve_pipeline(context, type, !can_be_whole_quad, mem, passes.variant[1]);
            // both facings must be ready, a half drawn object would be worse than waiting like any other pending pipeline
            if (new_split == nullptr)
                new_pipeline = nullptr;
        }
        context.face_plan = plan;
        context.draw_variant[0] = passes.variant[0];
        context.draw_variant[1] = passes.variant[1];
        context.split_pipeline = new_split;

        if (new_pipeline != context.current_pipeline) {
            context.current_pipeline = new_pipeline;

            if (new_pipeline != nullptr)
                context.render_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, context.current_pipeline);
        }
        // Still compiling: look it up again on the next draw. Clearing the flag here kept the
        // pipeline null (draws skipped) until some unrelated state change, even long after the
        // asynchronous compile had finished.
        if (new_pipeline == nullptr)
            context.refresh_pipeline = true;
    }

    // can happen with asynchronous pipeline compilation
    if (context.current_pipeline == nullptr) {
#if ECV_ORACLE
        if (ecv::oracle::gpu::enabled())
            ecv::oracle::gpu::pipe_skip(vert_program->renderer_data->hash.data(), frag_program->renderer_data->hash.data());
#endif
        return;
    }
#if ECV_ORACLE
    if (ecv::oracle::gpu::dbg_skip_draw(vert_program->renderer_data->hash.data(), frag_program->renderer_data->hash.data()))
        return;
    if (ecv::oracle::gpu::draws_enabled()) {
        for (int i = 0; i < SCE_GXM_MAX_VERTEX_STREAMS; i++)
            if (context.record.vertex_streams[i].data)
                ecv::oracle::gpu::vb_note(i, context.record.vertex_streams[i].data.address(), static_cast<uint32_t>(context.record.vertex_streams[i].size));
        ecv::oracle::gpu::ib_note(indices.address(), static_cast<uint32_t>(count), format == SCE_GXM_INDEX_FORMAT_U16 ? 2u : 4u);
        const GxmRecordState &rec = context.record;
        ecv::oracle::gpu::draw_state(static_cast<uint32_t>(rec.front_depth_func), static_cast<uint32_t>(rec.front_depth_write_mode),
            static_cast<uint32_t>(rec.front_stencil_state_op.func), static_cast<uint32_t>(rec.front_stencil_state_op.stencil_fail),
            static_cast<uint32_t>(rec.front_stencil_state_op.depth_fail), static_cast<uint32_t>(rec.front_stencil_state_op.depth_pass),
            rec.front_stencil_state_values.ref, rec.front_stencil_state_values.compare_mask, rec.front_stencil_state_values.write_mask,
            static_cast<uint32_t>(rec.cull_mode), context.scissor.offset.x, context.scissor.offset.y, context.scissor.extent.width,
            context.scissor.extent.height);
    }
    if (ecv::oracle::gpu::draws_enabled())
        ecv::oracle::gpu::draw(vert_program->renderer_data->hash.data(), frag_program->renderer_data->hash.data(), static_cast<uint32_t>(count), instance_count);
#endif

    if (config.log_active_shaders) {
        LOG_DEBUG("\nVertex  : {}\nFragment: {}",
            hex_string(context.record.vertex_program.get(mem)->renderer_data->hash),
            hex_string(context.record.fragment_program.get(mem)->renderer_data->hash));
        LOG_DEBUG("Vertex default uniform buffer: {}\n", spdlog::to_hex(context.ubo_data[0], 16));
        LOG_DEBUG("Fragment default uniform buffer: {}\n", spdlog::to_hex(context.ubo_data[SCE_GXM_REAL_MAX_UNIFORM_BUFFER + 1], 16));
    }

    const bool use_memory_mapping = context.state.features.enable_memory_mapping;

    // update uniforms if needed
    // first update the buffer and texture count
    auto &vert_render_data = context.record.vertex_program.get(mem)->renderer_data;
    auto &frag_render_data = context.record.fragment_program.get(mem)->renderer_data;

    if (use_memory_mapping) {
        context.curr_vert_ublock.set_buffer_count(vert_render_data->buffer_count);
        context.curr_frag_ublock.set_buffer_count(frag_render_data->buffer_count);
    }

    if (context.state.features.use_texture_viewport) {
        context.curr_vert_ublock.set_texture_count(vert_render_data->texture_count);
        context.curr_frag_ublock.set_texture_count(frag_render_data->texture_count);
    }

    auto &vert_ublock = context.curr_vert_ublock.base_block;
    vert_ublock.viewport_flip = context.record.viewport_flip;
    vert_ublock.viewport_flag = (context.record.viewport_flat) ? 0.0f : 1.0f;
    vert_ublock.z_offset = context.record.z_offset;
    vert_ublock.z_scale = context.record.z_scale;
    vert_ublock.screen_width = context.render_target->width / context.state.res_multiplier;
    vert_ublock.screen_height = context.render_target->height / context.state.res_multiplier;

    if (context.curr_vert_ublock.changed || memcmp(&context.prev_vert_ublock, &vert_ublock, sizeof(vert_ublock)) != 0) {
        // TODO: this intermediate step can be avoided
        context.curr_vert_ublock.copy_to(context.shader_info_temp);
        context.vertex_info_uniform_buffer.allocate(context.prerender_cmd, context.curr_vert_ublock.get_size(), context.shader_info_temp);
        memcpy(&context.prev_vert_ublock, &vert_ublock, sizeof(vert_ublock));
    }

    auto &frag_ublock = context.curr_frag_ublock.base_block;
    frag_ublock.writing_mask = context.record.writing_mask;
    frag_ublock.res_multiplier = context.state.res_multiplier;
    frag_ublock.use_raw_image = (context.state.features.preserve_f16_nan_as_u16
                                    && context.record.color_base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16
                                    && context.state.surface_cache.current_surface_raw_is_valid())
        ? 1.0f
        : 0.0f;
#if ECV_ORACLE
    if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_NO_RAW))
        frag_ublock.use_raw_image = 0.0f;
    if (renderer::get_program(*context.record.fragment_program.get(mem), mem)->is_frag_color_used())
        ecv::oracle::gpu::fbfetch_state(frag_program->renderer_data->hash.data(), frag_ublock.use_raw_image,
            reinterpret_cast<VKFragmentProgram *>(context.record.fragment_program.get(mem)->renderer_data.get())->blending.blendEnable,
            context.current_color_raw_view != nullptr);
#endif
    const bool has_msaa = context.render_target->multisample_mode;
    const bool has_downscale = context.record.color_surface.downscale;
    if (has_msaa && !has_downscale)
        frag_ublock.res_multiplier *= 2;
    else if (!has_msaa && has_downscale)
        frag_ublock.res_multiplier /= 2;

    // Cast-sampler UV re-anchor inputs
    frag_ublock.cast_sampler_mask = static_cast<float>(context.curr_frag_ublock.cast_sampler_bits);
    frag_ublock.cast_phase_mask = static_cast<float>(context.curr_frag_ublock.cast_phase_bits);
    frag_ublock.raw_cast_mask = static_cast<float>(context.curr_frag_ublock.raw_cast_bits);
    frag_ublock.inv_frag_width = 1.0f / static_cast<float>(context.render_target->width);
    frag_ublock.inv_frag_height = 1.0f / static_cast<float>(context.render_target->height);
    if (context.curr_frag_ublock.changed || memcmp(&context.prev_frag_ublock, &frag_ublock, sizeof(frag_ublock)) != 0) {
        // TODO: this intermediate step can be avoided
        context.curr_frag_ublock.copy_to(context.shader_info_temp);
        context.fragment_info_uniform_buffer.allocate(context.prerender_cmd, context.curr_frag_ublock.get_size(), context.shader_info_temp);
        memcpy(&context.prev_frag_ublock, &frag_ublock, sizeof(frag_ublock));
    }

    // create, update and bind descriptors (uniforms and textures)
    draw_bind_descriptors(context, mem);

    // Upload index data.
    vk::IndexType index_type = (format == SCE_GXM_INDEX_FORMAT_U16) ? vk::IndexType::eUint16 : vk::IndexType::eUint32;
    const size_t index_size = (format == SCE_GXM_INDEX_FORMAT_U16) ? 2 : 4;

    uint32_t max_index = 0;
    if (use_memory_mapping) {
        auto [buffer, offset] = context.state.get_matching_mapping(indices);
        if (context.state.mapping_method == MappingMethod::DoubleBuffer) {
            TrappedBuffer *trapped_buffer = context.state.buffer_trapping.access_buffer(indices.address(), count * index_size, mem);
            if (trapped_buffer->extra == ~0) {
                // store the max element in extra
                if (format == SCE_GXM_INDEX_FORMAT_U16) {
                    uint16_t *data = indices.cast<uint16_t>().get(mem);
                    trapped_buffer->extra = *std::max_element(&data[0], &data[count]);
                } else {
                    uint32_t *data = indices.cast<uint32_t>().get(mem);
                    trapped_buffer->extra = *std::max_element(&data[0], &data[count]);
                }
            }
            max_index = trapped_buffer->extra;
#if ECV_ORACLE
            if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_UB_CHECK) && trapped_buffer->mapped_location && trapped_buffer->size)
                ecv::oracle::gpu::mirror_check("ib", indices.address(), std::min<uint32_t>(trapped_buffer->size, count * index_size),
                    trapped_buffer->mapped_location, indices.cast<const uint8_t>().get(mem));
            if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_MIRROR_VERIFY) && trapped_buffer->size)
                ecv::oracle::gpu::mirror_note("ib", indices.address(), std::min<uint32_t>(trapped_buffer->size, count * index_size), trapped_buffer->mapped_location);
#endif
        }
#if ECV_ORACLE
        if (ecv::oracle::gpu::dbg(ecv::oracle::gpu::DBG_IB_COPY)) {
            context.index_stream_ring_buffer.allocate(context.prerender_cmd, index_size * count, indices_ptr);
            context.render_cmd.bindIndexBuffer(context.index_stream_ring_buffer.handle(), context.index_stream_ring_buffer.data_offset, index_type);
        } else
#endif
        context.render_cmd.bindIndexBuffer(buffer, offset, index_type);
    } else {
        const size_t index_buffer_size = index_size * count;
        context.index_stream_ring_buffer.allocate(context.prerender_cmd, index_buffer_size, indices_ptr);
        context.render_cmd.bindIndexBuffer(context.index_stream_ring_buffer.handle(), context.index_stream_ring_buffer.data_offset, index_type);
    }

    // bind the vertex streams
    if (!bind_vertex_streams(context, mem, instance_count, max_index)) {
        context.vertex_uniform_storage_allocated = false;
        context.fragment_uniform_storage_allocated = false;
        return;
    }

    apply_depth_bias(context, context.face_plan == dsmodel::FacePlan::Back);
    context.instance_draws++;
    const auto set_cull = [&](dsmodel::PassVariant variant) {
        if (!context.state.use_extended_dynamic_state)
            return;
        const vk::CullModeFlags mode = PipelineCache::draw_cull_mode(context.record.cull_mode, variant);
        if (!context.applied_cull_valid || context.applied_cull != mode) {
            context.render_cmd.setCullModeEXT(mode);
            context.applied_cull = mode;
            context.applied_cull_valid = true;
        }
    };
    set_cull(context.draw_variant[0]);
    context.render_cmd.drawIndexed(count, instance_count, 0, 0, 0);

    if (context.split_pipeline != nullptr) {
        // second facing: its own pipeline and bias, then bind the first pipeline back (the draw loop assumes it is bound)
        apply_depth_bias(context, true);
        context.render_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, context.split_pipeline);
        set_cull(context.draw_variant[1]);
        context.render_cmd.drawIndexed(count, instance_count, 0, 0, 0);
        context.render_cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, context.current_pipeline);
    }

    context.vertex_uniform_storage_allocated = false;
    context.fragment_uniform_storage_allocated = false;
}

} // namespace renderer::vulkan
