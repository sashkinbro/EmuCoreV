#pragma once

#include <array>
#include <cstdint>
#include <memory>

namespace emucorev::paf {

// One of the four source TextureOps quad.vp input records.
struct WaveGridNormalVertex { std::array<float, 4> data; };

// Register blocks consumed by quad.vp / grid_normal_8.fp. fragment_data is the
// four-float normal-pass block {1/sourceWidth, 0, 0,
// orientationSign/sourceHeight}, computed by PAF from source dimensions and
// surface orientation. The vertex
// shader's UV transform is in vertex_data, not fragment_data. Values are
// supplied by the caller; this class does not synthesize the source quad or
// GXM viewport state.
struct WaveGridNormalParameters {
    std::array<float, 20> vertex_data{};
    std::array<float, 4> fragment_data{};
    std::array<float, 12> viewport_state{};
    std::array<float, 12> fragment_state{};
};

// Native implementation of TextureOps::gridNormal8 (PAF 0x811a486a).
// The source surface and destination framebuffer are owned by the caller.
// The caller also establishes the source texture's filtering and wrap modes,
// as 810a25ba configures those before invoking this TextureOps routine.
// draw() leaves GLES state set for this numerical pass; the following pass
// must establish its own framebuffer, viewport, and raster state.
class WaveGridNormalRenderer {
public:
    WaveGridNormalRenderer();
    ~WaveGridNormalRenderer();
    WaveGridNormalRenderer(const WaveGridNormalRenderer&) = delete;
    WaveGridNormalRenderer& operator=(const WaveGridNormalRenderer&) = delete;

    void draw(std::uint32_t source_texture, std::uint32_t target_framebuffer,
        std::uint32_t width, std::uint32_t height,
        const std::array<WaveGridNormalVertex, 4>& vertices,
        const WaveGridNormalParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
