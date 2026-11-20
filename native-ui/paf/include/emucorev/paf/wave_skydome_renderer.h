#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <algorithm>
#include <vector>


namespace emucorev::paf {

struct WaveSkydomeVertex { float u; float v; };
struct WaveSkydomeMesh {
    std::vector<WaveSkydomeVertex> vertices;
    std::vector<std::uint16_t> triangle_strip_indices;
};

// Recreates the source row-major input stream and stitched strip indices from
// PAF FUN_81260b78 / FUN_81201278. The PAF default is 32 columns by 64 rows.
WaveSkydomeMesh make_wave_skydome_mesh(std::uint16_t columns = 32,
    std::uint16_t rows = 64);

// Runtime blocks are produced by WaveRenderer::draw from live source state.
// Zero initialization makes construction safe; it is not a valid visual default.
// The caller must populate every live uniform before drawing.
struct WaveSkydomeParameters {
    std::array<float, 88> vertex_data{};       // skydome.vp buffer0[0..21]
    std::array<float, 12> viewport_state{};    // GxmRenderVertBufferBlock
    std::array<float, 12> fragment_state{};    // GxmRenderFragBufferBlock
    bool blend_enabled = true;                 // 811a5d6c default; 674e toggles it
};

// Named source uniforms packed at the raw float offsets reported for
// skydome.vp blob 0x812e3f20. The gaps are unused register padding.
struct WaveSkydomeVertexUniforms {
    std::array<float, 16> view_projection{};  // float offset 0, PAF resource 0
    std::array<float, 16> model{};             // offset 16, resource 16
    std::array<float, 16> wave_model{};        // offset 32, resource 32
    float world_scale_hack{};                  // offset 48
    std::array<float, 4> horizon_angle{};       // offset 50
    std::array<float, 3> eye_position{};        // offset 54
    std::array<float, 3> sun_position{};        // offset 58
    std::array<float, 4> wave_fog_color{};      // offset 62
    std::array<float, 3> sun_color{};           // offset 66
    float sun_power{};                          // offset 69
    float sun_control{};                        // offset 70
    std::array<float, 3> zenith_color{};        // offset 72
    std::array<float, 3> horizon_color{};       // offset 76
    std::array<float, 2> color_blend_range{};   // offset 80
    std::array<float, 3> distortion_scale{};    // offset 82
};

inline std::array<float, 88> pack_wave_skydome_vertex_uniforms(
    const WaveSkydomeVertexUniforms& u) noexcept {
    std::array<float, 88> out{}; // zero only the unused GXM register padding
    std::copy(u.view_projection.begin(), u.view_projection.end(), out.begin());
    std::copy(u.model.begin(), u.model.end(), out.begin() + 16);
    std::copy(u.wave_model.begin(), u.wave_model.end(), out.begin() + 32);
    out[48] = u.world_scale_hack;
    std::copy(u.horizon_angle.begin(), u.horizon_angle.end(), out.begin() + 50);
    std::copy(u.eye_position.begin(), u.eye_position.end(), out.begin() + 54);
    std::copy(u.sun_position.begin(), u.sun_position.end(), out.begin() + 58);
    std::copy(u.wave_fog_color.begin(), u.wave_fog_color.end(), out.begin() + 62);
    std::copy(u.sun_color.begin(), u.sun_color.end(), out.begin() + 66);
    out[69] = u.sun_power;
    out[70] = u.sun_control;
    std::copy(u.zenith_color.begin(), u.zenith_color.end(), out.begin() + 72);
    std::copy(u.horizon_color.begin(), u.horizon_color.end(), out.begin() + 76);
    std::copy(u.color_blend_range.begin(), u.color_blend_range.end(), out.begin() + 80);
    std::copy(u.distortion_scale.begin(), u.distortion_scale.end(), out.begin() + 82);
    return out;
}

struct WaveGxmViewportUniforms {
    std::array<float, 4> viewport_flip{};
    float viewport_flag{};
    float screen_width{};
    float screen_height{};
    float z_offset{};
    float z_scale{};
};

inline std::array<float, 12> pack_wave_gxm_viewport(
    const WaveGxmViewportUniforms& u) noexcept {
    std::array<float, 12> out{};
    std::copy(u.viewport_flip.begin(), u.viewport_flip.end(), out.begin());
    out[4] = u.viewport_flag;
    out[5] = u.screen_width;
    out[6] = u.screen_height;
    out[7] = u.z_offset;
    out[8] = u.z_scale;
    return out;
}

struct WaveGxmFragmentUniforms {
    std::array<float, 11> values{}; // GxmRenderFragBufferBlock member order
};

inline std::array<float, 12> pack_wave_gxm_fragment(
    const WaveGxmFragmentUniforms& u) noexcept {
    std::array<float, 12> out{};
    std::copy(u.values.begin(), u.values.end(), out.begin());
    return out;
}

// GLES renderer for the verified PAF skydome.vp / skydome.fp pair. It owns
// the recovered 32x64 grid and stitched strip index buffer; the caller owns
// the animated WaveNorTex (including source sampler wrap/filter state) and
// target framebuffer and supplies live uniforms.
// draw() applies the source blend/depth-test/cull state and leaves its program,
// VAO, framebuffer, viewport, and texture-unit-16 binding active. Depth-write,
// stencil, scissor, and color-mask state are unchanged, matching 811a674e;
// callers must establish those before invoking the pass.
class WaveSkydomeRenderer {
public:
    WaveSkydomeRenderer();
    ~WaveSkydomeRenderer();
    WaveSkydomeRenderer(const WaveSkydomeRenderer&) = delete;
    WaveSkydomeRenderer& operator=(const WaveSkydomeRenderer&) = delete;

    void draw(std::uint32_t wave_normal_texture, std::uint32_t target_framebuffer,
        std::uint32_t width, std::uint32_t height,
        const WaveSkydomeParameters& parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
