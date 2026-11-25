#include <emucorev/paf/bubble_renderer.h>

#include <GLES3/gl32.h>
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace emucorev::paf {
namespace {
GLuint compile(GLenum stage, std::string_view source) {
    if (source.empty() || source.size() > 1024 * 1024)
        throw std::invalid_argument("Invalid bubble shader size");
    const GLuint shader = glCreateShader(stage);
    const char* text = source.data();
    const GLint length = static_cast<GLint>(source.size());
    glShaderSource(shader, 1, &text, &length);
    glCompileShader(shader);
    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint size = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetShaderInfoLog(shader, size, nullptr, log.data());
        glDeleteShader(shader);
        throw std::runtime_error("Bubble shader: " + log);
    }
    return shader;
}

void check_image(BubbleImage image) {
    if (!image.width || !image.height || image.width > 4096 || image.height > 4096
        || image.rgba.size() != std::size_t(image.width) * image.height * 4)
        throw std::invalid_argument("Invalid RGBA bubble image");
}
}

// Everything every bubble of a Home page has in common: program, mesh, environment map and the
// static viewport/state blocks. A page shows ten bubbles and a catalog can hold hundreds; one
// program per bubble cost a shader compile + link on the render thread each time a new bubble first
// appeared (a visible hitch on weak GPUs) and a program, VAO and mesh copy per game for good.
struct BubbleShared {
    GLuint program = 0, vao = 0;
    GLuint vertex_buffer = 0, index_buffer = 0;
    std::array<GLuint, 2> state_buffers{};
    GLuint environment = 0;
    GLsizei count = 0;

    ~BubbleShared() {
        if (environment) glDeleteTextures(1, &environment);
        glDeleteBuffers(2, state_buffers.data());
        if (vertex_buffer) glDeleteBuffers(1, &vertex_buffer);
        if (index_buffer) glDeleteBuffers(1, &index_buffer);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (program) glDeleteProgram(program);
    }
};

struct BubbleRenderer::Impl {
    std::shared_ptr<const BubbleShared> shared;
    // Per bubble: its icon and the two parameter blocks rewritten on every draw (kept per bubble so a
    // frame's ten uploads never rewrite a buffer an earlier draw of the same frame still reads).
    GLuint icon = 0;
    std::array<GLuint, 2> parameter_buffers{};

    ~Impl() {
        if (icon) glDeleteTextures(1, &icon);
        glDeleteBuffers(2, parameter_buffers.data());
    }
};

std::shared_ptr<const BubbleShared> make_bubble_shared(std::string_view vertex_source,
    std::string_view fragment_source, std::span<const BubbleVertex> vertices,
    std::span<const std::uint16_t> indices, BubbleImage environment) {
    static_assert(sizeof(BubbleVertex) == 32);
    check_image(environment);
    if (vertices.empty() || vertices.size() > 65536 || indices.empty()
        || indices.size() > 1024 * 1024
        || std::any_of(indices.begin(), indices.end(), [&](auto i) { return i >= vertices.size(); }))
        throw std::invalid_argument("Invalid bubble mesh");
    auto shared = std::make_shared<BubbleShared>();
    auto& p = *shared;
    const auto vs = compile(GL_VERTEX_SHADER, vertex_source);
    GLuint fs = 0;
    try { fs = compile(GL_FRAGMENT_SHADER, fragment_source); }
    catch (...) { glDeleteShader(vs); throw; }
    p.program = glCreateProgram();
    glAttachShader(p.program, vs);
    glAttachShader(p.program, fs);
    glLinkProgram(p.program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint size = 0;
        glGetProgramiv(p.program, GL_INFO_LOG_LENGTH, &size);
        std::string log(static_cast<std::size_t>(std::max(1, size)), '\0');
        glGetProgramInfoLog(p.program, size, nullptr, log.data());
        throw std::runtime_error("Bubble program: " + log);
    }
    glGenVertexArrays(1, &p.vao);
    glBindVertexArray(p.vao);
    glGenBuffers(1, &p.vertex_buffer);
    glBindBuffer(GL_ARRAY_BUFFER, p.vertex_buffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(), GL_STATIC_DRAW);
    for (GLuint i = 0; i < 2; ++i) {
        glEnableVertexAttribArray(i);
        glVertexAttribPointer(i, 4, GL_FLOAT, GL_FALSE, 32, reinterpret_cast<const void*>(std::uintptr_t(i * 16)));
    }
    glGenBuffers(1, &p.index_buffer);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, p.index_buffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size_bytes()), indices.data(), GL_STATIC_DRAW);
    p.count = static_cast<GLsizei>(indices.size());
    // Present directly to a conventional GLES target. The reference backend
    // reverses Y here and again at presentation; this renderer has no second
    // presentation flip, so its projection and winding keep native GLES Y.
    const std::array<float, 12> viewport{1, 1, 1, 1, 1, 960, 544, .5f, .5f, 0, 0, 0};
    const std::array<float, 12> fragment_state{0, 0, 0, 0, 1, 0, 0, 1.f / 960, 1.f / 544, 0, 112, 0};
    glGenBuffers(2, p.state_buffers.data());
    for (unsigned i = 0; i < 2; ++i) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.state_buffers[i]);
        glBufferData(GL_UNIFORM_BUFFER, 48, i == 0 ? viewport.data() : fragment_state.data(), GL_STATIC_DRAW);
    }
    glGenTextures(1, &p.environment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, p.environment);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, environment.width, environment.height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, environment.rgba.data());
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Cannot upload bubble resources");
    return shared;
}

