#pragma once

#include <gxm/types.h>
#include <util/hash.h>

#include <algorithm>

namespace renderer {
// Other formats retain their existing cache identity. Alpha-only surfaces
// compile different fragment code despite sharing the same GXP and R8 storage.
inline Sha256Hash fragment_shader_variant_hash(const Sha256Hash &hash, SceGxmColorFormat format, bool raw_input = false) {
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

inline uint64_t alpha_surface_pipeline_key(uint64_t key, SceGxmColorFormat format) {
    return format == SCE_GXM_COLOR_FORMAT_U8_A ? key ^ 0x41d9a8e527c6b30fULL : key;
}
}
