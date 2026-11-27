// GPU capability layer + strategy table (docs/gpu-sync-tracker.md, section "Strategy table").
//
// GpuCaps is filled ONCE at device creation from what the driver exposes (features, extensions, properties, memory
// types). choose_strategy() is a pure function of GpuCaps (+ optional forced paths for A/B): the renderer never
// branches on vendor id or device name. Every path has a correct fallback further down its ladder.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace renderer::vulkan {

// ---- driver identity and named rules --------------------------------------------------------------------------------------------
// Driver facts are keyed by vendorID + VkDriverId (+ driver version ranges), never by a model name. A driver we have no rule family for
// falls back to the generic policy: only paths that are correct on every conforming driver are chosen automatically.
struct GpuIdentity {
    uint32_t vendor_id = 0;
    uint32_t driver_id = 0; // VkDriverId (0 = not reported)
    uint32_t driver_version = 0;
};

enum class DriverFamily : uint8_t {
    Unknown,
    QualcommProprietary, // VK_DRIVER_ID_QUALCOMM_PROPRIETARY (8)
    MesaTurnip, // VK_DRIVER_ID_MESA_TURNIP (18)
    ArmProprietary, // VK_DRIVER_ID_ARM_PROPRIETARY (9)
    MesaOther, // radv, llvmpipe, panvk, v3dv, ...
    NvidiaProprietary, // (4)
    AmdProprietary, // (1, 2)
    IntelDriver, // (5, 6)
    ImaginationProprietary, // (7, 25)
    SamsungProprietary, // (21)
    SoftwareOrOther, // swiftshader, moltenvk
};

inline DriverFamily classify_driver(const GpuIdentity &id) {
    switch (id.driver_id) {
    case 8: return DriverFamily::QualcommProprietary;
    case 18: return DriverFamily::MesaTurnip;
    case 9: return DriverFamily::ArmProprietary;
    case 3: case 13: case 19: case 20: case 22: case 23: case 24: case 26: return DriverFamily::MesaOther;
    case 4: return DriverFamily::NvidiaProprietary;
    case 1: case 2: return DriverFamily::AmdProprietary;
    case 5: case 6: return DriverFamily::IntelDriver;
    case 7: case 25: return DriverFamily::ImaginationProprietary;
    case 21: return DriverFamily::SamsungProprietary;
    case 10: case 14: return DriverFamily::SoftwareOrOther;
    default: break;
    }
    return DriverFamily::Unknown; // driver id not reported (no VK_KHR_driver_properties): generic policy
}

inline const char *to_string(DriverFamily f) {
    switch (f) {
    case DriverFamily::QualcommProprietary: return "qcom-blob";
    case DriverFamily::MesaTurnip: return "turnip";
    case DriverFamily::ArmProprietary: return "arm-blob";
    case DriverFamily::MesaOther: return "mesa";
    case DriverFamily::NvidiaProprietary: return "nvidia";
    case DriverFamily::AmdProprietary: return "amd";
    case DriverFamily::IntelDriver: return "intel";
    case DriverFamily::ImaginationProprietary: return "imagination";
    case DriverFamily::SamsungProprietary: return "samsung";
    case DriverFamily::SoftwareOrOther: return "other";
    default: return "unknown";
    }
}

// What a rule changes. Effects are deliberately conservative: a rule may only take an automatic choice AWAY or demand a verification, it
// never enables something a driver did not offer.
enum class RuleEffect : uint8_t {
    FbfetchNeedsMeasurement, // the incoherent-framebuffer-fetch class (B025): the per-draw barrier rung is not trusted without a device A/B
    RoaaDestinationReadUnverified, // ROAA destination read has a known failure on a driver of the family: keep it, demand the startup self-test
    PipelineCacheStrict, // a foreign pipeline blob crashes the driver (B148): the cache identity check is mandatory (it always is; recorded for the log)
};

