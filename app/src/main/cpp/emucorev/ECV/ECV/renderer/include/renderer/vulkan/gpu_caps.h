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
    bool render_pass2 = false; // VK_KHR_create_renderpass2 listed (the MSAA diff needs vkCreateRenderPass2 for in-pass resolve + seed subpass)
    bool depth_stencil_resolve = false; // VK_KHR_depth_stencil_resolve enabled
    uint32_t ds_resolve_depth_modes = 0; // bit per vk::ResolveModeFlagBits (1 SAMPLE_ZERO, 2 AVERAGE, 4 MIN, 8 MAX)
    uint32_t ds_resolve_stencil_modes = 0;
    bool ds_resolve_independent = false;
    bool ds_resolve_independent_none = false;
    uint32_t framebuffer_color_sample_counts = 1; // bitmask of VkSampleCountFlagBits
    uint32_t framebuffer_depth_sample_counts = 1;
    uint32_t framebuffer_stencil_sample_counts = 1;
    // --- memory ---
    bool lazily_allocated_memory = false; // a DEVICE_LOCAL | LAZILY_ALLOCATED memory type exists (tiler signal)
    bool host_visible_coherent = false;
    bool host_visible_coherent_cached = false;
    // --- limits ---
    uint32_t max_bound_descriptor_sets = 4;
    uint32_t max_per_stage_sampled_images = 16;
    uint32_t max_descriptor_set_sampled_images = 96;

    bool supports_samples(uint32_t count) const { return (framebuffer_color_sample_counts & count) && (framebuffer_depth_sample_counts & count); }
};

// ---- strategy table --------------------------------------------------------------------------------------------

enum class BarrierStyle : uint8_t { Legacy, Sync2 };
enum class SubmitTracking : uint8_t { Fences, TimelineSemaphore };
enum class TransientMemory : uint8_t { Normal, Lazy };
// How the scene draws are enclosed: VkRenderPass + VkFramebuffer objects (always available), or vkCmdBeginRendering (dynamic rendering).
// The dynamic path needs VK_KHR_dynamic_rendering_local_read as well, because every framebuffer-fetch shader reads its colour attachment as an
// input attachment, which dynamic rendering only offers through local read.
enum class RenderPath : uint8_t { RenderPass, DynamicRendering };

// Programmable blending / framebuffer fetch ladder, best first. Each rung is correct on its own; lower rungs are slower
// or need more passes. "Copy" = snapshot the attachment into a texture and sample that (no feedback loop at all).
enum class FbFetchPath : uint8_t {
    RasterOrderAttachmentAccess, // ROAA: coherent input attachment read inside the render pass (existing)
    DynamicRenderingLocalRead, // VK_KHR_dynamic_rendering_local_read: attachment read through a local-read layout
    InputAttachmentBarrier, // input attachment + by-region self-dependency + a per-draw pipeline barrier (existing fallback)
    CopySnapshot, // end the pass, copy the colour attachment, restart the pass, sample the copy
};
inline constexpr size_t fbfetch_path_count = 4;

// Interlocked path used instead of fbfetch on desktop GPUs (shader_interlock + storage image); orthogonal to FbFetchPath
// because the existing renderer picks it only when ROAA is absent.
enum class MsaaResolvePath : uint8_t {
    None, // sample count 1 only: the driver cannot do multisampled framebuffers we need
    ResolveAttachment, // colour: pResolveAttachments, depth: VK_KHR_depth_stencil_resolve
    ResolveAttachmentColorOnly, // colour resolve attachment; depth resolved by a shader/copy pass (no ds-resolve extension)
};

enum class ForcePath : uint8_t {
    // oracle flag files <files>/ecv-force-path-<name> (debug builds only)
    None,
    Legacy_barriers, Sync2_barriers,
    Fences, Timeline,
    NormalMemory, LazyMemory,
    Roaa, LocalRead, InputAttachment, CopySnapshot,
    ColorOnlyResolve,
};

struct ForcedPaths {
    bool legacy_barriers = false, sync2_barriers = false;
    bool fences = false, timeline = false;
    bool normal_memory = false, lazy_memory = false;
    bool roaa = false, local_read = false, input_attachment = false, copy_snapshot = false;
    bool content_load_ops = false;
    bool dynamic_rendering = false, render_pass = false; // render_pass = stay on VkRenderPass objects
    bool color_only_resolve = false;
};

