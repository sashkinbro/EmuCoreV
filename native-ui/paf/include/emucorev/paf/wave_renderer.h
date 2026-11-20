#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>

#include <emucorev/paf/wave_preset.h>

namespace emucorev::paf {

// Complete 5-pass PS Vita Wave + SkyDome GPU background renderer:
//   Pass 1a: WavePositionPassRenderer (wave_pos)   -> 64x64 RGBA16F PosTex
//   Pass 1b: WaveGridNormalRenderer (gridNormal8)  -> 64x64 RGBA8 NorTex
//   Pass 2a: WaveLightingRenderer (wave_lit_tex)   -> 64x64 RGBA8 LitTex
//   Pass 2b: WaveFliptriFilter (fliptri)           -> 64x64 RGBA8 LitBlurTex
//   Pass 3:  Clear main color + depth buffer to fog clear color
//   Pass 4:  WaveSurfaceRenderer (wave)            -> 64x64 stitched strip mesh
//   Pass 5:  WaveSkydomeRenderer (skydome)         -> 32x64 sky dome (GL_LEQUAL behind wave)
class WaveRenderer {
public:
    explicit WaveRenderer(std::string_view theme_name = "effects_default_0512_01");
    ~WaveRenderer();
    WaveRenderer(const WaveRenderer&) = delete;
    WaveRenderer& operator=(const WaveRenderer&) = delete;

    void set_theme(std::string_view theme_name);
    void set_theme(const WaveThemePreset& theme);
    [[nodiscard]] const WaveThemePreset& theme() const noexcept;

    // Advances the 32x32 CPU wave heightfield simulation in firmware 60Hz ticks.
    void step(float elapsed_seconds);

    // Draws the complete Wave + SkyDome background into target_framebuffer,
    // which must have both color and depth attachments of width x height.
    // Restores clean GL state before returning.
    void draw(std::uint32_t target_framebuffer, std::uint32_t width, std::uint32_t height,
        float pitch_rad = std::numeric_limits<float>::quiet_NaN(), float roll_rad = 0.0f);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
