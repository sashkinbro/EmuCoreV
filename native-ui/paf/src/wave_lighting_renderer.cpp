#include <emucorev/paf/wave_lighting_renderer.h>

#include "wave_lighting_shaders.h"

#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace emucorev::paf {
namespace {

GLuint compile_shader(GLenum stage, std::string_view source, const char* label) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) {
        throw std::runtime_error(std::string("Could not create ") + label + " shader");
    }
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
        throw std::runtime_error(std::string(label) + " shader compile: " + log);
    }
    return shader;
}

GLuint make_program(std::string_view vert_src, std::string_view frag_src, const char* label) {
    const GLuint vertex = compile_shader(GL_VERTEX_SHADER, vert_src, label);
    GLuint fragment = 0;
    try {
        fragment = compile_shader(GL_FRAGMENT_SHADER, frag_src, label);
    } catch (...) {
        glDeleteShader(vertex);
        throw;
    }
    const GLuint program = glCreateProgram();
    if (!program) {
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        throw std::runtime_error(std::string("Could not create ") + label + " program");
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
        throw std::runtime_error(std::string(label) + " program link: " + log);
    }
    return program;
}

void setup_quad_buffers(GLuint& vao, std::array<GLuint, 5>& buffers, std::size_t frag_float_count) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(WaveGridNormalVertex) * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(WaveGridNormalVertex), nullptr);
    glBindBuffer(GL_UNIFORM_BUFFER, buffers[1]);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 20, nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, buffers[2]);
    glBufferData(GL_UNIFORM_BUFFER,
        static_cast<GLsizeiptr>(sizeof(float) * frag_float_count), nullptr, GL_DYNAMIC_DRAW);
    for (GLuint i = 0; i < 2; ++i) {
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[3 + i]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(float) * 12, nullptr, GL_DYNAMIC_DRAW);
    }
    glBindVertexArray(0);
}

void upload_and_prepare_quad(GLuint vao, const std::array<GLuint, 5>& buffers,
    const std::array<WaveGridNormalVertex, 4>& vertices,
    const std::array<float, 20>& vertex_data, const float* fragment_data, std::size_t frag_bytes,
    const std::array<float, 12>& viewport_state, const std::array<float, 12>& fragment_state,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    const char* label) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, buffers[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices.data());
    glBindBuffer(GL_UNIFORM_BUFFER, buffers[1]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(vertex_data), vertex_data.data());
    glBindBuffer(GL_UNIFORM_BUFFER, buffers[2]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(frag_bytes), fragment_data);
    glBindBuffer(GL_UNIFORM_BUFFER, buffers[3]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(viewport_state), viewport_state.data());
    glBindBuffer(GL_UNIFORM_BUFFER, buffers[4]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(fragment_state), fragment_state.data());

    glBindBufferBase(GL_UNIFORM_BUFFER, 0, buffers[1]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, buffers[2]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, buffers[3]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, buffers[4]);

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string(label) + " target framebuffer is incomplete");
    }
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

std::array<std::uint8_t, 8 * 8 * 4> make_fliptex_pixels() noexcept {
    // 811a0b3a L1351-1443: 8x8 RGBA8 checkerboard where R = (x&1)*255, G = (y&1)*255.
    std::array<std::uint8_t, 8 * 8 * 4> pixels{};
    for (std::size_t y = 0; y < 8; ++y) {
        for (std::size_t x = 0; x < 8; ++x) {
            const std::size_t offset = (y * 8 + x) * 4;
            pixels[offset + 0] = (x & 1u) ? 255u : 0u;
            pixels[offset + 1] = (y & 1u) ? 255u : 0u;
            pixels[offset + 2] = 0u;
            pixels[offset + 3] = 0u;
        }
    }
    return pixels;
}

} // namespace