struct DriverRule {
    const char *name; // stable, shown in the log and the golden snapshot
    DriverFamily family;
    uint32_t min_driver_version; // inclusive, 0 = no lower bound
    uint32_t max_driver_version; // inclusive, 0 = no upper bound
    RuleEffect effect;
    bool provisional; // true = derived from one observation, not yet proven across drivers (the comment says where)
};

// The named rules. Every entry cites the bug it comes from; none keys on a model.
inline constexpr std::array<DriverRule, 3> driver_rules = { {
    // B025: Adreno 840 facade noise / cape flicker with the barrier fbfetch path on the stock driver, gone with a pass restart per fetch draw
    { "qcom-blob-fbfetch-barrier-b025", DriverFamily::QualcommProprietary, 0, 0, RuleEffect::FbfetchNeedsMeasurement, true },
    // docs/emucorex-renderer-lessons.md: ROAA destination read broken on an Arm Mali driver; the measurement, not the name, decides
    { "arm-blob-roaa-dest-read-unverified", DriverFamily::ArmProprietary, 0, 0, RuleEffect::RoaaDestinationReadUnverified, true },
    // B148: the Adreno blob crashed in vkCreateGraphicsPipelines with a pipeline cache written by another build
    { "qcom-blob-stale-pipeline-cache-b148", DriverFamily::QualcommProprietary, 0, 0, RuleEffect::PipelineCacheStrict, false },
} };

inline bool rule_matches(const DriverRule &r, const GpuIdentity &id) {
    return r.family == classify_driver(id) && (r.min_driver_version == 0 || id.driver_version >= r.min_driver_version)
        && (r.max_driver_version == 0 || id.driver_version <= r.max_driver_version);
}

struct GpuCaps {
    GpuIdentity identity;
    // --- synchronisation ---
    bool sync2 = false; // VK_KHR_synchronization2 enabled + feature
    bool timeline_semaphore = false; // feature (core 1.2 / VK_KHR_timeline_semaphore) enabled
    // --- programmable blending / framebuffer fetch ---
    bool roaa_color = false; // VK_EXT/ARM_rasterization_order_attachment_access: rasterizationOrderColorAttachmentAccess
    bool shader_interlock = false; // VK_EXT_fragment_shader_interlock (fragmentShaderSampleInterlock) + fragmentStoresAndAtomics
    bool dynamic_rendering = false; // VK_KHR_dynamic_rendering enabled
    bool dynamic_rendering_local_read = false; // VK_KHR_dynamic_rendering_local_read enabled + feature
    bool shader_tile_image = false; // VK_EXT_shader_tile_image colour read access
    bool input_attachment_self_dependency = true; // core: input attachments + by-region self-dependency (every Vulkan 1.0 driver)
    uint32_t max_per_stage_input_attachments = 0;
    // --- MSAA / resolve ---
    bool extended_dynamic_state = false; // VK_EXT_extended_dynamic_state (cull mode is set per draw instead of being baked into the pipeline)
    bool render_pass2 = false; // VK_KHR_create_renderpass2 listed (the MSAA diff needs vkCreateRenderPass2 for in-pass resolve + seed subpass)
    bool depth_stencil_resolve = false; // VK_KHR_depth_stencil_resolve enabled
    uint32_t ds_resolve_depth_modes = 0; // bit per vk::ResolveModeFlagBits (1 SAMPLE_ZERO, 2 AVERAGE, 4 MIN, 8 MAX)
    uint32_t ds_resolve_stencil_modes = 0;
    bool ds_resolve_independent = false;
    bool ds_resolve_independent_none = false;
    bool sample_rate_shading = false; // feature sampleRateShading (per-sample framebuffer fetch of a multisampled colour attachment)
    uint32_t framebuffer_color_sample_counts = 1; // bitmask of VkSampleCountFlagBits
    uint32_t framebuffer_depth_sample_counts = 1;
    uint32_t framebuffer_stencil_sample_counts = 1;
    // --- memory ---
    bool lazily_allocated_memory = false; // a DEVICE_LOCAL | LAZILY_ALLOCATED memory type exists (tiler signal)
    bool host_visible_coherent = false;
    bool host_visible_coherent_cached = false;
    // --- rasteriser features the P8/P15 paths choose from (a missing feature selects the documented fallback) ---
    bool wide_lines = false; // lines wider than one pixel; without it a wide line is a brush of shifted lines (raster_model.h)
    bool large_points = false; // point sizes above one pixel; without it the point size register is clamped to 1
    bool fill_mode_non_solid = false; // line/point polygon modes and TRIANGLE_EDGES; without it triangles are drawn filled
    bool vertex_stores_and_atomics = false; // vertexPipelineStoresAndAtomics: vertex programs may store to guest buffers (USSE buffer stores)
    bool fragment_stores_and_atomics = false; // fragmentStoresAndAtomics: fragment programs may store to guest buffers
    bool occlusion_query_precise = false; // visibility entries count samples instead of a flag
    bool depth_clamp = false; // feature depthClamp
    bool shader_clip_distance = false; // feature shaderClipDistance
    float max_point_size = 1.0f;
    // --- colour surface formats (P7): host format chosen per GXM colour base format from the probed attachment/blend support ---
    // (colour_format_model.h owns the table of candidates; `repack` is its Repack enum value)
    struct ColourFormatCap {
        uint32_t host_format = 0; // VkFormat, 0 = not probed (legacy mapping)
        uint8_t repack = 0;
        bool blend_ok = true; // the host format blends; the pipeline draws without blending when it does not
        bool lossy = false; // reduced precision fallback (F32 to F16)
        bool forced = false; // nothing in the chain was usable, the primary candidate is used anyway
    };
    static constexpr size_t colour_format_count = 23;
    std::array<ColourFormatCap, colour_format_count> colour_format{};
    // --- limits ---
    uint32_t max_bound_descriptor_sets = 4;
    uint32_t max_per_stage_sampled_images = 16;
    uint32_t max_descriptor_set_sampled_images = 96;

