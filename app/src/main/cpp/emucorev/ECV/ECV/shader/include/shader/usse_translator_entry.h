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

//
// Decoder/translator usage (exposed API)
//

#include <cstdint>
#include <vector>

struct SceGxmProgram;
struct FeatureState;

namespace spv {
class Builder;
class Function;
} // namespace spv

namespace shader::usse {
struct SpirvShaderParameters;
struct NonDependentTextureQueryCallInfo;

namespace utils {
struct SpirvUtilFunctions;
}

using NonDependentTextureQueryCallInfos = std::vector<NonDependentTextureQueryCallInfo>;

// Instructions the USSE translator could not translate. Counted in every build type (the decoder logs
// are compiled out in release) so a shader that silently lost instructions can be detected.
struct UsseTranslationStats {
    static constexpr uint32_t max_recorded_words = 4;

    // no decoder table entry matched the instruction word
    uint32_t unmatched_count = 0;
    // a handler matched but reported the instruction as unsupported (returned false)
    uint32_t unsupported_count = 0;
    // first offending instruction words (at most max_recorded_words)
    uint32_t recorded_word_count = 0;
    uint64_t recorded_words[max_recorded_words] = {};

    uint32_t total() const { return unmatched_count + unsupported_count; }

    void record(bool unmatched, uint64_t word) {
        if (unmatched)
            ++unmatched_count;
        else
            ++unsupported_count;
        if (recorded_word_count < max_recorded_words)
            recorded_words[recorded_word_count++] = word;
    }
};

void convert_gxp_usse_to_spirv(spv::Builder &b, const SceGxmProgram &program, const FeatureState &features, const SpirvShaderParameters &parameters, utils::SpirvUtilFunctions &utils,
    spv::Function *begin_hook_func, spv::Function *end_hook_func, const NonDependentTextureQueryCallInfos &queries, const uint32_t render_info_id, spv::Function *spv_func_main, std::vector<uint32_t> &interfaces, UsseTranslationStats *stats_out = nullptr);

} // namespace shader::usse