struct WaveLightingRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    std::array<GLuint, 5> buffers{};
    GLint pos_location = -1;
    GLint nor_location = -1;

    ~Impl() {
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (vao) {
            glDeleteVertexArrays(1, &vao);
        }
        if (program) {
            glDeleteProgram(program);
        }
    }
};

WaveLightingRenderer::WaveLightingRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.program = make_program(detail::wave_lighting_vertex_shader,
        detail::wave_lighting_fragment_shader, "Wave lighting");
    p.pos_location = glGetUniformLocation(p.program, "fragTex_PosTex");
    p.nor_location = glGetUniformLocation(p.program, "fragTex_NorTex");
    if (p.pos_location < 0 || p.nor_location < 0) {
        throw std::runtime_error("Wave lighting PosTex/NorTex samplers were not linked");
    }
    setup_quad_buffers(p.vao, p.buffers, 96u);
}

WaveLightingRenderer::~WaveLightingRenderer() = default;

void WaveLightingRenderer::draw(std::uint32_t position_texture, std::uint32_t normal_texture,
    std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
    const std::array<WaveGridNormalVertex, 4>& vertices,
    const WaveLightingParameters& parameters) {
    if (!position_texture || !normal_texture || !width || !height
        || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        throw std::invalid_argument("Invalid wave lighting target or input textures");
    }
    auto& p = *impl_;
    upload_and_prepare_quad(p.vao, p.buffers, vertices, parameters.vertex_data,
        parameters.fragment_data.data(), sizeof(parameters.fragment_data),
        parameters.viewport_state, parameters.fragment_state,
        target_framebuffer, width, height, "Wave lighting");

    glUseProgram(p.program);
    glUniform1i(p.pos_location, 0);
    glUniform1i(p.nor_location, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, position_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normal_texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

struct WaveFliptriFilter::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint flip_texture = 0;
    std::array<GLuint, 5> buffers{};
    GLint source_location = -1;
    GLint flip_location = -1;

    ~Impl() {
        glDeleteBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
        if (flip_texture) {
            glDeleteTextures(1, &flip_texture);
        }
        if (vao) {
            glDeleteVertexArrays(1, &vao);
        }
        if (program) {
            glDeleteProgram(program);
        }
    }
};

WaveFliptriFilter::WaveFliptriFilter() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.program = make_program(detail::wave_fliptri_vertex_shader,
        detail::wave_fliptri_fragment_shader, "Wave fliptri");
    p.source_location = glGetUniformLocation(p.program, "fragTex_Source");
    p.flip_location = glGetUniformLocation(p.program, "fragTex_FlipTex");
    if (p.source_location < 0 || p.flip_location < 0) {
        throw std::runtime_error("Wave fliptri samplers were not linked");
    }
    setup_quad_buffers(p.vao, p.buffers, 8u);

    const auto flip_pixels = make_fliptex_pixels();
    glGenTextures(1, &p.flip_texture);
    glBindTexture(GL_TEXTURE_2D, p.flip_texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 8, 8);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, flip_pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

WaveFliptriFilter::~WaveFliptriFilter() = default;

void WaveFliptriFilter::draw(std::uint32_t source_texture, std::uint32_t target_framebuffer,
    std::uint32_t width, std::uint32_t height,
    const std::array<WaveGridNormalVertex, 4>& vertices,
    const WaveFliptriParameters& parameters) {
    if (!source_texture || !width || !height
        || width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        throw std::invalid_argument("Invalid wave fliptri target or source texture");
    }
    auto& p = *impl_;
    upload_and_prepare_quad(p.vao, p.buffers, vertices, parameters.vertex_data,
        parameters.fragment_data.data(), sizeof(parameters.fragment_data),
        parameters.viewport_state, parameters.fragment_state,
        target_framebuffer, width, height, "Wave fliptri");

    glUseProgram(p.program);
    glUniform1i(p.source_location, 0);
    glUniform1i(p.flip_location, 1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, p.flip_texture);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

} // namespace emucorev::paf
