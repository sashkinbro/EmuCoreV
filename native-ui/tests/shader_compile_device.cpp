#include <EGL/egl.h>
#include <GLES3/gl32.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct Context {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    ~Context() {
        if (display == EGL_NO_DISPLAY) return;
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
        if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
        eglTerminate(display);
    }
    void initialize() {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint major = 0, minor = 0;
        if (!eglInitialize(display, &major, &minor)) throw std::runtime_error("EGL initialization failed");
        constexpr EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
        EGLConfig config;
        EGLint count = 0;
        if (!eglChooseConfig(display, attributes, &config, 1, &count) || !count)
            throw std::runtime_error("No EGL pbuffer configuration");
        constexpr EGLint surface_attributes[] = {EGL_WIDTH, 8, EGL_HEIGHT, 8, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, surface_attributes);
        constexpr EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (!eglMakeCurrent(display, surface, surface, context)) throw std::runtime_error("Cannot activate GLES context");
    }
};
}

int main(int argc, char **argv) {
    if (argc != 3) {
        std::cerr << "Usage: paf_shader_compile_test GLSL_DIRECTORY EXPECTED_SHADER_COUNT\n";
        return 2;
    }
    try {
        std::cout << std::unitbuf;
        const auto expected = std::stoul(argv[2]);
        if (!expected) throw std::runtime_error("Expected shader count must be positive");
        Context context;
        context.initialize();
        std::cout << "GPU " << glGetString(GL_RENDERER) << " / " << glGetString(GL_VERSION) << '\n';
        GLint extensions = 0;
        glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
        for (GLint i = 0; i < extensions; ++i) {
            const std::string name(reinterpret_cast<const char *>(glGetStringi(GL_EXTENSIONS, i)));
            if (name.find("framebuffer_fetch") != std::string::npos || name.find("interlock") != std::string::npos
                || name.find("clip_cull") != std::string::npos) std::cout << "EXT " << name << '\n';
        }
        unsigned passed = 0, failed = 0;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(argv[1])) {
            const auto path = entry.path();
            const auto extension = path.extension().string();
            if (extension != ".vert" && extension != ".frag") continue;
            std::cout << "COMPILE " << path.parent_path().filename().string() << '/' << path.filename().string() << '\n';
            std::ifstream input(path, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read shader source");
            const std::string source{std::istreambuf_iterator<char>(input), {}};
            const auto shader = glCreateShader(extension == ".vert" ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER);
            if (!shader) throw std::runtime_error("Cannot create GLES shader");
            const char *text = source.c_str();
            const GLint length = static_cast<GLint>(source.size());
            glShaderSource(shader, 1, &text, &length);
            glCompileShader(shader);
            GLint compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled) ++passed;
            else {
                ++failed;
                GLint size = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &size);
                std::vector<char> log(std::size_t(size) + 1);
                glGetShaderInfoLog(shader, size, nullptr, log.data());
                std::cout << "FAIL " << path.parent_path().filename().string() << '/' << path.filename().string()
                    << '\n' << log.data() << '\n';
            }
            glDeleteShader(shader);
        }
        std::cout << "RESULT passed=" << passed << " failed=" << failed << " expected=" << expected << '\n';
        if (failed || passed != expected) return 1;
        std::cout << "PASS compilation only; linking, uniforms and rendered pixel equivalence remain unverified\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
