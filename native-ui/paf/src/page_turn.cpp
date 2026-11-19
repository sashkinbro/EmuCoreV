#include <emucorev/paf/page_turn.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#if defined(__aarch64__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

namespace emucorev::paf {
namespace {
void check_size(PagePoint size) {
    if (!std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0)
        throw std::invalid_argument("Invalid page-turn size");
}
PagePoint inverse_size(PagePoint size) {
    check_size(size);
    // Original vector reciprocal estimate with two AArch32, non-fused steps.
    size.x = std::clamp(size.x, 1.f, 3.4028234663852886e18f);
    size.y = std::clamp(size.y, 1.f, 3.4028234663852886e18f);
#if defined(__aarch64__) || defined(__ARM_NEON)
    const float values[]{size.x, size.y};
    const auto value = vld1_f32(values);
    auto inverse = vrecpe_f32(value);
    inverse = vmul_f32(inverse, vsub_f32(vdup_n_f32(2), vmul_f32(inverse, value)));
    inverse = vmul_f32(inverse, vsub_f32(vdup_n_f32(2), vmul_f32(inverse, value)));
    return {vget_lane_f32(inverse, 0), vget_lane_f32(inverse, 1)};
#else
    return {1.f / size.x, 1.f / size.y};
#endif
}
float reciprocal(float value) {
#if defined(__aarch64__) || defined(__ARM_NEON)
    const auto source = vdup_n_f32(value);
    auto result = vrecpe_f32(source);
    result = vmul_f32(result, vsub_f32(vdup_n_f32(2), vmul_f32(result, source)));
    result = vmul_f32(result, vsub_f32(vdup_n_f32(2), vmul_f32(result, source)));
    return vget_lane_f32(result, 0);
#else
    return 1.f / value;
#endif
}
float root(float value) {
#if defined(__aarch64__) || defined(__ARM_NEON)
    const auto source = vdup_n_f32(value);
    auto inverse = vrsqrte_f32(source);
    for (unsigned i = 0; i < 2; ++i) {
        const auto square = vmul_f32(inverse, inverse);
        const auto step = vmul_f32(vsub_f32(vdup_n_f32(3), vmul_f32(square, source)), vdup_n_f32(.5f));
        inverse = vmul_f32(inverse, step);
    }
    return vget_lane_f32(vmul_f32(source, inverse), 0);
#else
    return std::sqrt(value);
#endif
}
float light_reduce(float angle) {
    constexpr float period = 6.2831855f;
    if (angle > period) {
        float turns = angle / period;
        if ((std::bit_cast<std::uint32_t>(turns) << 1) < 0x98000000u)
            turns = static_cast<float>(static_cast<int>(turns));
        angle = angle - turns * period;
    }
    return angle;
}
float light_sine_polynomial(float angle) {
    const float square = angle * angle;
    return angle * (((square * -.00019418877f + .008331325f) * square - .16666639f) * square + 1.f);
}
float light_cosine_polynomial(float square) {
    return ((square * -.0013719854f + .04166315f) * square - .4999998f) * square + 1.f;
}
float light_sine(float input) {
    float angle = light_reduce(std::abs(input));
    auto sign = std::bit_cast<std::uint32_t>(input) & 0x80000000u;
    if (angle > 3.1415927f) { angle -= 3.1415927f; sign ^= 0x80000000u; }
    if (angle > 1.5707964f) angle = 3.1415927f - angle;
    float value;
    if (angle > .91734505f) {
        const float complement = 1.5707964f - angle;
        value = light_cosine_polynomial(complement * complement);
    } else value = light_sine_polynomial(angle);
    return value == 0 ? 0.f : std::bit_cast<float>(std::bit_cast<std::uint32_t>(std::abs(value)) | sign);
}
float light_cosine(float input) {
    float angle = light_reduce(std::abs(input));
    std::uint32_t sign = 0;
    if (angle > 3.1415927f) { angle -= 3.1415927f; sign = 0x80000000u; }
    if (angle > 1.5707964f) { angle = 3.1415927f - angle; sign ^= 0x80000000u; }
    const float value = angle > .65345126f ? light_sine_polynomial(1.5707964f - angle)
        : light_cosine_polynomial(angle * angle);
    return value == 0 ? 0.f : std::bit_cast<float>(std::bit_cast<std::uint32_t>(std::abs(value)) | sign);
}
PagePoint add(PagePoint a, PagePoint b) { return {a.x + b.x, a.y + b.y}; }
PagePoint subtract(PagePoint a, PagePoint b) { return {a.x - b.x, a.y - b.y}; }
PagePoint multiply(PagePoint a, float b) { return {a.x * b, a.y * b}; }
float dot(PagePoint a, PagePoint b) { return a.x * b.x + a.y * b.y; }
PageFoldProjection project(PagePoint center, PagePoint normal, PagePoint point) {
    const PagePoint tangent{-normal.y, normal.x};
    const auto delta = subtract(point, center);
    auto along = multiply(tangent, dot(tangent, delta));
    along = multiply(along, reciprocal(dot(tangent, tangent)));
    const auto projected = add(center, along);
    const float distance = dot(normal, delta);
    return {projected, distance < 0 ? add(projected, subtract(projected, point)) : point, distance};
}
unsigned clip_segment(PagePoint& start, PagePoint& end, PagePoint origin, PagePoint normal) {
    const float a = dot(normal, subtract(start, origin));
    const float b = dot(normal, subtract(end, origin));
    if (a >= 0 && b >= 0) return 0;
    if (a < 0 && b < 0) return 3;
    const auto projected = subtract(start, multiply(normal, a));
    const float numerator = dot(normal, subtract(projected, start));
    const float ratio = numerator * reciprocal(b - a);
    const auto intersection = add(start, multiply(subtract(end, start), ratio));
    if (a < 0) { start = intersection; return 1; }
    end = intersection;
    return 2;
}
bool clip(PagePoint& start, PagePoint& end, PagePoint origin, PagePoint normal) {
    return clip_segment(start, end, origin, normal) == 3;
}
struct Polygon {
    std::array<PagePoint, 12> points;
    unsigned count = 0;
};
Polygon clip_polygon(const Polygon& input, PagePoint origin, PagePoint normal) {
    Polygon output;
    auto append = [&](PagePoint point) {
        if (output.count == output.points.size()) throw std::runtime_error("Page-turn polygon exceeds bounds");
        output.points[output.count++] = point;
    };
    for (unsigned i = 0; i < input.count; ++i) {
        auto start = input.points[i], end = input.points[(i + 1) % input.count];
        const auto result = clip_segment(start, end, origin, normal);
        if (result == 0 || result == 2) append(end);
        else if (result == 1) { append(start); append(end); }
    }
    return output;
}
}
PageTurnLayout page_turn_layout(PagePoint size, float fold_fraction, float corner_aspect) {
    check_size(size);
    if (!std::isfinite(fold_fraction) || !std::isfinite(corner_aspect))
        throw std::invalid_argument("Invalid page-turn fold settings");
    const float fold = size.y * fold_fraction;
    return {{size.x * .5f - fold * corner_aspect, size.y * .5f - fold},
        {fold + 50.f, fold + 50.f}, fold * .5f - 10.f};
}
bool page_turn_hit_test(PagePoint size, const PageTurnLayout& layout, PagePoint point) {
    const auto inverse = inverse_size(size);
    const float dx = point.x - size.x * .5f, dy = point.y - size.y * .5f;
    if (dx + dy > -layout.corner_exclusion) return false;
    const float x = (point.x + size.x * .5f) * inverse.x;
    const float y = (point.y + size.y * .5f) * inverse.y;
    return x <= 1 && x >= 1.f - layout.hit_size.x * inverse.x
        && y <= 1 && y >= 1.f - layout.hit_size.y * inverse.y;
}
PagePoint page_turn_drag_target(PagePoint pointer, PagePoint initial, PagePoint offset) {
    return {(pointer.x + offset.x) + (pointer.x - initial.x) * .7f,
        (pointer.y + offset.y) + (pointer.y - initial.y) * .7f};
}
PageTurnDrawControl page_turn_draw_control(PagePoint size, PagePoint position,
    PagePoint offset, std::array<float, 3> opacity) {
    const auto inverse = inverse_size(size);
    const float alpha = (opacity[0] * opacity[1]) * opacity[2];
    return {{(position.x + size.x * .5f) * inverse.x,
             (position.y + size.y * .5f) * inverse.y},
        {offset.x * inverse.x, offset.y * inverse.y},
        alpha < .999f ? 0x00ffffffu : 0xffffffffu};
}
PageTurnRelease page_turn_release(PagePoint displacement, float threshold, bool allow_outward, bool gamepad) {
    if (allow_outward && displacement.x + displacement.y > (gamepad ? 80.f : 50.f))
        return PageTurnRelease::outward;
    const float squared = displacement.x * displacement.x + displacement.y * displacement.y;
    return squared > threshold * threshold ? PageTurnRelease::beyond_threshold : PageTurnRelease::within_threshold;
}
PageFoldShape page_turn_fold_shape(PagePoint deepest, PagePoint aspect) {
    check_size(aspect);
    if (!std::isfinite(deepest.x) || !std::isfinite(deepest.y))
        throw std::invalid_argument("Invalid page-turn fold point");
    constexpr float upper = std::bit_cast<float>(0x3f7fff58u);
    constexpr float lower = std::bit_cast<float>(0x3fffffacu);
    const PagePoint bounded{std::max(-aspect.x * lower, std::min(aspect.x * upper, deepest.x)),
        std::max(-aspect.y * lower, std::min(aspect.y * upper, deepest.y))};
    const auto delta = subtract(bounded, aspect);
    const float squared = dot(delta, delta);
    if (!std::isfinite(squared) || squared < 1.1754943508222875e-38f)
        throw std::invalid_argument("Degenerate page-turn fold extent");
    PageFoldShape result{};
    result.center = multiply(add(bounded, aspect), .5f);
    result.normal = multiply(delta, reciprocal(root(squared)));
    result.corners = {{{0, 0}, {aspect.x, 0}, aspect, {0, aspect.y}}};
    for (unsigned i = 0; i < 4; ++i)
        result.projected[i] = project(result.center, result.normal, result.corners[i]);
    result.far_corner = aspect;
    result.maximum = result.minimum = result.projected[0];
    for (const auto& corner : result.projected) {
        if (corner.distance < 0 && corner.distance < result.minimum.distance) result.minimum = corner;
        if (corner.distance > 0 && corner.distance > result.maximum.distance) result.maximum = corner;
    }
    const PagePoint tangent{-result.normal.y, result.normal.x};
    result.line_start = add(result.center, multiply(tangent, 1000));
    result.line_end = subtract(result.center, multiply(tangent, 1000));
    // Apply all four half-planes even after a rejection, as the original does.
    result.outside = clip(result.line_start, result.line_end, {0, 0}, {1, 0});
    result.outside = clip(result.line_start, result.line_end, {0, 0}, {0, 1}) || result.outside;
    result.outside = clip(result.line_start, result.line_end, aspect, {-1, -0.f}) || result.outside;
    result.outside = clip(result.line_start, result.line_end, aspect, {-0.f, -1}) || result.outside;
    result.aspect = aspect;
    return result;
}
void page_turn_mesh(const PageFoldShape& shape, PageTurnMesh& output) {
    output.vertex_count = output.front_count = output.folded_count = 0;
    Polygon page;
    page.count = 4;
    std::copy(shape.corners.begin(), shape.corners.end(), page.points.begin());
    auto append_band = [&](PagePoint start, PagePoint start_normal, PagePoint end,
                           PagePoint end_normal, bool folded) {
        const auto first = clip_polygon(page, start, start_normal);
        const auto polygon = clip_polygon(first, end, end_normal);
        if (polygon.count < 3) return;
        auto& count = folded ? output.folded_count : output.front_count;
        auto& fans = folded ? output.folded : output.front;
        if (count == fans.size() || output.vertex_count + polygon.count > output.vertices.size())
            throw std::runtime_error("Page-turn mesh exceeds native storage");
        fans[count++] = {output.vertex_count, polygon.count};
        for (unsigned i = 0; i < polygon.count; ++i)
            output.vertices[output.vertex_count++] = polygon.points[i];
    };
    const auto normal = shape.normal;
    const PagePoint reverse{-normal.x, -normal.y};
    auto start = shape.center;
    auto end = add(shape.center, multiply(normal, .01f));
    float step = 0;
    while (shape.maximum.distance > dot(normal, subtract(start, shape.center))) {
        append_band(start, normal, end, reverse, false);
        step += 1;
        if (step > 16384) throw std::runtime_error("Page-turn mesh does not converge");
        start = end;
        end = add(end, multiply(normal, step * .01f));
    }
    const float extent = std::abs(shape.minimum.distance);
    auto parameter = [&](unsigned index) {
        const float rate = static_cast<float>(index) * .0625f;
        return ((rate * rate) * (rate * -2.f + 3.f)) * extent;
    };
    for (unsigned i = 0; i < 16; ++i) {
        const auto a = subtract(shape.center, multiply(normal, parameter(i)));
        const auto b = subtract(shape.center, multiply(normal, parameter(i + 1)));
        append_band(a, reverse, b, normal, true);
    }
}
PageTurnShapeMaterial page_turn_shape_material(PagePoint point, const PageFoldShape& shape,
    const PageTurnShapeStyle& style) {
    check_size(shape.aspect);
    if (!std::isfinite(point.x) || !std::isfinite(point.y)
        || !std::isfinite(style.top_scale) || !std::isfinite(style.top_curl)
        || !std::isfinite(style.bottom_scale) || !std::isfinite(style.normal_scale_z))
        throw std::invalid_argument("Invalid page material input");
    // Original renderer clamps only the upper endpoint before the square.
    const float progress = std::min((point.x / shape.aspect.x + point.y) * .5f, 1.f);
    const float square = progress * progress;
    const float fresnel = 1.f - (1.f - square) * .35f;
    const float fade = std::min((1.f - progress) / .050000012f, 1.f);
    return {{shape.minimum.distance, shape.maximum.distance, shape.far_corner.x, shape.far_corner.y},
        point, {style.bottom_scale, fade * style.top_curl, fade * style.top_scale, 0},
        {shape.center.x, shape.center.y, shape.normal.x, shape.normal.y},
        {shape.aspect.x, shape.aspect.y, reciprocal(shape.aspect.x), reciprocal(shape.aspect.y)},
        style.normal_scale_z, fresnel};
}
PagePoint page_turn_normalized_aspect(PagePoint size) {
    check_size(size);
    const float inverse_height = reciprocal(size.y);
    return {size.x * inverse_height, size.y * inverse_height};
}
PageTurnTransforms page_turn_transforms(PagePoint size, const PageTurnPose& pose,
    const PageTurnMatrix& projection, const PageTurnMatrix& view) {
    check_size(size);
    auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    if (!finite(pose.scale) || !finite(pose.position) || !finite(pose.rotation)
        || !finite(projection) || !finite(view)) throw std::invalid_argument("Invalid page transform input");
    const auto [x, y, z, w] = pose.rotation;
    const float squared = (x * x + y * y) + (z * z + w * w);
    PageTurnMatrix rotation{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    if (squared != 0) {
        const float inverse = 2.f / squared;
        const float x2 = x * inverse, y2 = y * inverse, z2 = z * inverse;
        const float wy = w * y2, base = 1.f - x * x2;
        rotation = {(1.f - y * y2) - z * z2, w * z2 + x * y2, z * x2 - wy, 0,
            x * y2 - w * z2, base - z * z2, w * x2 + y * z2, 0,
            wy + z * x2, y * z2 - w * x2, base - y * y2, 0, 0,0,0,1};
    }
    const float half_width = (size.x * pose.scale[0]) * .5f;
    const float half_height = (size.y * pose.scale[1]) * .5f;
    auto model = rotation;
    for (unsigned row = 0; row < 3; ++row) {
        float origin = rotation[row] * -half_width;
        origin += rotation[row + 4] * -half_height;
        origin += rotation[row + 8] * 0.f;
        origin += pose.position[row];
        model[row + 12] = origin;
    }
    for (unsigned column = 0; column < 3; ++column) {
        const float scale = size.y * pose.scale[column];
        for (unsigned row = 0; row < 4; ++row) model[column * 4 + row] = rotation[column * 4 + row] * scale;
    }
    auto multiply = [](const PageTurnMatrix& a, const PageTurnMatrix& b) {
        PageTurnMatrix result{};
        for (unsigned column = 0; column < 4; ++column) for (unsigned row = 0; row < 4; ++row) {
            float value = a[row] * b[column * 4];
            value += a[row + 4] * b[column * 4 + 1];
            value += a[row + 8] * b[column * 4 + 2];
            value += a[row + 12] * b[column * 4 + 3];
            result[column * 4 + row] = value;
        }
        return result;
    };
    return {model, multiply(multiply(projection, view), model)};
}
PageTurnLightParameters page_turn_light_parameters(const PageTurnPointLight& light) {
    auto finite = [](const auto& values) {
        return std::all_of(values.begin(), values.end(), [](float value) { return std::isfinite(value); });
    };
    if (!std::isfinite(light.angles.x) || !std::isfinite(light.angles.y) || !std::isfinite(light.distance)
        || !std::isfinite(light.fade) || !finite(light.attenuation) || !finite(light.ambient)
        || !finite(light.diffuse)) throw std::invalid_argument("Invalid page light input");
    const float sine = light_sine(light.angles.x);
    const float cosine_phi = light_cosine(light.angles.y), sine_phi = light_sine(light.angles.y);
    const float x = sine * sine_phi, z = sine * cosine_phi;
    const float y = light_cosine(light.angles.x);
    auto attenuation = light.attenuation;
    if (attenuation[0] == 0 && attenuation[1] == 0 && attenuation[2] == 0) attenuation = {1, 0, 0};
    return {{x * light.distance, y * light.distance, z * light.distance, 1,
                attenuation[0], attenuation[1], attenuation[2], 0, 0,0,0,0, 0,0,0,0},
        {light.ambient[0], light.ambient[1], light.ambient[2], 1, 0,0,0,0,
                light.diffuse[0], light.diffuse[1], light.diffuse[2], 1,
                light.fade, light.fade, light.fade, light.fade}};
}
}
