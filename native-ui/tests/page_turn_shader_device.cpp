#include <emucorev/paf/page_turn_shaders.h>
#include <emucorev/paf/page_turn_renderer.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Context {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    Context() {
        EGLint major, minor, count;
        if (!eglInitialize(display, &major, &minor)) throw std::runtime_error("EGL initialization");
        constexpr EGLint config_attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        if (!eglChooseConfig(display, config_attributes, &config, 1, &count) || !count)
            throw std::runtime_error("EGL config");
        constexpr EGLint attributes[]{EGL_WIDTH, 128, EGL_HEIGHT, 64, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, attributes);
        constexpr EGLint version[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, version);
        if (!eglMakeCurrent(display, surface, surface, context)) throw std::runtime_error("EGL activation");
    }
    ~Context() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    }
};
GLuint compile(GLenum type, const std::string& source) {
    auto result = glCreateShader(type);
    const auto* text = source.c_str();
    glShaderSource(result, 1, &text, nullptr); glCompileShader(result);
    GLint ok; glGetShaderiv(result, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        std::array<char, 8192> log{};
        glGetShaderInfoLog(result, log.size(), nullptr, log.data());
        throw std::runtime_error(log.data());
    }
    return result;
}
GLuint program(const std::string& fragment) {
    const auto vs = compile(GL_VERTEX_SHADER, R"(#version 320 es
precision highp float;
layout(location=4) out vec4 v_TexCoord0;
layout(location=5) out vec4 v_TexCoord1;
void main() {
    vec2 uv=vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    gl_Position=vec4(uv*2.0-1.0,0.0,1.0);
    v_TexCoord0=vec4(uv,uv.x*1.7-0.25,1.0);
    v_TexCoord1=vec4(uv*4.0-2.0,0.0,1.0);
})");
    const auto fs = compile(GL_FRAGMENT_SHADER, fragment);
    const auto result = glCreateProgram();
    glAttachShader(result, vs); glAttachShader(result, fs); glLinkProgram(result);
    glDeleteShader(vs); glDeleteShader(fs);
    GLint ok; glGetProgramiv(result, GL_LINK_STATUS, &ok);
    if (!ok) throw std::runtime_error("Page-turn shader link failed");
    return result;
}
void check_mesh_renderer() {
    using namespace emucorev::paf;
    constexpr auto vertex = R"(#version 320 es
precision highp float;
layout(location=0) in vec2 point;
void main() { gl_Position=vec4(point.x-1.0, point.y*2.0-1.0, 0.0, 1.0); }
)";
    constexpr auto red = R"(#version 320 es
precision highp float;
layout(location=0) out vec4 color;
void main() { color=vec4(1.0,0.0,0.0,1.0); }
)";
    constexpr auto blue = R"(#version 320 es
