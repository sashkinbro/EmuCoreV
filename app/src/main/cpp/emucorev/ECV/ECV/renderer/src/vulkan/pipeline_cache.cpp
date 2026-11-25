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

#include <renderer/vulkan/pipeline_cache.h>

#include <cstring>
#include <renderer/vulkan/pipeline_cache_file.h>
#include <renderer/vulkan/render_pass_sync.h>

#include <renderer/functions.h>
#include <renderer/program_source.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/vertex_stream.h>
#include <renderer/vulkan/types.h>

#include <gxm/functions.h>
#include <gxm/types.h>
#include <renderer/shaders.h>
#include <renderer/shader_variant.h>
#include <shader/spirv_recompiler.h>

#include <util/fs.h>
#include <util/hash.h>
#include <util/align.h>
#include <util/log.h>

#include <array>
#include <bit>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>

#include <SDL3/SDL_cpuinfo.h>

#if defined(__ANDROID__) || defined(__linux__)
#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <sys/stat.h>
#endif

// don't use the dispatch version, because we always hash a small amount
// with a known size
#define XXH_INLINE_ALL
#include <xxhash.h>
#if ECV_ORACLE
#include <ecv_gpu_trace.h>
#endif

namespace renderer::vulkan {

// Cache status markers are never Vulkan objects and must not reach the driver.
const vk::Pipeline kPipelineCompiling = std::bit_cast<vk::Pipeline, uint64_t>(~0ULL);
// Remember driver refusals so later draws do not repeatedly queue the same failure.
const vk::Pipeline kPipelineFailed = std::bit_cast<vk::Pipeline, uint64_t>(~1ULL);

// Fixed-state prefix used for the pipeline cache key, not the async snapshot extent.
constexpr size_t record_pipeline_len = offsetof(GxmRecordState, vertex_streams);
static_assert(std::is_trivially_copyable_v<GxmRecordState>);

// structure containing everything needed to compile a pipeline
struct CompileRequest {
    // iterator to the pipeline location
    vk::Pipeline *pipeline;

    // this is everything we need to compile the shader on another thread (as the original data will change)
    SceGxmPrimitiveType type;
    vk::RenderPass render_pass;
    SceGxmVertexProgram *vertex_program_gxm;
    SceGxmFragmentProgram *fragment_program_gxm;
    ProgramSource vertex_source;
    ProgramSource fragment_source;
    shader::Hints hints;
    PipelineDsInfo ds;
    dsmodel::PassVariant variant;

    // Compilation also reads color_surface beyond the hashed fixed-state prefix.
    // Own the full value while the rendering thread advances its live record.
    GxmRecordState record;

    const GxmRecordState *get_record() {
        return &record;
    }
};

PipelineCache::PipelineCache(VKState &state)
    : state(state)
    , pipeline_compile_queue_token(pipeline_compile_queue) {
}

void PipelineCache::init(bool support_rasterized_order_access) {
    vk::PipelineCacheCreateInfo pipeline_info{};
    pipeline_cache = state.device.createPipelineCache(pipeline_info);

    // the layout for uniforms buffer can be made here as it will always be the same
    {
        std::array<vk::DescriptorSetLayoutBinding, 4> layout_bindings;
        // Our vertex uniform (GXMRenderVertUniformBlock)
        layout_bindings[0] = vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
        };
        // Our fragment uniform (GXMRenderFragUniformBlock)
        layout_bindings[1] = vk::DescriptorSetLayoutBinding{
            .binding = 1,
            .descriptorType = vk::DescriptorType::eUniformBufferDynamic,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        };
        // GXM vertex uniform (if no memory mapping)
        layout_bindings[2] = vk::DescriptorSetLayoutBinding{
            .binding = 2,
            .descriptorType = vk::DescriptorType::eStorageBufferDynamic,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eVertex,
        };
        // GXM Fragment uniform (if no memory mapping)
        layout_bindings[3] = vk::DescriptorSetLayoutBinding{
            .binding = 3,
            .descriptorType = vk::DescriptorType::eStorageBufferDynamic,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment,
        };

        vk::DescriptorSetLayoutCreateInfo descriptor_info{
            .bindingCount = state.features.enable_memory_mapping ? 2U : 4U,
            .pBindings = layout_bindings.data()
        };
        uniforms_layout = state.device.createDescriptorSetLayout(descriptor_info);
    }

    {
        // layout for the mask, color attachment as input, being an input attachment or a storage image
        // depending on whether or not we are using shader interlock
        std::array<vk::DescriptorSetLayoutBinding, 3> layout_binding;
        const vk::DescriptorType intput_image_descriptor = state.features.support_shader_interlock
            ? vk::DescriptorType::eStorageImage
            : vk::DescriptorType::eInputAttachment;
        uint32_t binding_count = 0;
        layout_binding[binding_count++] = vk::DescriptorSetLayoutBinding{
            .binding = 0,
            .descriptorType = intput_image_descriptor,
            .descriptorCount = 1,
            .stageFlags = vk::ShaderStageFlagBits::eFragment
        };
        if (state.features.use_mask_bit)
            layout_binding[binding_count++] = vk::DescriptorSetLayoutBinding{
                .binding = 1,
                .descriptorType = vk::DescriptorType::eStorageImage,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            };
        if (state.features.preserve_f16_nan_as_u16)
            // raw u16x4 alias of the color attachment (set 1, binding 2 in the shaders)
            layout_binding[binding_count++] = vk::DescriptorSetLayoutBinding{
                .binding = 2,
                .descriptorType = intput_image_descriptor,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eFragment
            };

        vk::DescriptorSetLayoutCreateInfo descriptor_info{
            .bindingCount = binding_count,
            .pBindings = layout_binding.data()
        };
        attachments_layout = state.device.createDescriptorSetLayout(descriptor_info);
    }

    {
        // texture layout

        // empty descriptor
        {
            vk::DescriptorSetLayoutCreateInfo empty_info{};
            vertex_textures_layout[0] = state.device.createDescriptorSetLayout(empty_info);
            fragment_textures_layout[0] = vertex_textures_layout[0];
        }

        // first vertex
        std::array<vk::DescriptorSetLayoutBinding, 16> layout_bindings;
        for (uint32_t i = 0; i < 16; i++) {
            layout_bindings[i] = {
                .binding = i,
                .descriptorType = vk::DescriptorType::eCombinedImageSampler,
                .descriptorCount = 1,
                .stageFlags = vk::ShaderStageFlagBits::eVertex
            };
        }
        for (uint32_t i = 1; i <= 16; i++) {
            vk::DescriptorSetLayoutCreateInfo descriptor_info{
                .bindingCount = i,
                .pBindings = layout_bindings.data()
            };
            vertex_textures_layout[i] = state.device.createDescriptorSetLayout(descriptor_info);
        }

        // then fragment
        for (uint32_t i = 0; i < 16; i++) {
            layout_bindings[i].stageFlags = vk::ShaderStageFlagBits::eFragment;
        }
        for (uint32_t i = 1; i <= 16; i++) {
            vk::DescriptorSetLayoutCreateInfo descriptor_info{
                .bindingCount = i,
                .pBindings = layout_bindings.data()
            };
            fragment_textures_layout[i] = state.device.createDescriptorSetLayout(descriptor_info);
        }
    }

