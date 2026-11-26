// EmuCoreV: real multisampling for GXM MSAA scenes (B140).
//
// A scene whose render target asks for 2X/4X multisampling (or whose sample count is forced by the config) and whose colour surface
// carries the downscale bit ("resolve to this size") is rasterised with N coverage samples into a multisampled colour attachment and
// resolved, inside the same render pass instance, into the single sample surface the rest of the renderer (surface cache, texture
// reads, display, save states) keeps using. Depth/stencil is multisampled as well and is resolved (sample zero) into the single sample
// depth/stencil image. f16 surfaces with the raw u16 alias get a multisampled raw attachment too, resolved exactly (integer resolve =
// sample zero), so the raw bits survive.
//
// One implementation per render path of the GPU strategy (gpu_caps.h): VkRenderPass objects (render pass 2: in-pass resolve) and
// dynamic rendering (resolve in the rendering attachments). Capabilities come from GpuCaps / Strategy: render pass 2 or dynamic
// rendering, depth/stencil resolve (plan_ds_resolve), sampleRateShading, the framebuffer sample counts and the transient (lazily
// allocated) memory choice. A device that lacks one of them keeps every scene at one sample (decided once, at device creation).
//
// Rules of the design:
//  * The resolve targets are write-only attachments (loadOp DONT_CARE). The scene starts from the surface content: before each pass
//    instance the surface is copied and the first draw of the instance (the seed) writes the copy into every sample.
//  * Programmable blending (colour read-back) is per sample: the multisampled colour (and raw) attachment is the pass' input attachment
//    (subpass input / local read), the fetching shaders are compiled with multisampled input attachments read at gl_SampleID and their
//    pipelines run with sample shading. Nothing is skipped and nothing falls back to one sample mid-scene.
//  * A scene can restart its pass (mid-scene flush, cast copies, fbfetch splits, depth transfers). The pass instances are recorded
//    deferred (VKContext::emit_pass), so the multisampled depth/stencil is chosen per instance: an instance that is followed by
//    another one (or whose game stores the depth) stores it in a persistent image and a restarted instance loads it; the last
//    instance of a scene whose depth is dead uses a transient (lazily allocated) image that never leaves tile memory. The colour is
//    always transient: a restarted instance re-seeds it from the resolved single sample surface.
#pragma once

#include <vkutil/objects.h>

#include <gxm/types.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <unordered_map>

struct MemState;
struct SceGxmColorSurface;
struct SceGxmDepthStencilSurface;

namespace renderer::vulkan {

struct VKState;
struct VKContext;
struct VKRenderTarget;
struct DepthStencilSurfaceCacheInfo;

// per-scene MSAA state owned by the VKContext
struct MsaaScene {
    bool active = false;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    // the f16 surface has a raw u16 alias attachment: multisampled and resolved too
    bool with_raw = false;
    vk::Format ds_format = vk::Format::eUndefined;
    uint32_t depth_resolve_mode = 0;
    uint32_t stencil_resolve_mode = 0;
    // the first instance loads the multisampled depth/stencil from the previous multisampled scene (the game loads its depth surface)
    bool load_depth = false;
    // the first instance clears the multisampled depth/stencil with the surface defaults instead of the record values
    bool clear_to_defaults = false;
    // the depth/stencil attachment is the render target's own scratch image (no guest surface behind it)
    bool ds_is_scratch = false;
    // pass instances of this scene emitted so far (instance 0 is the scene's first)
    uint32_t instances = 0;
    // multisampled attachments of the scene (transient), also the fetch descriptors' views
    vkutil::Image *color_ms = nullptr;
    vkutil::Image *raw_ms = nullptr;
    // extent of the resolved image, also of the seed draw
    uint32_t width = 0;
    uint32_t height = 0;
    vk::Format format = vk::Format::eUndefined;
    vk::Pipeline seed_pipeline{};
    // seed of the instance being recorded: the descriptor of the sampled copies and the copies themselves
    vk::DescriptorSet seed_set{};
    vkutil::Image *seed_color_copy = nullptr;
    vkutil::Image *seed_raw_copy = nullptr;
    // draws seen in the running instance (debug restart test)
    uint32_t draw_count = 0;
};

// what msaa::plan decided before the framebuffer is built
struct MsaaPlan {
    bool use = false;
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    bool with_raw = false;
    bool load_depth = false;
    bool clear_to_defaults = false;
    bool ds_is_scratch = false;
    vk::Format ds_format = vk::Format::eUndefined;
    uint32_t depth_resolve_mode = 0;
    uint32_t stencil_resolve_mode = 0;
};

struct MsaaDepth {
    std::unique_ptr<vkutil::Image> image;
    vk::Image resolved_image{};
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
    uint32_t width = 0;
    uint32_t height = 0;
    // the image holds what the last pass instance stored
    bool valid = false;
};

struct MsaaPassKey {
    vk::Format color_format;
    vk::SampleCountFlagBits samples;
    bool with_raw;
    bool load_depth;
    bool store_depth;
    bool ds_is_scratch;
    vk::Format ds_format;
    uint32_t depth_mode;
    uint32_t stencil_mode;
    bool operator<(const MsaaPassKey &o) const {
        return std::tie(color_format, samples, with_raw, load_depth, store_depth, ds_is_scratch, ds_format, depth_mode, stencil_mode)
            < std::tie(o.color_format, o.samples, o.with_raw, o.load_depth, o.store_depth, o.ds_is_scratch, o.ds_format, o.depth_mode, o.stencil_mode);
    }
};

// everything the VKState keeps for MSAA (capabilities live in GpuCaps / Strategy)
struct MsaaState {
    // decided once at device creation from GpuCaps/Strategy (msaa::init)
    bool enabled = false;
    // debug A/B switches (oracle builds / host replay only), see msaa.cpp
    uint32_t debug_bits = 0;
    uint64_t debug_next_poll_ns = 0;

