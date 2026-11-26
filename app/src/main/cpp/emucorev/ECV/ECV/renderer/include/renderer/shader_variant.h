#pragma once

#include <gxm/types.h>
#include <shader/hints.h>
#include <util/hash.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace renderer {
// Other formats retain their existing cache identity. Alpha-only surfaces
// compile different fragment code despite sharing the same GXP and R8 storage.
inline Sha256Hash fragment_shader_variant_hash(const Sha256Hash &hash, SceGxmColorFormat format, bool raw_input = false, uint32_t vertex_output_mask = 0xFFFFFFFFu) {
    if (vertex_output_mask != 0xFFFFFFFFu) {
        std::array<uint8_t, 40> tagged{};
        const auto surface_hash = fragment_shader_variant_hash(hash, format, raw_input);
        std::copy(surface_hash.begin(), surface_hash.end(), tagged.begin());
        tagged[32] = 'I'; tagged[33] = 'T'; tagged[34] = 'E'; tagged[35] = 'R';
        for (uint32_t i = 0; i < 4; ++i)
            tagged[36 + i] = static_cast<uint8_t>(vertex_output_mask >> (i * 8));
        return sha256(tagged.data(), tagged.size());
    }
    if (raw_input) {
        std::array<uint8_t, 36> tagged{};
        const auto surface_hash = fragment_shader_variant_hash(hash, format, false);
        std::copy(surface_hash.begin(), surface_hash.end(), tagged.begin());
        tagged[32] = 'R'; tagged[33] = 'A'; tagged[34] = 'W'; tagged[35] = 'I';
        return sha256(tagged.data(), tagged.size());
    }
    if (format != SCE_GXM_COLOR_FORMAT_U8_A)
        return hash;
    std::array<uint8_t, 36> tagged{};
    std::copy(hash.begin(), hash.end(), tagged.begin());
    tagged[32] = 'U';
    tagged[33] = '8';
    tagged[34] = '_';
    tagged[35] = 'A';
    return sha256(tagged.data(), tagged.size());
}

// Canonical byte description of every input of shader::convert_gxp (Vulkan target) other than the
// program binary and the device features. Two compilations with equal program hash and equal bytes
// produce the same SPIR-V, so this is the identity of a shader module and of its disk cache entry.
// Only what the recompiler reads for that stage is serialized, so unrelated state (for example the
// color format while compiling a vertex shader, or textures the program never samples) does not split
// the cache. Reads in shader/src/spirv_recompiler.cpp:
//  - hints.attributes (regIndex, format, componentCount): vertex only (scaled/rgb attribute emulation and
//    stripped-symbol attribute creation)
//  - hints.vertex_textures / fragment_textures: component type and count of every sampled slot
//  - hints.color_format, raw_color_attachment_input, vertex_output_mask: fragment only
//  - maskupdate: changes the generated entry point
// used_textures_mask is gxp::get_textures_used() of the program (bit i = texture slot i).
inline std::vector<uint8_t> vulkan_shader_variant_key_bytes(bool is_vertex, const shader::Hints &hints, uint32_t used_textures_mask, bool maskupdate) {
    std::vector<uint8_t> bytes;
    bytes.reserve(64 + (is_vertex && hints.attributes ? hints.attributes->size() * 7 : 0));
    const auto put = [&](uint32_t value, int byte_count) {
        for (int i = 0; i < byte_count; ++i)
            bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
    };

    put('V' | ('S' << 8) | ('V' << 16) | ('1' << 24), 4);
    put(is_vertex ? 1 : 0, 1);
    put(maskupdate ? 1 : 0, 1);
    put(used_textures_mask, 4);

    const SceGxmTextureFormat *textures = is_vertex ? hints.vertex_textures : hints.fragment_textures;
    for (uint32_t i = 0; i < SCE_GXM_MAX_TEXTURE_UNITS; ++i) {
        if (used_textures_mask & (1u << i))
            put(static_cast<uint32_t>(textures[i]), 4);
    }

    if (is_vertex) {
        if (hints.attributes) {
            put(1, 1);
            put(static_cast<uint32_t>(hints.attributes->size()), 4);
            for (const SceGxmVertexAttribute &attribute : *hints.attributes) {
                put(attribute.regIndex, 2);
                put(static_cast<uint32_t>(attribute.format), 4);
                put(attribute.componentCount, 1);
            }
        } else {
            put(0, 1);
        }
    } else {
        put(static_cast<uint32_t>(hints.color_format), 4);
        put(hints.raw_color_attachment_input ? 1 : 0, 1);
        put(hints.vertex_output_mask, 4);
        // only a multisampled fetch variant carries this byte: the identity of every other shader module is unchanged
        if (hints.ms_color_input)
            put(0x4D, 1);
    }
    return bytes;
}

inline Sha256Hash vulkan_shader_variant_hash(const Sha256Hash &program_hash, bool is_vertex, const shader::Hints &hints, uint32_t used_textures_mask, bool maskupdate) {
    std::vector<uint8_t> data(program_hash.begin(), program_hash.end());
    const std::vector<uint8_t> key = vulkan_shader_variant_key_bytes(is_vertex, hints, used_textures_mask, maskupdate);
    data.insert(data.end(), key.begin(), key.end());
    return sha256(data.data(), data.size());
}

inline uint64_t alpha_surface_pipeline_key(uint64_t key, SceGxmColorFormat format) {
    return format == SCE_GXM_COLOR_FORMAT_U8_A ? key ^ 0x41d9a8e527c6b30fULL : key;
}
}