    // compute all possible pipeline layouts
    for (uint32_t vert_texture_count = 0; vert_texture_count <= 16; vert_texture_count++) {
        for (uint32_t frag_texture_count = 0; frag_texture_count <= 16; frag_texture_count++) {
            vk::PipelineLayoutCreateInfo layout_info{};
            vk::DescriptorSetLayout set_layouts[] = { uniforms_layout, attachments_layout, vertex_textures_layout[vert_texture_count], fragment_textures_layout[frag_texture_count] };
            layout_info.setSetLayouts(set_layouts);
            pipeline_layouts[vert_texture_count][frag_texture_count] = state.device.createPipelineLayout(layout_info);
        }
    }

    {
        // look for rgb vertex attribute support
        // we need to look at each format because it is not the same for all usual 3-component formats (checked on AMD Radeon HD 7800)
        // no need to test for 32-bit types, they are always supported
        vk::Format formats[] = {
            vk::Format::eR16G16B16Unorm, vk::Format::eR16G16B16Snorm,
            vk::Format::eR16G16B16Uscaled, vk::Format::eR16G16B16Sscaled,
            vk::Format::eR16G16B16Uint, vk::Format::eR16G16B16Sint,
            vk::Format::eR16G16B16Sfloat,
            vk::Format::eR8G8B8Unorm, vk::Format::eR8G8B8Snorm,
            vk::Format::eR8G8B8Uscaled, vk::Format::eR8G8B8Sscaled,
            vk::Format::eR8G8B8Uint, vk::Format::eR8G8B8Sint
        };
        for (auto fmt : formats) {
            vk::FormatProperties rgb_property = state.physical_device.getFormatProperties(fmt);
            if (!(rgb_property.bufferFeatures & vk::FormatFeatureFlagBits::eVertexBuffer)) {
                unsupported_rgb_vertex_attribute_formats.emplace(fmt);
            }
        }

        // same for scaled formats
        const vk::FormatProperties scaled_property = state.physical_device.getFormatProperties(vk::Format::eR8G8B8A8Uscaled);
        support_scaled_vertex_attribute = static_cast<bool>(scaled_property.bufferFeatures & vk::FormatFeatureFlagBits::eVertexBuffer);
        state.features.support_scaled_attribute_formats = support_scaled_vertex_attribute;

        // handle interactions between these 2 properties, the scaled support will be applied before the 3-component support
        if (!support_scaled_vertex_attribute) {
            vk::Format scaled_fmt[] = { vk::Format::eR16G16B16Uscaled, vk::Format::eR16G16B16Sscaled, vk::Format::eR8G8B8Uscaled, vk::Format::eR8G8B8Sscaled };
            for (auto fmt : scaled_fmt)
                unsupported_rgb_vertex_attribute_formats.erase(fmt);
        }

        state.features.support_rgb_attributes = unsupported_rgb_vertex_attribute_formats.empty();
    }

    support_coherent_framebuffer_fetch = support_rasterized_order_access;
#if ECV_ORACLE
    {
        fs::path oracle_root = state.vita_fs_path;
        if (!oracle_root.has_filename())
            oracle_root = oracle_root.parent_path();
        if (fs::exists(oracle_root.parent_path() / "dbg-no-roaa")) {
            LOG_WARN("dbg-no-roaa: rasterization-order attachment access disabled");
            support_coherent_framebuffer_fetch = false;
        }
    }
#endif

    const int nb_logical_threads = SDL_GetNumLogicalCPUCores();
    // took this from RPCS3 (slightly modified)
    if (nb_logical_threads > 12)
        nb_worker_threads = 6;
    else if (nb_logical_threads > 8)
        nb_worker_threads = 4;
    else if (nb_logical_threads >= 8)
        nb_worker_threads = 3;
    else if (nb_logical_threads >= 6)
        nb_worker_threads = 2;
    else
        nb_worker_threads = 1;

    if (use_async_compilation) {
        // we could not initialize the worker threads previously
        use_async_compilation = false;
        set_async_compilation(true);
    }
}

void PipelineCache::set_async_compilation(bool enable) {
    if (enable == use_async_compilation)
        return;

    use_async_compilation = enable;
    if (nb_worker_threads == 0)
        // not ingame yet
        return;

    if (enable) {
        LOG_INFO("Enabling asynchronous pipeline compilation with {} threads", nb_worker_threads);
        worker_threads.reserve(nb_worker_threads);
        for (int i = 0; i < nb_worker_threads; i++) {
            worker_threads.emplace_back(&PipelineCache::compiler_thread, this, std::ref(*state.mem));
        }
    } else {
        LOG_INFO("Asynchronous pipeline compilation is now disabled");

        for (size_t i = 0; i < worker_threads.size(); i++)
            // if a thread receives nullptr, it exits
            pipeline_compile_queue.enqueue(nullptr);

        for (auto &thread : worker_threads) {
            if (thread.joinable())
                thread.join();
        }
        worker_threads.clear();
    }
}

// magic number put at the beginning of the pipeline cache file
// Identity of the running build: the ELF build id of the module that contains this code (changes with every relink), else a stamp of its file.
static uint64_t current_binary_id() {
#if defined(__ANDROID__) || defined(__linux__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&current_binary_id), &info) && info.dli_fbase) {
        struct Ctx {
            const void *base;
            uint64_t id;
        } ctx{ info.dli_fbase, 0 };
        dl_iterate_phdr([](dl_phdr_info *module, size_t, void *data) -> int {
            auto *c = static_cast<Ctx *>(data);
            for (int i = 0; i < module->dlpi_phnum; i++) {
                const ElfW(Phdr) &ph = module->dlpi_phdr[i];
                if (ph.p_type != PT_LOAD)
                    continue;
                const auto start = module->dlpi_addr + ph.p_vaddr;
                if (reinterpret_cast<uintptr_t>(c->base) < start || reinterpret_cast<uintptr_t>(c->base) >= start + ph.p_memsz)
                    continue;
                // this module: look for the GNU build id note
                for (int j = 0; j < module->dlpi_phnum; j++) {
                    const ElfW(Phdr) &note_ph = module->dlpi_phdr[j];
                    if (note_ph.p_type != PT_NOTE)
                        continue;
                    const uint8_t *p = reinterpret_cast<const uint8_t *>(module->dlpi_addr + note_ph.p_vaddr);
                    const uint8_t *end = p + note_ph.p_memsz;
                    while (p + sizeof(ElfW(Nhdr)) <= end) {
                        const auto *nh = reinterpret_cast<const ElfW(Nhdr) *>(p);
                        const uint8_t *name = p + sizeof(ElfW(Nhdr));
                        const uint8_t *desc = name + ((nh->n_namesz + 3) & ~3u);
                        if (nh->n_type == NT_GNU_BUILD_ID && desc + nh->n_descsz <= end) {
                            c->id = XXH3_64bits(desc, nh->n_descsz);
                            return 1;
                        }
                        p = desc + ((nh->n_descsz + 3) & ~3u);
                    }
                }
                return 1;
            }
            return 0;
        }, &ctx);
        if (ctx.id)
            return ctx.id;
        // no build id note: stamp of the library file
        if (info.dli_fname) {
            struct stat st{};
            if (stat(info.dli_fname, &st) == 0) {
                const uint64_t parts[2] = { static_cast<uint64_t>(st.st_size), static_cast<uint64_t>(st.st_mtime) };
                return XXH3_64bits(parts, sizeof(parts));
            }
        }
    }
