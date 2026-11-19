#include <emucorev/paf/page_turn_renderer.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <stdexcept>
#include <string>

namespace emucorev::paf {
namespace {
GLuint compile(GLenum type, std::string_view source) {
    if (source.empty() || source.size() > 1024 * 1024)
        throw std::invalid_argument("Invalid page surface shader");
    const auto shader = glCreateShader(type);
    const auto* text = source.data();
    const auto length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length); glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 8192> log{};
        glGetShaderInfoLog(shader, log.size(), nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error(std::string("Page surface shader: ") + log.data());
    }
    return shader;
}
GLuint link(PageSurfaceShaders source) {
    const auto vertex = compile(GL_VERTEX_SHADER, source.vertex);
    GLuint fragment = 0;
    try { fragment = compile(GL_FRAGMENT_SHADER, source.fragment); }
    catch (...) { glDeleteShader(vertex); throw; }
    const auto program = glCreateProgram();
    glAttachShader(program, vertex); glAttachShader(program, fragment); glLinkProgram(program);
    glDeleteShader(vertex); glDeleteShader(fragment);
    GLint ok = 0; glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        std::array<char, 8192> log{};
        glGetProgramInfoLog(program, log.size(), nullptr, log.data()); glDeleteProgram(program);
        throw std::runtime_error(std::string("Page surface program: ") + log.data());
    }
    // Vertex lookup and fragment unit zero must never alias a texture binding.
    const auto lookup = glGetUniformLocation(program, "vertTex_ExpMinusTexture");
    if (lookup >= 0) { glUseProgram(program); glUniform1i(lookup, 4); }
    return program;
}
}
struct PageTurnRenderer::Impl {
    std::array<GLuint, 2> programs{};
    std::array<GLuint, 5> buffers{};
    GLuint vao = 0;
    std::array<PageTurnFan, 128> front{}, folded{};
    unsigned front_count = 0, folded_count = 0;
    ~Impl() {
        for (auto program : programs) if (program) glDeleteProgram(program);
        glDeleteBuffers(buffers.size(), buffers.data());
        if (vao) glDeleteVertexArrays(1, &vao);
    }
};
PageTurnRenderer::PageTurnRenderer(PageSurfaceShaders front, PageSurfaceShaders folded)
    : impl_(std::make_unique<Impl>()) {
    auto& p = *impl_;
    p.programs[0] = link(front); p.programs[1] = link(folded);
    glGenVertexArrays(1, &p.vao); glBindVertexArray(p.vao);
    glGenBuffers(p.buffers.size(), p.buffers.data());
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferData(GL_ARRAY_BUFFER, 1024 * sizeof(PagePoint), nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(PagePoint), nullptr);
    for (unsigned i = 1; i < p.buffers.size(); ++i) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[i]);
        glBufferData(GL_UNIFORM_BUFFER, i <= 2 ? 512 : 48, nullptr, GL_DYNAMIC_DRAW);
    }
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Page surface GPU allocation failed");
}
PageTurnRenderer::~PageTurnRenderer() = default;
void PageTurnRenderer::upload_mesh(const PageTurnMesh& mesh) {
    if (mesh.vertex_count > mesh.vertices.size() || mesh.front_count > mesh.front.size()
        || mesh.folded_count > mesh.folded.size()) throw std::invalid_argument("Invalid page mesh counts");
    auto check = [&](const auto& fans, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            const auto fan = fans[i];
            if (fan.count < 3 || fan.first > mesh.vertex_count || fan.count > mesh.vertex_count - fan.first)
                throw std::invalid_argument("Invalid page mesh fan");
        }
    };
    check(mesh.front, mesh.front_count); check(mesh.folded, mesh.folded_count);
    auto& p = *impl_;
    glBindBuffer(GL_ARRAY_BUFFER, p.buffers[0]);
    glBufferSubData(GL_ARRAY_BUFFER, 0, mesh.vertex_count * sizeof(PagePoint), mesh.vertices.data());
    std::copy_n(mesh.front.begin(), mesh.front_count, p.front.begin());
    std::copy_n(mesh.folded.begin(), mesh.folded_count, p.folded.begin());
    p.front_count = mesh.front_count; p.folded_count = mesh.folded_count;
}
void PageTurnRenderer::draw(PageSurface surface, const PageSurfaceDraw& material) {
    if (material.vertex_parameters.size() > 128 || material.fragment_parameters.size() > 128)
        throw std::invalid_argument("Page surface parameter block exceeds storage");
    if (surface != PageSurface::front && surface != PageSurface::folded)
        throw std::invalid_argument("Invalid page surface");
    auto& p = *impl_;
    const bool folded = surface == PageSurface::folded;
    glUseProgram(p.programs[folded ? 1 : 0]); glBindVertexArray(p.vao);
    // Zero padding prevents short blocks from inheriting another material's tail.
    std::array<float, 128> vertex{}, fragment{};
    std::copy(material.vertex_parameters.begin(), material.vertex_parameters.end(), vertex.begin());
    std::copy(material.fragment_parameters.begin(), material.fragment_parameters.end(), fragment.begin());
    const std::array<const float*, 4> data{vertex.data(), fragment.data(), material.viewport.data(), material.fragment_state.data()};
    for (unsigned i = 0; i < 4; ++i) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.buffers[i + 1]);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, i < 2 ? 512 : 48, data[i]);
        glBindBufferBase(GL_UNIFORM_BUFFER, i, p.buffers[i + 1]);
    }
    for (unsigned i = 0; i < material.textures.size(); ++i) {
        glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, material.textures[i]);
    }
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_CULL_FACE);
    if (material.blend) {
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    } else glDisable(GL_BLEND);
    const auto& fans = folded ? p.folded : p.front;
    const auto count = folded ? p.folded_count : p.front_count;
    for (unsigned i = 0; i < count; ++i)
        glDrawArrays(GL_TRIANGLE_FAN, fans[i].first, fans[i].count);
}
}
