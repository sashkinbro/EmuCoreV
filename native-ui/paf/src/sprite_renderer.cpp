#include <emucorev/paf/sprite_renderer.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace emucorev::paf {
namespace {
GLuint shader(GLenum kind, const char* source) {
    const auto value = glCreateShader(kind);
    glShaderSource(value, 1, &source, nullptr);
    glCompileShader(value);
    GLint ok = 0;
    glGetShaderiv(value, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint size = 0;
        glGetShaderiv(value, GL_INFO_LOG_LENGTH, &size);
        std::string detail(std::max(1, size), '\0');
        glGetShaderInfoLog(value, size, nullptr, detail.data());
        glDeleteShader(value);
        throw std::runtime_error("Sprite shader: " + detail);
    }
    return value;
}
}
struct SpriteRenderer::Impl {
    GLuint program = 0, vao = 0;
    GLint canvas = -1, rectangle = -1, uv_rectangle = -1, tint = -1;
    std::unordered_map<std::uint32_t, GLuint> textures;
    ~Impl() {
        for (const auto& [id, texture] : textures) glDeleteTextures(1, &texture);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

SpriteRenderer::SpriteRenderer() : impl_(std::make_unique<Impl>()) {
    constexpr auto vertex = R"(#version 300 es
precision highp float;
uniform vec2 canvas;
uniform vec4 rectangle;
uniform vec4 uv_rectangle;
out vec2 uv;
void main() {
    vec2 unit = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    uv = uv_rectangle.xy + unit * uv_rectangle.zw;
    vec2 pixel = rectangle.xy + unit * rectangle.zw;
    gl_Position = vec4(pixel.x / canvas.x * 2.0 - 1.0,
                       1.0 - pixel.y / canvas.y * 2.0, 0.0, 1.0);
})";
    constexpr auto fragment = R"(#version 300 es
precision highp float;
uniform sampler2D image;
uniform vec4 tint;
in vec2 uv;
out vec4 color;
void main() { color = texture(image, uv) * tint; }
)";
    const auto vs = shader(GL_VERTEX_SHADER, vertex);
    GLuint fs = 0;
    try { fs = shader(GL_FRAGMENT_SHADER, fragment); }
    catch (...) { glDeleteShader(vs); throw; }
    auto& p = *impl_;
    p.program = glCreateProgram();
    glAttachShader(p.program, vs);
    glAttachShader(p.program, fs);
    glLinkProgram(p.program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok) throw std::runtime_error("Cannot link sprite program");
    p.canvas = glGetUniformLocation(p.program, "canvas");
    p.rectangle = glGetUniformLocation(p.program, "rectangle");
    p.uv_rectangle = glGetUniformLocation(p.program, "uv_rectangle");
    p.tint = glGetUniformLocation(p.program, "tint");
    glUseProgram(p.program);
    glUniform1i(glGetUniformLocation(p.program, "image"), 0);
    glGenVertexArrays(1, &p.vao);
}
SpriteRenderer::~SpriteRenderer() = default;

void SpriteRenderer::upload(std::uint32_t id, unsigned width, unsigned height,
    std::span<const std::uint8_t> rgba) {
    if (!width || !height || width > 4096 || height > 4096
        || rgba.size() != std::size_t(width) * height * 4)
        throw std::invalid_argument("Invalid sprite RGBA image");
    auto& texture = impl_->textures[id];
    if (!texture) glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Cannot upload sprite image");
}
void SpriteRenderer::begin(float width, float height) {
    if (width <= 0 || height <= 0) throw std::invalid_argument("Invalid sprite canvas");
    const auto& p = *impl_;
    glUseProgram(p.program);
    glBindVertexArray(p.vao);
    glUniform2f(p.canvas, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glActiveTexture(GL_TEXTURE0);
}
bool SpriteRenderer::draw(std::uint32_t id, float x, float y, float width, float height,
    std::array<float, 4> tint) {
    return draw_patch(id, x, y, width, height, 0.f, 0.f, 1.f, 1.f, tint);
}

bool SpriteRenderer::draw_patch(std::uint32_t id, float x, float y, float width, float height,
    float u, float v, float uv_width, float uv_height, std::array<float, 4> tint) {
    const auto& p = *impl_;
    const auto image = p.textures.find(id);
    if (image == p.textures.end()) return false;
    glBindTexture(GL_TEXTURE_2D, image->second);
    glUniform4f(p.rectangle, x, y, width, height);
    glUniform4f(p.uv_rectangle, u, v, uv_width, uv_height);
    glUniform4fv(p.tint, 1, tint.data());
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return true;
}
}
