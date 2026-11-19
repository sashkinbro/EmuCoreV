#include <emucorev/paf/plane_div.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>

namespace emucorev::paf {

namespace {

// Primary triangle index tables copied verbatim from paf.elf DAT_8129aaac (modes 1..9).
constexpr std::array<std::uint16_t, 18> kIndices1x3{
    0, 1, 2, 1, 2, 3, 2, 3, 4, 3, 4, 5, 4, 5, 6, 5, 6, 7};
constexpr std::array<std::uint16_t, 18> kIndices3x1{
    0, 1, 4, 1, 4, 5, 1, 2, 5, 2, 5, 6, 2, 3, 6, 3, 6, 7};
constexpr std::array<std::uint16_t, 54> kIndices3x3{
    0, 1, 4, 1, 4, 5, 1, 2, 5, 2, 5, 6, 2, 3, 6, 3, 6, 7,
    4, 5, 8, 5, 8, 9, 5, 6, 9, 6, 9, 10, 6, 7, 10, 7, 10, 11,
    8, 9, 12, 9, 12, 13, 9, 10, 13, 10, 13, 14, 10, 11, 14, 11, 14, 15};
constexpr std::array<std::uint16_t, 90> kIndices3x5{
    0, 1, 4, 1, 4, 5, 1, 2, 5, 2, 5, 6, 2, 3, 6, 3, 6, 7,
    4, 5, 8, 5, 8, 9, 5, 6, 9, 6, 9, 10, 6, 7, 10, 7, 10, 11,
    8, 9, 12, 9, 12, 13, 9, 10, 13, 10, 13, 14, 10, 11, 14, 11, 14, 15,
    12, 13, 16, 13, 16, 17, 13, 14, 17, 14, 17, 18, 14, 15, 18, 15, 18, 19,
    16, 17, 20, 17, 20, 21, 17, 18, 21, 18, 21, 22, 18, 19, 22, 19, 22, 23};
constexpr std::array<std::uint16_t, 90> kIndices5x3{
    0, 1, 6, 1, 6, 7, 1, 2, 7, 2, 7, 8, 2, 3, 8, 3, 8, 9, 3, 4, 9, 4, 9, 10, 4, 5, 10, 5, 10, 11,
    6, 7, 12, 7, 12, 13, 7, 8, 13, 8, 13, 14, 8, 9, 14, 9, 14, 15, 9, 10, 15, 10, 15, 16, 10, 11, 16, 11, 16, 17,
    12, 13, 18, 13, 18, 19, 13, 14, 19, 14, 19, 20, 14, 15, 20, 15, 20, 21, 15, 16, 21, 16, 21, 22, 16, 17, 22, 17, 22, 23};
constexpr std::array<std::uint16_t, 48> kIndices3x3Border{
    0, 1, 4, 1, 4, 5, 1, 2, 5, 2, 5, 6, 2, 3, 6, 3, 6, 7,
    4, 5, 8, 5, 8, 9, 6, 7, 10, 7, 10, 11,
    8, 9, 12, 9, 12, 13, 9, 10, 13, 10, 13, 14, 10, 11, 14, 11, 14, 15};
constexpr std::array<std::uint16_t, 12> kIndices1x3OpaqueBorder{
    0, 1, 2, 1, 2, 3, 4, 5, 6, 5, 6, 7};
constexpr std::array<std::uint16_t, 12> kIndices3x1OpaqueBorder{
    0, 1, 4, 1, 4, 5, 2, 3, 6, 3, 6, 7};

// Second-pass opaque center triangles from paf.elf DAT_8129ab1c (modes 7, 8, 9).
constexpr std::array<std::uint16_t, 6> kOpaqueCenter3x3{5, 6, 9, 6, 9, 10};
constexpr std::array<std::uint16_t, 6> kOpaqueCenter1x3{2, 3, 4, 3, 4, 5};
constexpr std::array<std::uint16_t, 6> kOpaqueCenter3x1{1, 2, 5, 2, 5, 6};

// Matches FUN_81083892 case 0 (scale_mode == 0, scale_value == 1).
void compute_axis_layout_scale0(
    float base_pos,
    float base_size,
    std::int32_t align,
    float tex_extent,
    float margin_min,
    float margin_max,
    float& out_pos,
    float& out_size) noexcept {
    const float margin_sum = margin_min + margin_max;
    float f6 = 0.f;
    float f9 = 0.f;
    if (tex_extent - margin_sum > 0.f) {
        f6 = base_size;
        f9 = base_size / ((tex_extent - margin_sum) / tex_extent);
    }
    float align_off = 0.f;
    if (align == 1) {
        align_off = base_size * -0.5f + f6 * 0.5f;
    } else if (align == 2) {
        align_off = base_size * 0.5f - f6 * 0.5f;
    }
    if (f6 != 0.f) {
        align_off = align_off - (f9 / tex_extent) * (margin_min - margin_max) * 0.5f;
    }
    out_size = f9;
    out_pos = base_pos + align_off;
}

std::array<float, 2> compute_3border_axis(
    float layout_extent,
    float tex_extent,
    float border_scale,
    std::array<float, 2> ratios) noexcept {
    std::array<float, 2> b{0.f, 0.f};
    if (layout_extent > 1.0f) {
        const float inv_layout = arm_recip_estimate_2step(layout_extent);
        const float scaled_tex = border_scale * tex_extent;
        b[0] = (ratios[0] * scaled_tex) * inv_layout;
        b[1] = (ratios[1] * scaled_tex) * inv_layout;
        const float sum = b[0] + b[1];
        if (sum > 1.0f) {
            const float inv_sum = arm_recip_estimate_2step(sum);
            b[0] *= inv_sum;
            b[1] *= inv_sum;
        }
    }
    return b;
}

template <std::size_t N>
void append_indices(std::vector<std::uint16_t>& dst, const std::array<std::uint16_t, N>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

void emit_quads_from_triangles(
    const PlaneDivMesh& mesh,
    const std::vector<std::uint16_t>& tri_indices,
    std::vector<PlaneDivScreenQuad>& out) {
    const float layout_cx = mesh.layout.center_x;
    const float layout_cy_screen = -mesh.layout.center_y;
    const float lw = mesh.layout.width;
    const float lh = mesh.layout.height;
    // paf.elf stores 2 triangles per quad in index order: (tl, tr, bl), (tr, bl, br).
    for (std::size_t i = 0; i + 5 < tri_indices.size(); i += 6) {
        const auto& v0 = mesh.vertices[tri_indices[i + 0]];
        const auto& v3 = mesh.vertices[tri_indices[i + 5]];
        const float x0 = layout_cx + v0.x * lw;
        const float x1 = layout_cx + v3.x * lw;
        const float y0 = layout_cy_screen - v0.y * lh;
        const float y1 = layout_cy_screen - v3.y * lh;
        const float w = x1 - x0;
        const float h = y1 - y0;
        if (w <= 0.f || h <= 0.f) continue;
        out.push_back(PlaneDivScreenQuad{
            x0,
            y0,
            w,
            h,
            v0.u,
            v0.v,
            v3.u - v0.u,
            v3.v - v0.v,
        });
    }
}

} // namespace

std::array<float, 2> resolve_plane_div_ratio(
    PlaneDivRatioPreset preset,
    std::array<float, 2> custom) noexcept {
    switch (preset) {
    case PlaneDivRatioPreset::Zero:
        return {0.f, 0.f};
    case PlaneDivRatioPreset::Third: {
        const float v = std::bit_cast<float>(0x3eaaaaabu);
        return {v, v};
    }
    case PlaneDivRatioPreset::FortyNine: {
        const float v = std::bit_cast<float>(0x3efae148u);
        return {v, v};
    }
    case PlaneDivRatioPreset::Half:
        return {0.5f, 0.5f};
    case PlaneDivRatioPreset::Custom:
        return custom;
    }
    return {0.f, 0.f};
}

float arm_recip_estimate_2step(float x) noexcept {
    const std::uint32_t u = std::bit_cast<std::uint32_t>(x);
    const std::uint32_t sign = u & 0x80000000u;
    const std::uint32_t exp = (u >> 23) & 0xffu;
    const std::uint32_t frac = u & 0x7fffffu;
    if (exp == 0u || exp == 0xffu) {
        return 1.0f / x;
    }
    if (exp >= 253u) {
        return std::bit_cast<float>(sign);
    }
    const std::uint32_t scaled = 256u + (frac >> 15);
    const std::uint32_t est = ((524288u / (scaled * 2u + 1u)) + 1u) >> 1;
    float r = std::bit_cast<float>(sign | ((253u - exp) << 23) | ((est & 0xffu) << 15));
    r = r * (2.0f - (r * x));
    r = r * (2.0f - (r * x));
    return r;
}

PlaneDivLayout compute_plane_div_layout(const PlaneDivSpec& spec) noexcept {
    PlaneDivLayout out{};
    out.center_z = spec.pos_z;

    const float tex_w = static_cast<float>(spec.tex_width);
    const float tex_h = static_cast<float>(spec.tex_height);
    const float ml = static_cast<float>(spec.margin_left);
    const float mb = static_cast<float>(spec.margin_bottom);
    const float mr = static_cast<float>(spec.margin_right);
    const float mt = static_cast<float>(spec.margin_top);

    const auto mode = static_cast<std::uint32_t>(spec.div_mode);
    const float base_w = spec.size_x >= 0.f ? spec.size_x : 0.f;
    const float base_h = spec.size_y >= 0.f ? spec.size_y : 0.f;

    if (mode == 0u || mode == 1u || mode == 8u) {
        compute_axis_layout_scale0(
            spec.pos_x, base_w, spec.align_x, tex_w, ml, mr, out.center_x, out.width);
    } else {
        float aligned_cx = 0.f;
        float w = 0.f;
        compute_axis_layout_scale0(
            spec.pos_x, base_w, spec.align_x, tex_w, 0.f, 0.f, aligned_cx, w);
        out.width = w + ml + mr;
        float dx = (mr - ml) * 0.5f;
        if (spec.has_rot) {
            dx *= std::cos(spec.rot_y);
        }
        out.center_x = aligned_cx + dx;
    }

    // Matches UpdateLayout @ 0x81084a44: Y excludes only modes 0 and 2 here.
    if (mode == 0u || mode == 2u) {
        compute_axis_layout_scale0(
            spec.pos_y, base_h, spec.align_y, tex_h, mb, mt, out.center_y, out.height);
    } else {
        float aligned_cy = 0.f;
        float h = 0.f;
        compute_axis_layout_scale0(
            spec.pos_y, base_h, spec.align_y, tex_h, 0.f, 0.f, aligned_cy, h);
        out.height = h + mb + mt;
        out.center_y = aligned_cy + (mt - mb) * 0.5f;
    }

    return out;
}

PlaneDivMesh build_plane_div_mesh(const PlaneDivSpec& spec) {
    PlaneDivMesh mesh{};
    mesh.layout = compute_plane_div_layout(spec);

    const auto mode = static_cast<std::uint32_t>(spec.div_mode);
    if (mode == 0u || mode > 9u) {
        return mesh;
    }

    const float tex_w = static_cast<float>(spec.tex_width);
    const float tex_h = static_cast<float>(spec.tex_height);
    const float lw = mesh.layout.width;
    const float lh = mesh.layout.height;

    std::array<float, 6> xs{};
    std::array<float, 6> ys{};
    std::array<float, 6> us{};
    std::array<float, 6> vs{};

    if (mode == 4u || mode == 5u) {
        // Mode 4 (3x5) and mode 5 (5x3) from 0x81084d92..0x8108526c and 0x81085358.
        const auto bx = compute_3border_axis(lw, tex_w, spec.div_border_scale, spec.div_ratio_h);
        xs[0] = -0.5f;
        xs[1] = bx[0] + -0.5f;
        xs[2] = 0.5f - bx[1];
        xs[3] = 0.5f;

        us[0] = 0.0f;
        us[1] = spec.div_ratio_h[0];
        us[2] = 1.0f - spec.div_ratio_h[1];
        us[3] = 1.0f;

        mesh.cols = 4;
        if (mode == 5u) {
            mesh.cols = 6;
            xs[4] = xs[2];
            xs[5] = xs[3];
            us[4] = us[2];
            us[5] = us[3];

            std::array<float, 2> sx{0.f, 0.f};
            if (lw > 1.0f) {
                const float inv_w = arm_recip_estimate_2step(lw);
                const float scaled_tex = spec.div_border_scale * tex_w;
                sx[0] = (spec.div_ratio_stretch[0] * scaled_tex) * inv_w;
                sx[1] = (spec.div_ratio_stretch[1] * scaled_tex) * inv_w;
                float avail = 1.0f - (bx[0] + bx[1]);
                if (avail <= 0.f) avail = 0.f;
                const float ssum = sx[0] + sx[1];
                if (avail < ssum) {
                    const float ratio = avail / ssum;
                    sx[0] *= ratio;
                    sx[1] *= ratio;
                }
            }
            xs[2] = xs[1] + sx[0];
            xs[3] = xs[4] - sx[1];
            us[2] = spec.div_ratio_h[0] + spec.div_ratio_stretch[0];
            us[3] = us[4] - spec.div_ratio_stretch[1];

            if (lw > 1.0f) {
                const float center_uv = (1.0f - (spec.div_ratio_h[0] + spec.div_ratio_h[1]))
                    - (spec.div_ratio_stretch[0] + spec.div_ratio_stretch[1]);
                float center_w = (tex_w * center_uv) / lw;
                if (center_w <= 0.f) center_w = 0.f;
                const float avail = 1.0f - (bx[0] + bx[1]);
                const float gap = avail - (sx[0] + sx[1] + center_w);
                if (gap > 0.f) {
                    const float off = spec.div_stretch_offset / lw;
                    const float half_gap = gap * 0.5f;
                    const float neg_half = -(gap * 0.5f);
                    float clamped = neg_half > off ? neg_half : off;
                    if (half_gap <= clamped) {
                        clamped = half_gap;
                    }
                    xs[2] = (clamped + half_gap) + xs[2];
                    xs[3] = xs[3] - (half_gap - clamped);
                }
            }
        }

        const auto by = compute_3border_axis(lh, tex_h, spec.div_border_scale, spec.div_ratio_v);
        ys[0] = 0.5f;
        ys[1] = 0.5f - by[0];
        ys[2] = by[1] + -0.5f;
        ys[3] = -0.5f;

        vs[0] = 0.0f;
        vs[1] = spec.div_ratio_v[0];
        vs[2] = 1.0f - spec.div_ratio_v[1];
        vs[3] = 1.0f;

        mesh.rows = 4;
        if (mode == 4u) {
            mesh.rows = 6;
            ys[4] = ys[2];
            ys[5] = ys[3];
            vs[4] = vs[2];
            vs[5] = vs[3];

            std::array<float, 2> sy{0.f, 0.f};
            float avail = 1.0f;
            if (lh > 1.0f) {
                const float inv_h = arm_recip_estimate_2step(lh);
                const float scaled_tex = spec.div_border_scale * tex_h;
                sy[0] = (spec.div_ratio_stretch[0] * scaled_tex) * inv_h;
                sy[1] = (spec.div_ratio_stretch[1] * scaled_tex) * inv_h;
                avail = 1.0f - (by[0] + by[1]);
                if (avail <= 0.f) avail = 0.f;
                const float ssum = sy[0] + sy[1];
                if (avail < ssum) {
                    const float ratio = avail / ssum;
                    sy[0] *= ratio;
                    sy[1] *= ratio;
                }
            }
            ys[2] = ys[1] - sy[0];
            ys[3] = ys[4] + sy[1];
            vs[2] = spec.div_ratio_v[0] + spec.div_ratio_stretch[0];
            vs[3] = vs[4] - spec.div_ratio_stretch[1];

            if (lh > 1.0f) {
                const float center_uv = (1.0f - (spec.div_ratio_v[0] + spec.div_ratio_v[1]))
                    - (spec.div_ratio_stretch[0] + spec.div_ratio_stretch[1]);
                float center_h = (tex_h * center_uv) / lh;
                if (center_h <= 0.f) center_h = 0.f;
                const float gap = avail - (sy[0] + sy[1] + center_h);
                if (gap > 0.f) {
                    const float off = spec.div_stretch_offset / lh;
                    const float half_gap = gap * 0.5f;
                    const float neg_half = -(gap * 0.5f);
                    float clamped = neg_half > off ? neg_half : off;
                    if (half_gap <= clamped) {
                        clamped = half_gap;
                    }
                    ys[2] = ys[2] - (half_gap - clamped);
                    ys[3] = (clamped + half_gap) + ys[3];
                }
            }
        }
    } else {
        std::array<float, 2> bx{0.f, 0.f};
        std::array<float, 2> by{0.f, 0.f};
        if (mode != 1u && mode != 8u) {
            bx = compute_3border_axis(lw, tex_w, spec.div_border_scale, spec.div_ratio_h);
        }
        if (mode != 2u && mode != 9u) {
            by = compute_3border_axis(lh, tex_h, spec.div_border_scale, spec.div_ratio_v);
        }

        xs[0] = -0.5f;
        xs[1] = 0.5f;
        xs[2] = 0.5f - bx[1];
        xs[3] = 0.5f;
        us[0] = 0.0f;
        us[1] = 1.0f;
        us[2] = 1.0f;
        us[3] = 1.0f;
        mesh.cols = 2;
        if (mode != 1u && mode != 8u) {
            mesh.cols = 4;
            xs[1] = bx[0] + -0.5f;
            us[1] = spec.div_ratio_h[0];
            us[2] = 1.0f - spec.div_ratio_h[1];
        }

        ys[0] = 0.5f;
        ys[1] = -0.5f;
        ys[2] = by[1] + -0.5f;
        ys[3] = -0.5f;
        vs[0] = 0.0f;
        vs[1] = 1.0f;
        vs[2] = 1.0f;
        vs[3] = 1.0f;
        mesh.rows = 2;
        if (mode != 2u && mode != 9u) {
            mesh.rows = 4;
            ys[1] = 0.5f - by[0];
            vs[1] = spec.div_ratio_v[0];
            vs[2] = 1.0f - spec.div_ratio_v[1];
        }
    }

    mesh.vertices.reserve(static_cast<std::size_t>(mesh.rows) * mesh.cols);
    for (std::uint32_t r = 0; r < mesh.rows; ++r) {
        for (std::uint32_t c = 0; c < mesh.cols; ++c) {
            mesh.vertices.push_back(PlaneDivVertex{xs[c], ys[r], us[c], vs[r]});
        }
    }

    switch (spec.div_mode) {
    case PlaneDivMode::None:
        break;
    case PlaneDivMode::Grid1x3:
        append_indices(mesh.indices, kIndices1x3);
        break;
    case PlaneDivMode::Grid3x1:
        append_indices(mesh.indices, kIndices3x1);
        break;
    case PlaneDivMode::Grid3x3:
        append_indices(mesh.indices, kIndices3x3);
        break;
    case PlaneDivMode::Grid3x5:
        append_indices(mesh.indices, kIndices3x5);
        break;
    case PlaneDivMode::Grid5x3:
        append_indices(mesh.indices, kIndices5x3);
        break;
    case PlaneDivMode::Grid3x3Border:
        append_indices(mesh.indices, kIndices3x3Border);
        break;
    case PlaneDivMode::Grid3x3Opaque:
        append_indices(mesh.indices, kIndices3x3Border);
        append_indices(mesh.opaque_indices, kOpaqueCenter3x3);
        break;
    case PlaneDivMode::Grid1x3Opaque:
        append_indices(mesh.indices, kIndices1x3OpaqueBorder);
        append_indices(mesh.opaque_indices, kOpaqueCenter1x3);
        break;
    case PlaneDivMode::Grid3x1Opaque:
        append_indices(mesh.indices, kIndices3x1OpaqueBorder);
        append_indices(mesh.opaque_indices, kOpaqueCenter3x1);
        break;
    }

    return mesh;
}

std::vector<PlaneDivScreenQuad> build_plane_div_screen_quads(
    PlaneDivSpec spec,
    float screen_center_x,
    float screen_center_y,
    bool include_opaque) {
    spec.pos_x = screen_center_x;
    spec.pos_y = -screen_center_y;
    const auto mesh = build_plane_div_mesh(spec);
    std::vector<PlaneDivScreenQuad> quads;
    quads.reserve((mesh.indices.size() + (include_opaque ? mesh.opaque_indices.size() : 0u)) / 6u);
    emit_quads_from_triangles(mesh, mesh.indices, quads);
    if (include_opaque && !mesh.opaque_indices.empty()) {
        emit_quads_from_triangles(mesh, mesh.opaque_indices, quads);
    }
    return quads;
}

} // namespace emucorev::paf
