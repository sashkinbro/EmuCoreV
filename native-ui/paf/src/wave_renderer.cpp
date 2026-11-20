#include <emucorev/paf/wave_renderer.h>

#include <emucorev/paf/wave_grid_normal_renderer.h>
#include <emucorev/paf/wave_lighting_renderer.h>
#include <emucorev/paf/wave_position_renderer.h>
#include <emucorev/paf/wave_scene.h>
#include <emucorev/paf/wave_simulation.h>
#include <emucorev/paf/wave_skydome_renderer.h>
#include <emucorev/paf/wave_surface_renderer.h>

#include <GLES3/gl32.h>
#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace emucorev::paf {

namespace {

constexpr float kWaveSimTickSeconds = 1.0f / 60.0f;

constexpr std::array<WavePositionVertex, 4> kPositionQuadVertices{{
    {{0.0f, 1.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 0.0f, 0.0f}},
    {{1.0f, 1.0f, 1.0f, 1.0f}},
    {{1.0f, 0.0f, 1.0f, 0.0f}},
}};

// PAF 811a486a (gridNormal8) and 811a4f46 (fliptri) flip V in the source quad
// so texops GXM-oriented sampling reads upright rows.
constexpr std::array<WaveGridNormalVertex, 4> kFlippedQuadVertices{{
    {{0.0f, 1.0f, 0.0f, 0.0f}},
    {{0.0f, 0.0f, 0.0f, 1.0f}},
    {{1.0f, 1.0f, 1.0f, 0.0f}},
    {{1.0f, 0.0f, 1.0f, 1.0f}},
}};

bool sim_presets_equal(const WaveSimPreset& a, const WaveSimPreset& b) noexcept {
    return a.gravity == b.gravity
        && a.A == b.A
        && a.logA == b.logA
        && a.patch_size == b.patch_size
        && a.wind_speed == b.wind_speed
        && a.wind_dir == b.wind_dir
        && a.time_step == b.time_step;
}

void set_tex2d_sampler(GLuint tex, GLint filter, GLint wrap) {
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

void create_rtt(GLenum internal_format, GLuint& out_tex, GLuint& out_fbo, const char* label) {
    glGenTextures(1, &out_tex);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, out_tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, internal_format,
        static_cast<GLsizei>(kWaveOffscreenSize), static_cast<GLsizei>(kWaveOffscreenSize));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &out_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, out_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out_tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        throw std::runtime_error(std::string("Incomplete wave offscreen FBO: ") + label);
    }
}

} // namespace

struct WaveRenderer::Impl {
    WaveThemePreset theme;
    WaveHeightfield32 sim{};
    std::array<std::uint16_t, kWaveFftCellCount> height_half{};
    float sim_accum = 0.0f;
    bool height_dirty = true;

    WaveSurfaceMesh surface_mesh;
    WavePositionPassRenderer position;
    WaveGridNormalRenderer grid_normal;
    WaveLightingRenderer lighting;
    WaveFliptriFilter fliptri;
    WaveSurfaceRenderer surface;
    WaveSkydomeRenderer skydome;

    GLuint height_tex = 0;
    GLuint pos_tex = 0, pos_fbo = 0;
    GLuint nor_tex = 0, nor_fbo = 0;
    GLuint lit_tex = 0, lit_fbo = 0;
    GLuint lit_blur_tex = 0, lit_blur_fbo = 0;

    explicit Impl(const WaveThemePreset& initial_theme)
        : theme(initial_theme),
          surface_mesh(make_wave_surface_mesh(kWaveOffscreenSize, kWaveOffscreenSize)) {
        if (!sim.reset(theme.sim, 1u) || !sim.step(height_half, false)) {
            throw std::runtime_error("Failed to initialize WaveHeightfield32");
        }

        // HeightMap is sampled in wave_pos.fp via .a; swizzle R into RGBA.
        glGenTextures(1, &height_tex);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, height_tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16F,
            static_cast<GLsizei>(kWaveFftSize), static_cast<GLsizei>(kWaveFftSize));
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_G, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_RED);

        create_rtt(GL_RGBA16F, pos_tex, pos_fbo, "PosTex");
        create_rtt(GL_RGBA8, nor_tex, nor_fbo, "NorTex");
        create_rtt(GL_RGBA8, lit_tex, lit_fbo, "LitTex");
        create_rtt(GL_RGBA8, lit_blur_tex, lit_blur_fbo, "LitBlurTex");

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    ~Impl() {
        const std::array<GLuint, 4> fbos{pos_fbo, nor_fbo, lit_fbo, lit_blur_fbo};
        glDeleteFramebuffers(static_cast<GLsizei>(fbos.size()), fbos.data());
        const std::array<GLuint, 5> texs{height_tex, pos_tex, nor_tex, lit_tex, lit_blur_tex};
        glDeleteTextures(static_cast<GLsizei>(texs.size()), texs.data());
    }
};

WaveRenderer::WaveRenderer(std::string_view theme_name)
    : impl_(std::make_unique<Impl>(wave_theme_preset_or_default(theme_name))) {}

WaveRenderer::~WaveRenderer() = default;

void WaveRenderer::set_theme(std::string_view theme_name) {
    set_theme(wave_theme_preset_or_default(theme_name));
}

