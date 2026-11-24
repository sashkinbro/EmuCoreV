#include <emucorev/paf/bubble_reference.h>

#include <bit>
#include <cmath>

namespace emucorev::paf {
namespace {
constexpr std::uint32_t kVertexUniformBits[96] = {
#include "bubble_vertex_uniforms.inc"
};
constexpr std::uint32_t kFragmentUniformBits[10] = {
#include "bubble_fragment_uniforms.inc"
};
constexpr int kRings = 19, kSamples = 32;
}  // namespace

BubbleReference bubble_reference() {
    BubbleReference mesh;
    constexpr double pi = 3.14159265358979323846;
    mesh.vertices.reserve(kRings * kSamples);
    for (int ring = 0; ring < kRings; ++ring) {
        const double theta = pi * ring / 18.0;
        // The last ring's normal is the mirrored one of the 175 degree sample.
        const double normal_theta = ring < 18 ? theta : pi * 175.0 / 180.0;
        const double nx = 0.3 * std::sin(normal_theta), ny = std::cos(normal_theta);
        const double length = std::sqrt(nx * nx + ny * ny);
        for (int sample = 0; sample < kSamples; ++sample) {
            const double angle = 2.0 * pi * sample / 31.0;
            BubbleVertex vertex;
            vertex.profile = {float(std::sin(theta)), float(0.3 * std::cos(theta)), float(nx / length), float(ny / length)};
            vertex.texture = {float(sample / 31.0), float(ring / 18.0), float(std::cos(angle)), float(0.0 - std::sin(angle))};
            mesh.vertices.push_back(vertex);
        }
    }
    // One strip per ring pair; four stitching indices join consecutive strips.
    mesh.indices.reserve(18 * 64 + 17 * 4);
    for (int strip = 0; strip < kRings - 1; ++strip) {
        const auto upper = std::uint16_t((strip + 1) * kSamples);
        if (strip) {
            mesh.indices.push_back(std::uint16_t(strip * kSamples - 1));  // last sample of the previous lower ring
            mesh.indices.insert(mesh.indices.end(), 3, upper);
        }
        for (int sample = 0; sample < kSamples; ++sample) {
            mesh.indices.push_back(std::uint16_t(upper + sample));
            mesh.indices.push_back(std::uint16_t(strip * kSamples + sample));
        }
    }
    for (std::size_t i = 0; i < mesh.vertex_uniforms.size(); ++i) mesh.vertex_uniforms[i] = std::bit_cast<float>(kVertexUniformBits[i]);
    for (std::size_t i = 0; i < mesh.fragment_uniforms.size(); ++i) mesh.fragment_uniforms[i] = std::bit_cast<float>(kFragmentUniformBits[i]);
    return mesh;
}

std::vector<std::uint8_t> bubble_placeholder_icon() {
    std::vector<std::uint8_t> icon(128 * 128 * 4);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const std::uint8_t shade = std::uint8_t(96 + (y * 64) / 127);
            std::uint8_t* pixel = icon.data() + (std::size_t(y) * 128 + x) * 4;
            pixel[0] = pixel[1] = pixel[2] = shade;
            pixel[3] = 255;
        }
    return icon;
}

std::vector<std::uint8_t> bubble_environment_map() { return std::vector<std::uint8_t>(64 * 64 * 4, 0); }

}  // namespace emucorev::paf