    bool supports_samples(uint32_t count) const { return (framebuffer_color_sample_counts & count) && (framebuffer_depth_sample_counts & count); }
};

// ---- strategy table --------------------------------------------------------------------------------------------
// One path per capability tier: the best tier the driver exposes is chosen, the tier below it exists only as the fallback for drivers that lack the feature.
//   barriers   sync2                       | legacy stage/access masks (no VK_KHR_synchronization2)
//   submit     timeline semaphore          | fences (no timelineSemaphore)
//   transient  lazily allocated memory     | normal memory (no LAZILY_ALLOCATED type)
//   render     dynamic rendering + local read | VkRenderPass / VkFramebuffer objects
//   fbfetch    local read (dynamic path) | ROAA | input attachment + barrier | copy snapshot, then (runtime verdict, renderer.cpp) split between draws
//   depth      depth clamp + clip distances | depth clamp + fragment discard | per-vertex depth clamp (see DepthRangePath)

enum class BarrierStyle : uint8_t { Legacy, Sync2 };
enum class SubmitTracking : uint8_t { Fences, TimelineSemaphore };
enum class TransientMemory : uint8_t { Normal, Lazy };
// The dynamic path needs VK_KHR_dynamic_rendering_local_read as well, because every framebuffer-fetch shader reads its colour attachment as an
// input attachment, which dynamic rendering only offers through local read.
enum class RenderPath : uint8_t { RenderPass, DynamicRendering };

enum class FbFetchPath : uint8_t {
    RasterOrderAttachmentAccess, // ROAA: coherent input attachment read inside the render pass
    DynamicRenderingLocalRead, // VK_KHR_dynamic_rendering_local_read: attachment read through a local-read layout (dynamic path only)
    InputAttachmentBarrier, // input attachment + by-region self-dependency + a per-draw pipeline barrier
    CopySnapshot, // end the pass, copy the colour attachment into a snapshot image, restart the pass, the shader reads the snapshot
};
inline constexpr size_t fbfetch_path_count = 4;