#endif
    // host builds (replay): the compile time of this file
    static const char stamp[] = __DATE__ " " __TIME__;
    return XXH3_64bits(stamp, sizeof(stamp));
}

static renderer::vulkan::PipelineCacheIdentity pipeline_cache_identity(const vk::PhysicalDeviceProperties &props) {
    renderer::vulkan::PipelineCacheIdentity id;
    id.binary_id = current_binary_id();
    id.translator_version = static_cast<uint32_t>(shader::CURRENT_VERSION);
    id.driver_version = props.driverVersion;
    id.api_version = props.apiVersion;
    id.vendor_id = props.vendorID;
    id.device_id = props.deviceID;
    for (size_t i = 0; i < id.uuid.size(); i++)
        id.uuid[i] = props.pipelineCacheUUID[i];
    return id;
}

static fs::path pipeline_cache_path(const VKState &state) {
    return state.shaders_path / fmt::format("pipeline-cache-vk{}.dat", shader::CURRENT_VERSION);
}

void PipelineCache::read_pipeline_cache() {
    using namespace renderer::vulkan;
    const fs::path path = pipeline_cache_path(state);

    fs::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open())
        return;

    file.seekg(0, fs::ifstream::end);
    const uint64_t file_size = static_cast<uint64_t>(file.tellg());
    file.seekg(0);

    // Never hand an unvalidated blob to the driver (B148: another build's blob crashed vkCreateGraphicsPipelines on Adreno at startup).
    auto discard = [&](CacheReject reason) {
        // lab-checkable: grep "pipeline cache: discarded"
        LOG_WARN("pipeline cache: discarded ({})", to_string(reason));
        file.close();
        boost::system::error_code ignored;
        fs::remove(path, ignored); // rewritten with the right identity at the next save
    };

    PipelineCacheFileHeader header;
    if (file_size < sizeof(header)) {
        discard(CacheReject::TooSmall);
        return;
    }
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    const CacheReject reject = validate_header(header, pipeline_cache_identity(state.physical_device_properties), file_size);
    if (reject != CacheReject::None) {
        discard(reject);
        return;
    }

    std::vector<uint64_t> keys(header.pipeline_count);
    file.read(reinterpret_cast<char *>(keys.data()), static_cast<std::streamsize>(keys.size() * sizeof(uint64_t)));
    std::vector<char> blob(header.blob_size);
    file.read(blob.data(), static_cast<std::streamsize>(blob.size()));
    if (!file || XXH3_64bits(blob.data(), blob.size()) != header.blob_hash) {
        discard(file ? CacheReject::Corrupt : CacheReject::Truncated);
        return;
    }
    file.close();

    vk::PipelineCacheCreateInfo cache_info{
        .initialDataSize = blob.size(),
        .pInitialData = blob.data()
    };
    state.device.destroyPipelineCache(pipeline_cache);
    pipeline_cache = state.device.createPipelineCache(cache_info);
    // only now are the keys trusted: they describe pipelines this cache can serve quickly
    for (const uint64_t key : keys)
        pipelines[key] = nullptr;
    LOG_WARN("pipeline cache: loaded ({} pipelines, {} bytes)", keys.size(), blob.size()); // WARN: the lab log level hides INFO
}

void PipelineCache::save_pipeline_cache() {
    using namespace renderer::vulkan;
    // first save the shader hashes
    // do a copy for thread safety
    std::vector<ShadersHash> shader_cache_copy;
    {
        std::lock_guard<std::mutex> guard(shaders_mutex);
        shader_cache_copy = state.shaders_cache_hashs;
    }
    renderer::save_shaders_cache_hashs(state, shader_cache_copy);

    const std::vector<uint8_t> pipeline_data = state.device.getPipelineCacheData(pipeline_cache);
    if (pipeline_data.empty())
        // No pipeline was created
        return;

    const fs::path path = pipeline_cache_path(state);
    // write next to the real file and rename, a crash or a full disk can no longer leave a truncated cache
    const fs::path tmp_path = fs::path(path.string() + ".tmp");
    fs::ofstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file.is_open())
        return;

    LOG_INFO("Saving pipeline cache...");

    const PipelineCacheFileHeader header = make_header(pipeline_cache_identity(state.physical_device_properties), pipeline_data.size(),
        XXH3_64bits(pipeline_data.data(), pipeline_data.size()), pipelines.size());
    file.write(reinterpret_cast<const char *>(&header), sizeof(header));
    for (auto &[hash, _] : pipelines) {
        const uint64_t key = hash;
        file.write(reinterpret_cast<const char *>(&key), sizeof(key));
    }
    file.write(reinterpret_cast<const char *>(pipeline_data.data()), static_cast<std::streamsize>(pipeline_data.size()));
    file.close();
    if (!file.good()) {
        LOG_WARN("Could not write the pipeline cache, keeping the previous one.");
        boost::system::error_code ignored;
        fs::remove(tmp_path, ignored);
        return;
    }
    boost::system::error_code rename_ec;
    fs::rename(tmp_path, path, rename_ec);
    if (rename_ec) {
        LOG_WARN("Could not replace the pipeline cache: {}", rename_ec.message());
        return;
    }
    LOG_WARN("pipeline cache: saved ({} pipelines)", pipelines.size());
    last_saved_pipeline_count = pipelines.size();
    last_save_time_s = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void PipelineCache::cleanup() {
    // stop threads
    if (use_async_compilation)
        set_async_compilation(false);

    for (auto &[hash, pipeline] : pipelines) {
        if (pipeline && pipeline != kPipelineCompiling && pipeline != kPipelineFailed)
            state.device.destroy(pipeline);
    }
    pipelines.clear();

    {
        std::lock_guard<std::mutex> guard(shaders_mutex);
        for (auto &[hash, gate] : shaders) {
            vk::ShaderModule shader;
            if (gate.try_get(shader))
                state.device.destroy(shader);
        }
        shaders.clear();
    }

    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++)
                for (int l = 0; l < 2; l++) {
                    for (auto &[fmt, pass] : render_passes[i][j][k][l])
                        state.device.destroy(pass);
                    render_passes[i][j][k][l].clear();
                }

    for (auto &[fmt, pass] : shader_interlock_pass)
        state.device.destroy(pass);
    shader_interlock_pass.clear();

    for (int i = 0; i < 17; i++)
        for (int j = 0; j < 17; j++) {
            state.device.destroy(pipeline_layouts[i][j]);
            pipeline_layouts[i][j] = nullptr;
        }

    state.device.destroy(uniforms_layout);
    uniforms_layout = nullptr;
    state.device.destroy(attachments_layout);
    attachments_layout = nullptr;

    state.device.destroy(vertex_textures_layout[0]);
    vertex_textures_layout[0] = nullptr;
    fragment_textures_layout[0] = nullptr;

    for (int i = 1; i <= 16; i++) {
        state.device.destroy(vertex_textures_layout[i]);
        vertex_textures_layout[i] = nullptr;
        state.device.destroy(fragment_textures_layout[i]);
        fragment_textures_layout[i] = nullptr;
    }

    state.device.destroy(pipeline_cache);
    pipeline_cache = nullptr;

    next_pipeline_cache_save = std::numeric_limits<uint64_t>::max();
    nb_worker_threads = 0;
}