void WaveRenderer::set_theme(const WaveThemePreset& theme) {
    const bool sim_changed = !sim_presets_equal(impl_->theme.sim, theme.sim);
    impl_->theme = theme;
    if (sim_changed) {
        if (!impl_->sim.reset(theme.sim, 1u) || !impl_->sim.step(impl_->height_half, false)) {
            throw std::runtime_error("Failed to reset WaveHeightfield32 for theme");
        }
        impl_->sim_accum = 0.0f;
        impl_->height_dirty = true;
    }
}

const WaveThemePreset& WaveRenderer::theme() const noexcept {
    return impl_->theme;
}

void WaveRenderer::step(float elapsed_seconds) {
    if (elapsed_seconds <= 0.0f) return;
    impl_->sim_accum += std::min(elapsed_seconds, 0.1f);
    while (impl_->sim_accum >= kWaveSimTickSeconds) {
        impl_->sim_accum -= kWaveSimTickSeconds;
        if (!impl_->sim.step(impl_->height_half, true)) {
            throw std::runtime_error("WaveHeightfield32 step failed");
        }
        impl_->height_dirty = true;
    }
}

void WaveRenderer::draw(std::uint32_t target_framebuffer, std::uint32_t width,
    std::uint32_t height, float pitch_rad, float roll_rad) {
    if (!width || !height) {
        throw std::invalid_argument("WaveRenderer target dimensions must be positive");
    }

    auto& p = *impl_;
    if (p.height_dirty) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, p.height_tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
            static_cast<GLsizei>(kWaveFftSize), static_cast<GLsizei>(kWaveFftSize),
            GL_RED, GL_HALF_FLOAT, p.height_half.data());
        p.height_dirty = false;
    }

    const WaveSceneFrame frame = build_wave_scene_frame(p.theme, width, height, pitch_rad, roll_rad);

    // Offscreen RTT passes assume clean raster state without blending, depth, or culling.
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glFrontFace(GL_CCW);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    // Pass 1a: HeightMap -> 64x64 RGBA16F PosTex
    set_tex2d_sampler(p.height_tex, GL_LINEAR, GL_REPEAT);
    p.position.draw(p.height_tex, p.pos_fbo, kWaveOffscreenSize, kWaveOffscreenSize,
        kPositionQuadVertices, frame.position);

    // Pass 1b: PosTex -> 64x64 RGBA8 NorTex
    set_tex2d_sampler(p.pos_tex, GL_NEAREST, GL_CLAMP_TO_EDGE);
    p.grid_normal.draw(p.pos_tex, p.nor_fbo, kWaveOffscreenSize, kWaveOffscreenSize,
        kFlippedQuadVertices, frame.grid_normal);

    // Pass 2a: PosTex + NorTex -> 64x64 RGBA8 LitTex
    set_tex2d_sampler(p.pos_tex, GL_NEAREST, GL_CLAMP_TO_EDGE);
    set_tex2d_sampler(p.nor_tex, GL_NEAREST, GL_CLAMP_TO_EDGE);
    p.lighting.draw(p.pos_tex, p.nor_tex, p.lit_fbo, kWaveOffscreenSize, kWaveOffscreenSize,
        kPafTextureOpsQuadVertices, frame.lighting);

    // Pass 2b: LitTex -> 64x64 RGBA8 LitBlurTex
    set_tex2d_sampler(p.lit_tex, GL_LINEAR, GL_CLAMP_TO_EDGE);
    p.fliptri.draw(p.lit_tex, p.lit_blur_fbo, kWaveOffscreenSize, kWaveOffscreenSize,
        kFlippedQuadVertices, frame.fliptri);

    // Pass 3: Clear main color + depth buffer to active fog clear color.
    glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(width), static_cast<GLsizei>(height));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(frame.clear_color[0], frame.clear_color[1], frame.clear_color[2], 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Pass 4: Wave surface mesh (writes depth for SkyDome occlusion when needed).
    set_tex2d_sampler(p.pos_tex, GL_LINEAR, GL_CLAMP_TO_EDGE);
    set_tex2d_sampler(p.lit_blur_tex, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glDisable(GL_BLEND);
    glFrontFace(GL_CCW);
    p.surface.draw(p.pos_tex, p.lit_blur_tex, target_framebuffer, width, height,
        p.surface_mesh.vertices, p.surface_mesh.triangle_strip_indices,
        WaveSurfacePrimitive::triangle_strip, frame.surface_raster, frame.surface);

    // Pass 5: Sky dome blended behind wave geometry.
    // Upright viewport_flip.y=+1 flips SkyDome winding relative to GXM;
    // WaveSkydomeRenderer sets glCullFace(GL_FRONT), so flip glFrontFace for this pass.
    set_tex2d_sampler(p.nor_tex, GL_LINEAR, GL_CLAMP_TO_EDGE);
    glFrontFace(GL_CW);
    p.skydome.draw(p.nor_tex, target_framebuffer, width, height, frame.skydome);
    glFrontFace(GL_CCW);

    // Restore clean GL state before returning to SpriteRenderer / BubbleRenderer / PageTurn.
    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

} // namespace emucorev::paf
