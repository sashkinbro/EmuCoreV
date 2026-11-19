#include <emucorev/paf/page_turn_material.h>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace emucorev::paf;

namespace {
void expect(const PageTurnMaterialBlock &block, PageTurnUniform uniform,
    const std::array<float, 4> &expected, unsigned count, const char *label,
    PageTurnUniformRange (*lookup)(PageTurnUniform) = page_turn_top_uniform) {
    const auto range = lookup(uniform);
    if (range.count < count) throw std::runtime_error(std::string("range too small for ") + label);
    for (unsigned i = 0; i < count; ++i) {
        const auto bits = std::bit_cast<std::uint32_t>(block.values[range.offset + i]);
        if (bits != std::bit_cast<std::uint32_t>(expected[i]))
            throw std::runtime_error(std::string("top material mismatch: ") + label);
    }
}
}

int main(int argc, char **argv) {
    try {
        if (argc != 2) throw std::runtime_error("Usage: paf_page_top_material_test ORIGINAL_CORPUS");
        std::ifstream file(argv[1], std::ios::binary);
        std::array<char, 8> magic{};
        file.read(magic.data(), magic.size());
        if (magic != std::array<char, 8>{'P', 'A', 'F', 'T', 'P', 'M', '0', '1'})
            throw std::runtime_error("Invalid top-material corpus");
        std::uint32_t count = 0;
        file.read(reinterpret_cast<char *>(&count), sizeof(count));
        if (!count || count > 64) throw std::runtime_error("Invalid corpus count");
        auto word = [&]() {
            std::uint32_t value = 0;
            file.read(reinterpret_cast<char *>(&value), sizeof(value));
            if (!file) throw std::runtime_error("Truncated corpus");
            return value;
        };
        auto real = [&]() { return std::bit_cast<float>(word()); };
        for (std::uint32_t record = 0; record < count; ++record) {
            const PagePoint size{real(), real()};
            const PagePoint point{real(), real()};
            const PageTurnShapeStyle style{real(), real(), real(), real()};
            std::array<float, 32> matrices{};
            std::array<float, 21> shape{};
            std::array<float, 32> lights{};
            for (auto &value : matrices) value = real();
            for (auto &value : shape) value = real();
            for (auto &value : lights) value = real();
            PageTurnPointLight light;
            light.angles = {1.3f, .5f};
            light.distance = 2200.f;
            light.attenuation = {0.f, 0.f, 0.f};
            const auto block = page_turn_top_vertex_material(size, point, style, {}, page_turn_identity, page_turn_identity, light);
            auto slot = [](const auto &array, unsigned offset) {
                std::array<float, 4> values{};
                for (unsigned i = 0; i < values.size(); ++i)
                    if (offset + i < array.size()) values[i] = array[offset + i];
                return values;
            };
            expect(block, PageTurnUniform::mvp, slot(matrices, 16), 4, "MVP");
            expect(block, PageTurnUniform::model_matrix, slot(matrices, 0), 4, "ModelMatrix");
            expect(block, PageTurnUniform::deepest, slot(shape, 0), 4, "Deepest");
            expect(block, PageTurnUniform::grep, slot(shape, 4), 2, "Grep");
            expect(block, PageTurnUniform::normal_scale_z, slot(shape, 18), 1, "NormalScaleZ");
            expect(block, PageTurnUniform::params, slot(shape, 6), 4, "Params");
            expect(block, PageTurnUniform::fold_plane, slot(shape, 10), 4, "FoldPlane");
            expect(block, PageTurnUniform::aspect, slot(shape, 14), 4, "Aspect");
            expect(block, PageTurnUniform::point_light_pack2, slot(lights, 16), 4, "PointLightPack2");
            expect(block, PageTurnUniform::point_light_pack1, slot(lights, 0), 4, "PointLightPack1");
            const auto bottom = page_turn_bottom_vertex_material(size, point, style);
            expect(bottom, PageTurnUniform::mvp, slot(matrices, 16), 4, "bottom MVP", page_turn_bottom_uniform);
            expect(bottom, PageTurnUniform::deepest, slot(shape, 0), 4, "bottom Deepest", page_turn_bottom_uniform);
            expect(bottom, PageTurnUniform::grep, slot(shape, 4), 2, "bottom Grep", page_turn_bottom_uniform);
            expect(bottom, PageTurnUniform::params, slot(shape, 6), 4, "bottom Params", page_turn_bottom_uniform);
            expect(bottom, PageTurnUniform::fold_plane, slot(shape, 10), 4, "bottom FoldPlane", page_turn_bottom_uniform);
            expect(bottom, PageTurnUniform::aspect, slot(shape, 14), 4, "bottom Aspect", page_turn_bottom_uniform);
            const auto empty = page_turn_empty_back_vertex_material(size, point, style);
            expect(empty, PageTurnUniform::mvp, slot(matrices, 16), 4, "empty MVP", page_turn_empty_back_uniform);
            expect(empty, PageTurnUniform::fold_plane, slot(shape, 10), 4, "empty FoldPlane", page_turn_empty_back_uniform);
            expect(empty, PageTurnUniform::aspect, slot(shape, 14), 4, "empty Aspect", page_turn_empty_back_uniform);
            std::array<PageTurnPass, 3> passes{};
            if (page_turn_draw_passes(size, point, false, style, passes.data(), 2) != 2
                || passes[0].role != PageTurnPassRole::top
                || passes[1].role != PageTurnPassRole::bottom)
                throw std::runtime_error("flat draw list is not top+bottom");
            const auto default_top = page_turn_top_vertex_material(size, point, style);
            if (std::memcmp(passes[0].material.values.data(), default_top.values.data(),
                    sizeof(default_top.values)) != 0)
                throw std::runtime_error("flat draw list top block differs from the assembled block");
            if (page_turn_draw_passes(size, point, true, style, passes.data(), 2) != 0)
                throw std::runtime_error("folded draw list accepted too small a capacity");
            if (page_turn_draw_passes(size, point, true, style, passes.data(), 3) != 3
                || passes[2].role != PageTurnPassRole::empty_back)
                throw std::runtime_error("folded draw list is not top+bottom+empty_back");
        }
        std::cout << "PASS top vertex material: " << count
            << " records match the original MVP/Model/shape/light uniforms; flat/folded pass lists verified\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