// Vulkan structs used to specify a specialization constant
// Also, booleans in SPIRV are 32bit wide
static const vk::SpecializationMapEntry srgb_entry = {
    .constantID = shader::GAMMA_CORRECTION_SPECIALIZATION_ID,
    .offset = 0,
    .size = sizeof(uint32_t)
};

static const uint32_t srgb_entry_true = vk::True;
static const uint32_t srgb_entry_false = vk::False;

static const vk::SpecializationInfo srgb_info_true = {
    .mapEntryCount = 1,
    .pMapEntries = &srgb_entry,
    .dataSize = sizeof(uint32_t),
    .pData = &srgb_entry_true
};

static const vk::SpecializationInfo srgb_info_false = {
    .mapEntryCount = 1,
    .pMapEntries = &srgb_entry,
    .dataSize = sizeof(uint32_t),
    .pData = &srgb_entry_false
};

vk::PipelineShaderStageCreateInfo PipelineCache::retrieve_shader(const SceGxmProgram *program, const Sha256Hash &hash, bool is_vertex, bool maskupdate, MemState &mem, const shader::Hints &hints, bool is_srgb) {
    // The module identity covers everything convert_gxp reads (see vulkan_shader_variant_key_bytes)
    const uint32_t used_textures_mask = static_cast<uint32_t>(gxp::get_textures_used(*program).to_ulong());
    const auto variant_hash = vulkan_shader_variant_hash(hash, is_vertex, hints, used_textures_mask, maskupdate);
    if (maskupdate)
        LOG_WARN_ONCE("Mask not implemented in the vulkan renderer!");

    const vk::SpecializationInfo *spec_info = nullptr;
    if (!is_vertex && state.features.should_use_shader_interlock() && program->is_frag_color_used()) {
        // if the specialization constant is used in the shader
        spec_info = is_srgb ? &srgb_info_true : &srgb_info_false;
    }

    // a null module means the shader could not be built, the caller must fail the pipeline
    const auto make_stage = [&](vk::ShaderModule module) {
        return vk::PipelineShaderStageCreateInfo{
            .stage = is_vertex ? vk::ShaderStageFlagBits::eVertex : vk::ShaderStageFlagBits::eFragment,
            .module = module,
            .pName = is_vertex ? "main_vs" : "main_fs",
            .pSpecializationInfo = spec_info,
        };
    };

    CompileGate<vk::ShaderModule> *gate;
    {
        // the map is only protected by shaders_mutex, the entries by their own gate
        std::lock_guard<std::mutex> lock(shaders_mutex);
        gate = &shaders.try_emplace(variant_hash).first->second;
    }
    // another thread may be compiling the same exact shader at the same time
    // it's no use re-compiling it, so wait (blocking, not spinning) for the other thread being done
    const auto acquired = gate->acquire();
    if (acquired.status == CompileGate<vk::ShaderModule>::Status::Failed)
        return make_stage(nullptr);
    if (acquired.status == CompileGate<vk::ShaderModule>::Status::Ready)
        return make_stage(acquired.value);

    // We own the gate: whatever happens it must be released, or waiters would block forever
    vk::ShaderModule module = nullptr;
    bool permanent_failure = false;
    try {
        // precompiled on disk?
        module = load_cached_shader_module(variant_hash);

        if (!module) {
            const std::string hash_text = hex_string(variant_hash);

            LOG_INFO("Generating vulkan spv shader {}", hash_text);
            const std::string shader_version = fmt::format("vk{}", shader::CURRENT_VERSION);

            shader::usse::SpirvCode source = load_spirv_shader(*program, state.features, true, hints, maskupdate, state.shaders_path, state.shaders_log_path, shader_version, false, renderer::shader_cache_key(state));

            if (source.empty()) {
                LOG_ERROR("Vulkan shader {} generated no SPIR-V", hash_text);
                permanent_failure = true;
            } else {
                vk::ShaderModuleCreateInfo shader_info{
                    .codeSize = sizeof(uint32_t) * source.size(),
                    .pCode = source.data()
                };

                module = state.device.createShaderModule(shader_info);

                std::lock_guard<std::mutex> guard(shaders_mutex);
                // Save shader cache hashes
                // vertex and fragment shaders are not linked together so no need to associate them
                Sha256Hash empty_hash{};
                if (is_vertex) {
                    state.shaders_cache_hashs.push_back({ variant_hash, empty_hash });
                } else {
                    state.shaders_cache_hashs.push_back({ empty_hash, variant_hash });
                }
            }
        }
    } catch (const vk::SystemError &e) {
        // device errors (out of memory, device lost...) may be transient: let a later request retry
        LOG_ERROR("Could not create the vulkan shader module {}: {}", hex_string(variant_hash), e.what());
        module = nullptr;
    } catch (const std::exception &e) {
        LOG_ERROR("Could not generate the vulkan shader {}: {}", hex_string(variant_hash), e.what());
        module = nullptr;
        permanent_failure = true;
    }

    if (module)
        gate->finish(module);
    else
        gate->fail(permanent_failure);

    return make_stage(module);
}

