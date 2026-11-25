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

#include <util/fs.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace shader {
struct Hints;
}

struct SceGxmProgram;
struct FeatureState;

namespace renderer {

struct ShadersHash;
struct State;

// Shaders.
bool get_shaders_cache_hashs(State &renderer);
void save_shaders_cache_hashs(State &renderer, std::vector<ShadersHash> &shaders_cache_hashs);
std::string load_glsl_shader(const SceGxmProgram &program, const FeatureState &features, const shader::Hints &hints, bool maskupdate, const fs::path &shader_cache_path, const fs::path &shader_log_path, const std::string &shader_version, bool shader_cache);
// cache_key: shader_cache_key() of the renderer; 0 keeps the unframed legacy .spv files (OpenGL).
std::vector<uint32_t> load_spirv_shader(const SceGxmProgram &program, const FeatureState &features, bool is_vulkan, const shader::Hints &hints, bool maskupdate, const fs::path &shader_cache_path, const fs::path &shader_log_path, const std::string &shader_version, bool shader_cache, uint64_t cache_key = 0);
std::string pre_load_shader_glsl(const fs::path &shader_path);
// A framed entry (renderer/shader_cache_file.h): empty when the file is missing, or when it is stale, truncated or
// corrupt (it is then deleted and counted, see ShaderCacheStats).
std::vector<uint32_t> pre_load_shader_spirv(const fs::path &shader_path, uint64_t cache_key);

// Identity of the translator + shader-affecting config of this renderer, and its 64-bit fingerprint stored in every entry.
uint64_t shader_cache_key(State &renderer);

// Counters of the shader cache for the current game (reset by get_shaders_cache_hashs), logged as one line.
struct ShaderCacheStats {
    std::atomic<uint32_t> loaded{ 0 }; // SPIR-V entries read from disk and verified
    std::atomic<uint32_t> generated{ 0 }; // shaders run through the translator
    std::atomic<uint32_t> discarded{ 0 }; // entries rejected (stale, truncated, corrupt)
    std::atomic<uint32_t> pipelines_compiled{ 0 }; // pipelines created without a hint from the pipeline cache (first use)
    std::atomic<uint32_t> pipelines_cached{ 0 }; // pipelines recreated from the driver pipeline cache
};
ShaderCacheStats &shader_cache_stats();
void log_shader_cache_summary(const char *phase);
// Waits for the background writer (entries and manifest queued so far are on disk afterwards).
void flush_shader_cache_writes();
// Crash-loop guard of the start-of-game pre-warm (renderer/shader_cache_file.h). begin returns false when the
// previous warm-ups died twice; end must be called when the warm-up loop is over.
bool shader_cache_begin_warmup(State &renderer);
void shader_cache_end_warmup(State &renderer);

} // namespace renderer
