// Pure decisions about render-pass attachment load/store and depth-stencil resolve (host-tested:
// app/src/test/core-host/render_pass_policy_tests.cpp). Nothing here touches Vulkan.
#pragma once

#include <renderer/vulkan/gpu_caps.h>

#include <cstdint>

namespace renderer::vulkan {

// ---- depth store derivation ------------------------------------------------------------------------------------------
// Decided when a pass instance ENDS (VKContext::emit_pass), because the instance is begun then (deferred recording):
//  * the surface is backed by guest memory   -> any later scene / sampler / write-back may read it: store
//  * the instance is followed by another one of the same scene (restart: flush, cast, fbfetch split, macroblock) -> the next one loads it: store
//  * otherwise (unbacked depth AND the last instance of the scene, sceGxmEndScene) -> dead, DONT_CARE
struct DepthStoreInput {
    bool guest_force_store = false; // the game asked for the depth to be written back
    bool backed_by_guest_surface = false;
    bool followed_by_another_instance = true;
};
inline bool derive_depth_store(const DepthStoreInput &in) {
    if (in.guest_force_store || in.backed_by_guest_surface || in.followed_by_another_instance)
        return true;
    return false;
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
