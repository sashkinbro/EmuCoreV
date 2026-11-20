#include <emucorev/paf/wave_grid_normal_renderer.h>

#include "wave_grid_normal_shaders.h"

#include <GLES3/gl32.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace emucorev::paf {
namespace {
GLuint compile_shader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) throw std::runtime_error("Could not create wave grid-normal shader");
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
        throw std::runtime_error("Wave grid-normal shader compile: " + log);
    }
    return shader;
}

GLuint make_program() {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, detail::wave_grid_normal_vertex_shader);
    GLuint fragment = 0;
    try {
        fragment = compile_shader(GL_FRAGMENT_SHADER, detail::wave_grid_normal_fragment_shader);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }
    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw std::runtime_error("Could not create wave grid-normal program");
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
        throw std::runtime_error("Wave grid-normal program link: " + log);
    }
    return program;
}
} // namespace

struct WaveGridNormalRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    std::array<GLuint, 5> buffers{};
    GLint source_location = -1;
    ~Impl() {
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

WaveGridNormalRenderer::WaveGridNormalRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.program = make_program();
    p.source_location = glGetUniformLocation(p.program, "fragTex_Source");
    if (p.source_location < 0)
        throw std::runtime_error("Wave grid-normal source sampler was not linked");
    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(static_cast<GLsizei>(p.buffers.size()), p.buffers.data());
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(WaveGridNormalVertex) * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(WaveGridNormalVertex), nullptr);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[1]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 20, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[2]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 4, nullptr, GL_DYNAMIC_DRAW);
    for (GLuint i = 0; i < 2; ++i) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3 + i]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
    }
    glBindVertexArray(0);
}

WaveGridNormalRenderer::~WaveGridNormalRenderer() = default;

void WaveGridNormalRenderer::draw(std::uint32_t source_texture,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    const std::array<WaveGridNormalVertex, 4>& vertices,
    const WaveGridNormalParameters& parameters) {
    if (!source_texture || !width || !height
        || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
        throw std::invalid_argument("Invalid wave grid-normal target or source texture");
    auto& p = *impl_;
    glBindVertexArray(p.vao);
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[1]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.vertex_data), parameters.vertex_data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[2]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.fragment_data), parameters.fragment_data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.viewport_state), parameters.viewport_state.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[4]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.fragment_state), parameters.fragment_state.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.buffers[1]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, p.buffers[2]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, p.buffers[3]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, p.buffers[4]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Wave grid-normal target framebuffer is incomplete");
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(p.program);
    glUniform1i(p.source_location, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
} // namespace emucorev::paf