// Which rungs have an implementation in this build. A capability that is present but not implemented is never selected
// (and is listed as `available-unused` in the log), so the table never claims a path the renderer cannot run.
struct Implemented {
    bool sync2 = true; // vkutil::sync::Batch emits both vkCmdPipelineBarrier2 and legacy masks
    bool timeline = true; // scene submits signal a timeline semaphore; frame waits use it (fences stay for per-scene recycling)
    // Not chosen automatically yet: one lavapipe replay pair of a Need for Speed capture differed by 9 px (max 5) with timeline on, 0 of 3 with it off,
    // so it stays opt-in (ecv-force-path-timeline) until a device A/B says it is equal. Flip to true to let the caps decide.
    bool timeline_auto = false;
    bool lazy_memory = true; // Image::init_transient_attachment (used by MSAA attachments; no existing attachment is provably dead)
    bool fbfetch_roaa = true;
    bool fbfetch_local_read = true; // only reachable together with Implemented::dynamic_rendering
    bool dynamic_rendering = true; // context.cpp / pipeline_cache.cpp: vkCmdBeginRendering path with explicit tracker barriers
    // opt-in until a device A/B (B025 table) says it is equal or better: `ecv-force-path-dynamic-rendering` selects it
    bool dynamic_auto = false;
    // Tracked-content load ops (Cleared -> CLEAR, Invalidated -> DONT_CARE) on the dynamic path. Opt-in: a lavapipe replay A/B showed run-to-run
    // variation (max 3 on 24 px) with it on and none with it off, which points at a surface whose content changed through a path the
    // tracker does not see. `ecv-force-path-content-load-ops` enables it for the lab until that is understood.
    bool content_load_auto = false;
    bool fbfetch_input_attachment = true;
    bool fbfetch_copy = false; // the generic copy rung (only the SceShell glow snapshot exists)
    bool ds_resolve = true; // render_pass_policy.h::plan_ds_resolve; MSAA itself is B140 (C:\w\msaa)
};

struct Strategy {
    BarrierStyle barriers = BarrierStyle::Legacy;
    SubmitTracking submit = SubmitTracking::Fences;
    TransientMemory transient = TransientMemory::Normal;
    RenderPath render_path = RenderPath::RenderPass;
    FbFetchPath fbfetch = FbFetchPath::InputAttachmentBarrier;
    MsaaResolvePath msaa_resolve = MsaaResolvePath::None;
    bool use_shader_interlock = false; // desktop interlock path (only when no fbfetch rung above InputAttachment is available)
    // loadOp/storeOp derived from tracked content state (never needs a capability: always correct)
    bool state_driven_load_store = true;
    // which request could not be honoured (forced path not supported): shown in the log
    bool force_rejected = false;
    bool content_load_ops = false;
    DriverFamily family = DriverFamily::Unknown;
    // rule effects that apply to this identity (bit per RuleEffect). None changes a selection today: they are verdict flags for the B025 work
    uint32_t rule_effects = 0;
};

inline bool fbfetch_path_available(const GpuCaps &c, FbFetchPath p, const Implemented &impl = {}, bool dynamic_rendering_selected = false) {
    switch (p) {
    case FbFetchPath::RasterOrderAttachmentAccess: return c.roaa_color && impl.fbfetch_roaa;
    case FbFetchPath::DynamicRenderingLocalRead: return dynamic_rendering_selected && c.dynamic_rendering_local_read && impl.fbfetch_local_read;
    case FbFetchPath::InputAttachmentBarrier: return c.input_attachment_self_dependency && c.max_per_stage_input_attachments >= 1 && impl.fbfetch_input_attachment;
    case FbFetchPath::CopySnapshot: return impl.fbfetch_copy;
    }
    return false;
}

