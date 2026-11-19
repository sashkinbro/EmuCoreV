#include <emucorev/paf/page_turn_material.h>

namespace emucorev::paf {
namespace {
constexpr PageTurnUniformRange none{};
}

PageTurnUniformRange page_turn_top_uniform(PageTurnUniform uniform) noexcept {
    switch (uniform) {
    case PageTurnUniform::mvp: return {0, 16};
    case PageTurnUniform::model_matrix: return {16, 16};
    case PageTurnUniform::material_pack: return {32, 16};
    case PageTurnUniform::uv_transform: return {48, 4};
    case PageTurnUniform::squeeze_z: return {52, 4};
    case PageTurnUniform::deepest: return {56, 4};
    case PageTurnUniform::grep: return {60, 2};
    case PageTurnUniform::normal_scale_z: return {62, 1};
    case PageTurnUniform::params: return {64, 4};
    case PageTurnUniform::fold_plane: return {68, 4};
    case PageTurnUniform::aspect: return {72, 4};
    case PageTurnUniform::point_light_pack2: return {76, 16};
    case PageTurnUniform::point_light_pack1: return {92, 16};
    case PageTurnUniform::eye: return {108, 4};
    default: return none;
    }
}

PageTurnUniformRange page_turn_bottom_uniform(PageTurnUniform uniform) noexcept {
    switch (uniform) {
    case PageTurnUniform::mvp: return {0, 16};
    case PageTurnUniform::shadow: return {16, 4};
    case PageTurnUniform::uv_transform: return {20, 4};
    case PageTurnUniform::squeeze_z: return {24, 4};
    case PageTurnUniform::exp_minus_range: return {28, 2};
    case PageTurnUniform::deepest: return {30, 4};
    case PageTurnUniform::grep: return {34, 2};
    case PageTurnUniform::params: return {36, 4};
    case PageTurnUniform::fold_plane: return {40, 4};
    case PageTurnUniform::aspect: return {44, 4};
    default: return none;
    }
}

PageTurnUniformRange page_turn_empty_back_uniform(PageTurnUniform uniform) noexcept {
    switch (uniform) {
    case PageTurnUniform::mvp: return {0, 16};
    case PageTurnUniform::shadow: return {16, 4};
    case PageTurnUniform::uv_transform: return {20, 4};
    case PageTurnUniform::squeeze_z: return {24, 4};
    case PageTurnUniform::fold_plane: return {28, 4};
    case PageTurnUniform::aspect: return {32, 4};
    default: return none;
    }
}
}
