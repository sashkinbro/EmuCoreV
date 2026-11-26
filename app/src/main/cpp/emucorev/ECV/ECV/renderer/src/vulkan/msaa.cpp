// EmuCoreV: real multisampling for GXM MSAA scenes (B140). Design rules in renderer/vulkan/msaa.h.
//
// VkRenderPass path, one subpass:
//   attachment 0     multisampled colour          transient, never loaded or stored; also the pass' input attachment (per-sample fetch)
//   attachment 1     multisampled raw u16 alias   (f16 surfaces with the alias) same
//   next             multisampled depth/stencil   transient, or persistent when this instance stores it / a restarted one loads it
//   next             single sample colour surface (and raw alias): resolve targets, write only
//   last             single sample depth/stencil: depth/stencil resolve target (sample zero), write only
// The first draw of every instance is the seed: one triangle that copies a sampled copy of the surface (taken just before the instance)
// into every sample, so the instance starts from the surface content like the single sample passes do.

#include <renderer/vulkan/msaa.h>

#include <renderer/vulkan/depth_stencil_model.h>
#include <renderer/vulkan/gxm_to_vulkan.h>
#include <renderer/vulkan/render_pass_policy.h>
#include <renderer/vulkan/render_pass_sync.h>
#include <renderer/vulkan/state.h>
#include <renderer/vulkan/types.h>

#include <util/log.h>

#include <vulkan/vulkan_format_traits.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

