#include <emucorev/paf/bubble_env_renderer.h>

#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>
#include <string_view>

namespace emucorev::paf {
namespace {

constexpr std::string_view kVertexShader = R"(#version 320 es
layout(location = 0) in vec2 pos;
layout(location = 0) out vec2 v_uv;
void main() {
    v_uv = pos * 0.5 + 0.5;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)";

// Inverse angular map matching PAF bubble_shaders.cpp (0x159154936 = 1/(2*pi)):
// uv = (0.5, 0.5) faces +Z (viewer), r = 0.25 is horizon, r > 0.25 wraps behind.
constexpr std::string_view kFragmentShader = R"(#version 320 es
precision highp float;
layout(location = 0) in vec2 v_uv;
layout(binding = 0) uniform sampler2D u_wave_color;
layout(location = 0) out vec4 out_color;

void main() {
    vec2 p = v_uv - vec2(0.5);
    float r = length(p);
    float theta = clamp(r * 6.28318530718, 0.0, 3.14159265359);
    vec2 dir2d = (r > 1e-5) ? (p / r) : vec2(0.0, 1.0);
    float sin_t = sin(theta);
    float cos_t = cos(theta);
    // Notice bubble_shaders.cpp flips reflected Y in screen space; map dir2d to live Wave FBO
    // (where v = 1.0 is top sky and v = 0.0 is bottom wave trough).
    vec3 refl = vec3(dir2d.x * sin_t, dir2d.y * sin_t, cos_t);

    vec2 wave_uv = clamp(vec2(0.5 + 0.46 * refl.x, 0.54 + 0.44 * refl.y), vec2(0.02), vec2(0.98));
    vec3 wave_col = texture(u_wave_color, wave_uv).rgb;

    // Soft glass dome reflection tinting from live wave + signature Vita upper-left skylight sheen.
    float rim = smoothstep(0.10, 0.92, 1.0 - clamp(abs(refl.z), 0.0, 1.0));
    float upper_dome = smoothstep(-0.15, 0.85, refl.y * 0.85 - refl.x * 0.25);
    float crest_spot = pow(max(0.0, dot(normalize(refl), normalize(vec3(-0.32, 0.72, 0.62)))), 6.0);

    vec3 env_rgb = wave_col * (0.48 + 0.52 * rim);
    env_rgb += vec3(0.48, 0.51, 0.55) * (0.30 * upper_dome + 0.62 * crest_spot);
    env_rgb += vec3(0.32, 0.34, 0.38) * (rim * rim * 0.65);
    out_color = vec4(clamp(env_rgb, vec3(0.0), vec3(1.0)), 1.0);
}
)";

GLuint compile_shader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) throw std::runtime_error("Could not create bubble env shader");
    const char* text = source.data();
    const GLint len = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &len);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("BubbleEnvRenderer shader compile: " + log);
    }
    return shader;
}

GLuint make_program() {
    const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = 0;
    try {
        fs = compile_shader(GL_FRAGMENT_SHADER, kFragmentShader);
    } catch (...) {
        glDeleteShader(vs);
        throw;
    }
    const GLuint prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        throw std::runtime_error("Could not create bubble env program");
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint size = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetProgramInfoLog(prog, size, nullptr, log.data());
        glDeleteProgram(prog);
        throw std::runtime_error("BubbleEnvRenderer program link: " + log);
    }
    return prog;
}

} // namespace

struct BubbleEnvRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint fbo = 0;
    GLuint tex = 0;
    ~Impl() {
        if (tex) glDeleteTextures(1, &tex);
        if (fbo) glDeleteFramebuffers(1, &fbo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

BubbleEnvRenderer::BubbleEnvRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    GLint prev_draw_fbo = 0;
    GLint prev_read_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_draw_fbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev_read_fbo);

    p.program = make_program();

    constexpr std::array<float, 8> kQuad = {{-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f}};
    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(1, &p.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);

    glGenTextures(1, &p.tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, kEnvSize, kEnvSize);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &p.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p.tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw_fbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
        throw std::runtime_error("Incomplete BubbleEnvRenderer FBO");
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_draw_fbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prev_read_fbo));
}

BubbleEnvRenderer::~BubbleEnvRenderer() = default;

void BubbleEnvRenderer::capture(std::uint32_t wave_color_texture) {
    auto& p = *impl_;
    GLint prev_fbo = 0;
    std::array<GLint, 4> prev_viewport{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_viewport.data());
    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);

    glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
    glViewport(0, 0, kEnvSize, kEnvSize);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(p.program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(wave_color_texture));
    glBindVertexArray(p.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_viewport[0], prev_viewport[1], prev_viewport[2], prev_viewport[3]);
    if (blend_enabled) glEnable(GL_BLEND);
    if (depth_enabled) glEnable(GL_DEPTH_TEST);
    if (cull_enabled) glEnable(GL_CULL_FACE);
}

std::uint32_t BubbleEnvRenderer::texture() const noexcept {
    return static_cast<std::uint32_t>(impl_->tex);
}

} // namespace emucorev::paf
