#pragma once

#include <emucorev/paf/wave_grid_normal_renderer.h>

#include <array>
#include <cstdint>
#include <memory>

namespace emucorev::paf {

// Register blocks consumed by quad.vp / wave_lit_tex.fp (0x812e4f4c).
struct WaveLightingParameters {
    std::array<float, 20> vertex_data{};    // quad.vp vertexData.buffer0[0..4]
    std::array<float, 96> fragment_data{};  // wave_lit_tex.fp fragmentData.buffer0[0..23]
    std::array<float, 12> viewport_state{}; // GxmRenderVertBufferBlock
    std::array<float, 12> fragment_state{}; // GxmRenderFragBufferBlock
};

// Register blocks consumed by quad.vp / fliptri.fp (0x812ff950).
struct WaveFliptriParameters {
    std::array<float, 20> vertex_data{};    // quad.vp vertexData.buffer0[0..4]
    std::array<float, 8> fragment_data{};   // Offset0, Offset1, Offset2, FlipTexSize
    std::array<float, 12> viewport_state{}; // GxmRenderVertBufferBlock
    std::array<float, 12> fragment_state{}; // GxmRenderFragBufferBlock
};

// Semantic GLES3 renderer for WaveRenderer::render_lighting_texture (wave_lit_tex.fp).
// Writes the pre-filtered 64x64 RGBA8 lit texture from PosTex and NorTex.
class WaveLightingRenderer {
public:
    WaveLightingRenderer();
    ~WaveLightingRenderer();
    WaveLightingRenderer(const WaveLightingRenderer&) = delete;
    WaveLightingRenderer& operator=(const WaveLightingRenderer&) = delete;

    void draw(std::uint32_t position_texture, std::uint32_t normal_texture,
        std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
        const std::array<WaveGridNormalVertex, 4>& vertices,
        const WaveLightingParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Semantic GLES3 renderer for TextureOps::fliptri (811a4f46 / 0x812ff950).
// Owns the 8x8 RGBA8 checkerboard FlipTex created in 811a0b3a (GL_NEAREST, GL_REPEAT).
class WaveFliptriFilter {
public:
    WaveFliptriFilter();
    ~WaveFliptriFilter();
    WaveFliptriFilter(const WaveFliptriFilter&) = delete;
    WaveFliptriFilter& operator=(const WaveFliptriFilter&) = delete;

    void draw(std::uint32_t source_texture, std::uint32_t target_framebuffer,
        std::uint32_t width, std::uint32_t height,
        const std::array<WaveGridNormalVertex, 4>& vertices,
        const WaveFliptriParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
