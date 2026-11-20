#include <emucorev/paf/wave_position_renderer.h>

#include "wave_position_shaders.h"

#include <GLES3/gl32.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace emucorev::paf {
namespace {

GLuint compile_shader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) throw std::runtime_error("Could not create wave position shader");
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
        throw std::runtime_error("Wave position shader compile: " + log);
    }
    return shader;
}

GLuint make_program() {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, detail::wave_position_vertex_shader);
    GLuint fragment = 0;
    try {
        fragment = compile_shader(GL_FRAGMENT_SHADER, detail::wave_position_fragment_shader);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }
    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw std::runtime_error("Could not create wave position program");
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
        throw std::runtime_error("Wave position program link: " + log);
    }
    return program;
}

} // namespace

struct WavePositionPassRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    std::array<GLuint, 5> buffers{};
    GLint height_map_location = -1;

    ~Impl() {
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

WavePositionPassRenderer::WavePositionPassRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.program = make_program();
    p.height_map_location = glGetUniformLocation(p.program, "fragTex_HeightMap");
    if (p.height_map_location < 0)
        throw std::runtime_error("Wave position HeightMap sampler was not linked");

    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(static_cast<GLsizei>(p.buffers.size()), p.buffers.data());

    // Slot 0 is the four-position stream. Slots 1 and 2 are the shader's
    // std140 uniform vertex/fragment register blocks. Slots 3 and 4 are GXM viewport
    // and fragment state (12 floats each, including std140 tail padding).
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(WavePositionVertex) * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(WavePositionVertex), nullptr);

    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[1]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 16, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.buffers[1]);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[2]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 32, nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, p.buffers[2]);

    for (GLuint slot = 0; slot < 2; ++slot) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3 + slot]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 2 + slot, p.buffers[3 + slot]);
    }
    glBindVertexArray(0);
}

WavePositionPassRenderer::~WavePositionPassRenderer() = default;

void WavePositionPassRenderer::draw(std::uint32_t height_map_texture,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    std::span<const WavePositionVertex, 4> vertices,
    const WavePositionPassParameters& parameters) {
    if (!height_map_texture || !width || !height
        || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
        throw std::invalid_argument("Invalid wave position target or HeightMap");

    auto& p = *impl_;
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(WavePositionVertex) * 4, vertices.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[1]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.vertex_data),
        parameters.vertex_data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[2]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.fragment_data),
        parameters.fragment_data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.viewport_state),
        parameters.viewport_state.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[4]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.fragment_state),
        parameters.fragment_state.data());

    // Indexed bindings are context-global, not VAO state. Reassert them on
    // every call so another renderer instance cannot leave stale blocks here.
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.buffers[1]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, p.buffers[2]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, p.buffers[3]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, p.buffers[4]);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        throw std::runtime_error("Wave position target framebuffer is incomplete");
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(p.program);
    glUniform1i(p.height_map_location, 7);
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_2D, height_map_texture);
    glBindVertexArray(p.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace emucorev::paf
