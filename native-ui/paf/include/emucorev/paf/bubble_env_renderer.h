#pragma once

#include <cstdint>
#include <memory>

namespace emucorev::paf {

// Generates the 64x64 RGBA8 angular reflection environment map consumed by
// BubbleRenderer's second texture sampler (PAF rect2sphere + Vita dome specular sheen).
class BubbleEnvRenderer {
public:
    static constexpr std::uint32_t kEnvSize = 64;

    BubbleEnvRenderer();
    ~BubbleEnvRenderer();
    BubbleEnvRenderer(const BubbleEnvRenderer&) = delete;
    BubbleEnvRenderer& operator=(const BubbleEnvRenderer&) = delete;

    // Captures live Wave background colors and upper-hemisphere glossy rim sheen
    // into an offscreen 64x64 RGBA8 angular map. Callers must rebind their target
    // FBO afterward if they are not rendering to default FBO 0.
    void capture(std::uint32_t wave_color_texture);

    [[nodiscard]] std::uint32_t texture() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
