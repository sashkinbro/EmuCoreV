#pragma once

#include <emucorev/paf/bubble_renderer.h>

#include <array>
#include <cstdint>
#include <vector>

namespace emucorev::paf {

// The Home bubble's mesh and base material, generated in process. Nothing here is
// read from firmware or from shipped asset files.
//
// Mesh: 19 profile rings x 32 angular samples (608 vertices) and 1220 strip
// indices, an analytic reconstruction of the original surface of revolution
// (elliptic profile, radius sin(theta), height 0.3 cos(theta)). It matches the
// recorded original vertex stream to within a few 1e-6 per component, not bit for
// bit (the original routines are not recovered).
// Uniforms: the original draw's vertex (24 vec4) and fragment (10 float) blocks,
// recorded values; PROVISIONAL provenance (GPU capture of firmware 3.74).
struct BubbleReference {
    std::vector<BubbleVertex> vertices;
    std::vector<std::uint16_t> indices;
    std::array<float, 96> vertex_uniforms;
    std::array<float, 10> fragment_uniforms;
};

BubbleReference bubble_reference();

// 128x128 RGBA neutral placeholder icon, and the 64x64 RGBA environment map
// (zero filled, as the original environment resource has never been recovered).
std::vector<std::uint8_t> bubble_placeholder_icon();
std::vector<std::uint8_t> bubble_environment_map();

}  // namespace emucorev::paf
