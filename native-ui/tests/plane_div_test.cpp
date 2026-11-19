#include <emucorev/paf/plane_div.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::uint32_t read_u32(std::ifstream& in) {
    std::array<std::uint8_t, 4> b{};
    in.read(reinterpret_cast<char*>(b.data()), 4);
    if (!in) throw std::runtime_error("Truncated plane_div corpus");
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8)
        | (std::uint32_t(b[2]) << 16) | (std::uint32_t(b[3]) << 24);
}

std::int32_t read_i32(std::ifstream& in) {
    return std::bit_cast<std::int32_t>(read_u32(in));
}

float read_f32(std::ifstream& in) {
    return std::bit_cast<float>(read_u32(in));
}

template <typename T>
T read_exact(std::ifstream& in) {
    T val{};
    in.read(reinterpret_cast<char*>(&val), sizeof(T));
    if (!in) throw std::runtime_error("Truncated plane_div corpus");
    return val;
}

void expect_bits(std::size_t idx, const char* label, float actual, std::uint32_t expected) {
    const std::uint32_t got = std::bit_cast<std::uint32_t>(actual);
    if (got != expected) {
        std::cerr << "case=" << idx << " field=" << label << std::hex
            << " expected=0x" << expected << " actual=0x" << got << std::dec << '\n';
        throw std::runtime_error("plane_div output differs bit-for-bit from paf.elf");
    }
}

void check_livearea_gate_quads() {
    emucorev::paf::PlaneDivSpec spec{};
    spec.size_x = 292.f;
    spec.size_y = 170.f;
    spec.tex_width = 74;
    spec.tex_height = 82;
    spec.div_mode = emucorev::paf::PlaneDivMode::Grid3x3;
    spec.div_ratio_h = {0.487800002f, 0.487800002f};
    spec.div_ratio_v = {0.487800002f, 0.487800002f};
    spec.margin_left = 5;
    spec.margin_right = 5;
    spec.margin_top = 9;
    spec.margin_bottom = 9;

    const auto quads = emucorev::paf::build_plane_div_screen_quads(spec, 480.f, 228.f);
    if (quads.size() != 9u) {
        throw std::runtime_error("Expected 9 patches for 3x3 LiveArea gate frame");
    }
    // Outer bounds: 292 + 5 + 5 = 302 wide centered at 480 -> [329, 631];
    // 170 + 9 + 9 = 188 tall centered at 228 -> [134, 322].
    const auto& tl = quads.front();
    const auto& br = quads.back();
    if (tl.x != 329.f || tl.y != 134.f) {
        throw std::runtime_error("Unexpected top-left coordinate for LiveArea gate 9-slice");
    }
    if (br.x + br.width != 631.f || br.y + br.height != 322.f) {
        throw std::runtime_error("Unexpected bottom-right coordinate for LiveArea gate 9-slice");
    }
    if (tl.width <= 36.09f || tl.width >= 36.10f || std::fabs(br.width - tl.width) > 1e-4f) {
        throw std::runtime_error("Corner width not preserved symmetrically for LiveArea gate 9-slice");
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: paf_plane_div_test ORIGINAL_CORPUS");
        std::ifstream input(argv[1], std::ios::binary);
        char magic[8];
        input.read(magic, 8);
        if (!input || std::string(magic, 8) != "PAFPDV01") {
            throw std::runtime_error("Invalid plane_div corpus magic");
        }
        const std::uint32_t count = read_u32(input);
        if (count == 0u || count > 1000u) throw std::runtime_error("Invalid corpus case count");

        for (std::uint32_t i = 0; i < count; ++i) {
            emucorev::paf::PlaneDivSpec spec{};
            spec.pos_x = read_f32(input);
            spec.pos_y = read_f32(input);
            spec.pos_z = read_f32(input);
            spec.size_x = read_f32(input);
            spec.size_y = read_f32(input);
            spec.has_rot = read_u32(input) != 0u;
            spec.rot_y = read_f32(input);
            spec.align_x = read_i32(input);
            spec.align_y = read_i32(input);
            spec.tex_width = read_exact<std::int16_t>(input);
            spec.tex_height = read_exact<std::int16_t>(input);
            spec.div_mode = static_cast<emucorev::paf::PlaneDivMode>(read_u32(input));
            const auto use_presets = read_exact<std::uint8_t>(input) != 0u;
            const auto preset_h = static_cast<emucorev::paf::PlaneDivRatioPreset>(read_exact<std::uint8_t>(input));
            const auto preset_v = static_cast<emucorev::paf::PlaneDivRatioPreset>(read_exact<std::uint8_t>(input));
            const float rh0 = read_f32(input);
            const float rh1 = read_f32(input);
            const float rv0 = read_f32(input);
            const float rv1 = read_f32(input);
            spec.div_ratio_stretch = {read_f32(input), read_f32(input)};
            spec.div_ratio_h = use_presets
                ? emucorev::paf::resolve_plane_div_ratio(preset_h, {rh0, rh1})
                : std::array<float, 2>{rh0, rh1};
            spec.div_ratio_v = use_presets
                ? emucorev::paf::resolve_plane_div_ratio(preset_v, {rv0, rv1})
                : std::array<float, 2>{rv0, rv1};
            spec.margin_left = read_exact<std::uint16_t>(input);
            spec.margin_right = read_exact<std::uint16_t>(input);
            spec.margin_top = read_exact<std::uint16_t>(input);
            spec.margin_bottom = read_exact<std::uint16_t>(input);
            spec.div_border_scale = read_f32(input);
            spec.div_stretch_offset = read_f32(input);

            const std::uint32_t exp_cx = read_u32(input);
            const std::uint32_t exp_cy = read_u32(input);
            const std::uint32_t exp_cz = read_u32(input);
            const std::uint32_t exp_w = read_u32(input);
            const std::uint32_t exp_h = read_u32(input);
            const std::uint32_t exp_vcount = read_u32(input);

            const auto mesh = emucorev::paf::build_plane_div_mesh(spec);
            expect_bits(i, "center_x", mesh.layout.center_x, exp_cx);
            expect_bits(i, "center_y", mesh.layout.center_y, exp_cy);
            expect_bits(i, "center_z", mesh.layout.center_z, exp_cz);
            expect_bits(i, "width", mesh.layout.width, exp_w);
            expect_bits(i, "height", mesh.layout.height, exp_h);
            if (mesh.vertices.size() != exp_vcount) {
                throw std::runtime_error("Unexpected vertex count");
            }
            for (std::uint32_t vi = 0; vi < exp_vcount; ++vi) {
                const auto ex = read_u32(input);
                const auto ey = read_u32(input);
                const auto eu = read_u32(input);
                const auto ev = read_u32(input);
                expect_bits(i, "vx", mesh.vertices[vi].x, ex);
                expect_bits(i, "vy", mesh.vertices[vi].y, ey);
                expect_bits(i, "vu", mesh.vertices[vi].u, eu);
                expect_bits(i, "vv", mesh.vertices[vi].v, ev);
            }
        }
        if (input.peek() != std::char_traits<std::ifstream::char_type>::eof()) {
            throw std::runtime_error("Trailing corpus data");
        }
        check_livearea_gate_quads();
        std::cout << "PASS " << count << " bit-exact original PlaneObj div cases\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
