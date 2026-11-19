#include <emucorev/paf/page_turn_material.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    using namespace emucorev::paf;
    try {
        if (argc != 2) throw std::runtime_error("Usage: paf_page_material_pack_test ORIGINAL_CORPUS");
        std::ifstream file(argv[1], std::ios::binary);
        std::array<char, 8> magic{};
        file.read(magic.data(), magic.size());
        if (magic != std::array<char, 8>{'P','A','F','M','P','K','0','1'})
            throw std::runtime_error("Invalid PageTurn MaterialPack corpus");
        auto word = [&]() {
            std::uint32_t value = 0;
            file.read(reinterpret_cast<char*>(&value), sizeof(value));
            if (!file) throw std::runtime_error("Truncated PageTurn MaterialPack corpus");
            return value;
        };
        const auto count = word();
        if (!count || count > 256) throw std::runtime_error("Invalid MaterialPack case count");
        for (std::uint32_t i = 0; i < count; ++i) {
            std::array<float, 21> values{};
            for (auto& value : values) value = std::bit_cast<float>(word());
            const PagePoint size{values[0], values[1]}, point{values[2], values[3]};
            const PageTurnShapeStyle shape_style{values[4], values[5], values[6], values[7]};
            PageTurnMaterialStyle material_style;
            std::copy_n(values.begin() + 8, 4, material_style.color.begin());
            material_style.diffuse_coefficient = values[12];
            material_style.specular_coefficient = values[13];
            material_style.specular_power = values[14];
            material_style.fresnel_front = values[15];
            material_style.fresnel_control = values[16];
            material_style.fresnel_coefficient = values[17];
            material_style.refraction_coefficient = values[18];
            material_style.refraction_amount = values[19];
            material_style.second_reflection = values[20];

            const auto aspect = page_turn_normalized_aspect(size);
            const auto shape = page_turn_fold_shape(point, aspect);
            const auto adapted = page_turn_shape_material(point, shape, shape_style);
            const auto actual = page_turn_material_pack(adapted.fresnel_multiplier, material_style);
            for (std::uint32_t component = 0; component < actual.size(); ++component) {
                const auto expected = word();
                if (expected != std::bit_cast<std::uint32_t>(actual[component])) {
                    std::cerr << "case=" << i << " component=" << component << std::hex
                        << " expected=" << expected << " actual=" << std::bit_cast<std::uint32_t>(actual[component]) << '\n';
                    throw std::runtime_error("Original PageTurn MaterialPack differs");
                }
            }
        }
        if (file.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("Trailing PageTurn MaterialPack corpus data");
        std::cout << "PASS " << count << " bit-exact original PageTurn MaterialPack records / "
            << count * 16 << " words\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