namespace renderer::vulkan::msaa {

static_assert(MAX_FRAMES_RENDERING == 3, "MsaaState keeps one seed pool per frame slot");

namespace {

constexpr uint32_t seed_vertex_spv[] = {
#include "msaa_seed_vert.inc"
};
constexpr uint32_t seed_fragment_spv[] = {
#include "msaa_seed_frag.inc"
};
constexpr uint32_t seed_raw_fragment_spv[] = {
#include "msaa_seed_raw_frag.inc"
};

// Debug A/B switches (oracle builds / host replay only, never in release). Each one is a file in the oracle flag directory
// (the parent of the vita fs root) or an entry of the ECV_MSAA_DEBUG environment variable (comma separated names):
//   dbg-no-msaa      / "off"      every scene uses 1 sample
//   dbg-msaa-no-seed / "seed"     the seed draw is skipped (the samples start undefined)
//   dbg-msaa-restart / "restart"  every multisampled pass instance is ended before its 4th draw (restart path test)
// The transient/lazy choice is forced with the GPU path flag ecv-force-path-normal-memory.
enum : uint32_t { DBG_OFF = 1, DBG_NO_SEED = 2, DBG_FORCE_RESTART = 4 };

uint32_t debug_bits(VKState &state) {
#if ECV_ORACLE || defined(ECV_MSAA_DEBUG_FLAG)
    MsaaState &ms = state.msaa_state;
    const uint64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now >= ms.debug_next_poll_ns) {
        ms.debug_next_poll_ns = now + 1000000000ull;
        uint32_t bits = 0;
        if (std::getenv("ECV_NO_MSAA") != nullptr)
            bits |= DBG_OFF;
        if (const char *env = std::getenv("ECV_MSAA_DEBUG")) {
            const std::string list = std::string(",") + env + ",";
            const auto has = [&](const char *name) { return list.find(std::string(",") + name + ",") != std::string::npos; };
            bits |= (has("off") ? DBG_OFF : 0) | (has("seed") ? DBG_NO_SEED : 0) | (has("restart") ? DBG_FORCE_RESTART : 0);
        }
        if (!state.vita_fs_path.empty()) {
            try {
                const auto dir = state.vita_fs_path.parent_path();
                bits |= (fs::exists(dir / "dbg-no-msaa") ? DBG_OFF : 0) | (fs::exists(dir / "dbg-msaa-no-seed") ? DBG_NO_SEED : 0)
                    | (fs::exists(dir / "dbg-msaa-restart") ? DBG_FORCE_RESTART : 0);
            } catch (...) {
            }
        }
        if (bits != ms.debug_bits)
            LOG_WARN("MSAA debug override changed: bits 0x{:X} (1 off, 2 no seed, 4 forced restart)", bits);
        ms.debug_bits = bits;
    }
    return ms.debug_bits;
#else
    (void)state;
    return 0;
#endif
}

uint32_t sample_count_value(vk::SampleCountFlagBits samples) {
    return static_cast<uint32_t>(samples);
}

// sample counts the device offers for a colour + depth + stencil framebuffer
uint32_t usable_counts(const GpuCaps &caps) {
    return caps.framebuffer_color_sample_counts & caps.framebuffer_depth_sample_counts & caps.framebuffer_stencil_sample_counts;
}

// requested sample count (2 or 4) clamped to what the device offers: 4 -> 2 -> 1
vk::SampleCountFlagBits choose_samples(const GpuCaps &caps, int requested) {
    const uint32_t counts = usable_counts(caps);
    if (requested >= 4 && (counts & 4))
        return vk::SampleCountFlagBits::e4;
    if (requested >= 2 && (counts & 2))
        return vk::SampleCountFlagBits::e2;
    // 2X asked but only 4 offered: more samples are never worse than the requested ones
    if (requested == 2 && (counts & 4))
        return vk::SampleCountFlagBits::e4;
    return vk::SampleCountFlagBits::e1;
}

bool is_depth_format(vk::Format format) {
    return format == vk::Format::eD16UnormS8Uint || format == vk::Format::eD24UnormS8Uint || format == vk::Format::eD32SfloatS8Uint
        || format == vk::Format::eX8D24UnormPack32 || format == vk::Format::eD16Unorm || format == vk::Format::eD32Sfloat
        || format == vk::Format::eS8Uint;
}

// can an image of this format be created with this sample count (as an attachment)
bool format_supports_samples(VKState &state, vk::Format format, vk::SampleCountFlagBits samples) {
    const bool depth = is_depth_format(format);
    const auto key = std::make_tuple(format, sample_count_value(samples), depth);
    auto &cache = state.msaa_state.format_support;
    auto it = cache.find(key);
    if (it != cache.end())
        return it->second == 2;

    bool ok = false;
    try {
        const vk::ImageUsageFlags usage = depth ? vk::ImageUsageFlagBits::eDepthStencilAttachment : vk::ImageUsageFlagBits::eColorAttachment;
        const vk::ImageFormatProperties props = state.physical_device.getImageFormatProperties(format, vk::ImageType::e2D, vk::ImageTiling::eOptimal, usage);
        ok = static_cast<bool>(props.sampleCounts & samples);
        if (ok) {
            const vk::FormatFeatureFlags needed = depth ? vk::FormatFeatureFlagBits::eDepthStencilAttachment
                                                        : (vk::FormatFeatureFlagBits::eColorAttachment);
            ok = static_cast<bool>(state.physical_device.getFormatProperties(format).optimalTilingFeatures & needed);
        }
    } catch (const vk::SystemError &) {
        ok = false;
    }
    cache[key] = ok ? 2 : 1;
    return ok;
}

vk::RenderPass create_pass(VKState &state, const MsaaPassKey &key) {
    const vk::ImageLayout ds_attachment_layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    const vk::ImageAspectFlags ds_aspects = dsmodel::host_aspects(key.ds_format);

    std::vector<vk::AttachmentDescription2> attachments;
    // multisampled colour (and raw): never loaded (the seed fills it), never stored (only the resolve leaves the tile). General layout:
    // they are the colour attachments AND the input attachments of the per-sample framebuffer fetch
    const auto ms_color = [&](vk::Format format) {
        return vk::AttachmentDescription2{
            .format = format,
            .samples = key.samples,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eUndefined,
            .finalLayout = vk::ImageLayout::eGeneral
        };
    };
    // the single sample surfaces are write only resolve targets: the resolve overwrites the whole render area
    const auto resolve_target = [&](vk::Format format) {
        return vk::AttachmentDescription2{
            .format = format,
            .samples = vk::SampleCountFlagBits::e1,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
            .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
            .initialLayout = vk::ImageLayout::eGeneral,
            .finalLayout = vk::ImageLayout::eGeneral
        };
    };

    uint32_t next = 0;
    const uint32_t ms_color_index = next++;
    attachments.push_back(ms_color(key.color_format));
    uint32_t ms_raw_index = 0;
    if (key.with_raw) {
        ms_raw_index = next++;
        attachments.push_back(ms_color(vk::Format::eR16G16B16A16Uint));
    }

    // multisampled depth/stencil: cleared or loaded at the start of the instance, stored only when a later instance / the game needs it
    const vk::AttachmentLoadOp ds_load = key.load_depth ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear;
    const vk::AttachmentStoreOp ds_store = key.store_depth ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare;
    const uint32_t ms_ds_index = next++;
    attachments.push_back(vk::AttachmentDescription2{
        .format = key.ds_format,
        .samples = key.samples,
        .loadOp = ds_load,
        .storeOp = ds_store,
        .stencilLoadOp = ds_load,
        .stencilStoreOp = ds_store,
        .initialLayout = key.load_depth ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : vk::ImageLayout::eUndefined,
        .finalLayout = key.store_depth ? vk::ImageLayout::eDepthStencilReadOnlyOptimal : ds_attachment_layout });

    const uint32_t color_1x_index = next++;
    attachments.push_back(resolve_target(key.color_format));
    uint32_t raw_1x_index = 0;
    if (key.with_raw) {
        raw_1x_index = next++;
        // integer formats resolve to sample zero: the raw bits of the alias are never averaged
        attachments.push_back(resolve_target(vk::Format::eR16G16B16A16Uint));
    }

    // the single sample depth/stencil, also write only. The cached surface is kept in read only layout between scenes (outside the
    // render area its content must survive), the scratch image of a render target without guest depth is not.
    const uint32_t ds_1x_index = next++;
    attachments.push_back(vk::AttachmentDescription2{
        .format = key.ds_format,
        .samples = vk::SampleCountFlagBits::e1,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
        .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
        .stencilStoreOp = vk::AttachmentStoreOp::eStore,
        .initialLayout = key.ds_is_scratch ? vk::ImageLayout::eUndefined : vk::ImageLayout::eDepthStencilReadOnlyOptimal,
        .finalLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal });

    const uint32_t color_count = key.with_raw ? 2U : 1U;
    const std::array<uint32_t, 2> ms_indices = { ms_color_index, ms_raw_index };
    const std::array<uint32_t, 2> single_indices = { color_1x_index, raw_1x_index };
    std::array<vk::AttachmentReference2, 2> color_refs{};
    std::array<vk::AttachmentReference2, 2> input_refs{};
    std::array<vk::AttachmentReference2, 2> resolve_refs{};
    for (uint32_t i = 0; i < color_count; i++) {
        color_refs[i] = { .attachment = ms_indices[i], .layout = vk::ImageLayout::eGeneral, .aspectMask = vk::ImageAspectFlagBits::eColor };
        input_refs[i] = color_refs[i];
        resolve_refs[i] = { .attachment = single_indices[i], .layout = vk::ImageLayout::eGeneral, .aspectMask = vk::ImageAspectFlagBits::eColor };
    }
    const vk::AttachmentReference2 ds_ref{ .attachment = ms_ds_index, .layout = ds_attachment_layout, .aspectMask = ds_aspects };
    const vk::AttachmentReference2 ds_resolve_ref{ .attachment = ds_1x_index, .layout = ds_attachment_layout, .aspectMask = ds_aspects };
    const vk::SubpassDescriptionDepthStencilResolve ds_resolve{
        .depthResolveMode = static_cast<vk::ResolveModeFlagBits>(key.depth_mode),
        .stencilResolveMode = static_cast<vk::ResolveModeFlagBits>(key.stencil_mode),
        .pDepthStencilResolveAttachment = &ds_resolve_ref
    };
    vk::SubpassDescription2 subpass{
        .pNext = &ds_resolve,
        .flags = state.pipeline_cache.uses_coherent_framebuffer_fetch() ? vk::SubpassDescriptionFlagBits::eRasterizationOrderAttachmentColorAccessEXT : vk::SubpassDescriptionFlags(),
        .pipelineBindPoint = vk::PipelineBindPoint::eGraphics
    };
    subpass.inputAttachmentCount = color_count;
    subpass.pInputAttachments = input_refs.data();
    subpass.colorAttachmentCount = color_count;
    subpass.pColorAttachments = color_refs.data();
    subpass.pResolveAttachments = resolve_refs.data();
    subpass.pDepthStencilAttachment = &ds_ref;

    // the same external dependencies as the single sample passes (render_pass_sync.h)
    const auto target_dependencies = render_target_dependencies(false);
    const auto to_dependency2 = [](const vk::SubpassDependency &d) {
        return vk::SubpassDependency2{
            .srcSubpass = d.srcSubpass,
            .dstSubpass = d.dstSubpass,
            .srcStageMask = d.srcStageMask,
            .dstStageMask = d.dstStageMask,
            .srcAccessMask = d.srcAccessMask,
            .dstAccessMask = d.dstAccessMask,
            .dependencyFlags = d.dependencyFlags
        };
    };
    std::array<vk::SubpassDependency2, 5> dependencies = {
        to_dependency2(target_dependencies[0]),
        to_dependency2(target_dependencies[1]),
        to_dependency2(target_dependencies[2]),
        // self-dependency: a pipeline barrier in the pass for programmable blending (pixels written by earlier draws -> input attachment read)
        vk::SubpassDependency2{
            .srcSubpass = 0,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
            .dstStageMask = vk::PipelineStageFlagBits::eFragmentShader,
            .srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
            .dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead,
            .dependencyFlags = vk::DependencyFlagBits::eByRegion },
        // mid-scene flush: a vertex shader writes a buffer the next draw reads as vertex input
        vk::SubpassDependency2{
            .srcSubpass = 0,
            .dstSubpass = 0,
            .srcStageMask = vk::PipelineStageFlagBits::eVertexShader,
            .dstStageMask = vk::PipelineStageFlagBits::eVertexInput,
            .srcAccessMask = vk::AccessFlagBits::eShaderWrite,
            .dstAccessMask = vk::AccessFlagBits::eVertexAttributeRead }
    };

    vk::RenderPassCreateInfo2 info{};
    info.setAttachments(attachments);
    info.setSubpasses(subpass);
    info.setDependencies(dependencies);

    try {
        // the instance is created with at most Vulkan 1.1: render pass 2 is reached through its extension
        return state.device.createRenderPass2KHR(info);
    } catch (const vk::SystemError &e) {
        LOG_ERROR("MSAA render pass creation failed: {}", e.what());
        return nullptr;
    }
}

vk::RenderPass retrieve_pass(VKState &state, const MsaaPassKey &key) {
    auto it = state.msaa_state.passes.find(key);
    if (it != state.msaa_state.passes.end())
        return it->second;
    const vk::RenderPass pass = create_pass(state, key);
    // remember a failure too, it would fail every scene again
    state.msaa_state.passes[key] = pass;
    return pass;
}

MsaaPassKey pass_key(const MsaaScene &scene, bool load_depth, bool store_depth) {
    return MsaaPassKey{ scene.format, scene.samples, scene.with_raw, load_depth, store_depth, scene.ds_is_scratch, scene.ds_format,
        scene.depth_resolve_mode, scene.stencil_resolve_mode };
}

// transient multisampled attachment (colour/raw with input attachment use, or depth/stencil): lazily allocated when the strategy picked that memory
vkutil::Image *retrieve_transient(VKState &state, std::map<std::tuple<uint32_t, uint32_t, vk::Format, uint32_t>, std::unique_ptr<vkutil::Image>> &pool,
    uint32_t width, uint32_t height, vk::Format format, vk::SampleCountFlagBits samples) {
    const auto key = std::make_tuple(width, height, format, sample_count_value(samples));
    auto it = pool.find(key);
    if (it != pool.end())
        return it->second.get();
    auto image = std::make_unique<vkutil::Image>(width, height, format);
    const vk::ImageUsageFlags usage = is_depth_format(format) ? vk::ImageUsageFlagBits::eDepthStencilAttachment
                                                             : (vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment);
    image->init_transient_attachment(usage, state.gpu_strategy.transient == TransientMemory::Lazy, samples);
    vkutil::Image *raw = image.get();
    pool[key] = std::move(image);
    return raw;
}

// one set per pass instance; another pool is created when a frame slot's pools are full
vk::DescriptorPool create_seed_pool(VKState &state) {
    const vk::DescriptorPoolSize pool_size{ .type = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 256 };
    vk::DescriptorPoolCreateInfo pool_info{ .maxSets = 128 };
    pool_info.setPoolSizes(pool_size);
    return state.device.createDescriptorPool(pool_info);
}

// descriptor set layouts, pipeline layouts, sampler, shaders and the per frame pools of the seed
bool ensure_seed_resources(VKState &state) {
    MsaaState &ms = state.msaa_state;
    if (ms.seed_pipeline_layout[0])
        return true;
    try {
        for (uint32_t raw = 0; raw < 2; raw++) {
            const std::array<vk::DescriptorSetLayoutBinding, 2> bindings = {
                vk::DescriptorSetLayoutBinding{ .binding = 0, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment },
                vk::DescriptorSetLayoutBinding{ .binding = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .descriptorCount = 1, .stageFlags = vk::ShaderStageFlagBits::eFragment }
            };
            vk::DescriptorSetLayoutCreateInfo set_info{};
            set_info.bindingCount = raw ? 2U : 1U;
            set_info.pBindings = bindings.data();
            ms.seed_set_layout[raw] = state.device.createDescriptorSetLayout(set_info);
            vk::PipelineLayoutCreateInfo layout_info{};
            layout_info.setSetLayouts(ms.seed_set_layout[raw]);
            ms.seed_pipeline_layout[raw] = state.device.createPipelineLayout(layout_info);
        }

        const vk::SamplerCreateInfo sampler_info{
            .magFilter = vk::Filter::eNearest,
            .minFilter = vk::Filter::eNearest,
            .mipmapMode = vk::SamplerMipmapMode::eNearest,
            .addressModeU = vk::SamplerAddressMode::eClampToEdge,
            .addressModeV = vk::SamplerAddressMode::eClampToEdge,
            .addressModeW = vk::SamplerAddressMode::eClampToEdge,
            .maxLod = 0.0f
        };
        ms.seed_sampler = state.device.createSampler(sampler_info);

        for (auto &pools : ms.seed_pools)
            pools.push_back(create_seed_pool(state));

        const vk::ShaderModuleCreateInfo vert_info{ .codeSize = sizeof(seed_vertex_spv), .pCode = seed_vertex_spv };
        const vk::ShaderModuleCreateInfo frag_info{ .codeSize = sizeof(seed_fragment_spv), .pCode = seed_fragment_spv };
        const vk::ShaderModuleCreateInfo raw_info{ .codeSize = sizeof(seed_raw_fragment_spv), .pCode = seed_raw_fragment_spv };
        ms.seed_vertex = state.device.createShaderModule(vert_info);
        ms.seed_fragment[0] = state.device.createShaderModule(frag_info);
        ms.seed_fragment[1] = state.device.createShaderModule(raw_info);
    } catch (const vk::SystemError &e) {
        LOG_ERROR("MSAA seed resources could not be created: {}", e.what());
        return false;
    }
    return true;
}

// the seed pipeline of a scene configuration: VkRenderPass path against an MSAA pass, dynamic rendering against the rendering formats
vk::Pipeline retrieve_seed_pipeline(VKState &state, const MsaaScene &scene, vk::RenderPass pass) {
    MsaaState &ms = state.msaa_state;
    const auto key = std::make_tuple(scene.format, sample_count_value(scene.samples), scene.with_raw, scene.ds_format, state.use_dynamic_rendering);
    auto it = ms.seed_pipelines.find(key);
    if (it != ms.seed_pipelines.end())
        return it->second;

    const uint32_t raw = scene.with_raw ? 1U : 0U;
    const std::array<vk::PipelineShaderStageCreateInfo, 2> stages = {
        vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eVertex, .module = ms.seed_vertex, .pName = "main" },
        vk::PipelineShaderStageCreateInfo{ .stage = vk::ShaderStageFlagBits::eFragment, .module = ms.seed_fragment[raw], .pName = "main" }
    };
    const vk::PipelineVertexInputStateCreateInfo vertex_input{};
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly{ .topology = vk::PrimitiveTopology::eTriangleList };
    const vk::PipelineViewportStateCreateInfo viewport{ .viewportCount = 1, .scissorCount = 1 };
    const vk::PipelineRasterizationStateCreateInfo rasterizer{
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eNone,
        .frontFace = vk::FrontFace::eCounterClockwise,
        .lineWidth = 1.0f
    };
    const vk::PipelineMultisampleStateCreateInfo multisampling{ .rasterizationSamples = scene.samples };
    // the subpass has a depth/stencil attachment: the seed neither tests nor writes it
    const vk::PipelineDepthStencilStateCreateInfo depth_stencil{
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE
    };
    const std::array<vk::PipelineColorBlendAttachmentState, 2> blend_attachments = {
        vk::PipelineColorBlendAttachmentState{ .blendEnable = VK_FALSE, .colorWriteMask = vkutil::default_color_mask },
        vk::PipelineColorBlendAttachmentState{ .blendEnable = VK_FALSE, .colorWriteMask = vkutil::default_color_mask }
    };
    vk::PipelineColorBlendStateCreateInfo color_blending{};
    // the colour blend ROAA flag is part of the pipeline's compatibility with the subpass flag
    if (state.pipeline_cache.uses_coherent_framebuffer_fetch())
        color_blending.flags = vk::PipelineColorBlendStateCreateFlagBits::eRasterizationOrderAttachmentAccessEXT;
    color_blending.attachmentCount = raw ? 2U : 1U;
    color_blending.pAttachments = blend_attachments.data();
    // the scene pipelines keep their dynamic state across this bind only if this one declares the same states as dynamic
    std::array<vk::DynamicState, 7> dynamic_states = {
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

    vk::GraphicsPipelineCreateInfo info{
        .stageCount = 2,
        .pStages = stages.data(),
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = ms.seed_pipeline_layout[raw],
        .renderPass = pass,
        .subpass = 0
    };
    // dynamic rendering: no render pass object, the attachment formats go through the rendering info
    vk::PipelineRenderingCreateInfo rendering_info{};
    const std::array<vk::Format, 2> rendering_formats = { scene.format, vk::Format::eR16G16B16A16Uint };
    if (state.use_dynamic_rendering) {
        rendering_info.colorAttachmentCount = raw ? 2U : 1U;
        rendering_info.pColorAttachmentFormats = rendering_formats.data();
        rendering_info.depthAttachmentFormat = vkutil::format_has_depth(scene.ds_format) ? scene.ds_format : vk::Format::eUndefined;
        rendering_info.stencilAttachmentFormat = vkutil::format_has_stencil(scene.ds_format) ? scene.ds_format : vk::Format::eUndefined;
        info.pNext = &rendering_info;
        info.renderPass = nullptr;
    }

    vk::Pipeline pipeline = nullptr;
    try {
        const auto result = state.device.createGraphicsPipeline(nullptr, info);
        if (result.result == vk::Result::eSuccess)
            pipeline = result.value;
    } catch (const vk::SystemError &e) {
        LOG_ERROR("MSAA seed pipeline creation failed: {}", e.what());
    }
    ms.seed_pipelines[key] = pipeline;
    return pipeline;
}

// the sampled copy of a single sample surface in this frame slot (reused across instances; the tracker orders the WAR between them)
vkutil::Image *retrieve_copy(VKState &state, size_t slot, uint32_t width, uint32_t height, vk::Format format) {
    auto &copies = state.msaa_state.seed_copies[slot];
    const auto key = std::make_tuple(width, height, format);
    auto it = copies.find(key);
    if (it != copies.end())
        return it->second.get();
    if (copies.size() >= 8) {
        for (auto &entry : copies)
            state.frame().destroy_queue.add_image(*entry.second);
        copies.clear();
    }
    auto image = std::make_unique<vkutil::Image>(width, height, format);
    image->init_image(vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
    vkutil::Image *raw = image.get();
    copies.emplace(key, std::move(image));
    return raw;
}

// copy the render area of a single sample surface into its sampled copy (outside a render pass)
void record_surface_copy(vk::CommandBuffer cmd, vkutil::Image &source, vkutil::Image &copy, uint32_t width, uint32_t height) {
    source.transition_to(cmd, vkutil::ImageLayout::TransferSrc);
    copy.transition_to_discard(cmd, vkutil::ImageLayout::TransferDst);
    const vk::ImageCopy region{
        .srcSubresource = vkutil::color_subresource_layer,
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = vkutil::color_subresource_layer,
        .dstOffset = { 0, 0, 0 },
        .extent = { width, height, 1 }
    };
    cmd.copyImage(source.image, vk::ImageLayout::eTransferSrcOptimal, copy.image, vk::ImageLayout::eTransferDstOptimal, region);
    copy.transition_to(cmd, vkutil::ImageLayout::SampledImage);
    source.transition_to(cmd, vkutil::ImageLayout::ColorAttachmentReadWrite);
}

// the multisampled depth/stencil an instance renders into: persistent (stored by an instance, loaded by a restarted one or by the game)
// or transient. Returns the image; `load` is cleared when a persistent image has nothing valid to load.
vkutil::Image *select_depth(VKContext &context, bool &load, bool store) {
    VKState &state = context.state;
    MsaaState &ms = state.msaa_state;
    MsaaScene &scene = context.ms_scene;
    vkutil::Image *ds_image = context.current_ds_image;
    if (!load && !store)
        return retrieve_transient(state, ms.transient_depth, scene.width, scene.height, scene.ds_format, scene.samples);

    MsaaDepth &pd = ms.depth[ds_image];
    const bool reusable = pd.image && pd.samples == scene.samples && pd.width == scene.width && pd.height == scene.height
        && pd.resolved_image == ds_image->image && pd.image->format == scene.ds_format;
    if (!reusable) {
        if (pd.image)
            state.frame().destroy_queue.add_image(*pd.image);
        pd.image = std::make_unique<vkutil::Image>(scene.width, scene.height, scene.ds_format);
        // persistent across instances (stored and loaded): never transient
        pd.image->init_transient_attachment(vk::ImageUsageFlagBits::eDepthStencilAttachment, false, scene.samples);
        pd.samples = scene.samples;
        pd.width = scene.width;
        pd.height = scene.height;
        pd.valid = false;
    }
    pd.resolved_image = ds_image->image;
    if (load && !pd.valid)
        load = false; // nothing was stored (the surface was recreated): start from the initial content
    return pd.image.get();
}

void note_depth_stored(VKContext &context, bool persistent_used, bool store) {
    if (!persistent_used)
        return;
    auto it = context.state.msaa_state.depth.find(context.current_ds_image);
    if (it != context.state.msaa_state.depth.end())
        it->second.valid = store;
}

} // namespace

void init(VKState &state) {
    MsaaState &ms = state.msaa_state;
    const GpuCaps &caps = state.gpu_caps;
    const uint32_t counts = usable_counts(caps) & ~1u;
    // in-pass resolve: render pass 2 (VkRenderPass path) or dynamic rendering (resolve in the rendering attachments); a depth/stencil
    // resolve with SAMPLE_ZERO so that the single sample depth/stencil is always coherent; sample rate shading + input attachments for the
    // per-sample framebuffer fetch (the fetching shaders read the multisampled attachment at gl_SampleID)
    const DsResolvePlan resolve = plan_ds_resolve(caps, (counts & 4) ? 4 : 2, true);
    const bool path_ok = state.use_dynamic_rendering ? caps.dynamic_rendering : caps.render_pass2;
    const bool fetch_ok = state.features.direct_fragcolor && caps.sample_rate_shading && !state.features.support_shader_interlock;
    ms.enabled = path_ok && counts != 0 && fetch_ok && resolve.kind == DsResolveKind::ResolveAttachment;
    LOG_WARN("MSAA: {} (path={} sample counts mask 0x{:X} ds_resolve={} sample_rate_shading={} fetch=input-attachment transient={})",
        ms.enabled ? "available" : "unavailable, every scene stays at 1 sample", state.use_dynamic_rendering ? "dynamic-rendering" : "render-pass", counts,
        resolve.kind == DsResolveKind::ResolveAttachment ? "attachment" : "none", caps.sample_rate_shading, to_string(state.gpu_strategy.transient));
}

MsaaPlan plan(VKContext &context, const VKRenderTarget &target, bool restoring, bool colour_surface_present,
    vk::Format colour_format, bool colour_has_raw_alias, bool msaa_expanded,
    const SceGxmDepthStencilSurface *ds_surface, bool depth_load_requested, bool game_force_load, bool game_force_store) {
    (void)game_force_load;
    (void)game_force_store;
    MsaaPlan result;
    VKState &state = context.state;
    MsaaState &ms = state.msaa_state;

    // config override: 0 follows the GXM multisample mode of the render target, 1 forces single sample, 2/4 force that count for every scene
    const int forced = state.msaa_samples_override;
    // the start-up self-test can still turn the fetch path into the copy-snapshot rung (a single sample mechanism) after msaa::init
    if (!ms.enabled || forced == 1 || state.use_copy_snapshot)
        return result;
    if (forced == 0 && target.multisample_mode == SCE_GXM_MULTISAMPLE_NONE)
        return result;
    if (debug_bits(state) & DBG_OFF)
        return result;

    const char *why = nullptr;
    // checkpoint continuations rebuild a scene from the single sample content
    if (restoring)
        why = "restored scene";
    // MSAA without the downscale bit is emulated with a 2x2 larger target (SSAA) by set_context
    else if (msaa_expanded || (forced == 0 && !context.record.color_surface.downscale))
        why = "no downscale bit";
    else if (!colour_surface_present)
        why = "no colour surface";
    else if (target.has_macroblock_sync)
        why = "macroblock sync";
    if (why) {
        LOG_WARN_ONCE("MSAA scene stays at 1 sample: {}", why);
        return result;
    }

    const int requested = forced > 0 ? forced : (target.multisample_mode == SCE_GXM_MULTISAMPLE_4X ? 4 : 2);
    const vk::SampleCountFlagBits samples = choose_samples(state.gpu_caps, requested);
    if (samples == vk::SampleCountFlagBits::e1)
        return result;
    const bool with_raw = colour_has_raw_alias && state.features.preserve_f16_nan_as_u16 && colour_format == vk::Format::eR16G16B16A16Sfloat;
    const vk::Format ds_format = context.ds_host_format != vk::Format::eUndefined ? context.ds_host_format : state.deep_stencil_use;
    if (!format_supports_samples(state, colour_format, samples) || !format_supports_samples(state, ds_format, samples)
        || (with_raw && !format_supports_samples(state, vk::Format::eR16G16B16A16Uint, samples))) {
        LOG_WARN_ONCE("MSAA scene stays at 1 sample: colour format {} or depth format {} not multisampled on this device",
            vk::to_string(colour_format), vk::to_string(ds_format));
        return result;
    }
    const DsResolvePlan resolve = plan_ds_resolve(state.gpu_caps, sample_count_value(samples), dsmodel::host_has_stencil(ds_format));
    if (resolve.kind != DsResolveKind::ResolveAttachment) {
        LOG_WARN_ONCE("MSAA scene stays at 1 sample: no depth/stencil resolve attachment for {}", vk::to_string(ds_format));
        return result;
    }

    result.ds_format = ds_format;
    result.samples = samples;
    result.with_raw = with_raw;
    result.ds_is_scratch = ds_surface == nullptr;
    result.depth_resolve_mode = resolve.depth_mode;
    result.stencil_resolve_mode = resolve.stencil_mode;

    if (ds_surface != nullptr && depth_load_requested) {
        // The game wants the content of its depth surface. A multisampled scene can only load what a previous multisampled scene stored.
        bool would_remake = false;
        DepthStencilSurfaceCacheInfo *entry = state.surface_cache.find_depth_stencil_entry(*ds_surface,
            static_cast<uint32_t>(target.width), static_cast<uint32_t>(target.height), would_remake);
        auto it = entry ? ms.depth.find(&entry->texture) : ms.depth.end();
        const bool ms_content = it != ms.depth.end() && it->second.valid && it->second.image && !would_remake
            && it->second.samples == samples && it->second.resolved_image == entry->texture.image;
        if (ms_content) {
            result.load_depth = true;
        } else if (entry == nullptr || !entry->depth_content_stored || would_remake) {
            // nothing meaningful is stored in the surface memory (never written, a scene that did not store it, or the cached
            // image is about to be recreated cleared): what hardware would load is the surface's initial content
            result.clear_to_defaults = true;
        } else {
            // the single sample surface holds content written by a single sample scene or by the CPU: it cannot seed a multisampled depth/stencil
            LOG_WARN_ONCE("MSAA scene stays at 1 sample: depth/stencil is loaded from single sample content");
            result = MsaaPlan{};
            return result;
        }
    }

    result.use = true;
    return result;
}

void note_single_sample_scene(VKContext &context, const SceGxmDepthStencilSurface *ds_surface) {
    VKState &state = context.state;
    state.msaa_state.scenes_single_sample++;
    if (state.msaa_state.depth.empty() || ds_surface == nullptr)
        return;
    bool would_remake = false;
    const DepthStencilSurfaceCacheInfo *entry = state.surface_cache.find_depth_stencil_entry(*ds_surface, 0, 0, would_remake);
    if (entry == nullptr)
        return;
    auto it = state.msaa_state.depth.find(&entry->texture);
    if (it != state.msaa_state.depth.end())
        it->second.valid = false;
}

void activate(VKContext &context, const MsaaPlan &plan, vk::ImageView colour_view, vk::ImageView ds_view, uint32_t fb_width, uint32_t fb_height) {
    VKState &state = context.state;
    MsaaState &ms = state.msaa_state;
    context.ms_scene = MsaaScene{};

    vkutil::Image *base_image = context.current_color_base_image;
    vkutil::Image *ds_image = context.current_ds_image;
    if (!plan.use || fb_width == 0 || fb_height == 0 || !colour_view || !ds_view || !base_image || !base_image->image || !ds_image || !ds_image->image)
        return;
    // the surface copy is a plain image copy: same texel size, big enough
    const vk::Format colour_format = context.current_color_format;
    if (vk::blockSize(base_image->format) != vk::blockSize(colour_format) || base_image->width < fb_width || base_image->height < fb_height)
        return;
    if (plan.with_raw && (!context.current_color_raw_image || !context.current_color_raw_image->image || context.current_color_raw_image->width < fb_width
                             || context.current_color_raw_image->height < fb_height))
        return;
    if (!ensure_seed_resources(state))
        return;

    // a render target that changes size often would pile up images: start over once the pools are large (never between the lookups below)
    for (auto *pool : { &ms.color_images, &ms.transient_depth }) {
        if (pool->size() >= 8) {
            for (auto &entry : *pool)
                state.frame().destroy_queue.add_image(*entry.second);
            pool->clear();
        }
    }

    MsaaScene scene;
    scene.active = true;
    scene.samples = plan.samples;
    scene.with_raw = plan.with_raw;
    scene.ds_format = plan.ds_format;
    scene.depth_resolve_mode = plan.depth_resolve_mode;
    scene.stencil_resolve_mode = plan.stencil_resolve_mode;
    scene.load_depth = plan.load_depth;
    scene.clear_to_defaults = plan.clear_to_defaults;
    scene.ds_is_scratch = plan.ds_is_scratch;
    scene.width = fb_width;
    scene.height = fb_height;
    scene.format = colour_format;

    try {
        scene.color_ms = retrieve_transient(state, ms.color_images, fb_width, fb_height, colour_format, plan.samples);
        if (plan.with_raw)
            scene.raw_ms = retrieve_transient(state, ms.color_images, fb_width, fb_height, vk::Format::eR16G16B16A16Uint, plan.samples);
    } catch (const vk::SystemError &e) {
        LOG_ERROR("MSAA attachments could not be created ({}), scene stays at 1 sample", e.what());
        return;
    }
    if (!scene.color_ms || !scene.color_ms->view || (plan.with_raw && (!scene.raw_ms || !scene.raw_ms->view)))
        return;

    // the pass pipelines and secondary command buffers are created against (VkRenderPass path): any variant is compatible
    vk::RenderPass pass{};
    if (!state.use_dynamic_rendering) {
        pass = retrieve_pass(state, pass_key(scene, false, false));
        if (!pass)
            return;
    }
    scene.seed_pipeline = retrieve_seed_pipeline(state, scene, pass);
    if (!scene.seed_pipeline)
        return;

    context.ms_scene = scene;
    context.current_render_pass = pass;
    ms.scenes_multisampled++;

    // warning level so the lines reach the device log: the first scenes, then a periodic count
    if (ms.scenes_multisampled <= 8 || (ms.scenes_multisampled % 1024) == 0)
        LOG_WARN("MSAA scene #{}: {}x samples, {}x{}, colour {}{}, depth {} ({}{}), {} instances, depth stores {} (avoided {})", ms.scenes_multisampled,
            sample_count_value(plan.samples), fb_width, fb_height, vk::to_string(colour_format), plan.with_raw ? "+raw" : "", vk::to_string(plan.ds_format),
            plan.load_depth ? "load" : "clear", plan.clear_to_defaults ? ", surface defaults" : "", ms.instances_emitted, ms.depth_stores, ms.depth_stores_avoided);
}

void begin_instance(VKContext &context) {
    MsaaScene &scene = context.ms_scene;
    if (!scene.active)
        return;
    VKState &state = context.state;
    MsaaState &ms = state.msaa_state;
    const size_t slot = static_cast<size_t>(state.current_frame_idx) % ms.seed_pools.size();
    const uint32_t raw = scene.with_raw ? 1U : 0U;

    // the sampled copies this instance's seed reads and the descriptor of them
    try {
        scene.seed_color_copy = retrieve_copy(state, slot, scene.width, scene.height, scene.format);
        scene.seed_raw_copy = scene.with_raw ? retrieve_copy(state, slot, scene.width, scene.height, vk::Format::eR16G16B16A16Uint) : nullptr;
        vk::DescriptorSetAllocateInfo alloc_info{ .descriptorPool = ms.seed_pools[slot].back() };
        alloc_info.setSetLayouts(ms.seed_set_layout[raw]);
        try {
            scene.seed_set = state.device.allocateDescriptorSets(alloc_info)[0];
        } catch (const vk::OutOfPoolMemoryError &) {
            ms.seed_pools[slot].push_back(create_seed_pool(state));
            alloc_info.descriptorPool = ms.seed_pools[slot].back();
            scene.seed_set = state.device.allocateDescriptorSets(alloc_info)[0];
        } catch (const vk::FragmentedPoolError &) {
            ms.seed_pools[slot].push_back(create_seed_pool(state));
            alloc_info.descriptorPool = ms.seed_pools[slot].back();
            scene.seed_set = state.device.allocateDescriptorSets(alloc_info)[0];
        }
    } catch (const vk::SystemError &e) {
        // the scene would otherwise be undefined, so say so loudly
        LOG_ERROR("MSAA seed descriptors exhausted ({}): the instance starts from undefined samples", e.what());
        scene.seed_set = nullptr;
        return;
    }
    std::array<vk::DescriptorImageInfo, 2> images = {
        vk::DescriptorImageInfo{ .sampler = ms.seed_sampler, .imageView = scene.seed_color_copy->view, .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal },
        vk::DescriptorImageInfo{ .sampler = ms.seed_sampler, .imageView = scene.seed_raw_copy ? scene.seed_raw_copy->view : vk::ImageView{},
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal }
    };
    std::array<vk::WriteDescriptorSet, 2> writes = {
        vk::WriteDescriptorSet{ .dstSet = scene.seed_set, .dstBinding = 0, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &images[0] },
        vk::WriteDescriptorSet{ .dstSet = scene.seed_set, .dstBinding = 1, .descriptorCount = 1, .descriptorType = vk::DescriptorType::eCombinedImageSampler, .pImageInfo = &images[1] }
    };
    state.device.updateDescriptorSets(scene.with_raw ? 2U : 1U, writes.data(), 0, nullptr);

    // the first draw of the instance: samples <- sampled copy of the surface
    if (!(debug_bits(state) & DBG_NO_SEED)) {
        vk::CommandBuffer cmd = context.render_cmd;
        const vk::Rect2D area = context.pass_area;
        const vk::Viewport viewport{
            .x = static_cast<float>(area.offset.x),
            .y = static_cast<float>(area.offset.y),
            .width = static_cast<float>(area.extent.width),
            .height = static_cast<float>(area.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        cmd.setViewport(0, viewport);
        cmd.setScissor(0, area);
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, scene.seed_pipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, ms.seed_pipeline_layout[raw], 0, scene.seed_set, {});
        cmd.draw(3, 1, 0, 0);
        // the scene's own viewport and scissor; the pipeline and descriptors of the draws are bound again (refresh_pipeline)
        cmd.setViewport(0, context.viewport);
        cmd.setScissor(0, context.scissor);
    }
    scene.draw_count = 0;
}

void record_copies(VKContext &context) {
    MsaaScene &scene = context.ms_scene;
    if (!scene.active || !scene.seed_color_copy)
        return;
    record_surface_copy(context.render_cmd, *context.current_color_base_image, *scene.seed_color_copy, scene.width, scene.height);
    if (scene.with_raw && scene.seed_raw_copy)
        record_surface_copy(context.render_cmd, *context.current_color_raw_image, *scene.seed_raw_copy, scene.width, scene.height);
}

namespace {

// what the instance does with the multisampled depth/stencil
struct DepthOps {
    bool load = false;
    bool store = false;
    vk::ClearDepthStencilValue clear{};
    vkutil::Image *image = nullptr;
};

DepthOps choose_depth_ops(VKContext &context, const MsaaInstance &instance) {
    MsaaScene &scene = context.ms_scene;
    MsaaState &ms = context.state.msaa_state;
    DepthOps ops;
    const bool first = scene.instances == 0;
    // a restarted instance loads what the previous one stored; the first one loads only when the game loads its depth surface
    ops.load = first ? scene.load_depth : true;
    // stored only when it is needed again: another instance follows, or the game keeps the depth in memory (a later scene may load it)
    // (render_pass_policy.h: the multisampled copy is not backed by a guest surface, so only the game's store or a following instance keep it)
    ops.store = derive_depth_store({ instance.guest_store, false, instance.followed });
    ops.clear = instance.ds_clear;
    if (first && scene.clear_to_defaults)
        ops.clear = vk::ClearDepthStencilValue{ .depth = 1.0f, .stencil = 0 };
    const bool wanted_load = ops.load;
    ops.image = select_depth(context, ops.load, ops.store);
    if (wanted_load && !ops.load && !first)
        LOG_WARN_ONCE("MSAA: a restarted pass instance found no stored multisampled depth/stencil (cleared)");
    (ops.store ? ms.depth_stores : ms.depth_stores_avoided)++;
    return ops;
}

} // namespace

void record_render_pass(VKContext &context, const MsaaInstance &instance) {
    VKState &state = context.state;
    MsaaScene &scene = context.ms_scene;
    const DepthOps ops = choose_depth_ops(context, instance);
    const bool persistent = ops.load || ops.store;
    const vk::RenderPass pass = retrieve_pass(state, pass_key(scene, ops.load, ops.store));

    std::array<vk::ImageView, 6> views{};
    uint32_t count = 0;
    views[count++] = scene.color_ms->view;
    if (scene.with_raw)
        views[count++] = scene.raw_ms->view;
    const uint32_t depth_index = count;
    views[count++] = ops.image->view;
    views[count++] = context.current_color_view;
    if (scene.with_raw)
        views[count++] = context.current_color_raw_view;
    views[count++] = context.current_ds_view;
    vk::FramebufferCreateInfo fb_info{ .renderPass = pass, .width = scene.width, .height = scene.height, .layers = 1 };
    fb_info.attachmentCount = count;
    fb_info.pAttachments = views.data();
    vk::Framebuffer framebuffer = state.device.createFramebuffer(fb_info);
    // the pass is recorded now and submitted later: destroyed once this frame slot has been waited on
    vk::Framebuffer to_destroy = framebuffer;
    state.frame().destroy_queue.add(to_destroy);

    std::array<vk::ClearValue, 6> clear_values{};
    clear_values[depth_index].depthStencil = ops.clear;
    vk::RenderPassBeginInfo info{ .renderPass = pass, .framebuffer = framebuffer, .renderArea = instance.area };
    info.setClearValues(clear_values);
    context.render_cmd.beginRenderPass(info, vk::SubpassContents::eSecondaryCommandBuffers);
    context.render_cmd.executeCommands(context.pass_secondary);
    context.render_cmd.endRenderPass();

    note_depth_stored(context, persistent, ops.store);
    scene.instances++;
    state.msaa_state.instances_emitted++;
}

void record_rendering(VKContext &context, const MsaaInstance &instance) {
    VKState &state = context.state;
    MsaaScene &scene = context.ms_scene;
    const DepthOps ops = choose_depth_ops(context, instance);
    const bool persistent = ops.load || ops.store;
    const bool has_depth = vkutil::format_has_depth(scene.ds_format);
    const bool has_stencil = vkutil::format_has_stencil(scene.ds_format);

    // Dynamic rendering has no render pass dependencies and no initial/final layouts: the multisampled images go through the sync tracker like
    // every other attachment. The colour (and raw) samples are never loaded (discard); the depth/stencil is loaded only by a restarted instance.
    {
        namespace sync = vkutil::sync;
        sync::Batch batch;
        const sync::Stage color_stages = sync::S::eColorAttachmentOutput | sync::S::eFragmentShader;
        const sync::Access color_access = sync::A::eColorAttachmentRead | sync::A::eColorAttachmentWrite | sync::A::eInputAttachmentRead;
        batch.use_image(scene.color_ms->sync, scene.color_ms->image, vkutil::color_subresource_range, color_stages, color_access, vk::ImageLayout::eGeneral, true);
        if (scene.with_raw)
            batch.use_image(scene.raw_ms->sync, scene.raw_ms->image, vkutil::color_subresource_range, color_stages, color_access, vk::ImageLayout::eGeneral, true);
        batch.use_image(ops.image->sync, ops.image->image, vkutil::aspect_clamped_range(scene.ds_format, vkutil::ds_subresource_range),
            sync::S::eEarlyFragmentTests | sync::S::eLateFragmentTests, sync::A::eDepthStencilAttachmentRead | sync::A::eDepthStencilAttachmentWrite,
            vk::ImageLayout::eDepthStencilAttachmentOptimal, !ops.load);
        batch.flush(context.render_cmd);
    }

    std::array<vk::RenderingAttachmentInfo, 2> colors{};
    // float colour: the average of the samples; the raw alias is integer data: sample zero, bit exact
    colors[0] = vk::RenderingAttachmentInfo{
        .imageView = scene.color_ms->view,
        .imageLayout = vk::ImageLayout::eGeneral,
        .resolveMode = vk::ResolveModeFlagBits::eAverage,
        .resolveImageView = context.current_color_view,
        .resolveImageLayout = vk::ImageLayout::eGeneral,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eDontCare,
    };
    if (scene.with_raw) {
        colors[1] = vk::RenderingAttachmentInfo{
            .imageView = scene.raw_ms->view,
            .imageLayout = vk::ImageLayout::eGeneral,
            .resolveMode = vk::ResolveModeFlagBits::eSampleZero,
            .resolveImageView = context.current_color_raw_view,
            .resolveImageLayout = vk::ImageLayout::eGeneral,
            .loadOp = vk::AttachmentLoadOp::eDontCare,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
        };
    }
    const auto ds_attachment = [&](bool wanted) {
        vk::RenderingAttachmentInfo a{
            .imageView = ops.image->view,
            .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .resolveMode = static_cast<vk::ResolveModeFlagBits>(wanted ? scene.depth_resolve_mode : 0),
            .resolveImageView = context.current_ds_view,
            .resolveImageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
            .loadOp = ops.load ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear,
            .storeOp = ops.store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
        };
        a.clearValue.depthStencil = ops.clear;
        return a;
    };
    vk::RenderingAttachmentInfo depth = ds_attachment(true);
    vk::RenderingAttachmentInfo stencil = ds_attachment(false);
    stencil.resolveMode = static_cast<vk::ResolveModeFlagBits>(scene.stencil_resolve_mode);

    vk::RenderingInfo rendering{
        .flags = vk::RenderingFlagBits::eContentsSecondaryCommandBuffers,
        .renderArea = instance.area,
        .layerCount = 1,
        .colorAttachmentCount = scene.with_raw ? 2u : 1u,
        .pColorAttachments = colors.data(),
        .pDepthAttachment = has_depth ? &depth : nullptr,
        .pStencilAttachment = has_stencil ? &stencil : nullptr,
    };
    context.render_cmd.beginRenderingKHR(rendering);
    context.render_cmd.executeCommands(context.pass_secondary);
    context.render_cmd.endRenderingKHR();

    note_depth_stored(context, persistent, ops.store);
    scene.instances++;
    state.msaa_state.instances_emitted++;
}

vk::ImageView fetch_color_view(const VKContext &context) {
    return context.ms_scene.active ? context.ms_scene.color_ms->view : context.current_color_view;
}

vk::ImageView fetch_raw_view(const VKContext &context) {
    return context.ms_scene.active && context.ms_scene.raw_ms ? context.ms_scene.raw_ms->view : context.current_color_raw_view;
}

bool debug_force_restart(VKContext &context) {
#if ECV_ORACLE || defined(ECV_MSAA_DEBUG_FLAG)
    MsaaScene &scene = context.ms_scene;
    if (!scene.active || !(debug_bits(context.state) & DBG_FORCE_RESTART))
        return false;
    return ++scene.draw_count == 4;
#else
    (void)context;
    return false;
#endif
}

void begin_frame(VKState &state, int frame_index) {
    MsaaState &ms = state.msaa_state;
    if (!ms.seed_pipeline_layout[0] || frame_index < 0 || static_cast<size_t>(frame_index) >= ms.seed_pools.size())
        return;
    // the GPU is done with this frame slot (its fences were waited on): the sets are free again, the copies stay for reuse
    for (vk::DescriptorPool pool : ms.seed_pools[frame_index])
        state.device.resetDescriptorPool(pool);
}

void cleanup(VKState &state) {
    MsaaState &ms = state.msaa_state;
    if (!state.device)
        return;
    for (auto &[key, pipeline] : ms.seed_pipelines)
        state.device.destroyPipeline(pipeline);
    ms.seed_pipelines.clear();
    for (auto &[key, pass] : ms.passes)
        state.device.destroyRenderPass(pass);
    ms.passes.clear();
    for (auto &pools : ms.seed_pools) {
        for (vk::DescriptorPool pool : pools)
            state.device.destroyDescriptorPool(pool);
        pools.clear();
    }
    if (ms.seed_sampler)
        state.device.destroySampler(ms.seed_sampler);
    for (auto &layout : ms.seed_pipeline_layout) {
        if (layout)
            state.device.destroyPipelineLayout(layout);
        layout = nullptr;
    }
    for (auto &layout : ms.seed_set_layout) {
        if (layout)
            state.device.destroyDescriptorSetLayout(layout);
        layout = nullptr;
    }
    if (ms.seed_vertex)
        state.device.destroyShaderModule(ms.seed_vertex);
    for (auto &module : ms.seed_fragment) {
        if (module)
            state.device.destroyShaderModule(module);
        module = nullptr;
    }
    ms.seed_sampler = nullptr;
    ms.seed_vertex = nullptr;
    for (auto &copies : ms.seed_copies)
        copies.clear();
    ms.color_images.clear();
    ms.transient_depth.clear();
    ms.depth.clear();
    LOG_INFO("MSAA: {} multisampled scenes, {} single sample scenes, {} pass instances, {} multisampled depth stores ({} avoided)", ms.scenes_multisampled,
        ms.scenes_single_sample, ms.instances_emitted, ms.depth_stores, ms.depth_stores_avoided);
}

} // namespace renderer::vulkan::msaa
