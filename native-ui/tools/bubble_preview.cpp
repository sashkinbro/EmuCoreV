#include <emucorev/paf/bubble_renderer.h>
#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
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
        EGLint major = 0, minor = 0, count = 0;
        if (!eglInitialize(display, &major, &minor)) throw std::runtime_error("Cannot initialize EGL");
        constexpr EGLint attributes[]{EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
            EGL_OPENGL_ES3_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config;
        if (!eglChooseConfig(display, attributes, &config, 1, &count) || !count)
            throw std::runtime_error("Cannot select EGL config");
        constexpr EGLint surface_attributes[]{EGL_WIDTH, 960, EGL_HEIGHT, 544, EGL_NONE};
        surface = eglCreatePbufferSurface(display, config, surface_attributes);
        constexpr EGLint context_attributes[]{EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, context_attributes);
        if (!eglMakeCurrent(display, surface, surface, context)) throw std::runtime_error("Cannot activate GLES");
    }
};

std::vector<std::uint8_t> read(const std::filesystem::path& path, std::size_t expected = 0) {
    std::ifstream input(path, std::ios::binary);
    if (!input || std::filesystem::file_size(path) > 2 * 1024 * 1024)
        throw std::runtime_error("Cannot read input: " + path.string());
    std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(input), {}};
    if (expected && data.size() != expected) throw std::runtime_error("Unexpected input size: " + path.string());
    return data;
}

template<class T> std::vector<T> typed(const std::filesystem::path& path, std::size_t expected) {
    auto bytes = read(path, expected * sizeof(T));
    std::vector<T> output(expected);
    std::memcpy(output.data(), bytes.data(), bytes.size());
    return output;
}
}

// External research inputs are not product assets. No guest code is executed.
int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "Usage: paf_bubble_preview CAPTURE_DIR VERTEX_GLSL FRAGMENT_GLSL ENVIRONMENT_64_RGBA OUTPUT_RGBA\n";
        return 2;
    }
    try {
        const std::filesystem::path directory(argv[1]);
        const auto vertices = typed<emucorev::paf::BubbleVertex>(directory / "stream-0.bin", 608);
        const auto indices = typed<std::uint16_t>(directory / "indices.bin", 1220);
        const auto vertex_parameters = typed<float>(directory / "vertex-ubo-0.bin", 96);
        const auto fragment_parameters = typed<float>(directory / "fragment-ubo-0.bin", 10);
        const auto palette = read(directory / "fragment-texture-0-palette.bin", 1024);
        const auto icon_indices = read(directory / "fragment-texture-0.bin", 128 * 128);
        std::vector<std::uint8_t> icon(128 * 128 * 4);
        for (std::size_t i = 0; i < icon_indices.size(); ++i)
            std::copy_n(palette.data() + icon_indices[i] * 4, 4, icon.data() + i * 4);
        const auto environment = read(argv[4], 64 * 64 * 4);
        const auto vertex_shader = read(argv[2]), fragment_shader = read(argv[3]);
        Context context;
        context.initialize();
        emucorev::paf::BubbleRenderer renderer(
            {reinterpret_cast<const char*>(vertex_shader.data()), vertex_shader.size()},
            {reinterpret_cast<const char*>(fragment_shader.data()), fragment_shader.size()},
            vertices, indices, {128, 128, icon}, {64, 64, environment});
        glViewport(0, 0, 960, 544);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.draw(std::span<const float, 96>(vertex_parameters.data(), 96),
            std::span<const float, 10>(fragment_parameters.data(), 10));
        std::vector<std::uint8_t> pixels(960 * 544 * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 960, 544, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Native bubble draw failed");
        std::ofstream output(argv[5], std::ios::binary);
        for (int row = 543; row >= 0; --row)
            output.write(reinterpret_cast<const char*>(pixels.data() + row * 960 * 4), 960 * 4);
        output.flush();
        if (!output) throw std::runtime_error("Cannot write native framebuffer");
        std::cout << "Rendered native bubble: 960x544 RGBA, GPU " << glGetString(GL_RENDERER) << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
