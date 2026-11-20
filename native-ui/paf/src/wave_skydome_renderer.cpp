#include <emucorev/paf/wave_skydome_renderer.h>

#include <emucorev/paf/plane_div.h>
#include "wave_skydome_shaders.h"

#include <GLES3/gl32.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace emucorev::paf {

WaveSkydomeMesh make_wave_skydome_mesh(std::uint16_t columns, std::uint16_t rows) {
    if (columns < 2 || rows < 2
        || static_cast<std::uint32_t>(columns) * rows > 65536u)
        throw std::invalid_argument("Invalid PAF skydome grid dimensions");
    const float inv_columns = arm_recip_estimate_2step(static_cast<float>(columns - 1));
    const float inv_rows = arm_recip_estimate_2step(static_cast<float>(rows - 1));
    WaveSkydomeMesh mesh;
    mesh.vertices.reserve(static_cast<std::size_t>(columns) * rows);
    for (std::uint32_t row = 0; row < rows; ++row) {
        const float v = static_cast<float>(row) * inv_rows;
        for (std::uint32_t col = 0; col < columns; ++col) {
            const float u = static_cast<float>(col) * inv_columns;
            mesh.vertices.push_back({u, v});
        }
    }

    // PAF FUN_81201278: each row pair is a triangle strip; four indices
    // (last-old, first-next x3) stitch adjacent strips without restart.
    mesh.triangle_strip_indices.reserve(
        (static_cast<std::size_t>(rows) - 1) * columns * 2
            + (static_cast<std::size_t>(rows) - 2) * 4);
    for (std::uint32_t row = 0; row + 1 < rows; ++row) {
        const auto old_start = static_cast<std::uint16_t>(row * columns);
        const auto new_start = static_cast<std::uint16_t>((row + 1) * columns);
        for (std::uint32_t col = 0; col < columns; ++col) {
            mesh.triangle_strip_indices.push_back(
                static_cast<std::uint16_t>(new_start + col));
            mesh.triangle_strip_indices.push_back(
                static_cast<std::uint16_t>(old_start + col));
        }
        if (row + 2 < rows) {
            mesh.triangle_strip_indices.push_back(
                static_cast<std::uint16_t>(new_start - 1));
            const auto first_next_strip = static_cast<std::uint16_t>((row + 2) * columns);
            mesh.triangle_strip_indices.push_back(first_next_strip);
            mesh.triangle_strip_indices.push_back(first_next_strip);
            mesh.triangle_strip_indices.push_back(first_next_strip);
        }
    }
    return mesh;
}

namespace {
GLuint compile_shader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) throw std::runtime_error("Could not create wave skydome shader");
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
        throw std::runtime_error("Wave skydome shader compile: " + log);
    }
    return shader;
}

GLuint make_program() {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, detail::wave_skydome_vertex_shader);
    GLuint fragment = 0;
    try {
        fragment = compile_shader(GL_FRAGMENT_SHADER, detail::wave_skydome_fragment_shader);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }
    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw std::runtime_error("Could not create wave skydome program");
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
        throw std::runtime_error("Wave skydome program link: " + log);
    }
    return program;
}
} // namespace

struct WaveSkydomeRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    std::array<GLuint, 5> buffers{};
    GLint normal_texture_location = -1;
    GLsizei index_count = 0;
    ~Impl() {
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

WaveSkydomeRenderer::WaveSkydomeRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    const WaveSkydomeMesh mesh = make_wave_skydome_mesh();
    p.program = make_program();
    p.normal_texture_location = glGetUniformLocation(p.program, "vertTex_WaveNorTex");
    if (p.normal_texture_location < 0)
        throw std::runtime_error("Wave skydome normal texture sampler was not linked");
    p.index_count = static_cast<GLsizei>(mesh.triangle_strip_indices.size());
    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(static_cast<GLsizei>(p.buffers.size()), p.buffers.data());
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(WaveSkydomeVertex)),
        mesh.vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    // PAF stream contains exactly float2; the shader's vec4 input receives
    // GLES defaults z=0,w=1.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(WaveSkydomeVertex), nullptr);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.buffers[1]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(mesh.triangle_strip_indices.size() * sizeof(std::uint16_t)),
        mesh.triangle_strip_indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[2]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 88, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[4]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
    glBindVertexArray(0);
}

WaveSkydomeRenderer::~WaveSkydomeRenderer() = default;

void WaveSkydomeRenderer::draw(std::uint32_t wave_normal_texture,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    const WaveSkydomeParameters& parameters) {
    if (!wave_normal_texture || !width || !height
        || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max()))
        throw std::invalid_argument("Invalid wave skydome target or normal texture");
    auto& p = *impl_;
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[2]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.vertex_data),
        parameters.vertex_data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[3]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.viewport_state),
        parameters.viewport_state.data());
    // Binding points are context-global and must be restored for each draw.
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.buffers[2]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, p.buffers[3]);
    glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[4]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(parameters.fragment_state),
        parameters.fragment_state.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, p.buffers[4]);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error("Wave skydome target framebuffer is incomplete");
    }
    // Source 811a674e: its one-byte blend flag selects blend disable or this
    // factor/equation state. PAF/GXM CullMode(1) maps to GL_FRONT.
    if (parameters.blend_enabled) {
        glEnable(GL_BLEND);
        glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_FRONT);
    glUseProgram(p.program);
    glUniform1i(p.normal_texture_location, 16);
    glActiveTexture(GL_TEXTURE0 + 16);
    glBindTexture(GL_TEXTURE_2D, wave_normal_texture);
    glBindVertexArray(p.vao);
    glDrawElements(GL_TRIANGLE_STRIP, p.index_count, GL_UNSIGNED_SHORT, nullptr);
}

} // namespace emucorev::paf
