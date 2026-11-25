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

struct GpuCaps {
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
    bool color_only_resolve = false;
};

// Which rungs have an implementation in this build. A capability that is present but not implemented is never selected
// (and is listed as `available-unused` in the log), so the table never claims a path the renderer cannot run.
struct Implemented {
    bool sync2 = true; // vkutil::sync::Batch emits both vkCmdPipelineBarrier2 and legacy masks
    bool timeline = true; // scene submits signal a timeline semaphore; frame waits use it (fences stay for per-scene recycling)
    bool lazy_memory = true; // Image::init_transient_attachment (used by MSAA attachments; no existing attachment is provably dead)
    bool fbfetch_roaa = true;
    bool fbfetch_local_read = false;
    bool fbfetch_input_attachment = true;
    bool fbfetch_copy = false; // the generic copy rung (only the SceShell glow snapshot exists)
    bool ds_resolve = true; // render_pass_policy.h::plan_ds_resolve; MSAA itself is B140 (C:\w\msaa)
};

struct Strategy {
    BarrierStyle barriers = BarrierStyle::Legacy;
    SubmitTracking submit = SubmitTracking::Fences;
    TransientMemory transient = TransientMemory::Normal;
    FbFetchPath fbfetch = FbFetchPath::InputAttachmentBarrier;
    MsaaResolvePath msaa_resolve = MsaaResolvePath::None;
    bool use_shader_interlock = false; // desktop interlock path (only when no fbfetch rung above InputAttachment is available)
    // loadOp/storeOp derived from tracked content state (never needs a capability: always correct)
    bool state_driven_load_store = true;
    // which request could not be honoured (forced path not supported): shown in the log
    bool force_rejected = false;
};

inline bool fbfetch_path_available(const GpuCaps &c, FbFetchPath p, const Implemented &impl = {}) {
    switch (p) {
    case FbFetchPath::RasterOrderAttachmentAccess: return c.roaa_color && impl.fbfetch_roaa;
    case FbFetchPath::DynamicRenderingLocalRead: return c.dynamic_rendering_local_read && impl.fbfetch_local_read;
    case FbFetchPath::InputAttachmentBarrier: return c.input_attachment_self_dependency && c.max_per_stage_input_attachments >= 1 && impl.fbfetch_input_attachment;
    case FbFetchPath::CopySnapshot: return impl.fbfetch_copy;
    }
    return false;
}

inline Strategy choose_strategy(const GpuCaps &c, const ForcedPaths &f = {}, const Implemented &impl = {}) {
    Strategy s;
    // barriers
    s.barriers = (c.sync2 && impl.sync2 && !f.legacy_barriers) ? BarrierStyle::Sync2 : BarrierStyle::Legacy;
    if (f.sync2_barriers && !(c.sync2 && impl.sync2))
        s.force_rejected = true;
    // submit tracking
    s.submit = (c.timeline_semaphore && impl.timeline && !f.fences) ? SubmitTracking::TimelineSemaphore : SubmitTracking::Fences;
    if (f.timeline && !(c.timeline_semaphore && impl.timeline))
        s.force_rejected = true;
    // transient attachments
    s.transient = (c.lazily_allocated_memory && impl.lazy_memory && !f.normal_memory) ? TransientMemory::Lazy : TransientMemory::Normal;
    if (f.lazy_memory && !(c.lazily_allocated_memory && impl.lazy_memory))
        s.force_rejected = true;
    // fbfetch ladder: first available rung unless one is forced and available
    FbFetchPath forced = FbFetchPath::CopySnapshot;
    bool have_forced = false;
    if (f.roaa) { forced = FbFetchPath::RasterOrderAttachmentAccess; have_forced = true; }
    else if (f.local_read) { forced = FbFetchPath::DynamicRenderingLocalRead; have_forced = true; }
    else if (f.input_attachment) { forced = FbFetchPath::InputAttachmentBarrier; have_forced = true; }
    else if (f.copy_snapshot) { forced = FbFetchPath::CopySnapshot; have_forced = true; }
    if (have_forced && fbfetch_path_available(c, forced, impl)) {
        s.fbfetch = forced;
    } else {
        if (have_forced)
            s.force_rejected = true;
        s.fbfetch = FbFetchPath::CopySnapshot;
        for (size_t i = 0; i < fbfetch_path_count; i++) {
            const FbFetchPath p = static_cast<FbFetchPath>(i);
            if (fbfetch_path_available(c, p, impl)) {
                s.fbfetch = p;
                break;
            }
        }
    }
    // interlock stands in only when nothing better than the barrier rung exists (matches the pre-existing renderer rule:
    // ROAA wins over interlock)
    s.use_shader_interlock = c.shader_interlock && !f.roaa && s.fbfetch != FbFetchPath::RasterOrderAttachmentAccess
        && s.fbfetch != FbFetchPath::DynamicRenderingLocalRead && !f.input_attachment && !f.copy_snapshot;
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
    std::string r = "barriers=";
    r += to_string(s.barriers);
    r += " submit=";
    r += to_string(s.submit);
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
    else if (name == "local-read") f.local_read = true;
    else if (name == "input-attachment") f.input_attachment = true;
    else if (name == "copy-snapshot") f.copy_snapshot = true;
    else if (name == "color-only-resolve") f.color_only_resolve = true;
    else return false;
    return true;
}
inline constexpr std::array<std::string_view, 11> force_path_names = { "legacy-barriers", "sync2-barriers", "fences", "timeline",
    "normal-memory", "lazy-memory", "roaa", "local-read", "input-attachment", "copy-snapshot", "color-only-resolve" };

} // namespace renderer::vulkan