precision highp float;
layout(location=0) out vec4 color;
void main() { color=vec4(0.0,0.0,1.0,1.0); }
)";
    PageTurnRenderer renderer({vertex,red},{vertex,blue});
    PageSurfaceDraw material{};
    PageTurnMesh mesh;
    std::array<unsigned char,128*64*4> pixels;
    unsigned checked = 0;
    for (auto deepest : std::array<PagePoint,4>{{{1.9f,.95f},{1.1f,.4f},{-.8f,-.3f},{-3.f,-2.f}}}) {
        const auto shape = page_turn_fold_shape(deepest,{2,1});
        page_turn_mesh(shape,mesh); renderer.upload_mesh(mesh);
        glClearColor(0,1,0,0); glClear(GL_COLOR_BUFFER_BIT);
        renderer.draw(PageSurface::front,material); renderer.draw(PageSurface::folded,material);
        glReadPixels(0,0,128,64,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        if(glGetError()!=GL_NO_ERROR) throw std::runtime_error("Mesh renderer GPU error");
        for(unsigned y=0;y<64;++y) for(unsigned x=0;x<128;++x) {
            const float distance = ((x+.5f)/64.f-shape.center.x)*shape.normal.x
                + ((y+.5f)/64.f-shape.center.y)*shape.normal.y;
            if(std::abs(distance)<.00001f) continue;
            const auto i=(y*128+x)*4;
            if(pixels[i]!=(distance>=0?255:0) || pixels[i+1]!=0
                || pixels[i+2]!=(distance<0?255:0) || pixels[i+3]!=255)
                throw std::runtime_error("Mesh renderer leaves a hole or draws the wrong page side");
            ++checked;
        }
    }
    std::cout << "PASS page surface fan submission: " << checked << " pixel centers / four folds\n";
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 2) throw std::runtime_error("Usage: paf_page_shader_test [REFERENCE_FRAGMENT]");
        Context context;
        std::cout << std::unitbuf << "GPU " << glGetString(GL_RENDERER) << '\n';
        const auto native = program(std::string(emucorev::paf::page_turn_empty_back_fragment_shader()));
        GLuint reference = 0;
        if (argc == 2) {
            std::ifstream file(argv[1], std::ios::binary);
            if (!file) throw std::runtime_error("Missing reference shader");
            std::string source{std::istreambuf_iterator<char>(file), {}};
            const std::string old = "layout(binding = 1, std430) restrict readonly buffer fragmentDataType";
            const auto at = source.find(old);
            if (at == std::string::npos) throw std::runtime_error("Unexpected reference parameter block");
            source.replace(at, old.size(), "layout(binding = 1, std140) uniform fragmentDataType");
            reference = program(source);
        }
        GLuint vao, buffers[2], textures[2], framebuffer, output;
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glGenBuffers(2, buffers);
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[0]);
        glBufferData(GL_UNIFORM_BUFFER, 64, nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 1, buffers[0]);
        const std::array<float, 12> state{0,0,0,0,1,0,0,1.f/128,1.f/64,0,48,0};
        glBindBuffer(GL_UNIFORM_BUFFER, buffers[1]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(state), state.data(), GL_STATIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 3, buffers[1]);
        glGenTextures(2, textures);
        std::array<unsigned char, 32*32*4> pixels;
        for (unsigned t = 0; t < 2; ++t) {
            for (unsigned i = 0; i < pixels.size(); ++i)
                pixels[i] = static_cast<unsigned char>((i*37 + (i/4)*13 + t*79) % 256);
            glActiveTexture(GL_TEXTURE0 + t); glBindTexture(GL_TEXTURE_2D, textures[t]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 32,32,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
        }
        glGenTextures(1, &output); glBindTexture(GL_TEXTURE_2D, output);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 128,64);
        glGenFramebuffers(1, &framebuffer); glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output,0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Incomplete RGBA target");
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, textures[1]);
        glViewport(0,0,128,64); glDisable(GL_BLEND); glDisable(GL_DITHER);
        std::array<unsigned char, 128*64*4> original{}, actual{};
        unsigned differences = 0, nonzero = 0, reported = 0;
        std::array<unsigned,4> channel_differences{}, maximum{};
        for (unsigned sample = 0; sample < 16; ++sample) {
            const float rate = static_cast<float>(sample)/15;
            const std::array<float,16> material{.2f,.3f,.8f,1.2f,rate,0,.7f,.9f,
                1.1f,.25f+rate,.1f,.2f,.3f,0,1.f+rate,.7f};
            glBindBuffer(GL_UNIFORM_BUFFER,buffers[0]);
            glBufferSubData(GL_UNIFORM_BUFFER,0,sizeof(material),material.data());
            auto render = [&](GLuint shader, auto& result) {
                glUseProgram(shader); glDrawArrays(GL_TRIANGLE_STRIP,0,4);
                glReadPixels(0,0,128,64,GL_RGBA,GL_UNSIGNED_BYTE,result.data());
                if (glGetError() != GL_NO_ERROR) throw std::runtime_error("GPU draw error");
            };
            render(native, actual);
            nonzero += std::count_if(actual.begin(),actual.end(),[](auto value){return value != 0;});
            if (reference) {
                render(reference, original);
                for (unsigned i=0;i<actual.size();++i) {
                    if(i%4==3 && actual[i]!=original[i] && reported++<8)
                        std::cout << "alpha sample " << sample << " pixel " << i/4 << " native " << unsigned(actual[i]) << " original " << unsigned(original[i]) << '\n';
                    differences += actual[i] != original[i];
                    channel_differences[i%4] += actual[i] != original[i];
                    maximum[i%4] = std::max(maximum[i%4], static_cast<unsigned>(std::abs(int(actual[i])-int(original[i]))));
                }
            }
        }
        glDeleteProgram(native); if(reference) glDeleteProgram(reference);
        glDeleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&output);
        glDeleteTextures(2,textures); glDeleteBuffers(2,buffers); glDeleteVertexArrays(1,&vao);
        glBindFramebuffer(GL_FRAMEBUFFER,0);
        check_mesh_renderer();
        std::cout << "16 material draws, " << nonzero << " nonzero channels\n";
        if(reference) {
            std::cout << differences << " differing channels\n";
            for(unsigned i=0;i<4;++i) std::cout << "channel " << i << ": " << channel_differences[i] << " max " << maximum[i] << '\n';
        } else std::cout << "Native draw only; no reference comparison\n";
        if (!nonzero || differences) return 1;
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
