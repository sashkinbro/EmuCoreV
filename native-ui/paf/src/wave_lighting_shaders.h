#pragma once
#include "wave_grid_normal_shaders.h"
#include <string_view>

namespace emucorev::paf::detail {

// Exact translated Vita quad.vp shared with gridNormal8 (source 0x812fc998).
inline constexpr auto wave_lighting_vertex_shader = wave_grid_normal_vertex_shader;
inline constexpr auto wave_fliptri_vertex_shader = wave_grid_normal_vertex_shader;

// Semantic GLSL ES 3.20 port of libpaf shader 0x812e4f4c (paf/cg/drawobj/wave/wave_lit_tex.fp).
// Reads fragmentData.buffer0[0..23] directly without raw byte-shift buffer fetching so
// Adreno LLVM compilers optimize cleanly without spilling.
// Uniform buffer layout (96 floats across 24 vec4s, from inventory.json):
//   buffer0[0..3]         : ModelMatrix (4x4, translation + rotation)
//   buffer0[4..7]         : FogPack (color, density*scale, sun_color, {d_scale, d_offset, 0, 0})
//   buffer0[8..11]        : MaterialPack (color, {diffuse_coeff, ...}, ...)
//   buffer0[12..15]       : PointLightPack2 ({ambient, 1}, {0}, {diffuse, 1}, {fade})
//   buffer0[16].xy        : HalfPixel ({0.5/w, 0.5/h})
//   buffer0[16].zw, [17].x: EyePos
//   buffer0[17].zw..[21].xy: PointLightPack1 (LightPos in [17].zw, [18].x)
//   buffer0[21].zw..[22].xy: EdgeColor ({rgb, edge_power})
//   buffer0[22].zw..[23].xy: CenterColor ({rgb, center_power})
//   buffer0[23].zw        : EdgeFade ({x, y})
inline constexpr std::string_view wave_lighting_fragment_shader = R"WAVELIGHTFRAG(#version 320 es
precision highp float;
precision highp int;

layout(binding = 1, std140) uniform fragmentDataType
{
    vec4 buffer0[24];
} fragmentData;

layout(binding = 3, std140) uniform GxmRenderFragBufferBlock
{
    float back_disabled;
    float front_disabled;
    float writing_mask;
    float use_raw_image;
    mediump float res_multiplier;
    float cast_sampler_mask;
    float cast_phase_mask;
    float inv_frag_width;
    float inv_frag_height;
    float raw_cast_mask;
    float iterator_written_mask;
} renderFragInfo;

layout(binding = 0) uniform highp sampler2D fragTex_PosTex;
layout(binding = 1) uniform highp sampler2D fragTex_NorTex;

layout(location = 4) in vec4 v_TexCoord0;
layout(location = 0) out mediump vec4 out_color;

float safe_pow(float base, float exponent)
{
    if (exponent == 0.0) {
        return 1.0;
    }
    return (base > 0.0) ? exp2(log2(base) * exponent) : 0.0;
}

void main()
{
    if (renderFragInfo.front_disabled != 0.0 && gl_FrontFacing) {
        discard;
    }
    if (renderFragInfo.back_disabled != 0.0 && !gl_FrontFacing) {
        discard;
    }

    vec2 tc = ((uint(renderFragInfo.iterator_written_mask) & 16u) != 0u)
        ? v_TexCoord0.xy
        : vec2(0.0);
    tc.y = 1.0 - tc.y;
    vec2 uv = tc - fragmentData.buffer0[16].xy;

    vec3 pos_local = texture(fragTex_PosTex, uv).xyz;
    vec3 nor_raw = texture(fragTex_NorTex, uv).xyz;
    vec3 nor_local = normalize(nor_raw * 2.0 - 1.0);

    mat3 model_rot = mat3(
        fragmentData.buffer0[0].xyz,
        fragmentData.buffer0[1].xyz,
        fragmentData.buffer0[2].xyz);
    vec3 model_trans = fragmentData.buffer0[3].xyz;

    vec3 N = normalize(model_rot * nor_local);
    vec3 P = model_rot * pos_local + model_trans;

    vec3 eye_pos = vec3(fragmentData.buffer0[16].zw, fragmentData.buffer0[17].x);
    vec3 V_vec = eye_pos - P;
    float inv_dist = inversesqrt(dot(V_vec, V_vec));
    vec3 V = V_vec * inv_dist;
    float dist = 1.0 / inv_dist;

    float NdotV = clamp(dot(N, V), 0.0, 1.0);

    vec3 light_pos = vec3(fragmentData.buffer0[17].zw, fragmentData.buffer0[18].x);
    vec3 L = normalize(light_pos - P);

    vec2 edge_fade = fragmentData.buffer0[23].zw;
    vec2 t_edge = clamp((abs(tc * 2.0 - 1.0) - edge_fade) / (1.0 - edge_fade), 0.0, 1.0);
    vec2 s_edge = t_edge * t_edge * (3.0 - 2.0 * t_edge);
    float edge_vis = (1.0 - s_edge.y) * (1.0 - s_edge.x);

    vec3 fog_color = fragmentData.buffer0[4].xyz;
    vec3 fog_density = fragmentData.buffer0[5].xyz;
    vec3 fog_sun_color = fragmentData.buffer0[6].xyz;
    float fog_d_scale = fragmentData.buffer0[7].x;
    float fog_d_offset = fragmentData.buffer0[7].y;

    vec3 fog_t = clamp((dist - fog_d_offset) / (fog_density * fog_d_scale), 0.0, 1.0);
    vec3 fog_s = fog_t * fog_t * (3.0 - 2.0 * fog_t);
    float LdotV = max(dot(L, V), 0.0);
    float sun_f = (LdotV > 0.0) ? exp2(log2(LdotV) * 8.0) : 0.0;
    vec3 fog_col = mix(fog_color, fog_sun_color, sun_f);

    vec3 mat_color = fragmentData.buffer0[8].xyz;
    float mat_diffuse = fragmentData.buffer0[9].x;
    vec3 light_ambient = fragmentData.buffer0[12].xyz;
    vec3 light_diffuse = fragmentData.buffer0[14].xyz;

    float wrap = dot(N, L) * 0.5 + 0.5;
    vec3 lit = light_diffuse * mat_diffuse * (wrap * wrap) + light_ambient;

    vec3 edge_color = vec3(fragmentData.buffer0[21].zw, fragmentData.buffer0[22].x);
    float edge_power = fragmentData.buffer0[22].y;
    vec3 center_color = vec3(fragmentData.buffer0[22].zw, fragmentData.buffer0[23].x);
    float center_power = fragmentData.buffer0[23].y;

    float center_f = safe_pow(NdotV, center_power);
    float edge_f = safe_pow(1.0 - NdotV, edge_power);
    vec3 surf = lit * mat_color + edge_color * edge_f + center_color * center_f;

    vec3 fogged = mix(surf, fog_col, fog_s);
    vec3 final_rgb = mix(fog_color, fogged, edge_vis);
    out_color = vec4(final_rgb, 1.0);
}
)WAVELIGHTFRAG";