vk::RenderPass PipelineCache::retrieve_render_pass(vk::Format format, bool depth_load, bool stencil_load, bool force_store, bool is_color_transient, bool no_color, bool has_raw_attachment, vk::Format ds_format) {
    // eUndefined = the combined format used for attachments without a guest surface
    if (ds_format == vk::Format::eUndefined)
        ds_format = state.deep_stencil_use;
    const uint64_t pass_key = (static_cast<uint64_t>(format) << 32) | static_cast<uint64_t>(ds_format);
    const bool with_raw_attachment = has_raw_attachment && !no_color && !is_color_transient && state.features.preserve_f16_nan_as_u16 && format == vk::Format::eR16G16B16A16Sfloat;
    bool shell_snapshot = false;
    // the SceShell glow always reads a snapshot (PAF samples completed pixels); the copy-snapshot fbfetch rung does it for every title and format
    shell_snapshot = !no_color && !is_color_transient && !with_raw_attachment
        && (state.use_copy_snapshot || (state.current_title_id == "SceShell" && format == vk::Format::eR8G8B8A8Unorm));

    auto &render_passes_map = no_color
        ? shader_interlock_pass
        : (with_raw_attachment
                  ? render_passes_with_raw[depth_load][stencil_load][force_store]
                  : render_passes[is_color_transient][depth_load][stencil_load][force_store]);

    auto it = render_passes_map.find(pass_key);

    if (it != render_passes_map.end())
        return it->second;

    // create a new render pass for this format

    vk::AttachmentReference color_refs[2] = {
        { .attachment = 0, .layout = vk::ImageLayout::eGeneral },
        { .attachment = 1, .layout = vk::ImageLayout::eGeneral }
    };
    vk::AttachmentReference ds_ref{
        .attachment = no_color ? 0U : (with_raw_attachment ? 2U : 1U),
        .layout = vk::ImageLayout::eDepthStencilAttachmentOptimal
    };
    const vk::AttachmentReference shell_input{ .attachment = 2, .layout = vk::ImageLayout::eGeneral };
    vk::SubpassDescription subpass{
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics
    };

    subpass.setPDepthStencilAttachment(&ds_ref);
    if (!no_color) {
        if (support_coherent_framebuffer_fetch)
            subpass.flags = vk::SubpassDescriptionFlagBits::eRasterizationOrderAttachmentColorAccessEXT;

        subpass.colorAttachmentCount = with_raw_attachment ? 2 : 1;
        subpass.pColorAttachments = color_refs;
        subpass.inputAttachmentCount = (with_raw_attachment && state.features.direct_fragcolor) ? 2 : 1;
        subpass.pInputAttachments = color_refs;
        if (shell_snapshot)
            subpass.pInputAttachments = &shell_input;
    }

    vk::AttachmentDescription color_attachment{
        .format = format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = is_color_transient ? vk::AttachmentLoadOp::eDontCare : vk::AttachmentLoadOp::eLoad,
        .storeOp = is_color_transient ? vk::AttachmentStoreOp::eDontCare : vk::AttachmentStoreOp::eStore,
        .initialLayout = is_color_transient ? vk::ImageLayout::eUndefined : vk::ImageLayout::eGeneral,
        .finalLayout = vk::ImageLayout::eGeneral
    };

    vk::AttachmentDescription raw_attachment{
        .format = vk::Format::eR16G16B16A16Uint,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eLoad,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = vk::ImageLayout::eGeneral,
        .finalLayout = vk::ImageLayout::eGeneral
    };

    vk::AttachmentStoreOp store_op = force_store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
    vk::AttachmentDescription ds_attachment{
        .format = ds_format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = depth_load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
        .storeOp = store_op,
        .stencilLoadOp = stencil_load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
        .stencilStoreOp = store_op,
        // eUndefined would allow discarding a loaded aspect, so use it only when neither aspect is loaded
        .initialLayout = (depth_load || stencil_load) ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eUndefined,
        .finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal
    };

    std::array<vk::SubpassDependency, 5> dependencies;

    // Render targets are sampled directly by later passes (surface cache SURF_COLOR / SURF_DEPTH views, no
    // copy and no pipeline barrier): see render_pass_sync.h for what each external dependency covers.
    const auto target_dependencies = render_target_dependencies(state.features.support_shader_interlock && !no_color);
    dependencies[0] = target_dependencies[0];
    dependencies[1] = target_dependencies[1];
    dependencies[2] = target_dependencies[2];

    // self-dependency
    // this allows us to use a pipeline barrier in the render pass for programmable blending
    dependencies[3] = {
        .srcSubpass = 0,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
        .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
        .dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead,
        .dependencyFlags = vk::DependencyFlagBits::eByRegion
    };

    // mid-scene flush
    // unity games use it to write to a buffer in a vertex shader then use it as the vertex input in the next draw
    dependencies[4] = {
        .srcSubpass = 0,
        .dstSubpass = 0,
        .srcStageMask = vk::PipelineStageFlagBits::eVertexShader,
        .dstStageMask = vk::PipelineStageFlagBits::eVertexInput,
        .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
        .dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead
    };

    vk::RenderPassCreateInfo pass_info{};
    vk::AttachmentDescription attachments[] = { color_attachment, with_raw_attachment ? raw_attachment : ds_attachment, ds_attachment };
    pass_info.setAttachments(attachments);
    pass_info.attachmentCount = with_raw_attachment ? 3 : 2;
    if (shell_snapshot) {
        attachments[2] = color_attachment;
        pass_info.attachmentCount = 3;
    }
    pass_info.setSubpasses(subpass);
    pass_info.setDependencies(dependencies);
    if (no_color) {
        // only add the ds attachment
        pass_info.pAttachments = &attachments[2];
        pass_info.attachmentCount = 1;
        // no need for the self-dependency (keep the incoming and outgoing external ones)
        pass_info.setDependencyCount(3);
    }

    render_passes_map[pass_key] = state.device.createRenderPass(pass_info);

    return render_passes_map[pass_key];
}

vk::PipelineVertexInputStateCreateInfo PipelineCache::get_vertex_input_state(const SceGxmVertexProgram &vertex_program, MemState &mem) {
    // pointer to these objects are returned (so it needs to be static)
    // and each thread needs one (hence the thread_local)
    static thread_local std::vector<vk::VertexInputBindingDescription> binding_descr;
    static thread_local std::vector<vk::VertexInputAttributeDescription> attr_descr;
    std::array<std::vector<VertexAttributeRange>, SCE_GXM_MAX_VERTEX_STREAMS> stream_attribute_ranges;
    binding_descr.clear();
    attr_descr.clear();

    // Vertex attributes.
    VertexProgram *vkvert = vertex_program.renderer_data.get();

    uint32_t used_streams = 0;

    for (const SceGxmVertexAttribute &attribute : vertex_program.attributes) {
        if (!vkvert->attribute_infos.contains(attribute.regIndex))
            continue;

        used_streams |= (1 << attribute.streamIndex);
        stream_attribute_ranges[attribute.streamIndex].push_back({
            attribute.offset,
            gxm::attribute_format_size(static_cast<SceGxmAttributeFormat>(attribute.format)) * attribute.componentCount });

        SceGxmAttributeFormat attribute_format = attribute.format;
        shader::usse::AttributeInformation info = vkvert->attribute_infos.at(attribute.regIndex);

        uint8_t component_count = attribute.componentCount;
        // these 2 values are only used when a matrix is used as a vertex attribute
        // this is only supported for regformated attribute for now
        // TODO: add support for matrix input for non-regformated attributes
        uint32_t array_size = 1;
        uint32_t array_element_size = 0;
        vk::Format format;
        if (info.regformat) {
            // use the data from the shader itself
            component_count = info.component_count;
            switch (info.gxm_type) {
            case SCE_GXM_PARAMETER_TYPE_U8:
            case SCE_GXM_PARAMETER_TYPE_S8:
            case SCE_GXM_PARAMETER_TYPE_C10:
                attribute_format = SCE_GXM_ATTRIBUTE_FORMAT_U8;
                break;
            case SCE_GXM_PARAMETER_TYPE_U16:
            case SCE_GXM_PARAMETER_TYPE_S16:
            case SCE_GXM_PARAMETER_TYPE_F16:
                attribute_format = SCE_GXM_ATTRIBUTE_FORMAT_U16;
                break;
            default:
                // U32 format
                attribute_format = SCE_GXM_ATTRIBUTE_FORMAT_UNTYPED;
                break;
            }

            if (info.gxm_type == SCE_GXM_PARAMETER_TYPE_C10)
                // this is 10-bit and not 8-bit
                component_count = (component_count * 10 + 7) / 8;

            if (component_count > 4) {
                // a matrix is used as an attribute, pack everything into an array of vec4
                array_size = (component_count + 3) / 4;
                array_element_size = 4 * gxm::attribute_format_size(attribute_format);
                component_count = 4;
            }

            // regformat attributes are int32
            format = translate_attribute_format(attribute_format, component_count, true, false);
            if (component_count == 3 && unsupported_rgb_vertex_attribute_formats.contains(format)) {
                component_count = 4;
                format = translate_attribute_format(attribute_format, component_count, true, false);
            }
        } else {
            // some Android GPUs do not support scaled attributes, do the conversion in the GPU instead
            if (!support_scaled_vertex_attribute)
                info.is_integer = true;

            // some AMD GPUs do not support rgb vertex attributes, so just put it as rgba
            // the 4th component will contain garbage but this is not an issue because the input
            // in the shader will be vec3 (or ivec3) and the 4th component will be discarded
            format = translate_attribute_format(attribute_format, component_count, info.is_integer, info.is_signed);
            if (component_count == 3 && unsupported_rgb_vertex_attribute_formats.contains(format)) {
                component_count = 4;
                format = translate_attribute_format(attribute_format, component_count, info.is_integer, info.is_signed);
            }
        }

        for (uint32_t i = 0; i < array_size; i++) {
            attr_descr.push_back(vk::VertexInputAttributeDescription{
                .location = info.location + i,
                .binding = attribute.streamIndex,
                .format = format,
                .offset = attribute.offset + i * array_element_size });
        }
    }

    for (unsigned int stream_index = 0; stream_index < SCE_GXM_MAX_VERTEX_STREAMS; stream_index++) {
        if (!(used_streams & (1 << stream_index)))
            continue;

        const SceGxmVertexStream &stream = vertex_program.streams[stream_index];

        const bool is_instanced = gxm::is_stream_instancing(static_cast<SceGxmIndexSource>(stream.indexSource));

        const bool restride = should_restride_vertex_stream(
            state.features.enable_memory_mapping, stream.stride, stream_attribute_ranges[stream_index]);
        const uint32_t stride = restride ? align(stream.stride, 4) : stream.stride;
        binding_descr.push_back(vk::VertexInputBindingDescription{
            .binding = stream_index,
            .stride = stride,
            .inputRate = is_instanced ? vk::VertexInputRate::eInstance : vk::VertexInputRate::eVertex });
    }

    vk::PipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.setVertexBindingDescriptions(binding_descr);
    vertex_input.setVertexAttributeDescriptions(attr_descr);
    return vertex_input;
}