enum class MsaaResolvePath : uint8_t {
    None, // sample count 1 only: the driver cannot do multisampled framebuffers we need
    ResolveAttachment, // colour: pResolveAttachments, depth: VK_KHR_depth_stencil_resolve SAMPLE_ZERO
    ResolveAttachmentColorOnly, // colour resolve attachment; depth resolved by a shader/copy pass
};

// How the Vita's depth range rule is reproduced. The Vita does not clip at the near plane in depth: geometry in front of the eye whose
// depth falls below 0 is drawn with its depth clamped (only geometry behind the eye, w <= 0, is cut). The far plane does clip: geometry
// beyond depth 1 is not drawn. Evidence: Stealth Inc (PCSE00280) hides its room covers by moving them beyond the far plane (host replay
// of its capture: far clip alone reproduces the device image, near clip alone does not); Vita3K drew near geometry with clamped depth
// (per-vertex clamp, later depthClampEnable) for Killzone and others. VK_EXT_depth_clip_enable cannot express this rule (its
// depthClipEnable switches both planes together, and depthClampEnable + depthClipEnable=false is what core depthClampEnable
// already does), so it is not a tier.
enum class DepthRangePath : uint8_t {
    VertexClamp, // no depth clamp: the rasteriser clips both planes; the vertex shader clamps depth to >= 0 so the near plane never cuts
                 // (approximate: a triangle crossing depth 0 gets a bent depth plane; the only option without depthClamp short of
                 // writing gl_FragDepth in every fragment shader)
    FragmentDiscard, // depthClampEnable, no clip distances: the fragment shader discards gl_FragCoord.z > 1 (far plane) and < -1 (near wedge)
    ClampClipDistance, // depthClampEnable + gl_ClipDistance: w > 0 (behind-eye guard), depth >= -1 (near wedge), depth <= 1 (far plane)
};

// Debug/lab: force a LOWER tier (or a specific fbfetch rung) to exercise the fallbacks on a device that has the better one.
struct ForcedPaths {
    bool legacy_barriers = false; // act as a driver without sync2
    bool fences = false; // act as a driver without timeline semaphores
    bool normal_memory = false; // act as a driver without lazily allocated memory
    bool render_pass = false; // act as a driver without dynamic rendering
    bool base_dynamic_state = false; // act as a driver without extended dynamic state
    bool roaa = false, input_attachment = false, copy_snapshot = false; // pick this fbfetch rung (only rungs of the chosen render path)
    bool vertex_clamp_depth = false; // act as a driver without depth clamp / clip distances
    bool fragment_discard_depth = false; // act as a driver with depth clamp but without clip distances
};

struct Strategy {
    BarrierStyle barriers = BarrierStyle::Legacy;
    SubmitTracking submit = SubmitTracking::Fences;
    TransientMemory transient = TransientMemory::Normal;
    RenderPath render_path = RenderPath::RenderPass;
    bool extended_dynamic_state = false; // cull mode dynamic: one pipeline per state instead of one per (state x cull mode)
    FbFetchPath fbfetch = FbFetchPath::InputAttachmentBarrier;
    MsaaResolvePath msaa_resolve = MsaaResolvePath::None;
    bool use_shader_interlock = false; // desktop interlock path (only when no fbfetch rung above InputAttachment is available)
    DepthRangePath depth_range = DepthRangePath::VertexClamp;
    // which request could not be honoured (forced rung not available): shown in the log
    bool force_rejected = false;
    DriverFamily family = DriverFamily::Unknown;
    // rule effects that apply to this identity (bit per RuleEffect). None changes a selection: they are verdict flags for the B025 work
    uint32_t rule_effects = 0;
};