// Semantic GLSL ES 3.20 port of libpaf TextureOps fliptri shader (0x812ff950).
inline constexpr std::string_view wave_fliptri_fragment_shader = R"WAVEFLIPFRAG(#version 320 es
precision highp float;
precision highp int;

layout(binding = 1, std140) uniform fragmentDataType
{
    vec4 buffer0[2];
} fragmentData;

layout(binding = 3, std140) uniform GxmRenderFragBufferBlock
{
    float back_disabled;
    float front_disabled;
    float writing_mask;
    float use_raw_image;
    mediump float res_multiplier;
    float cast_sampler_mask;
    float cast_phase_mask;
    float inv_frag_width;
    float inv_frag_height;
    float raw_cast_mask;
    float iterator_written_mask;
} renderFragInfo;

layout(binding = 0) uniform highp sampler2D fragTex_Source;
layout(binding = 1) uniform highp sampler2D fragTex_FlipTex;

layout(location = 4) in vec4 v_TexCoord0;
layout(location = 0) out mediump vec4 out_color;

void main()
{
    if (renderFragInfo.front_disabled != 0.0 && gl_FrontFacing) {
        discard;
    }
    if (renderFragInfo.back_disabled != 0.0 && !gl_FrontFacing) {
        discard;
    }

    vec2 tc = ((uint(renderFragInfo.iterator_written_mask) & 16u) != 0u)
        ? v_TexCoord0.xy
        : vec2(0.0);

    vec2 offset0 = fragmentData.buffer0[0].xy;
    vec2 offset1 = fragmentData.buffer0[0].zw;
    vec2 offset2 = fragmentData.buffer0[1].xy;
    vec2 flip_tex_size = fragmentData.buffer0[1].zw;

    vec2 sign_xy = texture(fragTex_FlipTex, tc * flip_tex_size).xy * 2.0 - 1.0;
    vec4 c = texture(fragTex_Source, tc + sign_xy * offset0)
           + texture(fragTex_Source, tc + sign_xy * offset1)
           + texture(fragTex_Source, tc + sign_xy * offset2);
    out_color = c * 0.333251953125;
}
)WAVEFLIPFRAG";

} // namespace emucorev::paf::detail
