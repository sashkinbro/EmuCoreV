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

#pragma once

// Decisions behind the GXM -> Vulkan sampler translation, kept free of Vulkan types so they can be unit tested on
// the host. vulkan/texture.cpp (configure_sampler) maps the results onto VkSamplerCreateInfo.

#include <gxm/types.h>

#include <algorithm>
#include <cstdint>

namespace renderer {

// What the device can do for samplers, filled once from the probed device capabilities.
struct SamplerSupport {
    // VK_KHR_sampler_mirror_clamp_to_edge (core in 1.2) is enabled on the device
    bool mirror_clamp_to_edge = false;
    // samplerAnisotropy is enabled on the device and limits.maxSamplerAnisotropy
    bool anisotropy = false;
    float max_anisotropy = 1.0f;
    // limits.maxSamplerLodBias
    float max_lod_bias = 0.0f;
    // the shader scales 2D texture coordinates per texture (texture viewport), which is how unnormalized (texel unit)
    // coordinates are done. Without it the VkSampler unnormalizedCoordinates mode is used.
    bool unnormalized_in_shader = false;
};

enum class AddressKind : uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    MirrorClampToEdge,
    ClampToBorder,
};

struct AddressTranslation {
    AddressKind kind;
    // the mode is a documented approximation of the GXM one
    bool approximate;
};

// GXM border semantics. The texture unit has no programmable border colour (there is no GXM call for it), the
// "border" modes sample a transparent black border: CLAMP_FULL_BORDER and CLAMP_HALF_BORDER clamp to it, the
// *_IGNORE_BORDER modes ignore it and behave like plain REPEAT / CLAMP.
// PROVISIONAL: HALF_BORDER (the border blends with the edge texel at the half-texel line) is mapped to the full
// border; the colour of the border is not backed by a firmware capture.
// MIRROR_CLAMP without VK_KHR_sampler_mirror_clamp_to_edge falls back to CLAMP (exact outside of (-1, 0)).
inline AddressTranslation translate_address(uint32_t gxm_mode, bool mirror_clamp_supported) {
    switch (gxm_mode) {
    case SCE_GXM_TEXTURE_ADDR_REPEAT:
    case SCE_GXM_TEXTURE_ADDR_REPEAT_IGNORE_BORDER:
        return { AddressKind::Repeat, false };
    case SCE_GXM_TEXTURE_ADDR_MIRROR:
        return { AddressKind::MirroredRepeat, false };
    case SCE_GXM_TEXTURE_ADDR_CLAMP:
    case SCE_GXM_TEXTURE_ADDR_CLAMP_IGNORE_BORDER:
        return { AddressKind::ClampToEdge, false };
    case SCE_GXM_TEXTURE_ADDR_MIRROR_CLAMP:
        return mirror_clamp_supported ? AddressTranslation{ AddressKind::MirrorClampToEdge, false }
                                      : AddressTranslation{ AddressKind::ClampToEdge, true };
    case SCE_GXM_TEXTURE_ADDR_CLAMP_FULL_BORDER:
        return { AddressKind::ClampToBorder, false };
    case SCE_GXM_TEXTURE_ADDR_CLAMP_HALF_BORDER:
        return { AddressKind::ClampToBorder, true };
    default:
        return { AddressKind::ClampToEdge, true };
    }
}

// The 6-bit LOD bias field is a signed fixed point value in 1/8 steps with an offset of 31 (-3.875 .. +4.0).
inline float translate_lod_bias(uint32_t field, float max_lod_bias) {
    const float bias = (static_cast<float>(field) - 31.0f) / 8.0f;
    return std::clamp(bias, -max_lod_bias, max_lod_bias);
}

struct AnisotropyPlan {
    bool enable;
    float max_anisotropy;
};

// Anisotropic filtering is a host option (GXM has no per sampler anisotropy). It needs the device feature, a
// linear filter (nearest filtering may hold packed data), and cannot be used together with unnormalized coordinates.
inline AnisotropyPlan plan_anisotropy(int host_setting, const SamplerSupport &support, bool mag_linear, bool min_linear, bool unnormalized_sampler) {
    AnisotropyPlan plan{ false, 1.0f };
    if (host_setting <= 1 || !support.anisotropy || unnormalized_sampler || (!mag_linear && !min_linear))
        return plan;
    plan.enable = true;
    plan.max_anisotropy = std::min(static_cast<float>(host_setting), support.max_anisotropy);
    return plan;
}

} // namespace renderer