inline bool fbfetch_path_available(const GpuCaps &c, FbFetchPath p, bool dynamic_rendering_selected) {
    switch (p) {
    case FbFetchPath::RasterOrderAttachmentAccess: return c.roaa_color && !dynamic_rendering_selected;
    case FbFetchPath::DynamicRenderingLocalRead: return dynamic_rendering_selected && c.dynamic_rendering_local_read;
    case FbFetchPath::InputAttachmentBarrier: return !dynamic_rendering_selected && c.input_attachment_self_dependency && c.max_per_stage_input_attachments >= 1;
    case FbFetchPath::CopySnapshot: return !dynamic_rendering_selected && c.max_per_stage_input_attachments >= 1;
    }
    return false;
}

inline Strategy choose_strategy(const GpuCaps &c, const ForcedPaths &f = {}) {
    Strategy s;
    s.family = classify_driver(c.identity);
    for (const DriverRule &r : driver_rules)
        if (rule_matches(r, c.identity))
            s.rule_effects |= 1u << static_cast<unsigned>(r.effect);
    s.barriers = (c.sync2 && !f.legacy_barriers) ? BarrierStyle::Sync2 : BarrierStyle::Legacy;
    s.submit = (c.timeline_semaphore && !f.fences) ? SubmitTracking::TimelineSemaphore : SubmitTracking::Fences;
    s.transient = (c.lazily_allocated_memory && !f.normal_memory) ? TransientMemory::Lazy : TransientMemory::Normal;
    const bool dr = c.dynamic_rendering && c.dynamic_rendering_local_read && !f.render_pass;
    s.render_path = dr ? RenderPath::DynamicRendering : RenderPath::RenderPass;
    s.extended_dynamic_state = c.extended_dynamic_state && !f.base_dynamic_state;

    // fbfetch: the dynamic path has exactly one rung; the VkRenderPass path walks ROAA, input attachment, copy snapshot
    FbFetchPath forced = FbFetchPath::CopySnapshot;
    bool have_forced = false;
    if (f.roaa) { forced = FbFetchPath::RasterOrderAttachmentAccess; have_forced = true; }
    else if (f.input_attachment) { forced = FbFetchPath::InputAttachmentBarrier; have_forced = true; }
    else if (f.copy_snapshot) { forced = FbFetchPath::CopySnapshot; have_forced = true; }
    if (dr) {
        if (have_forced)
            s.force_rejected = true; // those rungs belong to the VkRenderPass path
        s.fbfetch = FbFetchPath::DynamicRenderingLocalRead;
    } else if (have_forced && fbfetch_path_available(c, forced, false)) {
        s.fbfetch = forced;
    } else {
        if (have_forced)
            s.force_rejected = true;
        s.fbfetch = FbFetchPath::CopySnapshot;
        for (size_t i = 0; i < fbfetch_path_count; i++) {
            const FbFetchPath p = static_cast<FbFetchPath>(i);
            if (fbfetch_path_available(c, p, false)) {
                s.fbfetch = p;
                break;
            }
        }
    }
    // interlock stands in only when nothing better than the barrier rung exists (ROAA wins over interlock)
    s.use_shader_interlock = c.shader_interlock && !dr && s.fbfetch != FbFetchPath::RasterOrderAttachmentAccess && !f.roaa && !f.input_attachment && !f.copy_snapshot;

    if (!c.depth_clamp || f.vertex_clamp_depth)
        s.depth_range = DepthRangePath::VertexClamp;
    else if (!c.shader_clip_distance || f.fragment_discard_depth)
        s.depth_range = DepthRangePath::FragmentDiscard;
    else
        s.depth_range = DepthRangePath::ClampClipDistance;

    // MSAA resolve
    const bool ms = c.supports_samples(4) || c.supports_samples(2);
    if (!ms)
        s.msaa_resolve = MsaaResolvePath::None;
    else if (c.depth_stencil_resolve && (c.ds_resolve_depth_modes & 1))
        s.msaa_resolve = MsaaResolvePath::ResolveAttachment;
    else
        s.msaa_resolve = MsaaResolvePath::ResolveAttachmentColorOnly;
    return s;
}

