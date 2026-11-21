#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace emucorev::paf {

// Straight-alpha RGBA images, top-left pixel coordinates. All GPU access belongs
// to the owning context/thread; callers upload only when image data changes.
class SpriteRenderer {
public:
    SpriteRenderer();
    ~SpriteRenderer();
    SpriteRenderer(const SpriteRenderer&) = delete;
    SpriteRenderer& operator=(const SpriteRenderer&) = delete;
    void upload(std::uint32_t id, unsigned width, unsigned height,
        std::span<const std::uint8_t> rgba);
    void begin(float width, float height);
    void set_additive_blend(bool additive);
    // Offsets every following rectangle (canvas units); begin() resets it to zero.
    void set_origin(float x, float y) noexcept;
    bool draw(std::uint32_t id, float x, float y, float width, float height,
        std::array<float, 4> tint = {1, 1, 1, 1});
    bool draw_patch(std::uint32_t id, float x, float y, float width, float height,
        float u, float v, float uv_width, float uv_height,
        std::array<float, 4> tint = {1, 1, 1, 1});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
