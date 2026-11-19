#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace emucorev::paf {

struct WaveSurfaceVertex {
    std::array<float, 4> uv;
};

enum class WaveSurfacePrimitive : std::uint8_t { triangles, triangle_strip };

struct WaveSurfaceParameters {
    std::array<float, 36> vertex_data{};       // original wave.vp: 9 vec4s
    std::array<float, 12> viewport_state{};    // GxmRenderVertBufferBlock
    std::array<float, 12> fragment_state{};    // GxmRenderFragBufferBlock
};

struct WaveSurfaceRasterState {
    std::uint32_t depth_function;
    std::uint32_t cull_face;
    bool depth_test;
    bool depth_write;
    bool cull;
};

// Native implementation of WaveRenderer::render_color's named `wave.vp` /
// `wave.fp` surface program. The caller supplies the current simulated mesh,
// PosTex, LitTex, exact register packs, framebuffer and raster state. It does
// not approximate wave geometry, normals, simulation textures, or effect data.
// This pass disables blending, matching render_color. draw() leaves its
// program, VAO, framebuffer, viewport and texture-unit
// bindings active; the next pass must establish its own state. Vertex and
// index buffers are uploaded only when their supplied bytes change.
class WaveSurfaceRenderer {
public:
    WaveSurfaceRenderer();
    ~WaveSurfaceRenderer();
    WaveSurfaceRenderer(const WaveSurfaceRenderer&) = delete;
    WaveSurfaceRenderer& operator=(const WaveSurfaceRenderer&) = delete;

    void draw(std::uint32_t position_texture, std::uint32_t lit_texture,
        std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
        std::span<const WaveSurfaceVertex> vertices,
        std::span<const std::uint16_t> indices,
        WaveSurfacePrimitive primitive, const WaveSurfaceRasterState& raster,
        const WaveSurfaceParameters& parameters);
    void draw(std::uint32_t position_texture, std::uint32_t lit_texture,
        std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
        std::span<const WaveSurfaceVertex> vertices,
        std::span<const std::uint32_t> indices,
        WaveSurfacePrimitive primitive, const WaveSurfaceRasterState& raster,
        const WaveSurfaceParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