inline const char *to_string(BarrierStyle v) { return v == BarrierStyle::Sync2 ? "sync2" : "legacy"; }
inline const char *to_string(RenderPath v) { return v == RenderPath::DynamicRendering ? "dynamic" : "render-pass"; }
inline const char *to_string(SubmitTracking v) { return v == SubmitTracking::TimelineSemaphore ? "timeline" : "fences"; }
inline const char *to_string(TransientMemory v) { return v == TransientMemory::Lazy ? "lazy" : "normal"; }
inline const char *to_string(FbFetchPath v) {
    switch (v) {
    case FbFetchPath::RasterOrderAttachmentAccess: return "roaa";
    case FbFetchPath::DynamicRenderingLocalRead: return "local-read";
    case FbFetchPath::InputAttachmentBarrier: return "input-attachment";
    case FbFetchPath::CopySnapshot: return "copy";
    }
    return "?";
}
inline const char *to_string(DepthRangePath v) {
    switch (v) {
    case DepthRangePath::VertexClamp: return "vertex-clamp";
    case DepthRangePath::FragmentDiscard: return "clamp+fragment-discard";
    case DepthRangePath::ClampClipDistance: return "clamp+clip-distance";
    }
    return "?";
}
inline const char *to_string(MsaaResolvePath v) {
    switch (v) {
    case MsaaResolvePath::None: return "none";
    case MsaaResolvePath::ResolveAttachment: return "resolve-attachment";
    case MsaaResolvePath::ResolveAttachmentColorOnly: return "color-resolve+ds-fallback";
    }
    return "?";
}

// One line, stable keys: logged once per session and copied into the lab result (ECVLAB `paths` line, result.json "gpu_paths")
inline std::string describe(const GpuCaps &c, const Strategy &s) {
    std::string r = "family=";
    r += to_string(s.family);
    r += " rules=";
    bool any_rule = false;
    for (const DriverRule &rule : driver_rules) {
        if (rule_matches(rule, c.identity)) {
            r += any_rule ? "," : "";
            r += rule.name;
            any_rule = true;
        }
    }
    if (!any_rule)
        r += "none";
    r += " barriers=";
    r += to_string(s.barriers);
    r += " submit=";
    r += to_string(s.submit);
    r += " render_path=";
    r += to_string(s.render_path);
    r += " dyn_state=";
    r += s.extended_dynamic_state ? "extended" : "base";
    r += " transient=";
    r += to_string(s.transient);
    r += " fbfetch=";
    r += to_string(s.fbfetch);
    r += " interlock=";
    r += s.use_shader_interlock ? "1" : "0";
    r += " msaa_resolve=";
    r += to_string(s.msaa_resolve);
    r += " ds_resolve=";
    r += c.depth_stencil_resolve ? "1" : "0";
    r += " depth_range=";
    r += to_string(s.depth_range);
    r += " forced_rejected=";
    r += s.force_rejected ? "1" : "0";
    return r;
}

// Names of the flag files <files>/ecv-force-path-<name> and of the ECV_FORCE_PATHS environment list (debug builds only).
inline bool apply_force_name(ForcedPaths &f, std::string_view name) {
    if (name == "legacy-barriers") f.legacy_barriers = true;
    else if (name == "fences") f.fences = true;
    else if (name == "normal-memory") f.normal_memory = true;
    else if (name == "render-pass") f.render_pass = true;
    else if (name == "base-dynamic-state") f.base_dynamic_state = true;
    else if (name == "roaa") f.roaa = true;
    else if (name == "input-attachment") f.input_attachment = true;
    else if (name == "copy-snapshot") f.copy_snapshot = true;
    else if (name == "vertex-clamp-depth") f.vertex_clamp_depth = true;
    else if (name == "fragment-discard-depth") f.fragment_discard_depth = true;
    else return false;
    return true;
}
inline constexpr std::array<std::string_view, 10> force_path_names = { "legacy-barriers", "fences", "normal-memory", "render-pass", "base-dynamic-state", "roaa", "input-attachment", "copy-snapshot", "vertex-clamp-depth", "fragment-discard-depth" };

} // namespace renderer::vulkan
