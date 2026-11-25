// EmuCoreV: depth/stencil model of the Vulkan renderer (P5).
//
// Pure decisions only (no Vulkan calls), so that they can be unit tested on the host:
//  - which host format represents each GXM depth/stencil format, from the probed format features
//  - which aspects the guest surface has (a surface without depth never depth-tests, without stencil never stencil-tests)
//  - how the depth bias of each face is kept and how its units follow the precision of the chosen host format
//  - how a two-sided draw is issued when the guest gives the two faces different depth state
//
// The Vita draws one primitive with the front or the back state of the face it hits. Vulkan has per-face state only for
// stencil, so every other per-face difference is drawn as two passes split by facing (culling the other face).
#pragma once

#include <gxm/types.h>

// vkutil.h sets the vulkan.hpp configuration macros and platform defines before including it: never include vulkan.hpp first
#include <vkutil/vkutil.h>

#include <array>
#include <cstdint>
#include <initializer_list>

namespace renderer::vulkan::dsmodel {

// GXM depth/stencil formats grouped by what they store. DF32M is DF32 with a different memory layout, it renders the same.
enum class DsClass : uint8_t {
    D16, // 16 bit depth, no stencil
    DF32, // 32 bit float depth, no stencil (DF32, DF32M)
    S8, // stencil only
    DF32S8, // float depth + stencil (DF32_S8, DF32M_S8)
    S8D24, // 24 bit depth + stencil
    Count
};

// A format value of 0 (disabled surface) or an unknown one keeps the pre-model behaviour: depth and stencil both present.
inline DsClass ds_class(const SceGxmDepthStencilFormat format) {
    switch (format) {
    case SCE_GXM_DEPTH_STENCIL_FORMAT_D16: return DsClass::D16;
    case SCE_GXM_DEPTH_STENCIL_FORMAT_DF32:
    case SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M: return DsClass::DF32;
    case SCE_GXM_DEPTH_STENCIL_FORMAT_S8: return DsClass::S8;
    case SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24: return DsClass::S8D24;
    case SCE_GXM_DEPTH_STENCIL_FORMAT_DF32_S8:
    case SCE_GXM_DEPTH_STENCIL_FORMAT_DF32M_S8:
    default: return DsClass::DF32S8;
    }
}

inline bool gxm_has_depth(const DsClass c) {
    return c != DsClass::S8;
}
inline bool gxm_has_stencil(const DsClass c) {
    return c != DsClass::D16 && c != DsClass::DF32;
}

inline bool host_has_depth(const vk::Format f) {
    switch (f) {
    case vk::Format::eD16Unorm:
    case vk::Format::eD32Sfloat:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint: return true;
    default: return false;
    }
}
inline bool host_has_stencil(const vk::Format f) {
    switch (f) {
    case vk::Format::eS8Uint:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint: return true;
    default: return false;
    }
}
inline vk::ImageAspectFlags host_aspects(const vk::Format f) {
    vk::ImageAspectFlags a{};
    if (host_has_depth(f))
        a |= vk::ImageAspectFlagBits::eDepth;
    if (host_has_stencil(f))
        a |= vk::ImageAspectFlagBits::eStencil;
    return a;
}

// Candidate host formats the probe asks about.
inline constexpr std::array<vk::Format, 6> host_candidates = {
    vk::Format::eD16Unorm, vk::Format::eD32Sfloat, vk::Format::eS8Uint,
    vk::Format::eD16UnormS8Uint, vk::Format::eD24UnormS8Uint, vk::Format::eD32SfloatS8Uint
};

// Which candidates the device can use as depth/stencil attachment that is also sampled (the guest reads its depth as a texture).
struct HostDsSupport {
    uint32_t bits = 0;

