#include <emucorev/paf/page_turn_material.h>
#include <algorithm>
#include <cstring>

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

PageTurnMaterialBlock page_turn_top_vertex_material(PagePoint size, PagePoint point,
    const PageTurnShapeStyle& style, const PageTurnPose& pose,
    const PageTurnMatrix& projection, const PageTurnMatrix& view,
    const PageTurnPointLight& light) {
    PageTurnMaterialBlock block;
    block.size = 112;
    const auto transforms = page_turn_transforms(size, pose, projection, view);
    const auto shape = page_turn_fold_shape(point, page_turn_normalized_aspect(size));
    const auto material = page_turn_shape_material(point, shape, style);
    const auto lights = page_turn_light_parameters(light);
    auto put = [&](PageTurnUniform uniform, const float* source, std::uint32_t count) {
        const auto range = page_turn_top_uniform(uniform);
        const auto copied = std::min(range.count, count);
        std::memcpy(block.values.data() + range.offset, source, copied * sizeof(float));
    };
    put(PageTurnUniform::mvp, transforms.model_view_projection.data(), 16);
    put(PageTurnUniform::model_matrix, transforms.model.data(), 16);
    put(PageTurnUniform::deepest, material.deepest.data(), 4);
    put(PageTurnUniform::grep, &material.grep.x, 2);
    put(PageTurnUniform::normal_scale_z, &material.normal_scale_z, 1);
    put(PageTurnUniform::params, material.params.data(), 4);
    put(PageTurnUniform::fold_plane, material.fold_plane.data(), 4);
    put(PageTurnUniform::aspect, material.aspect.data(), 4);
    put(PageTurnUniform::point_light_pack2, lights.color.data(), 16);
    put(PageTurnUniform::point_light_pack1, lights.position.data(), 16);
    return block;
}

PageTurnMaterialBlock page_turn_bottom_vertex_material(PagePoint size, PagePoint point,
    const PageTurnShapeStyle& style, const PageTurnPose& pose,
    const PageTurnMatrix& projection, const PageTurnMatrix& view) {
    PageTurnMaterialBlock block;
    block.size = 48;
    const auto transforms = page_turn_transforms(size, pose, projection, view);
    const auto shape = page_turn_fold_shape(point, page_turn_normalized_aspect(size));
    const auto material = page_turn_shape_material(point, shape, style);
    auto put = [&](PageTurnUniform uniform, const float* source, std::uint32_t count) {
        const auto range = page_turn_bottom_uniform(uniform);
        std::memcpy(block.values.data() + range.offset, source,
            std::min(range.count, count) * sizeof(float));
    };
    put(PageTurnUniform::mvp, transforms.model_view_projection.data(), 16);
    put(PageTurnUniform::deepest, material.deepest.data(), 4);
    put(PageTurnUniform::grep, &material.grep.x, 2);
    put(PageTurnUniform::params, material.params.data(), 4);
    put(PageTurnUniform::fold_plane, material.fold_plane.data(), 4);
    put(PageTurnUniform::aspect, material.aspect.data(), 4);
    return block;
}

PageTurnMaterialBlock page_turn_empty_back_vertex_material(PagePoint size, PagePoint point,
    const PageTurnShapeStyle& style, const PageTurnPose& pose,
    const PageTurnMatrix& projection, const PageTurnMatrix& view) {
    PageTurnMaterialBlock block;
    block.size = 36;
    const auto transforms = page_turn_transforms(size, pose, projection, view);
    const auto shape = page_turn_fold_shape(point, page_turn_normalized_aspect(size));
    const auto material = page_turn_shape_material(point, shape, style);
    auto put = [&](PageTurnUniform uniform, const float* source, std::uint32_t count) {
        const auto range = page_turn_empty_back_uniform(uniform);
        std::memcpy(block.values.data() + range.offset, source,
            std::min(range.count, count) * sizeof(float));
    };
    put(PageTurnUniform::mvp, transforms.model_view_projection.data(), 16);
    put(PageTurnUniform::fold_plane, material.fold_plane.data(), 4);
    put(PageTurnUniform::aspect, material.aspect.data(), 4);
    return block;
}

std::uint32_t page_turn_draw_passes(PagePoint size, PagePoint point, bool folded,
    const PageTurnShapeStyle& style, PageTurnPass* output, std::uint32_t capacity) noexcept {
    const std::uint32_t required = folded ? 3 : 2;
    if (!output || capacity < required) return 0;
    output[0] = {PageTurnPassRole::top, page_turn_top_vertex_material(size, point, style)};
    output[1] = {PageTurnPassRole::bottom, page_turn_bottom_vertex_material(size, point, style)};
    if (folded)
        output[2] = {PageTurnPassRole::empty_back,
            page_turn_empty_back_vertex_material(size, point, style)};
    return required;
}
}
