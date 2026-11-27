#include <renderer/vulkan/gpu_caps_probe.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace renderer::vulkan {

static std::string g_paths_line;
void publish_paths_line(const std::string &line) {
    g_paths_line = line;
}
const char *paths_line_c() {
    return g_paths_line.c_str();
}

static bool has_extension(const std::vector<vk::ExtensionProperties> &exts, const char *name) {
    for (const auto &e : exts)
        if (std::strcmp(e.extensionName.data(), name) == 0)
            return true;
    return false;
}

GpuCaps probe_gpu_caps(vk::PhysicalDevice device, const std::vector<vk::ExtensionProperties> &exts, bool has_features2_khr) {
    GpuCaps c;
    const vk::PhysicalDeviceProperties props = device.getProperties();
    c.identity.vendor_id = props.vendorID;
    c.identity.driver_version = props.driverVersion;
    const auto &lim = props.limits;
    c.sample_rate_shading = static_cast<bool>(device.getFeatures().sampleRateShading);
    c.framebuffer_color_sample_counts = static_cast<uint32_t>(static_cast<VkSampleCountFlags>(lim.framebufferColorSampleCounts));
    c.framebuffer_depth_sample_counts = static_cast<uint32_t>(static_cast<VkSampleCountFlags>(lim.framebufferDepthSampleCounts));
    c.framebuffer_stencil_sample_counts = static_cast<uint32_t>(static_cast<VkSampleCountFlags>(lim.framebufferStencilSampleCounts));
    c.max_bound_descriptor_sets = lim.maxBoundDescriptorSets;
    {
        const vk::PhysicalDeviceFeatures features = device.getFeatures();
        c.wide_lines = features.wideLines;
        c.large_points = features.largePoints;
        c.fill_mode_non_solid = features.fillModeNonSolid;
        c.occlusion_query_precise = features.occlusionQueryPrecise;
        c.depth_clamp = features.depthClamp;
        c.shader_clip_distance = features.shaderClipDistance;
        c.vertex_stores_and_atomics = features.vertexPipelineStoresAndAtomics;
        c.fragment_stores_and_atomics = features.fragmentStoresAndAtomics;
        c.max_point_size = lim.pointSizeRange[1];
    }
    c.max_per_stage_input_attachments = lim.maxPerStageDescriptorInputAttachments;
    c.max_per_stage_sampled_images = lim.maxPerStageDescriptorSampledImages;
    c.max_descriptor_set_sampled_images = lim.maxDescriptorSetSampledImages;

    const vk::PhysicalDeviceMemoryProperties mem = device.getMemoryProperties();
    for (uint32_t i = 0; i < mem.memoryTypeCount; i++) {
        const vk::MemoryPropertyFlags f = mem.memoryTypes[i].propertyFlags;
        if ((f & vk::MemoryPropertyFlagBits::eLazilyAllocated) && (f & vk::MemoryPropertyFlagBits::eDeviceLocal))
            c.lazily_allocated_memory = true;
        if ((f & vk::MemoryPropertyFlagBits::eHostVisible) && (f & vk::MemoryPropertyFlagBits::eHostCoherent)) {
            c.host_visible_coherent = true;
            if (f & vk::MemoryPropertyFlagBits::eHostCached)
                c.host_visible_coherent_cached = true;
        }
    }

    // the instance is created with at most Vulkan 1.1: everything newer is reached through its extension
    const bool has_props2 = has_features2_khr;
    if (has_props2 && has_extension(exts, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME)) {
        const auto p = device.getProperties2KHR<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDriverProperties>();
        c.identity.driver_id = static_cast<uint32_t>(p.get<vk::PhysicalDeviceDriverProperties>().driverID);
    }
    if (has_props2 && has_extension(exts, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)) {
        const auto f = device.getFeatures2KHR<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        c.extended_dynamic_state = static_cast<bool>(f.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState);
    }
    c.render_pass2 = has_extension(exts, VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);
    if (has_props2 && has_extension(exts, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME)) {
        const auto f = device.getFeatures2KHR<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceTimelineSemaphoreFeatures>();
        c.timeline_semaphore = static_cast<bool>(f.get<vk::PhysicalDeviceTimelineSemaphoreFeatures>().timelineSemaphore);
    }
    if (has_props2 && has_extension(exts, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        const auto f = device.getFeatures2KHR<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceDynamicRenderingFeatures>();
        c.dynamic_rendering = static_cast<bool>(f.get<vk::PhysicalDeviceDynamicRenderingFeatures>().dynamicRendering);
    }
    if (has_props2 && c.dynamic_rendering && has_extension(exts, VK_KHR_DYNAMIC_RENDERING_LOCAL_READ_EXTENSION_NAME)) {
        const auto f = device.getFeatures2KHR<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceDynamicRenderingLocalReadFeatures>();
        c.dynamic_rendering_local_read = static_cast<bool>(f.get<vk::PhysicalDeviceDynamicRenderingLocalReadFeatures>().dynamicRenderingLocalRead);
    }
    if (has_props2 && has_extension(exts, VK_EXT_SHADER_TILE_IMAGE_EXTENSION_NAME)) {
        const auto f = device.getFeatures2KHR<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceShaderTileImageFeaturesEXT>();
        c.shader_tile_image = static_cast<bool>(f.get<vk::PhysicalDeviceShaderTileImageFeaturesEXT>().shaderTileImageColorReadAccess);
    }
    if (has_props2 && has_extension(exts, VK_KHR_DEPTH_STENCIL_RESOLVE_EXTENSION_NAME)) {
        const auto p = device.getProperties2KHR<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDepthStencilResolveProperties>();
        const auto &r = p.get<vk::PhysicalDeviceDepthStencilResolveProperties>();
        c.depth_stencil_resolve = static_cast<VkResolveModeFlags>(r.supportedDepthResolveModes) != 0;
        c.ds_resolve_depth_modes = static_cast<VkResolveModeFlags>(r.supportedDepthResolveModes);
        c.ds_resolve_stencil_modes = static_cast<VkResolveModeFlags>(r.supportedStencilResolveModes);
        c.ds_resolve_independent = static_cast<bool>(r.independentResolve);
        c.ds_resolve_independent_none = static_cast<bool>(r.independentResolveNone);
    }
    return c;
}

ForcedPaths read_forced_paths(const std::string &flag_dir) {
    ForcedPaths f;
#ifndef NDEBUG
    // host/replay/lab: ECV_FORCE_PATHS=name,name (same names as the flag files)
    if (const char *env = std::getenv("ECV_FORCE_PATHS")) {
        std::string_view list(env);
        while (!list.empty()) {
            const size_t comma = list.find(',');
            apply_force_name(f, list.substr(0, comma));
            list = comma == std::string_view::npos ? std::string_view() : list.substr(comma + 1);
        }
    }
    if (flag_dir.empty())
        return f;
    std::error_code ec;
    for (const std::string_view name : force_path_names)
        if (std::filesystem::exists(std::filesystem::path(flag_dir) / ("ecv-force-path-" + std::string(name)), ec))
            apply_force_name(f, name);
#else
    (void)flag_dir;
#endif
    return f;
}

} // namespace renderer::vulkan

// lab hook (tools/oracles/core-hooks/lab.cpp, debug builds only): the chosen GPU paths as one key=value line, copied into result.json "gpu_paths"
#ifndef NDEBUG
extern "C" const char *ecv_gpu_paths_line() {
    return renderer::vulkan::paths_line_c();
}
#endif