void PipelineCache::compiler_thread(MemState &mem) {
    moodycamel::ConsumerToken consumer_token(pipeline_compile_queue);

    // just a single loop, waiting for a pipeline compile request and compiling it
    CompileRequest *request;
    while (true) {
        pipeline_compile_queue.wait_dequeue(consumer_token, request);

        if (request == nullptr)
            // use this as an instruction to stop the thread
            break;

        vk::Pipeline pipeline = compile_pipeline(request->type, request->render_pass, *request->vertex_program_gxm, *request->fragment_program_gxm, request->vertex_source, request->fragment_source, *request->get_record(), request->hints, mem, request->ds, request->variant);
        // mark a refused pipeline as failed rather than leaving it null, which would make every
        // later draw queue the same doomed compilation again
        *request->pipeline = pipeline ? pipeline : kPipelineFailed;

        request->vertex_program_gxm->compile_threads_on.fetch_sub(1, std::memory_order_release);
        request->fragment_program_gxm->compile_threads_on.fetch_sub(1, std::memory_order_release);

        const auto time_s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        next_pipeline_cache_save = time_s + pipeline_cache_save_delay;

        renderer::shader_cache_stats().pipelines_compiled++;
        state.shaders_count_compiled++;

        delete request;
    }
}

static vk::StencilOpState convert_op_state(const GxmStencilStateOp &state) {
    return vk::StencilOpState{
        .failOp = translate_stencil_op(state.stencil_fail),
        .passOp = translate_stencil_op(state.depth_pass),
        .depthFailOp = translate_stencil_op(state.depth_fail),
        .compareOp = translate_stencil_func(state.func)
    };
}

