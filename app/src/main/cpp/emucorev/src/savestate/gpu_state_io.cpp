#include <emucorev/savestate/gpu_state_io.h>
#include <emucorev/savestate/scoped_gpu_context.h>
#include <emucorev/savestate/physical_gpu_contexts.h>
#include <emucorev/savestate/archive.h>
#include <emucorev/savestate/memory_image.h>
#include <emucorev/savestate/state_io.h>
#include <emucorev/savestate/gxm_state_io.h>
#include <emuenv/state.h>
#include <gxm/savestate.h>
#include <mem/ptr.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/frame_runtime_reset.h>
#include <renderer/vulkan/snapshot_request_barrier.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <exception>
#include <map>
#include <set>

namespace emucorev::savestate {
namespace {
constexpr uint32_t page_size = 4096;
constexpr uint32_t max_objects = 4096;

vkutil::ImageSnapshotDevice image_device(renderer::vulkan::VKState &state) {
    return { state.physical_device, state.device, state.general_queue, state.general_command_pool };
}

void write_bytes(BufferWriter &writer, const std::vector<uint8_t> &bytes) {
    writer.u32(static_cast<uint32_t>(bytes.size()));
    writer.append(bytes.data(), bytes.size());
}
bool read_bytes(BufferReader &reader, std::vector<uint8_t> &bytes) {
    const auto size = reader.u32();
    if (!reader.ok() || size > reader.remaining() || size > kMaxBufferedSectionSize)
        return false;
    bytes.resize(size);
    return reader.bytes(bytes.data(), size);
}
void write_image(BufferWriter &writer, const vkutil::ImageSnapshot &image) {
    writer.u32(image.width);
    writer.u32(image.height);
    writer.u32(static_cast<uint32_t>(image.format));
    writer.u32(static_cast<uint32_t>(image.layout));
    write_bytes(writer, image.bytes);
}
bool read_image(BufferReader &reader, vkutil::ImageSnapshot &image, std::string &error) {
    image.width = reader.u32();
    image.height = reader.u32();
    image.format = static_cast<vk::Format>(reader.u32());
    image.layout = static_cast<vkutil::ImageLayout>(reader.u32());
    return read_bytes(reader, image.bytes) && vkutil::validate_image_snapshot(image, error);
}
bool count(BufferReader &reader, uint32_t &value, size_t minimum) {
    value = reader.u32();
    return reader.ok() && value <= max_objects && value <= reader.remaining() / minimum;
}
void write_context(BufferWriter &writer, const renderer::vulkan::VKContextSnapshot &context) {
    writer.u32(context.context_address);
    writer.u32(context.render_target_address);
    writer.boolean(context.recording_open);
    writer.boolean(context.scene_wrote_depth);
    writer.boolean(context.scene_has_drawn);
    writer.boolean(context.scene_macroblock_flushed);
    writer.boolean(context.is_first_scene_draw);
    writer.value(context.surface_downscale);
    writer.value(context.viewport_x);
    writer.value(context.viewport_y);
    writer.value(context.viewport_width);
    writer.value(context.viewport_height);
    writer.value(context.viewport_min_depth);
    writer.value(context.viewport_max_depth);
    writer.i32(context.scissor_x);
    writer.i32(context.scissor_y);
    writer.u32(context.scissor_width);
    writer.u32(context.scissor_height);
    writer.u16(context.last_macroblock_x);
    writer.u16(context.last_macroblock_y);
    writer.boolean(context.ignore_macroblock);
    writer.i32(context.rendered_rect_x0);
    writer.i32(context.rendered_rect_y0);
    writer.i32(context.rendered_rect_x1);
    writer.i32(context.rendered_rect_y1);
    writer.i32(context.draw_rect_x0);
    writer.i32(context.draw_rect_y0);
    writer.i32(context.draw_rect_x1);
    writer.i32(context.draw_rect_y1);
    writer.u32(context.visibility_buffer_address);
    writer.u32(context.visibility_stride);
    writer.i32(context.visibility_query_index);
    writer.boolean(context.visibility_query_increment);
}
bool read_context(BufferReader &reader, renderer::vulkan::VKContextSnapshot &context, std::string &error) {
    context.context_address = reader.u32();
    context.render_target_address = reader.u32();
    context.recording_open = reader.boolean();
    context.scene_wrote_depth = reader.boolean();
    context.scene_has_drawn = reader.boolean();
    context.scene_macroblock_flushed = reader.boolean();
    context.is_first_scene_draw = reader.boolean();
    context.surface_downscale = reader.value<float>();
    context.viewport_x = reader.value<float>();
    context.viewport_y = reader.value<float>();
    context.viewport_width = reader.value<float>();
    context.viewport_height = reader.value<float>();
    context.viewport_min_depth = reader.value<float>();
    context.viewport_max_depth = reader.value<float>();
    context.scissor_x = reader.i32();
    context.scissor_y = reader.i32();
    context.scissor_width = reader.u32();
    context.scissor_height = reader.u32();
    context.last_macroblock_x = reader.u16();
    context.last_macroblock_y = reader.u16();
    context.ignore_macroblock = reader.boolean();
    context.rendered_rect_x0 = reader.i32();
    context.rendered_rect_y0 = reader.i32();
    context.rendered_rect_x1 = reader.i32();
    context.rendered_rect_y1 = reader.i32();
    context.draw_rect_x0 = reader.i32();
    context.draw_rect_y0 = reader.i32();
    context.draw_rect_x1 = reader.i32();
    context.draw_rect_y1 = reader.i32();
    context.visibility_buffer_address = reader.u32();
    context.visibility_stride = reader.u32();
    context.visibility_query_index = reader.i32();
    context.visibility_query_increment = reader.boolean();
    return reader.ok() && renderer::vulkan::validate_vk_context_snapshot(context, error);
}
}

bool capture_gpu_buffer_pages(const uint8_t *cpu, const uint8_t *gpu, uint32_t mapping_size,
    uint64_t allocation_size, uint64_t byte_budget, std::vector<GpuBufferPage> &pages, std::string &error) {
    if (!cpu || !gpu || !mapping_size || mapping_size % page_size
        || mapping_size > UINT32_MAX - page_size || allocation_size != uint64_t(mapping_size) + page_size) {
        error = "invalid GPU buffer snapshot layout";
        return false;
    }
    std::vector<GpuBufferPage> staged;
    uint64_t copied = 0;
    for (uint64_t offset = 0; offset < allocation_size; offset += page_size) {
        const bool differs = offset == mapping_size
            ? std::any_of(gpu + offset, gpu + offset + page_size, [](uint8_t byte) { return byte != 0; })
            : std::memcmp(gpu + offset, cpu + offset, page_size) != 0;
        if (!differs) continue;
        if (byte_budget - std::min(byte_budget, copied) < page_size) {
            error = "GPU buffer snapshot exceeds size limit";
            return false;
        }
        copied += page_size;
        staged.push_back({ static_cast<uint32_t>(offset), { gpu + offset, gpu + offset + page_size } });
    }
    pages = std::move(staged);
    return true;
}

bool validate_gpu_gxm_references(const GpuSnapshot &gpu, const GxmSnapshot &gxm, std::string &error) {
    const auto fail = [&] { error = "GPU snapshot does not match GXM objects"; return false; };
    if (!std::isfinite(gpu.resolution) || gpu.resolution <= 0 || gpu.resolution > 16) return fail();
    if (gpu.targets.size() != gxm.render_targets.size()) return fail();
    std::map<Address, const gxm::RenderTargetSnapshot *> targets;
    for (const auto &target : gxm.render_targets) targets.emplace(target.address, &target);
    std::set<Address> target_addresses;
    for (const auto &target : gpu.targets) {
        const auto entry = targets.find(target.address);
        if (entry == targets.end() || !target_addresses.insert(target.address).second) return fail();
        const auto &params = entry->second->params;
        if (params.flags & SCE_GXM_RENDER_TARGET_MACROTILE_SYNC) {
            const uint32_t nx = (params.flags >> 8) & 7, ny = (params.flags >> 12) & 7;
            if (!nx || nx > 4 || !ny || ny > 4) return fail();
            const double macro_width = double(params.width / nx) * gpu.resolution;
            const double macro_height = double(params.height / ny) * gpu.resolution;
            if (macro_width < 1 || macro_height < 1 || macro_width >= 65536 || macro_height >= 65536) return fail();
        }
        const uint32_t width = static_cast<uint32_t>(params.width * gpu.resolution);
        const uint32_t height = static_cast<uint32_t>(params.height * gpu.resolution);
        const uint32_t ds_scale = params.multisampleMode == SCE_GXM_MULTISAMPLE_4X ? 2 : 1;
        if (target.color.width != width || target.color.height != height || target.color.format != vk::Format::eR8G8B8A8Unorm
            || target.depth_stencil.width != width * ds_scale || target.depth_stencil.height != height * ds_scale) return fail();
    }
    std::set<Address> contexts;
    for (const auto &context : gxm.contexts) if (!context.deferred) contexts.insert(context.address);
    if (contexts.size() != gpu.clocks.size()) return fail();
    for (const auto &clock : gpu.clocks) if (contexts.erase(clock.address) != 1) return fail();
    for (const auto &context : gxm.contexts) if (!context.deferred) contexts.insert(context.address);
    if (contexts.size() != gpu.contexts.size()) return fail();
    for (const auto &context : gpu.contexts) {
        if (contexts.erase(context.context_address) != 1
            || (context.render_target_address && !targets.contains(context.render_target_address))) return fail();
    }
    if (gpu.mapping_method == static_cast<uint32_t>(MappingMethod::DoubleBuffer)) {
        std::map<Address, uint32_t> mappings;
        for (const auto &[address, info] : gxm.regions) if (info.size) mappings.emplace(address, info.size);
        if (mappings.size() != gpu.buffers.size()) return fail();
        for (const auto &buffer : gpu.buffers) {
            const auto found = mappings.find(buffer.address);
            if (found == mappings.end() || found->second != buffer.size) return fail();
            mappings.erase(found);
        }
    } else if (!gpu.buffers.empty()) return fail();
    return true;
}

bool capture_gpu_state(EmuEnvState &env, GpuSnapshot &snapshot, std::string &error) {
    if (env.renderer->current_backend != renderer::Backend::Vulkan) {
        error = "save states currently require Vulkan";
        return false;
    }
    auto &state = static_cast<renderer::vulkan::VKState &>(*env.renderer);
    GpuSnapshot staged;
    staged.mapping_method = static_cast<uint32_t>(state.mapping_method);
    staged.resolution = state.res_multiplier;
    staged.last_scene = state.last_scene_id;
    staged.current_frame = state.current_frame_idx;
    std::map<renderer::RenderTarget *, Address> target_addresses;
    for (const auto &[address, target] : gxm::runtime_render_targets(env)) target_addresses.emplace(target, address);
    for (const auto &[address, context_ptr] : physical_gpu_contexts<renderer::vulkan::VKContext>(gxm::runtime_contexts(env))) {
        auto &context = *context_ptr;
        const auto target = target_addresses.find(context.render_target);
        if (context.render_target && target == target_addresses.end()) { error = "untracked GPU render target"; return false; }
        renderer::vulkan::VKContextSnapshot checkpoint;
        if (!renderer::vulkan::prepare_vk_context_snapshot(context, error)
            || !renderer::vulkan::capture_vk_context_snapshot(context, address,
                target == target_addresses.end() ? 0 : target->second, checkpoint, error)) return false;
        staged.contexts.push_back(checkpoint);
        staged.clocks.push_back({ address, context.frame_timestamp, context.scene_timestamp });
    }
    uint64_t copied = 0;
    if (!renderer::vulkan::capture_surface_cache_snapshot(state.surface_cache, image_device(state), staged.surfaces, error))
        return false;
    for (const auto &color : staged.surfaces.colors)
        copied += color.texture.image.bytes.size() + color.raw_image.image.bytes.size();
    for (const auto &ds : staged.surfaces.depth_stencil) copied += ds.texture.image.bytes.size();
    if (copied > kMaxBufferedSectionSize) { error = "GPU surface snapshot exceeds size limit"; return false; }
    for (const auto &[address, target_ptr] : gxm::runtime_render_targets(env)) {
        auto &target = static_cast<renderer::vulkan::VKRenderTarget &>(*target_ptr);
        GpuTargetSnapshot target_snapshot;
        target_snapshot.address = address;
        if (!vkutil::capture_image_snapshot(image_device(state), target.color, target_snapshot.color, error)
            || !vkutil::capture_image_snapshot(image_device(state), target.depthstencil, target_snapshot.depth_stencil, error))
            return false;
        copied += target_snapshot.color.bytes.size() + target_snapshot.depth_stencil.bytes.size();
        if (copied > kMaxBufferedSectionSize) {
            error = "GPU target snapshot exceeds size limit";
            return false;
        }
        staged.targets.push_back(std::move(target_snapshot));
    }
    if (state.mapping_method == MappingMethod::DoubleBuffer) {
        for (const auto &[address, mapping] : state.mapped_memories) {
            const auto *buffer = std::get_if<vkutil::Buffer>(&mapping.buffer_impl);
            if (!buffer || !buffer->mapped_data || buffer->size != uint64_t(mapping.size) + page_size) {
                error = "GPU mapped buffer is unavailable";
                return false;
            }
            GpuBufferSnapshot saved{ address, mapping.size, {} };
            const auto *gpu = static_cast<const uint8_t *>(buffer->mapped_data);
            const auto *cpu = Ptr<const uint8_t>(address).get(env.mem);
            if (!capture_gpu_buffer_pages(cpu, gpu, mapping.size, buffer->size,
                    kMaxBufferedSectionSize - copied, saved.pages, error)) return false;
            for (const auto &page : saved.pages) copied += page.bytes.size();
            staged.buffers.push_back(std::move(saved));
        }
    }
    snapshot = std::move(staged);
    return true;
}

bool encode_gpu_state(const GpuSnapshot &snapshot, std::vector<uint8_t> &data, std::string &error) {
    std::vector<uint8_t> surfaces;
    if (!renderer::vulkan::encode_surface_cache_snapshot(snapshot.surfaces, surfaces, error))
        return false;
    uint64_t size = surfaces.size() + 64 + snapshot.clocks.size() * 20 + snapshot.contexts.size() * 111;
    for (const auto &target : snapshot.targets)
        size += 44 + target.color.bytes.size() + target.depth_stencil.bytes.size();
    for (const auto &buffer : snapshot.buffers) {
        size += 12;
        for (const auto &page : buffer.pages) size += 8 + page.bytes.size();
    }
    if (size > kMaxBufferedSectionSize) {
        error = "GPU snapshot exceeds section size limit";
        return false;
    }
    BufferWriter writer;
    writer.u32(2);
    writer.u32(snapshot.mapping_method);
    writer.value(snapshot.resolution);
    writer.i32(snapshot.last_scene);
    writer.i32(snapshot.current_frame);
    write_bytes(writer, surfaces);
    writer.u32(static_cast<uint32_t>(snapshot.targets.size()));
    for (const auto &target : snapshot.targets) {
        writer.u32(target.address);
        write_image(writer, target.color);
        write_image(writer, target.depth_stencil);
    }
    writer.u32(static_cast<uint32_t>(snapshot.buffers.size()));
    for (const auto &buffer : snapshot.buffers) {
        writer.u32(buffer.address);
        writer.u32(buffer.size);
        writer.u32(static_cast<uint32_t>(buffer.pages.size()));
        for (const auto &page : buffer.pages) {
            writer.u32(page.offset);
            write_bytes(writer, page.bytes);
        }
    }
    writer.u32(static_cast<uint32_t>(snapshot.clocks.size()));
    for (const auto &clock : snapshot.clocks) {
        writer.u32(clock.address);
        writer.u64(clock.frame);
        writer.u64(clock.scene);
    }
    writer.u32(static_cast<uint32_t>(snapshot.contexts.size()));
    for (const auto &context : snapshot.contexts) write_context(writer, context);
    data = std::move(writer.data());
    return true;
}

bool parse_gpu_state(const std::vector<uint8_t> &data, GpuSnapshot &snapshot, std::string &error) {
    BufferReader reader(data.data(), data.size());
    GpuSnapshot staged;
    if (reader.u32() != 2) { error = "unsupported GPU snapshot schema"; return false; }
    staged.mapping_method = reader.u32();
    staged.resolution = reader.value<float>();
    staged.last_scene = reader.i32();
    staged.current_frame = reader.i32();
    std::vector<uint8_t> surfaces;
    if (!read_bytes(reader, surfaces)
        || !renderer::vulkan::decode_surface_cache_snapshot(surfaces, staged.surfaces, error))
        return false;
    uint32_t amount;
    if (!count(reader, amount, 44)) return false;
    staged.targets.resize(amount);
    for (auto &target : staged.targets) {
        target.address = reader.u32();
        if (!read_image(reader, target.color, error) || !read_image(reader, target.depth_stencil, error)) return false;
    }
    if (!count(reader, amount, 12)) return false;
    staged.buffers.resize(amount);
    for (auto &buffer : staged.buffers) {
        buffer.address = reader.u32();
        buffer.size = reader.u32();
        const auto pages = reader.u32();
        if (!reader.ok() || pages > (uint64_t(buffer.size) + page_size) / page_size
            || pages > reader.remaining() / (page_size + 8)) return false;
        buffer.pages.resize(pages);
        for (auto &page : buffer.pages) {
            page.offset = reader.u32();
            if (!read_bytes(reader, page.bytes) || page.bytes.size() != page_size) return false;
        }
    }
    if (!count(reader, amount, 20)) return false;
    staged.clocks.resize(amount);
    for (auto &clock : staged.clocks) {
        clock.address = reader.u32();
        clock.frame = reader.u64();
        clock.scene = reader.u64();
    }
    if (!count(reader, amount, 107)) return false;
    staged.contexts.resize(amount);
    for (auto &context : staged.contexts) if (!read_context(reader, context, error)) return false;
    if (!reader.ok() || reader.remaining()) { error = "invalid GPU snapshot"; return false; }
    snapshot = std::move(staged);
    return true;
}

bool validate_gpu_state(const GpuSnapshot &snapshot, const MemoryImage &memory, std::string &error) {
    const auto fail = [&] { error = "invalid GPU snapshot references"; return false; };
    if (snapshot.mapping_method > static_cast<uint32_t>(MappingMethod::NativeBuffer)
        || !std::isfinite(snapshot.resolution) || snapshot.resolution <= 0 || snapshot.resolution > 16
        || snapshot.current_frame < 0 || snapshot.current_frame >= renderer::vulkan::MAX_FRAMES_RENDERING
        || snapshot.targets.size() > max_objects || snapshot.buffers.size() > max_objects || snapshot.clocks.size() > max_objects
        || snapshot.contexts.size() != snapshot.clocks.size())
        return fail();
    std::set<Address> addresses;
    for (const auto &target : snapshot.targets) {
        if (!addresses.insert(target.address).second || !memory.contains(target.address, 4)
            || !vkutil::validate_image_snapshot(target.color, error) || !vkutil::validate_image_snapshot(target.depth_stencil, error)) return fail();
    }
    addresses.clear();
    uint64_t previous_end = 0;
    std::map<Address, uint32_t> ranges;
    for (const auto &buffer : snapshot.buffers) {
        if (!buffer.size || buffer.size % page_size || buffer.size > UINT32_MAX - page_size
            || !ranges.emplace(buffer.address, buffer.size).second || !memory.contains(buffer.address, buffer.size)) return fail();
        uint64_t end = 0;
        for (const auto &page : buffer.pages) {
            if (page.offset % page_size || page.offset < end || page.bytes.size() != page_size
                || uint64_t(page.offset) + page_size > uint64_t(buffer.size) + page_size) return fail();
            end = uint64_t(page.offset) + page_size;
        }
    }
    for (const auto &[address, size] : ranges) {
        if (address < previous_end) return fail();
        previous_end = uint64_t(address) + size;
    }
    if (snapshot.mapping_method != static_cast<uint32_t>(MappingMethod::DoubleBuffer) && !snapshot.buffers.empty()) return fail();
    for (const auto &clock : snapshot.clocks) {
        if (!addresses.insert(clock.address).second || !memory.contains(clock.address, 4)
            || !clock.frame || clock.frame == UINT64_MAX || !clock.scene || clock.scene == UINT64_MAX) return fail();
    }
    for (const auto &context : snapshot.contexts) {
        if (addresses.erase(context.context_address) != 1
            || !renderer::vulkan::validate_vk_context_snapshot(context, error)
            || (context.visibility_buffer_address && !memory.contains(context.visibility_buffer_address, context.visibility_stride))) return fail();
    }
    for (const auto &color : snapshot.surfaces.colors)
        if (color.active && !memory.contains(color.guest_address, color.total_bytes)) return fail();
    for (const auto &ds : snapshot.surfaces.depth_stencil) {
        if (!ds.active) continue;
        if (ds.memory_width < 0 || ds.memory_height < 0 || ds.stride_samples < static_cast<uint32_t>(ds.memory_width)) return fail();
        const uint64_t samples = uint64_t(ds.stride_samples) * ds.memory_height;
        if ((ds.surface.depth_data && !memory.contains(ds.surface.depth_data.address(), samples * 4))
            || (ds.surface.stencil_data && !memory.contains(ds.surface.stencil_data.address(), samples))) return fail();
    }
    return renderer::vulkan::validate_surface_cache_snapshot(snapshot.surfaces, error);
}

bool prepare_gpu_teardown(EmuEnvState &env, std::string &error) {
    if (env.renderer->current_backend != renderer::Backend::Vulkan) { error = "save states require Vulkan"; return false; }
    auto &state = static_cast<renderer::vulkan::VKState &>(*env.renderer);
    if (!state.prepare_state_restore_teardown(error)) return false;
    std::vector<std::thread *> workers;
    for (const auto &[address, context] : physical_gpu_contexts<renderer::vulkan::VKContext>(gxm::runtime_contexts(env)))
        workers.push_back(&context->gpu_request_wait_thread);
    return renderer::vulkan::join_gpu_request_workers_after_stop(workers, error);
}

bool prepare_gpu_state_restore(EmuEnvState &env, std::string &error) {
    if (env.renderer->current_backend != renderer::Backend::Vulkan) { error = "save states require Vulkan"; return false; }
    return static_cast<renderer::vulkan::VKState &>(*env.renderer).reset_frame_runtime_for_restore(error);
}

bool restore_gpu_state(EmuEnvState &env, const GpuSnapshot &snapshot, std::string &error) {
    if (env.renderer->current_backend != renderer::Backend::Vulkan) { error = "save states require Vulkan"; return false; }
    auto &state = static_cast<renderer::vulkan::VKState &>(*env.renderer);
    if (static_cast<uint32_t>(state.mapping_method) != snapshot.mapping_method
        || state.res_multiplier != snapshot.resolution) {
        error = "GPU configuration differs from save state";
        return false;
    }
    std::map<Address, renderer::RenderTarget *> targets;
    for (const auto &entry : gxm::runtime_render_targets(env)) targets.insert(entry);
    const auto contexts = physical_gpu_contexts<renderer::vulkan::VKContext>(gxm::runtime_contexts(env));
    if (targets.size() != snapshot.targets.size() || contexts.size() != snapshot.clocks.size()
        || contexts.size() != snapshot.contexts.size()
        || (state.mapping_method == MappingMethod::DoubleBuffer && state.mapped_memories.size() != snapshot.buffers.size())) {
        error = "GPU runtime object tables differ from save state";
        return false;
    }
    for (const auto &target : snapshot.targets) {
        if (!targets.contains(target.address)) { error = "missing saved GPU render target"; return false; }
        const auto &live = static_cast<const renderer::vulkan::VKRenderTarget &>(*targets.at(target.address));
        const auto compatible = [](const vkutil::Image &image, const vkutil::ImageSnapshot &saved) {
            return image.width == saved.width && image.height == saved.height && image.format == saved.format;
        };
        if (!compatible(live.color, target.color) || !compatible(live.depthstencil, target.depth_stencil)) {
            error = "saved GPU target dimensions or format differ"; return false;
        }
    }
    for (const auto &saved : snapshot.buffers) {
        const auto entry = state.mapped_memories.find(saved.address);
        if (entry == state.mapped_memories.end() || entry->second.size != saved.size) { error = "missing saved GPU mapping"; return false; }
        const auto *buffer = std::get_if<vkutil::Buffer>(&entry->second.buffer_impl);
        if (!buffer || !buffer->mapped_data || buffer->size != uint64_t(saved.size) + page_size) { error = "invalid restored GPU mapping"; return false; }
    }
    for (const auto &clock : snapshot.clocks)
        if (!contexts.contains(clock.address)) { error = "missing saved GPU context"; return false; }
    for (const auto &checkpoint : snapshot.contexts) {
        if (!contexts.contains(checkpoint.context_address)
            || (checkpoint.render_target_address && !targets.contains(checkpoint.render_target_address))
            || !renderer::vulkan::validate_vk_context_snapshot(checkpoint, error)) {
            error = "invalid restored GPU context checkpoint"; return false;
        }
    }
    for (const auto &target : snapshot.targets) {
        if (!targets.contains(target.address)) { error = "missing saved GPU render target"; return false; }
        auto &live = static_cast<renderer::vulkan::VKRenderTarget &>(*targets.at(target.address));
        if (!vkutil::restore_image_snapshot(image_device(state), live.color, target.color, error)
            || !vkutil::restore_image_snapshot(image_device(state), live.depthstencil, target.depth_stencil, error)) return false;
    }
    if (!renderer::vulkan::restore_surface_cache_snapshot(state.surface_cache, env.mem, image_device(state), snapshot.surfaces, error)) return false;
    for (const auto &saved : snapshot.buffers) {
        const auto entry = state.mapped_memories.find(saved.address);
        if (entry == state.mapped_memories.end() || entry->second.size != saved.size) { error = "missing saved GPU mapping"; return false; }
        auto *buffer = std::get_if<vkutil::Buffer>(&entry->second.buffer_impl);
        if (!buffer || !buffer->mapped_data || buffer->size != uint64_t(saved.size) + page_size) { error = "invalid restored GPU mapping"; return false; }
        auto *gpu = static_cast<uint8_t *>(buffer->mapped_data);
        std::memcpy(gpu, Ptr<const void>(saved.address).get(env.mem), saved.size);
        std::memset(gpu + saved.size, 0, page_size);
        for (const auto &page : saved.pages) std::memcpy(gpu + page.offset, page.bytes.data(), page.bytes.size());
        state.allocator.flushAllocation(buffer->allocation, 0, buffer->size);
    }
    for (const auto &clock : snapshot.clocks) {
        if (!contexts.contains(clock.address)) { error = "missing saved GPU context"; return false; }
        auto &context = *contexts.at(clock.address);
        context.frame_timestamp = clock.frame;
        context.scene_timestamp = clock.scene;
        context.last_frame_waited = clock.frame;
    }
    state.last_scene_id = snapshot.last_scene;
    state.current_frame_idx = snapshot.current_frame;
    for (const auto &checkpoint : snapshot.contexts) {
        auto &context = *contexts.at(checkpoint.context_address);
        auto *target = checkpoint.render_target_address
            ? static_cast<renderer::vulkan::VKRenderTarget *>(targets.at(checkpoint.render_target_address)) : nullptr;
        const ScopedGpuContextSelection selection(state.context, context);
        if (!renderer::vulkan::restore_vk_context_snapshot(context, env.mem, target, checkpoint, state.features, error)) return false;
    }
    return true;
}

bool wait_gpu_snapshot_idle(EmuEnvState &env, std::string &error) {
    if (env.renderer->current_backend != renderer::Backend::Vulkan) {
        error = "save states require Vulkan";
        return false;
    }
    auto &state = static_cast<renderer::vulkan::VKState &>(*env.renderer);
    try {
        state.device.waitIdle();
    } catch (const std::exception &exception) {
        error = std::string("Vulkan device idle wait failed: ") + exception.what();
        return false;
    }

    const auto contexts = physical_gpu_contexts<renderer::vulkan::VKContext>(gxm::runtime_contexts(env));
    size_t worker_count = 0;
    for (const auto &[_, context] : contexts)
        if (context->gpu_request_wait_thread.joinable()) ++worker_count;
    try {
        if (!renderer::vulkan::wait_for_gpu_snapshot_requests(state.request_queue, worker_count,
                std::chrono::seconds(5), error)) return false;
    } catch (const std::exception &exception) {
        error = std::string("GPU request snapshot barrier failed: ") + exception.what();
        return false;
    }
    error.clear();
    return true;
}
}
