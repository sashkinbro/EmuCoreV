#include <emucorev/paf/page_turn_material.h>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace emucorev::paf;

namespace {
void expect(const PageTurnMaterialBlock &block, PageTurnUniform uniform,
    const std::array<float, 4> &expected, unsigned count, const char *label) {
    const auto range = page_turn_top_uniform(uniform);
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
        }
        std::cout << "PASS top vertex material: " << count
            << " records match the original MVP/Model/shape/light uniforms\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
