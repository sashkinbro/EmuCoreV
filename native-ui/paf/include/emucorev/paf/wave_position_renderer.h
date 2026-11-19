#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace emucorev::paf {

// One attribute record for the source wave_pos.vp `data` input.
struct WavePositionVertex {
    std::array<float, 4> data;
};

// Raw shader register blocks used by the translated Vita wave_pos program.
// Values are supplied by the caller because draw_pos' register upload fields
// depend on the live wave simulation and render context.
struct WavePositionPassParameters {
    std::array<float, 16> vertex_data;       // vertexData.buffer0[0..3]
    std::array<float, 32> fragment_data;     // fragmentData.buffer0[0..7]
    std::array<float, 12> viewport_state;    // GxmRenderVertBufferBlock
    std::array<float, 12> fragment_state;    // GxmRenderFragBufferBlock
};

// Native GLES implementation of the recovered wave_pos.vp / wave_pos.fp
// four-vertex position pass. It writes the shader's encoded position values
// into a caller-owned framebuffer; the caller owns the HeightMap and target
// attachment and provides runtime register blocks from its wave simulation.
// Construct and destroy on a thread with the intended GLES context current.
// draw() leaves its program, VAO, framebuffer, viewport, texture-unit-7 binding,
// and the pass's blend/depth/stencil/scissor/cull/dither state active; the next
// pass must establish its own state.
class WavePositionPassRenderer {
public:
    WavePositionPassRenderer();
    ~WavePositionPassRenderer();
    WavePositionPassRenderer(const WavePositionPassRenderer&) = delete;
    WavePositionPassRenderer& operator=(const WavePositionPassRenderer&) = delete;

    void draw(std::uint32_t height_map_texture, std::uint32_t target_framebuffer,
        std::uint32_t width, std::uint32_t height,
        std::span<const WavePositionVertex, 4> vertices,
        const WavePositionPassParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
