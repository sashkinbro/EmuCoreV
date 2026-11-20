#include <emucorev/paf/focus_cursor_renderer.h>

#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace emucorev::paf {
namespace {

constexpr std::string_view kVertexShader = R"(#version 320 es
layout(location = 0) in vec2 a_corner; // [0..1]
layout(location = 0) out vec2 v_uv;
uniform vec4 u_rect; // x, y, w, h in NDC or screen pixels + u_screen
uniform vec2 u_screen;
void main() {
    float px = u_rect.x + a_corner.x * u_rect.z;
    float py = u_rect.y + a_corner.y * u_rect.w;
    float nx = (px / u_screen.x) * 2.0 - 1.0;
    float ny = 1.0 - (py / u_screen.y) * 2.0;
    v_uv = a_corner;
    gl_Position = vec4(nx, ny, 0.0, 1.0);
}
)";

// Reconstructed 1:1 from libpaf 0x813008c8 (focus.fp):
// - tex.b * u_base (normal blend)
// - tex.g * u_glow (normal blend)
// - clamp(tex.r * 1.7, 0, 1) * u_add (additive)
// Combined into one exact premultiplied-alpha pass (GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
constexpr std::string_view kFragmentShader = R"(#version 320 es
precision highp float;
layout(location = 0) in vec2 v_uv;
layout(binding = 0) uniform sampler2D u_quad_tex;
uniform vec4 u_base_color;
uniform vec4 u_glow_color;
uniform vec4 u_add_color;
layout(location = 0) out vec4 out_color;

void main() {
    // Mirror the single top-left quadrant texture (outer corner (0,0), center (1,1)) into 4 quadrants.
    vec2 q = abs(v_uv * 2.0 - 1.0);
    vec2 tex_uv = clamp(vec2(1.0) - q, vec2(0.005), vec2(0.995));
    vec4 tex = texture(u_quad_tex, tex_uv);

    float a0 = clamp(u_base_color.a * tex.b, 0.0, 1.0);
    float a1 = clamp(u_glow_color.a * tex.g, 0.0, 1.0);
    float a2 = clamp(tex.r * 1.7, 0.0, 1.0) * clamp(u_add_color.a, 0.0, 1.0);

    vec3 rgb = u_base_color.rgb * (a0 * (1.0 - a1))
             + u_glow_color.rgb * a1
             + u_add_color.rgb * a2;
    float alpha = 1.0 - (1.0 - a0) * (1.0 - a1);
    out_color = vec4(rgb, alpha);
}
)";

GLuint compile_shader(GLenum stage, std::string_view source) {
    const GLuint shader = glCreateShader(stage);
    if (!shader) throw std::runtime_error("Could not create focus cursor shader");
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
        throw std::runtime_error("FocusCursorRenderer shader compile: " + log);
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
        throw std::runtime_error("Could not create focus cursor program");
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
        throw std::runtime_error("FocusCursorRenderer program link: " + log);
    }
    return prog;
}

} // namespace

struct FocusCursorRenderer::Impl {
    GLuint program = 0;
    GLuint vao = 0;
    GLuint vbo = 0;
    GLint rect_loc = -1;
    GLint screen_loc = -1;
    GLint base_loc = -1;
    GLint glow_loc = -1;
    GLint add_loc = -1;
    ~Impl() {
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

FocusCursorRenderer::FocusCursorRenderer() : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.program = make_program();
    p.rect_loc = glGetUniformLocation(p.program, "u_rect");
    p.screen_loc = glGetUniformLocation(p.program, "u_screen");
    p.base_loc = glGetUniformLocation(p.program, "u_base_color");
    p.glow_loc = glGetUniformLocation(p.program, "u_glow_color");
    p.add_loc = glGetUniformLocation(p.program, "u_add_color");

    constexpr std::array<float, 8> kCorners = {{0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f}};
    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(1, &p.vbo);
    glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCorners), kCorners.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray(0);
}

FocusCursorRenderer::~FocusCursorRenderer() = default;

void FocusCursorRenderer::draw(std::uint32_t quadrant_texture, float center_x, float center_y,
    float size, float pulse_phase_rad, std::uint32_t logical_width, std::uint32_t logical_height) {
    const float pulse = 0.5f + 0.5f * std::sin(pulse_phase_rad);
    const std::array<float, 4> base_color{0.10f, 0.65f, 1.00f, 0.25f + 0.20f * pulse};
    const std::array<float, 4> glow_color{0.16f, 0.84f, 1.00f, 0.42f + 0.56f * pulse};
    const std::array<float, 4> add_color{0.62f, 0.96f, 1.00f, 0.36f + 0.60f * pulse};
    draw_colors(quadrant_texture, center_x, center_y, size, base_color, glow_color, add_color,
        logical_width, logical_height);
}

void FocusCursorRenderer::draw_colors(std::uint32_t quadrant_texture, float center_x, float center_y,
    float size, std::array<float, 4> base_color, std::array<float, 4> glow_color,
    std::array<float, 4> add_color, std::uint32_t logical_width, std::uint32_t logical_height) {
    if (!quadrant_texture || size <= 0.0f) return;
    auto& p = *impl_;

    const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
    const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
    GLint blend_src_rgb = GL_ONE;
    GLint blend_dst_rgb = GL_ZERO;
    GLint blend_src_a = GL_ONE;
    GLint blend_dst_a = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_a);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_a);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glUseProgram(p.program);
    const float half = size * 0.5f;
    glUniform4f(p.rect_loc, center_x - half, center_y - half, size, size);
    glUniform2f(p.screen_loc, static_cast<float>(logical_width), static_cast<float>(logical_height));
    glUniform4fv(p.base_loc, 1, base_color.data());
    glUniform4fv(p.glow_loc, 1, glow_color.data());
    glUniform4fv(p.add_loc, 1, add_color.data());

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(quadrant_texture));
    glBindVertexArray(p.vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    glUseProgram(0);

    glBlendFuncSeparate(blend_src_rgb, blend_dst_rgb, blend_src_a, blend_dst_a);
    if (!blend_enabled) glDisable(GL_BLEND);
    if (depth_enabled) glEnable(GL_DEPTH_TEST);
    if (cull_enabled) glEnable(GL_CULL_FACE);
}

} // namespace emucorev::paf
