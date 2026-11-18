#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace emucorev::paf {

// GLES resources belong to the caller's current context and rendering thread.
// Shaders and image data are supplied by the host, never loaded from firmware
// or through an emulator by this renderer.
// Shader parameter blocks use std140 UBO bindings 0 (vertex) and 1 (fragment);
// bindings 2 and 3 are reserved for the reviewed viewport/material draw state.
struct BubbleVertex {
    std::array<float, 4> profile;
    std::array<float, 4> texture;
};

struct BubbleImage {
    std::uint32_t width, height;
    std::span<const std::uint8_t> rgba;
};

class BubbleRenderer {
public:
    BubbleRenderer(std::string_view vertex_shader, std::string_view fragment_shader,
        std::span<const BubbleVertex> vertices, std::span<const std::uint16_t> indices,
        BubbleImage icon, BubbleImage environment);
    ~BubbleRenderer();
    BubbleRenderer(const BubbleRenderer&) = delete;
    BubbleRenderer& operator=(const BubbleRenderer&) = delete;

    // Original material parameter layout: 24 vertex vec4s and 10 fragment floats.
    // No allocation, shader compilation, resource loading or guest work per draw.
    void draw(std::span<const float, 96> vertex_parameters,
        std::span<const float, 10> fragment_parameters);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emucorev::paf
