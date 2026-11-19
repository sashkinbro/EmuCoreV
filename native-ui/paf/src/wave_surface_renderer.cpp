#include <emucorev/paf/wave_surface_renderer.h>

#include "wave_surface_shaders.h"

#include <GLES3/gl32.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace emucorev::paf {
namespace {

GLuint compile_shader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) throw std::runtime_error("Could not create wave surface shader");
    const char* text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Wave surface shader compile: " + log);
    }
    return shader;
}

GLuint make_program() {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, detail::wave_surface_vertex_shader);
    GLuint fragment = 0;
    try {
        fragment = compile_shader(GL_FRAGMENT_SHADER, detail::wave_surface_fragment_shader);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }
    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw std::runtime_error("Could not create wave surface program");
    }
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint size = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetProgramInfoLog(program, size, nullptr, log.data());
        glDeleteProgram(program);
        throw std::runtime_error("Wave surface program link: " + log);
    }
    return program;
}

void validate_raster(const WaveSurfaceRasterState& raster) {
    switch (raster.depth_function) {
    case GL_NEVER: case GL_LESS: case GL_EQUAL: case GL_LEQUAL:
    case GL_GREATER: case GL_NOTEQUAL: case GL_GEQUAL: case GL_ALWAYS: break;
    default: throw std::invalid_argument("Invalid wave surface depth function");
    }
    if (raster.cull_face != GL_FRONT && raster.cull_face != GL_BACK
        && raster.cull_face != GL_FRONT_AND_BACK)
        throw std::invalid_argument("Invalid wave surface cull face");
}

} // namespace

struct WaveSurfaceRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    std::array<GLuint, 5> buffers{};
    GLint position_sampler = -1;
    GLint lit_sampler = -1;
    std::vector<std::byte> vertex_cache;
    std::vector<std::byte> index_cache;
    GLenum cached_index_type = 0;

    ~Impl() {
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }

    void draw(std::uint32_t position_texture, std::uint32_t lit_texture,
        std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
        std::span<const WaveSurfaceVertex> vertices, const void* indices,
        GLsizei index_count, GLenum index_type, GLenum primitive,
        const WaveSurfaceRasterState& raster, const WaveSurfaceParameters& parameters) {
        if (!position_texture || !lit_texture || !width || !height || vertices.empty()
            || !indices || index_count <= 0
            || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
            || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
            throw std::invalid_argument("Invalid wave surface draw input");
        validate_raster(raster);

        glBindVertexArray(vao);
        const auto vertex_bytes = std::as_bytes(vertices);
        if (vertex_cache.size() != vertex_bytes.size()
            || std::memcmp(vertex_cache.data(), vertex_bytes.data(), vertex_bytes.size()) != 0) {
            glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertex_bytes.size()),
                vertex_bytes.data(), GL_DYNAMIC_DRAW);
            vertex_cache.assign(vertex_bytes.begin(), vertex_bytes.end());
        }
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers[1]);
        const auto index_size = index_type == GL_UNSIGNED_SHORT ? sizeof(std::uint16_t) : sizeof(std::uint32_t);
        const auto index_bytes_size = static_cast<std::size_t>(index_count) * index_size;
        const auto* index_begin = static_cast<const std::byte*>(indices);
        if (cached_index_type != index_type || index_cache.size() != index_bytes_size
            || std::memcmp(index_cache.data(), index_begin, index_bytes_size) != 0) {
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(index_bytes_size),
                indices, GL_DYNAMIC_DRAW);
            index_cache.assign(index_begin, index_begin + index_bytes_size);
            cached_index_type = index_type;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffers[2]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(parameters.vertex_data),
            parameters.vertex_data.data());
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[3]);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.viewport_state),
            parameters.viewport_state.data());
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[4]);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.fragment_state),
            parameters.fragment_state.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, buffers[2]);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2, buffers[3]);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, buffers[4]);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_framebuffer);
        glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Wave surface target framebuffer is incomplete");
        if (raster.depth_test) glEnable(GL_DEPTH_TEST);
        else glDisable(GL_DEPTH_TEST);
        glDepthFunc(raster.depth_function);
        glDepthMask(raster.depth_write ? GL_TRUE : GL_FALSE);
        if (raster.cull) {
            glEnable(GL_CULL_FACE);
            glCullFace(raster.cull_face);
        } else glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

        glUseProgram(program);
        glUniform1i(position_sampler, 0);
        glUniform1i(lit_sampler, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, position_texture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, lit_texture);
        glDrawElements(primitive, index_count, index_type, nullptr);
    }
};

WaveSurfaceRenderer::WaveSurfaceRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.program = make_program();
    p.position_sampler = glGetUniformLocation(p.program, "vertTex_PosTex");
    p.lit_sampler = glGetUniformLocation(p.program, "fragTex_LitTex");
    if (p.position_sampler < 0 || p.lit_sampler < 0)
        throw std::runtime_error("Wave surface samplers were not linked");
    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(static_cast<GLsizei>(p.buffers.size()), p.buffers.data());
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(WaveSurfaceVertex), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.buffers[1]);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, p.buffers[2]);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(float) * 36, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, p.buffers[2]);
    for (GLuint slot = 0; slot < 2; ++slot) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3 + slot]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2 + slot, p.buffers[3 + slot]);
    }
    glBindVertexArray(0);
}

WaveSurfaceRenderer::~WaveSurfaceRenderer() = default;

void WaveSurfaceRenderer::draw(std::uint32_t position_texture, std::uint32_t lit_texture,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    std::span<const WaveSurfaceVertex> vertices, std::span<const std::uint16_t> indices,
    WaveSurfacePrimitive primitive, const WaveSurfaceRasterState& raster,
    const WaveSurfaceParameters& parameters) {
    if (indices.empty() || indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || std::any_of(indices.begin(), indices.end(), [&](std::uint16_t index) {
            return index >= vertices.size();
        }))
        throw std::invalid_argument("Invalid wave surface index stream");
    impl_->draw(position_texture, lit_texture, target_framebuffer, width, height, vertices,
        indices.data(), static_cast<GLsizei>(indices.size()), GL_UNSIGNED_SHORT,
        primitive == WaveSurfacePrimitive::triangles ? GL_TRIANGLES : GL_TRIANGLE_STRIP,
        raster, parameters);
}

void WaveSurfaceRenderer::draw(std::uint32_t position_texture, std::uint32_t lit_texture,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    std::span<const WaveSurfaceVertex> vertices, std::span<const std::uint32_t> indices,
    WaveSurfacePrimitive primitive, const WaveSurfaceRasterState& raster,
    const WaveSurfaceParameters& parameters) {
    if (indices.empty() || indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || std::any_of(indices.begin(), indices.end(), [&](std::uint32_t index) {
            return index >= vertices.size();
        }))
        throw std::invalid_argument("Invalid wave surface index stream");
    impl_->draw(position_texture, lit_texture, target_framebuffer, width, height, vertices,
        indices.data(), static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT,
        primitive == WaveSurfacePrimitive::triangles ? GL_TRIANGLES : GL_TRIANGLE_STRIP,
        raster, parameters);
}

} // namespace emucorev::paf
