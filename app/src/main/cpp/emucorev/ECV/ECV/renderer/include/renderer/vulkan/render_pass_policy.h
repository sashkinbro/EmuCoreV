// Pure decisions about render-pass attachment load/store and depth-stencil resolve (host-tested:
// app/src/test/core-host/render_pass_policy_tests.cpp). Nothing here touches Vulkan.
#pragma once

#include <renderer/vulkan/gpu_caps.h>

#include <cstdint>

namespace renderer::vulkan {

// ---- depth store derivation ------------------------------------------------------------------------------------------
// The render pass object is chosen when a scene begins and its storeOp is baked in, but the scene may restart its pass later
// (mid-scene flush, cast copies, framebuffer-fetch restarts, depth transfers: scene.cpp, context.cpp) and each restart LOADs what the
// previous instance stored. The store can therefore only be dropped when the content is provably dead:
//  * the surface is backed by guest memory       -> any later scene / sampler / write-back may read it: store
//  * the scene can restart its pass              -> the continuation loads it: store
//  * otherwise (unbacked depth AND a scene that cannot restart)                       -> DONT_CARE
// A scene that cannot restart is not knowable at scene begin (the guest decides later), so `scene_may_restart` is true for every
// scene today and the result is the conservative "always store" the renderer used to hard-code; the function exists so the rule is
// explicit, tested, and ready for a producer of the missing fact (command look-ahead).
struct DepthStoreInput {
    bool guest_force_store = false; // the game asked for the depth to be written back
    bool backed_by_guest_surface = false;
    bool scene_may_restart = true;
};
inline bool derive_depth_store(const DepthStoreInput &in) {
    if (in.guest_force_store || in.backed_by_guest_surface || in.scene_may_restart)
        return true;
    return false;
}

// What is knowable when a scene begins about pass restarts. Every restart source is listed; the ones the guest or the draw stream decide later
// are `true` unless a look-ahead proves otherwise (none exists: the render thread cannot see the rest of the scene).
struct SceneRestartFacts {
    bool shader_interlock = false; // a framebuffer-fetch draw ends and restarts the pass (scene.cpp)
    bool shell_snapshot = false; // SceShell glow snapshot: copy + restart per fetch draw
    bool macroblock_sync = false; // render target with macroblock sync: one pass per macroblock
    bool depth_backed = false; // guest depth surface: depth transfers / write-backs may interrupt the scene
    bool guest_mid_scene_flush_possible = true; // sceGxmMidSceneFlush is decided by the guest mid-scene
    bool texture_cast_possible = true; // a sampled surface that needs a cast copy is discovered when the draw binds it
};
inline bool scene_may_restart(const SceneRestartFacts &f) {
    return f.shader_interlock || f.shell_snapshot || f.macroblock_sync || f.depth_backed || f.guest_mid_scene_flush_possible || f.texture_cast_possible;
}

// Colour attachment: DONT_CARE load when the previous content is not needed.
inline bool derive_color_load(bool backed_by_guest_surface) {
    return backed_by_guest_surface;
}

// ---- depth/stencil MSAA resolve (VK_KHR_depth_stencil_resolve) ---------------------------------------------------------------
// bit values match VkResolveModeFlagBits
inline constexpr uint32_t resolve_mode_sample_zero = 1, resolve_mode_average = 2, resolve_mode_min = 4, resolve_mode_max = 8;

enum class DsResolveKind : uint8_t {
    None, // single sample, nothing to resolve
    ResolveAttachment, // pDepthStencilResolveAttachment in the render pass with `depth_mode` / `stencil_mode`
    ShaderCopy, // no usable mode: a full-screen pass writes sample 0 of the multisampled depth (gl_FragDepth) into the 1x depth image
};

struct DsResolvePlan {
    DsResolveKind kind = DsResolveKind::None;
    uint32_t depth_mode = 0; // VkResolveModeFlagBits, 0 when not resolved with an attachment
    uint32_t stencil_mode = 0;
};

// GXM resolves depth by taking the first sample (what the real hardware's tile resolve does for the display/sampled copy).
// There is deliberately no fallback to MIN/MAX/AVERAGE: they change the picture, so when SAMPLE_ZERO is not offered the shader
// copy (exact) is used instead.
inline DsResolvePlan plan_ds_resolve(const GpuCaps &c, uint32_t samples, bool has_stencil, bool allow_attachment_resolve = true) {
    DsResolvePlan p;
    if (samples <= 1 || !c.supports_samples(samples))
        return p;
    const bool depth_ok = c.depth_stencil_resolve && (c.ds_resolve_depth_modes & resolve_mode_sample_zero);
    const bool stencil_ok = !has_stencil || (c.ds_resolve_stencil_modes & resolve_mode_sample_zero);
    // depth and stencil with different modes need independentResolve(None); both use SAMPLE_ZERO so they are always legal
    if (allow_attachment_resolve && depth_ok && stencil_ok) {
        p.kind = DsResolveKind::ResolveAttachment;
        p.depth_mode = resolve_mode_sample_zero;
        p.stencil_mode = has_stencil ? resolve_mode_sample_zero : 0;
        return p;
    }
    p.kind = DsResolveKind::ShaderCopy;
    return p;
}

} // namespace renderer::vulkan