    std::map<MsaaPassKey, vk::RenderPass> passes;
    // transient multisampled colour/raw attachments and transient depth/stencil, by size, format and sample count
    std::map<std::tuple<uint32_t, uint32_t, vk::Format, uint32_t>, std::unique_ptr<vkutil::Image>> color_images;
    std::map<std::tuple<uint32_t, uint32_t, vk::Format, uint32_t>, std::unique_ptr<vkutil::Image>> transient_depth;
    // persistent multisampled depth/stencil images, one per single sample depth/stencil image
    std::unordered_map<const vkutil::Image *, MsaaDepth> depth;

    // seed: a full screen triangle that samples a copy of the single sample surface (and of its raw alias)
    std::map<std::tuple<vk::Format, uint32_t, bool, vk::Format, bool>, vk::Pipeline> seed_pipelines;
    vk::ShaderModule seed_vertex{};
    std::array<vk::ShaderModule, 2> seed_fragment{};
    std::array<vk::DescriptorSetLayout, 2> seed_set_layout{};
    std::array<vk::PipelineLayout, 2> seed_pipeline_layout{};
    vk::Sampler seed_sampler{};
    // the pools of seed descriptor sets and the sampled surface copies per frame slot (reset when the slot is reused);
    // a pool is added when the slot's pools run out, so any number of pass instances per frame is served
    std::array<std::vector<vk::DescriptorPool>, 3> seed_pools;
    std::array<std::map<std::tuple<uint32_t, uint32_t, vk::Format>, std::unique_ptr<vkutil::Image>>, 3> seed_copies;

    // cached per (format, samples, is depth) image support, 0 = unknown, 1 = no, 2 = yes
    std::map<std::tuple<vk::Format, uint32_t, bool>, uint8_t> format_support;

    // statistics (logged)
    uint64_t scenes_multisampled = 0;
    uint64_t scenes_single_sample = 0;
    uint64_t instances_emitted = 0;
    uint64_t depth_stores = 0;
    uint64_t depth_stores_avoided = 0;
};

// what an instance of the pass is begun with (computed by VKContext::emit_pass, the same values the single sample path uses)
struct MsaaInstance {
    bool followed = false; // another pass instance of the scene follows (a restart): the multisampled depth/stencil must survive
    bool guest_store = false; // the game keeps its depth surface in memory: a later scene may load it
    vk::ClearDepthStencilValue ds_clear{};
    vk::Rect2D area{};
};

namespace msaa {

// after GpuCaps/Strategy are known: is multisampling usable on this device (logs one line)
void init(VKState &state);

// decide, before the render pass and framebuffer of the scene are built, whether the scene is multisampled
MsaaPlan plan(VKContext &context, const VKRenderTarget &target, bool restoring, bool colour_surface_present,
    vk::Format colour_format, bool colour_has_raw_alias, bool msaa_expanded,
    const SceGxmDepthStencilSurface *ds_surface, bool depth_load_requested, bool game_force_load, bool game_force_store);

// a scene that is not multisampled writes the single sample depth/stencil: its multisampled copy becomes stale
void note_single_sample_scene(VKContext &context, const SceGxmDepthStencilSurface *ds_surface);

// build (or reuse) the multisampled attachments of the scene and the seed pipeline. On failure the scene silently stays single sample.
void activate(VKContext &context, const MsaaPlan &plan, vk::ImageView colour_view, vk::ImageView ds_view, uint32_t fb_width, uint32_t fb_height);

// the secondary command buffer of a new pass instance was begun: record the seed draw (samples <- sampled surface copy)
void begin_instance(VKContext &context);

// start of VKContext::emit_pass, outside a render pass: copy the surface the seed of this instance samples
void record_copies(VKContext &context);

// the instance itself: VkRenderPass path (render pass + framebuffer) and dynamic rendering path
void record_render_pass(VKContext &context, const MsaaInstance &instance);
void record_rendering(VKContext &context, const MsaaInstance &instance);

// views the framebuffer-fetch descriptors (binding 0 colour, 2 raw) must reference in a multisampled scene
vk::ImageView fetch_color_view(const VKContext &context);
vk::ImageView fetch_raw_view(const VKContext &context);

// debug test hook: true when the multisampled pass must be ended now to exercise the restart path (debug builds only)
bool debug_force_restart(VKContext &context);

// a frame slot is reused: its seed descriptors and surface copies are free again
void begin_frame(VKState &state, int frame_index);

void cleanup(VKState &state);

} // namespace msaa
} // namespace renderer::vulkan