BubbleRenderer::BubbleRenderer(std::string_view vertex_source, std::string_view fragment_source,
    std::span<const BubbleVertex> vertices, std::span<const std::uint16_t> indices,
    BubbleImage icon, BubbleImage environment)
    : BubbleRenderer(make_bubble_shared(vertex_source, fragment_source, vertices, indices, environment), icon) {}

BubbleRenderer::BubbleRenderer(std::shared_ptr<const BubbleShared> shared, BubbleImage icon)
    : impl_(std::make_unique<Impl>()) {
    if (!shared) throw std::invalid_argument("Missing shared bubble resources");
    check_image(icon);
    auto& p = *impl_;
    p.shared = std::move(shared);
    glGenBuffers(2, p.parameter_buffers.data());
    for (unsigned i = 0; i < 2; ++i) {
        glBindBuffer(GL_UNIFORM_BUFFER, p.parameter_buffers[i]);
        glBufferData(GL_UNIFORM_BUFFER, i == 0 ? 384 : 48, nullptr, GL_DYNAMIC_DRAW);
    }
    glGenTextures(1, &p.icon);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glBindTexture(GL_TEXTURE_2D, p.icon);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, icon.width, icon.height,
        0, GL_RGBA, GL_UNSIGNED_BYTE, icon.rgba.data());
    if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Cannot upload bubble resources");
}

BubbleRenderer::~BubbleRenderer() = default;

void BubbleRenderer::draw(std::span<const float, 96> vertex, std::span<const float, 10> fragment,
    unsigned environment_override) {
    auto& p = *impl_;
    const auto& shared = *p.shared;
    glUseProgram(shared.program);
    glBindVertexArray(shared.vao);
    const std::array<float, 12> padded_fragment{fragment[0], fragment[1], fragment[2], fragment[3],
        fragment[4], fragment[5], fragment[6], fragment[7], fragment[8], fragment[9], 0, 0};
    glBindBuffer(GL_UNIFORM_BUFFER, p.parameter_buffers[0]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 384, vertex.data());
    glBindBuffer(GL_UNIFORM_BUFFER, p.parameter_buffers[1]);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, 48, padded_fragment.data());
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, p.parameter_buffers[0]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 1, p.parameter_buffers[1]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 2, shared.state_buffers[0]);
    glBindBufferBase(GL_UNIFORM_BUFFER, 3, shared.state_buffers[1]);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.icon);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, environment_override != 0
        ? static_cast<GLuint>(environment_override) : shared.environment);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glCullFace(GL_BACK);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDrawElements(GL_TRIANGLE_STRIP, shared.count, GL_UNSIGNED_SHORT, nullptr);
}
} // namespace emucorev::paf
