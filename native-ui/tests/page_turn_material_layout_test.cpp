#include <emucorev/paf/page_turn_material.h>
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>

using namespace emucorev::paf;

namespace {
struct BlockExpectation {
    const char *name;
    std::uint32_t size;
    const PageTurnUniform *declared;
    std::uint32_t declared_count;
    PageTurnUniformRange (*lookup)(PageTurnUniform);
};

void check(const BlockExpectation &block) {
    std::array<bool, 128> used{};
    std::uint32_t declared = 0;
    for (std::uint32_t i = 0; i < block.declared_count; ++i) {
        const auto range = block.lookup(block.declared[i]);
        if (range.count == 0) throw std::runtime_error(std::string(block.name) + " misses a declared uniform");
        if (range.offset + range.count > block.size)
            throw std::runtime_error(std::string(block.name) + " uniform exceeds its block");
        for (std::uint32_t j = 0; j < range.count; ++j) {
            if (used[range.offset + j])
                throw std::runtime_error(std::string(block.name) + " uniforms overlap");
            used[range.offset + j] = true;
        }
        ++declared;
    }
    std::uint32_t end = 0;
    for (std::uint32_t i = 0; i < block.size; ++i)
        if (used[i]) end = i + 1;
    if (end != block.size)
        throw std::runtime_error(std::string(block.name) + " counted prefix does not end at the block size");
    std::cout << "PASS " << block.name << ": " << declared << " uniforms cover "
        << block.size << " floats without overlap\n";
}
}

int main() {
    try {
        const std::array top{PageTurnUniform::mvp, PageTurnUniform::model_matrix,
            PageTurnUniform::material_pack, PageTurnUniform::uv_transform,
            PageTurnUniform::squeeze_z, PageTurnUniform::deepest, PageTurnUniform::grep,
            PageTurnUniform::normal_scale_z, PageTurnUniform::params,
            PageTurnUniform::fold_plane, PageTurnUniform::aspect,
            PageTurnUniform::point_light_pack2, PageTurnUniform::point_light_pack1,
            PageTurnUniform::eye};
        check({"top vertex", 112, top.data(), static_cast<std::uint32_t>(top.size()), page_turn_top_uniform});
        const std::array bottom{PageTurnUniform::mvp, PageTurnUniform::shadow,
            PageTurnUniform::uv_transform, PageTurnUniform::squeeze_z,
            PageTurnUniform::exp_minus_range, PageTurnUniform::deepest,
            PageTurnUniform::grep, PageTurnUniform::params,
            PageTurnUniform::fold_plane, PageTurnUniform::aspect};
        check({"bottom vertex", 48, bottom.data(), static_cast<std::uint32_t>(bottom.size()), page_turn_bottom_uniform});
        const std::array empty{PageTurnUniform::mvp, PageTurnUniform::shadow,
            PageTurnUniform::uv_transform, PageTurnUniform::squeeze_z,
            PageTurnUniform::fold_plane, PageTurnUniform::aspect};
        check({"empty back vertex", 36, empty.data(), static_cast<std::uint32_t>(empty.size()), page_turn_empty_back_uniform});
        if (page_turn_top_uniform(PageTurnUniform::shadow).count != 0
            || page_turn_bottom_uniform(PageTurnUniform::model_matrix).count != 0
            || page_turn_empty_back_uniform(PageTurnUniform::grep).count != 0)
            throw std::runtime_error("Undeclared uniform was not reported empty");
        std::cout << "PASS undeclared uniforms report empty ranges\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