    static constexpr int index_of(const vk::Format f) {
        for (size_t i = 0; i < host_candidates.size(); i++)
            if (host_candidates[i] == f)
                return static_cast<int>(i);
        return -1;
    }
    void set(const vk::Format f, const bool usable) {
        const int i = index_of(f);
        if (i >= 0) {
            if (usable)
                bits |= 1u << i;
            else
                bits &= ~(1u << i);
        }
    }
    bool has(const vk::Format f) const {
        const int i = index_of(f);
        return i >= 0 && (bits & (1u << i)) != 0;
    }
};

// Feature test used by the probe: attachment use plus sampling.
inline bool format_usable(const vk::FormatFeatureFlags optimal_features) {
    constexpr vk::FormatFeatureFlags needed = vk::FormatFeatureFlagBits::eDepthStencilAttachment | vk::FormatFeatureFlagBits::eSampledImage;
    return (optimal_features & needed) == needed;
}

// The best host format for a GXM class. The Vita rasterises with an on-chip depth AND stencil buffer for every scene whether or not
// the game gave the surface memory for them (Killzone draws depth-tested scenes into a surface of format S8 without a depth
// pointer), so EVERY class needs a host format that has both aspects: the classes only differ in which precision comes first.
// Depth-only formats are the last resort of a device without any depth+stencil format (the stencil is then lost, the caller logs it).
// Returns eUndefined if nothing usable exists.
inline vk::Format select_host_format(const DsClass c, const HostDsSupport &s) {
    using F = vk::Format;
    const auto first = [&](std::initializer_list<F> list) {
        for (const F f : list)
            if (s.has(f))
                return f;
        return F::eUndefined;
    };
    switch (c) {
    case DsClass::D16:
        return first({ F::eD16UnormS8Uint, F::eD24UnormS8Uint, F::eD32SfloatS8Uint, F::eD16Unorm, F::eD32Sfloat });
    case DsClass::DF32:
    case DsClass::DF32S8:
        return first({ F::eD32SfloatS8Uint, F::eD24UnormS8Uint, F::eD16UnormS8Uint, F::eD32Sfloat, F::eD16Unorm });
    case DsClass::S8:
    case DsClass::S8D24:
        return first({ F::eD24UnormS8Uint, F::eD32SfloatS8Uint, F::eD16UnormS8Uint, F::eD32Sfloat, F::eD16Unorm });
    default:
        return F::eUndefined;
    }
}

// The one format used for attachments with no guest surface behind them and as the fallback of everything else.
inline vk::Format select_combined_format(const HostDsSupport &s) {
    return select_host_format(DsClass::DF32S8, s);
}

// Depth precision in bits (a float depth counts its 24 bit significand: the bias unit of a float format scales with the
// exponent of z, near the far plane it is 2^-24).
inline int host_precision_bits(const vk::Format f) {
    switch (f) {
    case vk::Format::eD16Unorm:
    case vk::Format::eD16UnormS8Uint: return 16;
    default: return 24;
    }
}
inline int guest_precision_bits(const DsClass c) {
    return c == DsClass::D16 ? 16 : 24;
}

// A GXM depth bias unit is the smallest depth step of the guest depth buffer; the Vulkan constant factor is a multiple of the
// smallest step of the HOST depth buffer. Scale the units so that the offset covers the same depth distance.
inline float depth_bias_units_scale(const DsClass guest, const vk::Format host) {
    const int shift = host_precision_bits(host) - guest_precision_bits(guest);
    return shift >= 0 ? static_cast<float>(1u << shift) : 1.0f / static_cast<float>(1u << -shift);
}

// sceGxmSetFront/BackDepthBias(factor, units): factor is the slope scale, units the constant offset.
struct DepthBias {
    int factor = 0;
    int units = 0;
    bool operator==(const DepthBias &) const = default;
};

// Per-face bias store. The two calls are independent: a Back call never touches the front values.
struct DepthBiasFaces {
    DepthBias front;
    DepthBias back;

    void set(const bool is_front, const int factor, const int units) {
        (is_front ? front : back) = DepthBias{ factor, units };
    }
    const DepthBias &face(const bool is_back) const {
        return is_back ? back : front;
    }
};

// The per-face state that cannot be expressed per face in Vulkan.
struct FaceState {
    SceGxmDepthFunc depth_func = SCE_GXM_DEPTH_FUNC_LESS_EQUAL;
    SceGxmDepthWriteMode depth_write = SCE_GXM_DEPTH_WRITE_ENABLED;
    SceGxmPolygonMode polygon_mode = SCE_GXM_POLYGON_MODE_TRIANGLE_FILL;
    SceGxmFragmentProgramMode fragment_mode = SCE_GXM_FRAGMENT_PROGRAM_ENABLED;
    DepthBias bias;
    bool operator==(const FaceState &) const = default;
};

enum class FacePlan : uint8_t {
    Front, // one draw with the front state
    Back, // one draw with the back state (only back faces can survive the cull mode)
    Split // two draws: front faces with the front state, back faces with the back state
};

// With two-sided disabled the back state is not used at all. Equal states never need a split. A cull mode that removes one
// facing leaves a single face, which then takes that face's state. GXM culls by winding with a counter clockwise front face,
// so SCE_GXM_CULL_CW removes the back faces and SCE_GXM_CULL_CCW the front faces.
inline FacePlan plan_faces(const bool two_sided, const SceGxmCullMode cull, const FaceState &front, const FaceState &back) {
    if (!two_sided || front == back)
        return FacePlan::Front;
    switch (cull) {
    case SCE_GXM_CULL_CW: return FacePlan::Front;
    case SCE_GXM_CULL_CCW: return FacePlan::Back;
    default: return FacePlan::Split;
    }
}

// What one pipeline variant of a draw is built from.
enum class PassVariant : uint8_t {
    Front = 0, // front state, the guest cull mode
    Back = 1, // back state, the guest cull mode
    SplitFront = 2, // front state, back faces culled
    SplitBack = 3 // back state, front faces culled
};

struct PassList {
    PassVariant variant[2] = {};
    uint32_t count = 0;
};

inline PassList passes_for(const FacePlan plan) {
    switch (plan) {
    case FacePlan::Front: return { { PassVariant::Front, PassVariant::Front }, 1 };
    case FacePlan::Back: return { { PassVariant::Back, PassVariant::Back }, 1 };
    default: return { { PassVariant::SplitFront, PassVariant::SplitBack }, 2 };
    }
}

inline bool variant_uses_back_state(const PassVariant v) {
    return v == PassVariant::Back || v == PassVariant::SplitBack;
}

} // namespace renderer::vulkan::dsmodel
