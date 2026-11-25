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

#include <renderer/shaders.h>

#include <renderer/vulkan/state.h>

#include <gxm/functions.h>
#include <gxm/types.h>
#include <renderer/state.h>
#include <renderer/types.h>
#include <shader/spirv_recompiler.h>
#include <renderer/shader_cache_file.h>
#include <renderer/shader_variant.h>
#include <shader/translator_identity.h>
#include <util/fs.h>
#include <util/log.h>

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace renderer {

ShaderCacheStats &shader_cache_stats() {
    static ShaderCacheStats stats;
    return stats;
}

void log_shader_cache_summary(const char *phase) {
    const ShaderCacheStats &s = shader_cache_stats();
    LOG_INFO("shader cache: {}: spirv {} loaded from disk, {} generated, {} discarded; pipelines {} compiled, {} from pipeline cache", phase, s.loaded.load(), s.generated.load(), s.discarded.load(), s.pipelines_compiled.load(), s.pipelines_cached.load());
}

static std::filesystem::path to_std_path(const fs::path &path) {
    return std::filesystem::path(path.native());
}

static ShaderCacheWriter &shader_cache_writer() {
    static ShaderCacheWriter writer;
    return writer;
}

void flush_shader_cache_writes() {
    shader_cache_writer().flush();
}

static std::filesystem::path warmup_marker_path(const State &renderer) {
    return to_std_path(renderer.shaders_path / shader_cache_warmup_marker_name);
}

bool shader_cache_begin_warmup(State &renderer) {
    uint32_t unclean = 0;
    if (begin_shader_cache_warmup(warmup_marker_path(renderer), &unclean))
        return true;
    LOG_WARN("shader cache: pre-warm disabled ({} unclean runs in a row), shaders load on demand", unclean);
    return false;
}

void shader_cache_end_warmup(State &renderer) {
    end_shader_cache_warmup(warmup_marker_path(renderer));
}

static ShaderCacheIdentity shader_cache_identity(State &renderer) {
    ShaderCacheIdentity id;
    id.translator_hash = shader::translator_source_hash();
    id.translator_version = shader::CURRENT_VERSION;
    id.features_mask = renderer.get_features_mask();
    return id;
}

uint64_t shader_cache_key(State &renderer) {
    return shader_cache_identity_key(shader_cache_identity(renderer));
}

// One line per reason and run for the file that triggered it, the rest at debug level: a translator change would
// otherwise log once per cached shader.
static void log_shader_cache_discard(ShaderCacheReject reason, const fs::path &file) {
    static std::atomic<uint32_t> logged_reasons{ 0 };
    const uint32_t bit = 1u << static_cast<uint32_t>(reason);
    if (logged_reasons.fetch_or(bit) & bit)
        LOG_DEBUG("shader cache: discarded ({}) {}", to_string(reason), file.filename().string());
    else
        LOG_WARN("shader cache: discarded ({}) {}", to_string(reason), file.filename().string());
}

static void discard_shader_cache(State &renderer, ShaderCacheReject reason) {
    flush_shader_cache_writes();
    boost::system::error_code ec;
    fs::remove_all(renderer.shaders_path, ec);
    fs::remove_all(renderer.shaders_log_path, ec);
    LOG_WARN("shader cache: discarded ({})", to_string(reason));
}

bool get_shaders_cache_hashs(State &renderer) {
    const std::string hash_file_name = fmt::format("hashs-{}.dat", (renderer.current_backend == Backend::OpenGL) ? "gl" : "vk");

    flush_shader_cache_writes();
    ShaderCacheStats &stats = shader_cache_stats();
    stats.loaded = 0;
    stats.generated = 0;
    stats.discarded = 0;
    stats.pipelines_compiled = 0;
    stats.pipelines_cached = 0;

    renderer.shaders_cache_hashs.clear();

    // leftovers of writes interrupted by a crash
    remove_stale_shader_cache_temp_files(to_std_path(renderer.shaders_path));

    std::vector<uint8_t> file;
    if (!read_file_bytes(to_std_path(renderer.shaders_path / hash_file_name), file))
        return false;

    std::vector<ShaderCacheManifestEntry> entries;
    const ShaderCacheReject reject = decode_shader_cache_manifest(file.data(), file.size(), shader_cache_identity(renderer), entries);
    if (reject != ShaderCacheReject::None) {
        discard_shader_cache(renderer, reject);
        return false;
    }

    if (renderer.current_backend == Backend::Vulkan) {
        // Read the pipeline cache
        dynamic_cast<vulkan::VKState &>(renderer).pipeline_cache.read_pipeline_cache();
    }

    renderer.shaders_cache_hashs.reserve(entries.size());
    for (const ShaderCacheManifestEntry &entry : entries)
        renderer.shaders_cache_hashs.push_back({ entry.frag, entry.vert });

    LOG_INFO("shader cache: manifest accepted ({} shaders)", renderer.shaders_cache_hashs.size());
    return !renderer.shaders_cache_hashs.empty();
}

void save_shaders_cache_hashs(State &renderer, std::vector<ShadersHash> &shaders_cache_hashs) {
    fs::create_directories(renderer.shaders_path);
    const std::string hash_file_name = fmt::format("hashs-{}.dat", (renderer.current_backend == Backend::OpenGL) ? "gl" : "vk");

    std::vector<ShaderCacheManifestEntry> entries;
    entries.reserve(shaders_cache_hashs.size());
    for (const auto &hash : shaders_cache_hashs)
        entries.push_back({ hash.frag, hash.vert });

    const std::vector<uint8_t> bytes = encode_shader_cache_manifest(shader_cache_identity(renderer), entries);
    shader_cache_writer().enqueue(to_std_path(renderer.shaders_path / hash_file_name), bytes);
}

// A framed SPIR-V entry. A missing file is a plain miss; anything else that does not verify is deleted so the
// shader is regenerated (and rewritten) instead of being fed to the driver.
static std::vector<uint32_t> load_spirv_entry(const fs::path &path, uint64_t cache_key) {
    std::vector<uint8_t> file;
    if (!read_file_bytes(to_std_path(path), file))
        return {};

    std::vector<uint32_t> words;
    const ShaderCacheReject reject = decode_shader_cache_entry(file.data(), file.size(), cache_key, words);
    if (reject != ShaderCacheReject::None) {
        boost::system::error_code ec;
        fs::remove(path, ec);
        shader_cache_stats().discarded++;
        log_shader_cache_discard(reject, path);
        return {};
    }
    shader_cache_stats().loaded++;
    return words;
}

static bool load_shader(const fs::path &shader_name, char **destination, std::size_t &size_read) {
    fs::ifstream is(shader_name, fs::ifstream::binary);
    if (!is) {
        return false;
    }

    is.seekg(0, fs::ifstream::end);
    size_read = is.tellg();
    is.seekg(0);

    if (size_read == 0) {
        return false;
    }

    if (destination == nullptr) {
        return true;
    }

    is.read(*destination, size_read);
    return true;
}

static Sha256Hash get_shader_hash(const SceGxmProgram &program) {
    const Sha256Hash hash_bytes = sha256(&program, program.size);
    return hash_bytes;
}

template <typename R>
static R load_shader_generic(const fs::path &shader_path) {
    std::size_t read_size = 0;
    R source;

    if (load_shader(shader_path, nullptr, read_size)) {
        source.resize((read_size + sizeof(typename R::value_type) - 1) / sizeof(typename R::value_type));

        char *dest_pointer = reinterpret_cast<char *>(source.data());
        load_shader(shader_path, &dest_pointer, read_size);
    }

    return source;
}

static shader::GeneratedShader load_shader_generic(shader::Target target, const SceGxmProgram &program, const FeatureState &features, const shader::Hints &hints, bool maskupdate, const fs::path &shader_cache_path, const fs::path &shaderlog_path, const char *shader_type_str, const std::string &shader_version, bool shader_cache, uint64_t cache_key) {
    // TODO: no need to recompute the hash here
    const auto hash = get_shader_hash(program);
    // Vulkan: the module identity covers every Hints field the recompiler reads (see
    // vulkan_shader_variant_key_bytes) and must match PipelineCache::retrieve_shader, which loads
    // these files by that hash. OpenGL keeps its program-level keys.
    const std::string hash_text = hex_string(target == shader::Target::SpirVVulkan
            ? vulkan_shader_variant_hash(hash, !program.is_fragment(), hints, static_cast<uint32_t>(gxp::get_textures_used(program).to_ulong()), maskupdate)
            : (program.is_fragment() ? fragment_shader_variant_hash(hash, hints.color_format, hints.raw_color_attachment_input, hints.vertex_output_mask) : hash));
    // Set Shader Hash with Version
    const std::string hash_hex_ver = fmt::format("{}-{}", shader_version, hash_text);
    const auto get_shader_path = [&](const char *ext) {
        return shader_cache_path / fmt::format("{}.{}", hash_hex_ver, ext);
    };
    const auto get_shaderlog_path = [&](const char *ext) {
        return shaderlog_path / fmt::format("{}.{}", hash_hex_ver, ext);
    };

    const auto shader_path = get_shader_path(shader_type_str);
    if (shader_cache) {
        if (target == shader::Target::GLSLOpenGL) {
            std::string source = load_shader_generic<std::string>(shader_path);
            if (!source.empty()) {
                return { source, std::vector<uint32_t>() };
            }
        } else {
            std::vector<uint32_t> source = cache_key ? load_spirv_entry(get_shader_path("spv"), cache_key) : load_shader_generic<std::vector<uint32_t>>(get_shader_path("spv"));
            if (!source.empty())
                return { "", source };
        }
    }

    LOG_DEBUG("Generating {} shader {}", shader_type_str, hash_text);

#if defined(NDEBUG)
    constexpr bool write_shader_dumps = false;
#elif defined(__ANDROID__)
    const bool write_shader_dumps = spdlog::should_log(spdlog::level::debug);
#else
    constexpr bool write_shader_dumps = true;
#endif

    auto shader_log_path = get_shaderlog_path("gxp");

    if (write_shader_dumps) {
        fs::create_directories(shaderlog_path);

        // Dump gxp binary
        fs_utils::dump_data(shader_log_path, &program, program.size);
    }
    const auto write_data_with_ext = [&](const std::string &ext, const std::string &data) {
        fs::path out_path;
        if (ext == shader_type_str) {
            out_path = shader_path;
        } else {
            if (!write_shader_dumps)
                return true;
            out_path = shader_log_path;
            out_path.replace_extension(ext);
        }
        fs_utils::dump_data(out_path, data.c_str(), data.size());
        return true;
    };

    shader::GeneratedShader source = shader::convert_gxp(program, hash_text, features, target, hints, maskupdate, false, write_data_with_ext, write_shader_dumps);
    shader_cache_stats().generated++;

    // Copy shader generate to shaders cache
    const auto shaders_cache_path = fs::path(shader_cache_path);
    fs::create_directories(shaders_cache_path);

    if (target != shader::Target::GLSLOpenGL) {
        const auto shader_dst_path = get_shader_path("spv");
        if (cache_key) {
            // a failed translation (empty SPIR-V) is not cached; written by the background writer, atomically (a crash or a full disk never leaves a half entry)
            if (!source.spirv.empty()) {
                shader_cache_writer().enqueue(to_std_path(shader_dst_path), encode_shader_cache_entry(cache_key, source.spirv.data(), source.spirv.size()));
            }
        } else {
            fs_utils::dump_data(shader_dst_path, source.spirv.data(), sizeof(uint32_t) * source.spirv.size());
        }
    }

    return source;
}

std::string load_glsl_shader(const SceGxmProgram &program, const FeatureState &features, const shader::Hints &hints, bool maskupdate, const fs::path &shader_cache_path, const fs::path &shader_log_path, const std::string &shader_version, bool shader_cache) {
    SceGxmProgramType program_type = program.get_type();

    auto shader_type_to_str = [](SceGxmProgramType type) {
        return (type == SceGxmProgramType::Vertex) ? "vert" : ((type == SceGxmProgramType::Fragment) ? "frag" : "unknown");
    };

    const char *shader_type_str = shader_type_to_str(program_type);

    return load_shader_generic(shader::Target::GLSLOpenGL, program, features, hints, maskupdate, shader_cache_path, shader_log_path, shader_type_str, shader_version, shader_cache, 0).glsl;
}

std::vector<uint32_t> load_spirv_shader(const SceGxmProgram &program, const FeatureState &features, bool is_vulkan, const shader::Hints &hints, bool maskupdate, const fs::path &shader_cache_path, const fs::path &shader_log_path, const std::string &shader_version, bool shader_cache, uint64_t cache_key) {
    const shader::Target target = is_vulkan ? shader::Target::SpirVVulkan : shader::Target::SpirVOpenGL;
    auto shader_type_to_str = [](SceGxmProgramType type) {
        return (type == SceGxmProgramType::Vertex) ? "vert.spv.txt" : ((type == SceGxmProgramType::Fragment) ? "frag.spv.txt" : "unknown.spv.txt");
    };
    const char *shader_type_str = shader_type_to_str(program.get_type());

    return load_shader_generic(target, program, features, hints, maskupdate, shader_cache_path, shader_log_path, shader_type_str, shader_version, shader_cache, cache_key).spirv;
}

std::string pre_load_shader_glsl(const fs::path &shader_path) {
    return load_shader_generic<std::string>(shader_path);
}

std::vector<uint32_t> pre_load_shader_spirv(const fs::path &shader_path, uint64_t cache_key) {
    return load_spirv_entry(shader_path, cache_key);
}

} // namespace renderer