vk::Pipeline PipelineCache::compile_pipeline(SceGxmPrimitiveType type, vk::RenderPass render_pass, const SceGxmVertexProgram &vertex_program_gxm, const SceGxmFragmentProgram &fragment_program_gxm, const ProgramSource &vertex_source, const ProgramSource &fragment_source, const GxmRecordState &record, const shader::Hints &hints, MemState &mem, const PipelineDsInfo &ds, dsmodel::PassVariant variant) {
    const VertexProgram &vertex_program = *vertex_program_gxm.renderer_data;
    const SceGxmProgram *gxm_fragment_shader = fragment_source.get();
    const VKFragmentProgram &fragment_program = *reinterpret_cast<VKFragmentProgram *>(
        fragment_program_gxm.renderer_data.get());

    // the vertex input state must be computed before shader are retrieved in case symbols are stripped
    const vk::PipelineVertexInputStateCreateInfo vertex_input = get_vertex_input_state(vertex_program_gxm, mem);

    const bool use_shader_interlock = state.features.support_shader_interlock && gxm_fragment_shader->is_frag_color_used();
    const bool with_raw_attachment = state.features.preserve_f16_nan_as_u16 && !use_shader_interlock
        && record.color_base_format == SCE_GXM_COLOR_BASE_FORMAT_F16F16F16F16 && record.color_surface.data;
    shader::Hints fragment_hints = hints;
    fragment_hints.raw_color_attachment_input = with_raw_attachment && state.features.direct_fragcolor && gxm_fragment_shader->is_frag_color_used();
    // draw() computes this from the paired GXP before queuing a pipeline.
    // Include it in both shader cache identities so async and disk caches agree.
    fragment_hints.vertex_output_mask = vertex_program.varying_location_mask;

    const vk::PipelineShaderStageCreateInfo vertex_shader = retrieve_shader(vertex_source.get(), vertex_program.hash, true, fragment_program_gxm.is_maskupdate, mem, hints);
    const vk::PipelineShaderStageCreateInfo fragment_shader = retrieve_shader(gxm_fragment_shader, fragment_program.hash, false, fragment_program_gxm.is_maskupdate, mem, fragment_hints, record.is_gamma_corrected);
    const vk::PipelineShaderStageCreateInfo shader_stages[] = { vertex_shader, fragment_shader };
    // disable the fragment shader if gxm asks us to
    // a split draw builds one pipeline per facing: this variant carries the front or the back state of the guest
    const bool back_state = dsmodel::variant_uses_back_state(variant);
    const SceGxmFragmentProgramMode face_fragment_mode = back_state ? record.back_side_fragment_program_mode : record.front_side_fragment_program_mode;
    const bool is_fragment_disabled = face_fragment_mode == SCE_GXM_FRAGMENT_PROGRAM_DISABLED || gxm_fragment_shader->has_no_effect();
    const uint32_t shader_stage_count = is_fragment_disabled ? 1U : 2U;
    if (!vertex_shader.module || (!is_fragment_disabled && !fragment_shader.module)) {
        // a shader could not be generated: fail this pipeline instead of passing a null module to the driver
        LOG_ERROR("Pipeline skipped: shader module unavailable");
        return nullptr;
    }

    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{
        .topology = translate_primitive(type)
    };

    const bool two_sided = (record.two_sided == SCE_GXM_TWO_SIDED_ENABLED);

    SceGxmPolygonMode face_polygon_mode = back_state ? record.back_polygon_mode : record.front_polygon_mode;
    vk::PolygonMode polygon_mode = translate_polygon_mode(face_polygon_mode);
    if (polygon_mode != vk::PolygonMode::eFill && !state.physical_device_features.fillModeNonSolid) {
        // line/point rasterisation needs fillModeNonSolid (enabled whenever the device has it)
        LOG_WARN_ONCE("Polygon mode line/point requested but the device has no fillModeNonSolid: drawing filled");
        polygon_mode = vk::PolygonMode::eFill;
    }
    vk::CullModeFlags cull_mode = translate_cull_mode(record.cull_mode);
    if (variant == dsmodel::PassVariant::SplitFront)
        cull_mode |= vk::CullModeFlagBits::eBack;
    else if (variant == dsmodel::PassVariant::SplitBack)
        cull_mode |= vk::CullModeFlagBits::eFront;

    const vk::PipelineRasterizationStateCreateInfo rasterizer{
        .depthClampEnable = (enable_depth_clamp && state.depth_clamp_enabled && state.physical_device_features.depthClamp) ? VK_TRUE : VK_FALSE,
        .polygonMode = polygon_mode,
        .cullMode = cull_mode,
        // front face is always counter clockwise
        .frontFace = vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = VK_TRUE,
        .lineWidth = 1.0f
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{
        .rasterizationSamples = vk::SampleCountFlagBits::e1
    };
    // depth and stencil tests are always enabled on the ps vita as there is almost no cost in doing so
    // on a tiled renderer. The tests are only off when the host format has no such aspect (device without a stencil format).
    const SceGxmDepthFunc face_depth_func = back_state ? record.back_depth_func : record.front_depth_func;
    const SceGxmDepthWriteMode face_depth_write = back_state ? record.back_depth_write_mode : record.front_depth_write_mode;
    const vk::PipelineDepthStencilStateCreateInfo ds_info{
        .depthTestEnable = ds.depth_test ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (ds.depth_test && face_depth_write == SCE_GXM_DEPTH_WRITE_ENABLED) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = ds.depth_test ? translate_depth_func(face_depth_func) : vk::CompareOp::eAlways,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = ds.stencil_test ? VK_TRUE : VK_FALSE,
        .front = convert_op_state(record.front_stencil_state_op),
        .back = convert_op_state(two_sided ? record.back_stencil_state_op : record.front_stencil_state_op)
    };

    vk::PipelineColorBlendStateCreateInfo color_blending{};
    if (support_coherent_framebuffer_fetch)
        color_blending.flags = vk::PipelineColorBlendStateCreateFlagBits::eRasterizationOrderAttachmentAccessEXT;

    const bool frag_has_no_output = static_cast<bool>(gxm_fragment_shader->program_flags & SCE_GXM_PROGRAM_FLAG_OUTPUT_UNDEFINED);
    std::array<vk::PipelineColorBlendAttachmentState, 2> blend_attachments;
    blend_attachments[1] = vk::PipelineColorBlendAttachmentState{
        .blendEnable = VK_FALSE,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
    };
    if (is_fragment_disabled || frag_has_no_output || use_shader_interlock) {
        // The write mask must be empty as the lack of a fragment shader results in undefined values
        blend_attachments[0] = vk::PipelineColorBlendAttachmentState{
            .blendEnable = VK_FALSE,
            .colorWriteMask = vk::ColorComponentFlags()
        };
        blend_attachments[1].colorWriteMask = vk::ColorComponentFlags();
    } else {
        blend_attachments[0] = fragment_program.blending;
        if (hints.color_format == SCE_GXM_COLOR_FORMAT_U8_A)
            blend_attachments[0] = translate_alpha_surface_blend(blend_attachments[0]);
    }
    color_blending.attachmentCount = with_raw_attachment ? 2 : 1;
    color_blending.pAttachments = blend_attachments.data();

    vk::PipelineLayout pipeline_layout = pipeline_layouts[vertex_program.texture_count][fragment_program.texture_count];

    // all of these can be changed at any time using the vita graphics api (like opengl)
    // Because each one can take a lot of different values, it's better to set them as dynamic
    const std::array dynamic_states = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eStencilCompareMask,
        vk::DynamicState::eStencilReference,
        vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eDepthBias,
        vk::DynamicState::eLineWidth,
    };
    vk::PipelineDynamicStateCreateInfo dynamic_info{};
    dynamic_info.setDynamicStates(dynamic_states);
    if (!state.physical_device_features.wideLines)
        dynamic_info.dynamicStateCount--;

    // we still need to specify the viewport and scissor count even though they are dynamic
    vk::PipelineViewportStateCreateInfo viewport{
        .viewportCount = 1,
        .scissorCount = 1
    };

    vk::GraphicsPipelineCreateInfo pipeline_info{
        .stageCount = shader_stage_count,
        .pStages = shader_stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &ds_info,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0
    };

    // RenderPath::DynamicRendering: no VkRenderPass, the attachment formats go through VkPipelineRenderingCreateInfo (same formats the
    // render pass objects would have been created with: retrieve_render_pass)
    vk::PipelineRenderingCreateInfo rendering_info{};
    std::array<vk::Format, 2> rendering_color_formats{};
    if (state.use_dynamic_rendering) {
        vk::Format color_format = color::translate_surface_format(record.color_base_format);
        if (record.is_gamma_corrected && color_format == vk::Format::eR8G8B8A8Unorm)
            color_format = vk::Format::eR8G8B8A8Srgb;
        rendering_color_formats = { color_format, vk::Format::eR16G16B16A16Uint };
        rendering_info.colorAttachmentCount = with_raw_attachment ? 2 : 1;
        rendering_info.pColorAttachmentFormats = rendering_color_formats.data();
        // the depth/stencil host format of this pipeline (P5: PipelineDsInfo::format instead of state.deep_stencil_use)
        const vk::Format ds_format = ds.format;
        rendering_info.depthAttachmentFormat = vkutil::format_has_depth(ds_format) ? ds_format : vk::Format::eUndefined;
        rendering_info.stencilAttachmentFormat = vkutil::format_has_stencil(ds_format) ? ds_format : vk::Format::eUndefined;
        pipeline_info.pNext = &rendering_info;
        pipeline_info.renderPass = nullptr;
    }

    // vulkan.hpp THROWS on an error result here, so a driver refusing the pipeline used to take
    // the whole emulator down before the check below could run. Some drivers (notably Mesa/Turnip)
    // also report an internal shader-compiler rejection as ErrorOutOfHostMemory rather than a
    // specific error.
    try {
        const auto result = state.device.createGraphicsPipeline(pipeline_cache, pipeline_info);
        if (result.result != vk::Result::eSuccess) {
            LOG_CRITICAL("Failed to create pipeline: {}", vk::to_string(result.result));
            return nullptr;
        }

        return result.value;
    } catch (const vk::SystemError &err) {
        LOG_CRITICAL("Failed to create pipeline: {}", err.what());
        return nullptr;
    }
}