inline Strategy choose_strategy(const GpuCaps &c, const ForcedPaths &f = {}, const Implemented &impl = {}) {
    Strategy s;
    // driver family + named rules
    s.family = classify_driver(c.identity);
    for (const DriverRule &r : driver_rules)
        if (rule_matches(r, c.identity))
            s.rule_effects |= 1u << static_cast<unsigned>(r.effect);
    // generic fallback: a driver that does not identify itself gets the baseline (legacy barriers; opt-in paths stay opt-in)
    const bool known_driver = s.family != DriverFamily::Unknown;
    // barriers
    s.barriers = (c.sync2 && impl.sync2 && !f.legacy_barriers && (known_driver || f.sync2_barriers)) ? BarrierStyle::Sync2 : BarrierStyle::Legacy;
    if (f.sync2_barriers && !(c.sync2 && impl.sync2))
        s.force_rejected = true;
    // submit tracking
    s.submit = (c.timeline_semaphore && impl.timeline && (impl.timeline_auto || f.timeline) && !f.fences) ? SubmitTracking::TimelineSemaphore : SubmitTracking::Fences;
    if (f.timeline && !(c.timeline_semaphore && impl.timeline))
        s.force_rejected = true;
    // transient attachments
    s.transient = (c.lazily_allocated_memory && impl.lazy_memory && !f.normal_memory) ? TransientMemory::Lazy : TransientMemory::Normal;
    if (f.lazy_memory && !(c.lazily_allocated_memory && impl.lazy_memory))
        s.force_rejected = true;
    // render path: dynamic rendering (+ local read) only when asked for, every other combination stays on VkRenderPass objects
    const bool dr_possible = c.dynamic_rendering && c.dynamic_rendering_local_read && impl.dynamic_rendering && impl.fbfetch_local_read;
    const bool dr_wanted = (f.dynamic_rendering || f.local_read || impl.dynamic_auto) && !f.render_pass;
    s.render_path = (dr_possible && dr_wanted) ? RenderPath::DynamicRendering : RenderPath::RenderPass;
    if ((f.dynamic_rendering || f.local_read) && !dr_possible)
        s.force_rejected = true;
    const bool dr = s.render_path == RenderPath::DynamicRendering;
    // fbfetch ladder: first available rung unless one is forced and available
    FbFetchPath forced = FbFetchPath::CopySnapshot;
    bool have_forced = false;
    if (dr) {
        // the dynamic path has exactly one way to read the attachment; other forced rungs belong to the VkRenderPass path
        if (f.roaa || f.input_attachment || f.copy_snapshot)
            s.force_rejected = true;
        forced = FbFetchPath::DynamicRenderingLocalRead;
        have_forced = true;
    } else if (f.roaa) { forced = FbFetchPath::RasterOrderAttachmentAccess; have_forced = true; }
    else if (f.local_read) { forced = FbFetchPath::DynamicRenderingLocalRead; have_forced = true; } // rejected below: needs dr
    else if (f.input_attachment) { forced = FbFetchPath::InputAttachmentBarrier; have_forced = true; }
    else if (f.copy_snapshot) { forced = FbFetchPath::CopySnapshot; have_forced = true; }
    if (have_forced && fbfetch_path_available(c, forced, impl, dr)) {
        s.fbfetch = forced;
    } else {
        if (have_forced)
            s.force_rejected = true;
        s.fbfetch = FbFetchPath::CopySnapshot;
        for (size_t i = 0; i < fbfetch_path_count; i++) {
            const FbFetchPath p = static_cast<FbFetchPath>(i);
            if (fbfetch_path_available(c, p, impl, dr)) {
                s.fbfetch = p;
                break;
            }
        }
    }
    // interlock stands in only when nothing better than the barrier rung exists (matches the pre-existing renderer rule:
    // ROAA wins over interlock)
    s.use_shader_interlock = c.shader_interlock && !dr && !f.roaa && s.fbfetch != FbFetchPath::RasterOrderAttachmentAccess
        && s.fbfetch != FbFetchPath::DynamicRenderingLocalRead && !f.input_attachment && !f.copy_snapshot;
    s.content_load_ops = dr && (f.content_load_ops || impl.content_load_auto);
    // MSAA resolve
    const bool ms = c.supports_samples(4) || c.supports_samples(2);
    if (!ms)
        s.msaa_resolve = MsaaResolvePath::None;
    else if (c.depth_stencil_resolve && impl.ds_resolve && (c.ds_resolve_depth_modes & 1) && !f.color_only_resolve)
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
    r += " load_store=";
    r += s.state_driven_load_store ? "state" : "fixed";
    r += " content_load_ops=";
    r += s.content_load_ops ? "1" : "0";
    r += " forced_rejected=";
    r += s.force_rejected ? "1" : "0";
    return r;
}

// Parse the oracle flag-file names: "legacy-barriers", "sync2-barriers", "fences", "timeline", "normal-memory",
// "lazy-memory", "roaa", "local-read", "input-attachment", "copy-snapshot", "color-only-resolve".
inline bool apply_force_name(ForcedPaths &f, std::string_view name) {
    if (name == "legacy-barriers") f.legacy_barriers = true;
    else if (name == "sync2-barriers") f.sync2_barriers = true;
    else if (name == "fences") f.fences = true;
    else if (name == "timeline") f.timeline = true;
    else if (name == "normal-memory") f.normal_memory = true;
    else if (name == "lazy-memory") f.lazy_memory = true;
    else if (name == "roaa") f.roaa = true;
    else if (name == "dynamic-rendering") f.dynamic_rendering = true;
    else if (name == "render-pass") f.render_pass = true;
    else if (name == "content-load-ops") f.content_load_ops = true;
    else if (name == "local-read") f.local_read = true;
    else if (name == "input-attachment") f.input_attachment = true;
    else if (name == "copy-snapshot") f.copy_snapshot = true;
    else if (name == "color-only-resolve") f.color_only_resolve = true;
    else return false;
    return true;
}
inline constexpr std::array<std::string_view, 14> force_path_names = { "content-load-ops", "dynamic-rendering", "render-pass", "legacy-barriers", "sync2-barriers", "fences", "timeline",
    "normal-memory", "lazy-memory", "roaa", "local-read", "input-attachment", "copy-snapshot", "color-only-resolve" };

} // namespace renderer::vulkan
