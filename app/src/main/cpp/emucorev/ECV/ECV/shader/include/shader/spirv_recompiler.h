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

#include <gxm/types.h>
#include <shader/hints.h>
#include <shader/usse_translator_entry.h>
#include <shader/usse_translator_types.h>
#include <shader/usse_types.h>

#include <features/state.h>

#include <string>
#include <vector>

namespace shader {

static constexpr int COLOR_ATTACHMENT_TEXTURE_SLOT_IMAGE = 0;
static constexpr int MASK_TEXTURE_SLOT_IMAGE = 1;
static constexpr int COLOR_ATTACHMENT_RAW_TEXTURE_SLOT_IMAGE = 3;
static constexpr uint32_t CURRENT_VERSION = 31; // 28: shader module keys cover every Hints field the recompiler reads; 29: native-colour o0 register layout, raw f16 register moves; 30: native-colour half flags; 31: native-colour o0 is packed bytes only when the program packs
// fragment shader using the rendering surface as a storage image (because of shader interlock) have a line
// layout (constant_id = GAMMA_CORRECTION_SPECIALIZATIO_ID) const bool is_srgb = false;
// Setting this constant to true performs gamma correction in the shader
static constexpr uint32_t GAMMA_CORRECTION_SPECIALIZATION_ID = 0;

enum struct Target {
    GLSLOpenGL,
    SpirVOpenGL,
    SpirVVulkan
};

struct GeneratedShader {
    std::string glsl;
    usse::SpirvCode spirv;
    // USSE instructions the translator dropped (counted in every build type, 0 = fully translated)
    usse::UsseTranslationStats usse_stats;
};

// Dump generated SPIR-V disassembly up to this point
void spirv_disasm_print(const usse::SpirvCode &spirv_binary, std::string *spirv_dump = nullptr);

// the returned object will only have its glsl or spirv field non-empty depending on the target
// Cache-only callbacks can disable disassembly without suppressing GLSL output.
GeneratedShader convert_gxp(const SceGxmProgram &program, const std::string &shader_hash, const FeatureState &features, const Target target, const Hints &hints, bool maskupdate = false,
    bool force_shader_debug = false, const std::function<bool(const std::string &ext, const std::string &dump)> &dumper = nullptr,
    bool dump_disassembly = true);

void convert_gxp_to_glsl_from_filepath(const std::string &shader_filepath_utf8);

} // namespace shader