vk::Pipeline PipelineCache::retrieve_pipeline(VKContext &context, SceGxmPrimitiveType &type, bool consider_for_async, MemState &mem, dsmodel::PassVariant variant) {
    const GxmRecordState &record = context.record;
    // get the hash of the current context
    const XXH128_hash_t record_hash = XXH3_128bits(&record, record_pipeline_len);
    uint64_t key = record_hash.low64;
    key = alpha_surface_pipeline_key(key, record.color_surface.colorFormat);

    // the blending, vertex-layout and primitive-type parts are mixed in below with a real hash (P14: they used to be XORed in,
    // so e.g. two different blending/layout pairs with the same XOR collided onto one pipeline)
    SceGxmFragmentProgram &fragment_program_gxm = *record.fragment_program.get(mem);
    const VKFragmentProgram &fragment_program = *reinterpret_cast<VKFragmentProgram *>(
        fragment_program_gxm.renderer_data.get());
    SceGxmVertexProgram &vertex_program_gxm = *record.vertex_program.get(mem);
    {
        const uint64_t parts[4] = { key, fragment_program.blending_hash, vertex_program_gxm.key_hash, static_cast<uint64_t>(type) };
        key = XXH3_64bits(parts, sizeof(parts));
        const uint64_t check_parts[5] = { record_hash.high64, parts[1], parts[2], parts[3], static_cast<uint64_t>(record.color_surface.colorFormat) };
        const uint64_t check = XXH3_64bits_withSeed(check_parts, sizeof(check_parts), 0xC2B2AE3D27D4EB4Full);
        for (int attempt = 0; attempt < 8; attempt++) {
            const auto found = pipeline_checks.find(key);
            if (found == pipeline_checks.end()) {
                pipeline_checks.emplace(key, check);
                break;
            }
            if (found->second == check)
                break;
            // same 64-bit key, different state: never hand out the other state's pipeline
            LOG_ERROR_ONCE("Pipeline key collision detected, re-keying");
            key = XXH3_64bits_withSeed(&key, sizeof(key), check);
        }
    }

    // the render pass is chosen per scene from the depth/stencil surface: its format and which tests exist change the pipeline,
    // and so does the facing variant of a draw split by facing
    const PipelineDsInfo ds_info{ context.ds_host_format, context.ds_depth_test, context.ds_stencil_test };
    key ^= (static_cast<uint64_t>(ds_info.format) * 0x9E3779B97F4A7C15ull) ^ (static_cast<uint64_t>(ds_info.depth_test) << 1) ^ (static_cast<uint64_t>(ds_info.stencil_test) << 2)
        ^ (static_cast<uint64_t>(variant) * 0xC2B2AE3D27D4EB4Full);

    // if the pipeline is in the pipeline cache, we can expect its creation time to be almost instantaneous
    bool already_in_cache = false;

    auto it = pipelines.find(key);
    if (it != pipelines.end()) {
        if (it->second != nullptr) {
            if (it->second == kPipelineCompiling)
                // pipeline is still compiling
                return nullptr;
            else if (it->second == kPipelineFailed) {
                // the driver already rejected this one, don't try again
#if ECV_ORACLE
                ecv::oracle::gpu::pipe_note(2);
#endif
                return nullptr;
            }
            else
                return it->second;
        }
        already_in_cache = true;
    } else {
        // the pipeline hash was not in the cache;
        it = pipelines.insert({ key, kPipelineCompiling }).first;
    }

    // get the correct renderpass here
    const auto vertex_source = capture_program_source(vertex_program_gxm, mem);
    const auto fragment_source = capture_program_source(fragment_program_gxm, mem);
    const SceGxmProgram *gxm_fragment_shader = fragment_source.get();
    const bool use_shader_interlock = state.features.support_shader_interlock && gxm_fragment_shader->is_frag_color_used();
    const vk::RenderPass render_pass = use_shader_interlock ? context.current_shader_interlock_pass : context.current_render_pass;
    // update the shader hints
    context.shader_hints.color_format = record.color_surface.colorFormat;
    context.shader_hints.attributes = &vertex_program_gxm.attributes;

    // note: the flag can_use_deferred_compilation is not considered here because it causes way too many false positives
    const bool sync_window = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()
        < sync_compile_until_ns.load(std::memory_order_acquire);
    const bool compile_pipeline_async = !already_in_cache && consider_for_async && use_async_compilation && !sync_window;

    if (compile_pipeline_async) {
        // create the pipeline compile request
        CompileRequest *request = new CompileRequest;
        *request = {
            .pipeline = &it->second,
            .type = type,
            .render_pass = render_pass,
            .vertex_program_gxm = &vertex_program_gxm,
            .fragment_program_gxm = &fragment_program_gxm,
            .vertex_source = vertex_source,
            .fragment_source = fragment_source,
            .hints = context.shader_hints,
            .ds = ds_info,
            .variant = variant,
            .record = record
        };
        it->second = kPipelineCompiling;

        // we must not delete these programs until the worker is done
        vertex_program_gxm.compile_threads_on.fetch_add(1, std::memory_order_relaxed);
        fragment_program_gxm.compile_threads_on.fetch_add(1, std::memory_order_relaxed);

        pipeline_compile_queue.enqueue(pipeline_compile_queue_token, request);

        return nullptr;
    } else {
        // can't wait, compile it right now
        vk::Pipeline result = compile_pipeline(type, render_pass, vertex_program_gxm, fragment_program_gxm, vertex_source, fragment_source, record, context.shader_hints, mem, ds_info, variant);

        const auto time_s = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        next_pipeline_cache_save = time_s + pipeline_cache_save_delay;

        if (!already_in_cache)
            state.shaders_count_compiled++;
        (already_in_cache ? renderer::shader_cache_stats().pipelines_cached : renderer::shader_cache_stats().pipelines_compiled)++;

#if ECV_ORACLE
        if (!result)
            ecv::oracle::gpu::pipe_note(3);
#endif
        it->second = result ? result : kPipelineFailed;

        return result;
    }
}

vk::ShaderModule PipelineCache::load_cached_shader_module(const Sha256Hash &hash) {
    if (!fs::exists(state.shaders_path) || fs::is_empty(state.shaders_path))
        return nullptr;

    Sha256Hash shader_hash;
    memcpy(shader_hash.data(), hash.data(), sizeof(Sha256Hash));
    const std::string shader_file_name = fmt::format("vk{}-{}.spv", shader::CURRENT_VERSION, hex_string(shader_hash));
    const std::vector<uint32_t> source = renderer::pre_load_shader_spirv(state.shaders_path / shader_file_name, renderer::shader_cache_key(state));

    if (source.empty())
        return nullptr;

    vk::ShaderModuleCreateInfo shader_info{
        .codeSize = sizeof(uint32_t) * source.size(),
        .pCode = source.data()
    };

    return state.device.createShaderModule(shader_info);
}

vk::ShaderModule PipelineCache::precompile_shader(const Sha256Hash &hash, bool search_first) {
    if (search_first) {
        // happens while loading the thread, no parallel access so no need for a mutex
        auto it = shaders.find(hash);
        vk::ShaderModule existing;
        if (it != shaders.end() && it->second.try_get(existing))
            return existing;
    }

    const vk::ShaderModule shader = load_cached_shader_module(hash);
    if (!shader)
        return nullptr;

    CompileGate<vk::ShaderModule> *gate;
    {
        std::lock_guard<std::mutex> guard(shaders_mutex);
        gate = &shaders.try_emplace(hash).first->second;
    }
    gate->finish(shader);

    return shader;
}
} // namespace renderer::vulkan
