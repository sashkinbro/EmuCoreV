#pragma once
#include <string_view>
namespace emucorev::paf::detail {
inline constexpr std::string_view wave_grid_normal_vertex_shader = R"WAVENORMALVERT(
#version 320 es
#ifdef GL_ARB_fragment_shader_interlock
#extension GL_ARB_fragment_shader_interlock : enable
#define SPIRV_Cross_beginInvocationInterlock() beginInvocationInterlockARB()
#define SPIRV_Cross_endInvocationInterlock() endInvocationInterlockARB()
#elif defined(GL_INTEL_fragment_shader_ordering)
#extension GL_INTEL_fragment_shader_ordering : enable
#define SPIRV_Cross_beginInvocationInterlock() beginFragmentShaderOrderingINTEL()
#define SPIRV_Cross_endInvocationInterlock()
#endif
// Shader Name: offline-paf

const vec4 _28[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
const bool _31[4] = bool[](false, false, false, false);
const int _34[2] = int[](0, 0);

layout(binding = 0, std430) restrict readonly buffer vertexDataType
{
    vec4 buffer0[5];
} vertexData;

layout(binding = 2, std140) uniform GxmRenderVertBufferBlock
{
    vec4 viewport_flip;
    float viewport_flag;
    float screen_width;
    float screen_height;
    float z_offset;
    float z_scale;
} renderVertInfo;

layout(location = 0) in vec4 data;
layout(location = 4) out vec4 v_TexCoord0;
vec4 pa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 sa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 internals[3] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0));
vec4 outs[20] = _28;

void primary_program()
{
    vec2 _189 = vec2(pa[0].x, pa[0].y);
    vec2 _196 = vec2(pa[0].z, pa[0].w);
    internals[0] = vec4(_189.x, _189.y, _196.x, _196.y);
    vec2 _213 = vec2(sa[3].x, sa[3].y);
    vec2 _220 = vec2(sa[3].z, sa[3].w);
    internals[1] = vec4(_213.x, _213.y, _220.x, _220.y);
    internals[1] = fma(vec4(internals[0].y, internals[0].y, internals[0].y, internals[0].y), sa[1], internals[1]);
    vec2 _267 = fma(vec2(internals[0].x, internals[0].x), vec2(sa[0].x, sa[0].y), vec2(internals[1].x, internals[1].y));
    outs[0] = vec4(_267.x, _267.y, outs[0].z, outs[0].w);
    vec2 _292 = fma(vec2(internals[0].x, internals[0].x), vec2(sa[0].z, sa[0].w), vec2(internals[1].z, internals[1].w));
    outs[0] = vec4(outs[0].x, outs[0].y, _292.x, _292.y);
    vec2 _317 = fma(vec2(internals[0].z, internals[0].w), vec2(sa[4].z, sa[4].w), vec2(sa[4].x, sa[4].y));
    outs[1] = vec4(_317.x, _317.y, outs[1].z, outs[1].w);
}

void vert_output_finalize()
{
    if (renderVertInfo.viewport_flag < 0.5)
    {
        gl_Position = ((outs[0] * (vec4(2.0, -2.0, 2.0, 2.0) / vec4(renderVertInfo.screen_width, renderVertInfo.screen_height, 1.0, 1.0))) + vec4(-1.0, 1.0, 0.0, 0.0)) * renderVertInfo.viewport_flip;
        gl_Position.z = gl_Position.w;
    }
    else
    {
        gl_Position = outs[0] * renderVertInfo.viewport_flip;
        gl_Position.z = (((((gl_Position.z / gl_Position.w) * renderVertInfo.z_scale) + renderVertInfo.z_offset) * 2.0) + (-1.0)) * gl_Position.w;
    }
    v_TexCoord0 = outs[1];
}

void main()
{
    int i = 0;
    while (i < 5)
    {
        sa[i + 0] = vertexData.buffer0[i];
        i++;
    }
    pa[0] = data;
    outs[0] = vec4(0.0);
    outs[1] = vec4(0.0);
    outs[2] = vec4(0.0);
    outs[3] = vec4(0.0);
    outs[4] = vec4(0.0);
    outs[5] = vec4(0.0);
    outs[6] = vec4(0.0);
    outs[7] = vec4(0.0);
    outs[8] = vec4(0.0);
    outs[9] = vec4(0.0);
    outs[10] = vec4(0.0);
    outs[11] = vec4(0.0);
    outs[12] = vec4(0.0);
    outs[13] = vec4(0.0);
    outs[14] = vec4(0.0);
    outs[15] = vec4(0.0);
    outs[16] = vec4(0.0);
    outs[17] = vec4(0.0);
    outs[18] = vec4(0.0);
    outs[19] = vec4(0.0);
    primary_program();
    vert_output_finalize();
}


)WAVENORMALVERT";
inline constexpr std::string_view wave_grid_normal_fragment_shader = R"WAVENORMALFRAG(
#version 320 es
#ifdef GL_ARB_fragment_shader_interlock
#extension GL_ARB_fragment_shader_interlock : enable
#define SPIRV_Cross_beginInvocationInterlock() beginInvocationInterlockARB()
#define SPIRV_Cross_endInvocationInterlock() endInvocationInterlockARB()
#elif defined(GL_INTEL_fragment_shader_ordering)
#extension GL_INTEL_fragment_shader_ordering : enable
#define SPIRV_Cross_beginInvocationInterlock() beginFragmentShaderOrderingINTEL()
#define SPIRV_Cross_endInvocationInterlock()
#endif
// Shader Name: offline-paf
precision highp float;
precision highp int;

const vec4 _28[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
const bool _31[4] = bool[](false, false, false, false);
const int _34[2] = int[](0, 0);

layout(binding = 1, std430) restrict readonly buffer fragmentDataType
{
    vec4 buffer0[1];
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

layout(location = 4) in vec4 v_TexCoord0;
layout(location = 0) out mediump vec4 out_color;
vec4 pa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 sa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 internals[3] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0));
vec4 r[20] = _28;

void frag_init()
{
    if ((renderFragInfo.front_disabled != 0.0) && gl_FrontFacing)
    {
        discard;
    }
    if ((renderFragInfo.back_disabled != 0.0) && (!gl_FrontFacing))
    {
        discard;
    }
}

float pack2xF16(vec2 _845)
{
    return uintBitsToFloat(packHalf2x16(mix(clamp(_845, vec2(-65504.0), vec2(65504.0)), _845, isnan(_845))));
}

void primary_program()
{
    vec2 _174 = vec2(sa[0].x, sa[0].y) + vec2(pa[0].x, pa[0].y);
    r[0] = vec4(_174.x, _174.y, r[0].z, r[0].w);
    vec2 _184 = vec2(r[0].x, r[0].y);
    bool _206 = (((uint(renderFragInfo.cast_sampler_mask) >> 0u) & 1u) != 0u) && (renderFragInfo.res_multiplier != 1.0);
    vec2 _210 = gl_FragCoord.xy * vec2(renderFragInfo.inv_frag_width, renderFragInfo.inv_frag_height);
    vec2 _211 = _184 - _210;
    vec2 _214 = _210 + (_211 / vec2(renderFragInfo.res_multiplier));
    vec2 _217 = abs(_211);
    float _218 = _184.x;
    float _222 = _217.x;
    float _232 = float(textureSize(fragTex_Source, 0).x);
    vec4 _249 = texture(fragTex_Source, vec2(_206 ? ((_222 > 4.9999998736893758177757263183594e-05) ? ((_222 <= 0.004999999888241291046142578125) ? _214.x : _218) : ((((floor((_218 * _232) * 0.5) * 2.0) + float((uint(renderFragInfo.cast_phase_mask) >> 0u) & 1u)) + 0.5) / _232)) : _218, ((_217.y <= 0.004999999888241291046142578125) && _206) ? _214.y : _184.y));
    vec4 _260 = round(_249 * 65535.0);
    vec4 _261 = vec4(0.0);
    uvec4 _266 = uvec4(mix(clamp(_260, _261, vec4(65535.0)), _261, isnan(_260)));
    r[1] = mix(_249, vec4(uintBitsToFloat(_266.x | (_266.y << 16u)), uintBitsToFloat(_266.z | (_266.w << 16u)), 0.0, 0.0), bvec4(((uint(renderFragInfo.raw_cast_mask) >> 0u) & 1u) != 0u));
    vec2 _296 = (vec2(0.0) - vec2(sa[0].x, sa[0].y)) + vec2(pa[0].x, pa[0].y);
    r[0] = vec4(_296.x, _296.y, r[0].z, r[0].w);
    vec2 _306 = vec2(r[0].x, r[0].y);
    bool _323 = (((uint(renderFragInfo.cast_sampler_mask) >> 0u) & 1u) != 0u) && (renderFragInfo.res_multiplier != 1.0);
    vec2 _327 = gl_FragCoord.xy * vec2(renderFragInfo.inv_frag_width, renderFragInfo.inv_frag_height);
    vec2 _328 = _306 - _327;
    vec2 _331 = _327 + (_328 / vec2(renderFragInfo.res_multiplier));
    vec2 _332 = abs(_328);
    float _333 = _306.x;
    float _337 = _332.x;
    float _346 = float(textureSize(fragTex_Source, 0).x);
    vec4 _363 = texture(fragTex_Source, vec2(_323 ? ((_337 > 4.9999998736893758177757263183594e-05) ? ((_337 <= 0.004999999888241291046142578125) ? _331.x : _333) : ((((floor((_333 * _346) * 0.5) * 2.0) + float((uint(renderFragInfo.cast_phase_mask) >> 0u) & 1u)) + 0.5) / _346)) : _333, ((_332.y <= 0.004999999888241291046142578125) && _323) ? _331.y : _306.y));
    vec4 _371 = round(_363 * 65535.0);
    vec4 _372 = vec4(0.0);
    uvec4 _377 = uvec4(mix(clamp(_371, _372, vec4(65535.0)), _372, isnan(_371)));
    r[2] = mix(_363, vec4(uintBitsToFloat(_377.x | (_377.y << 16u)), uintBitsToFloat(_377.z | (_377.w << 16u)), 0.0, 0.0), bvec4(((uint(renderFragInfo.raw_cast_mask) >> 0u) & 1u) != 0u));
    vec2 _407 = (vec2(0.0) - vec2(r[2].x, r[2].y)) + vec2(r[1].x, r[1].y);
    r[3] = vec4(_407.x, _407.y, r[3].z, r[3].w);
    vec4 _425 = r[3];
    _425.z = (0.0 - r[2].z) + r[1].z;
    r[3] = _425;
    vec2 _440 = vec2(sa[0].z, sa[0].w) + vec2(pa[0].x, pa[0].y);
    r[0] = vec4(_440.x, _440.y, r[0].z, r[0].w);
    vec2 _450 = vec2(r[0].x, r[0].y);
    bool _467 = (((uint(renderFragInfo.cast_sampler_mask) >> 0u) & 1u) != 0u) && (renderFragInfo.res_multiplier != 1.0);
    vec2 _471 = gl_FragCoord.xy * vec2(renderFragInfo.inv_frag_width, renderFragInfo.inv_frag_height);
    vec2 _472 = _450 - _471;
    vec2 _475 = _471 + (_472 / vec2(renderFragInfo.res_multiplier));
    vec2 _476 = abs(_472);
    float _477 = _450.x;
    float _481 = _476.x;
    float _490 = float(textureSize(fragTex_Source, 0).x);
    vec4 _507 = texture(fragTex_Source, vec2(_467 ? ((_481 > 4.9999998736893758177757263183594e-05) ? ((_481 <= 0.004999999888241291046142578125) ? _475.x : _477) : ((((floor((_477 * _490) * 0.5) * 2.0) + float((uint(renderFragInfo.cast_phase_mask) >> 0u) & 1u)) + 0.5) / _490)) : _477, ((_476.y <= 0.004999999888241291046142578125) && _467) ? _475.y : _450.y));
    vec4 _515 = round(_507 * 65535.0);
    vec4 _516 = vec4(0.0);
    uvec4 _521 = uvec4(mix(clamp(_515, _516, vec4(65535.0)), _516, isnan(_515)));
    r[1] = mix(_507, vec4(uintBitsToFloat(_521.x | (_521.y << 16u)), uintBitsToFloat(_521.z | (_521.w << 16u)), 0.0, 0.0), bvec4(((uint(renderFragInfo.raw_cast_mask) >> 0u) & 1u) != 0u));
    vec2 _551 = (vec2(0.0) - vec2(sa[0].z, sa[0].w)) + vec2(pa[0].x, pa[0].y);
    pa[0] = vec4(_551.x, _551.y, pa[0].z, pa[0].w);
    vec2 _561 = vec2(pa[0].x, pa[0].y);
    bool _578 = (((uint(renderFragInfo.cast_sampler_mask) >> 0u) & 1u) != 0u) && (renderFragInfo.res_multiplier != 1.0);
    vec2 _582 = gl_FragCoord.xy * vec2(renderFragInfo.inv_frag_width, renderFragInfo.inv_frag_height);
    vec2 _583 = _561 - _582;
    vec2 _586 = _582 + (_583 / vec2(renderFragInfo.res_multiplier));
    vec2 _587 = abs(_583);
    float _588 = _561.x;
    float _592 = _587.x;
    float _601 = float(textureSize(fragTex_Source, 0).x);
    vec4 _618 = texture(fragTex_Source, vec2(_578 ? ((_592 > 4.9999998736893758177757263183594e-05) ? ((_592 <= 0.004999999888241291046142578125) ? _586.x : _588) : ((((floor((_588 * _601) * 0.5) * 2.0) + float((uint(renderFragInfo.cast_phase_mask) >> 0u) & 1u)) + 0.5) / _601)) : _588, ((_587.y <= 0.004999999888241291046142578125) && _578) ? _586.y : _561.y));
    vec4 _626 = round(_618 * 65535.0);
    vec4 _627 = vec4(0.0);
    uvec4 _632 = uvec4(mix(clamp(_626, _627, vec4(65535.0)), _627, isnan(_626)));
    r[2] = mix(_618, vec4(uintBitsToFloat(_632.x | (_632.y << 16u)), uintBitsToFloat(_632.z | (_632.w << 16u)), 0.0, 0.0), bvec4(((uint(renderFragInfo.raw_cast_mask) >> 0u) & 1u) != 0u));
    vec2 _661 = vec2(r[1].x, r[1].y);
    vec3 _672 = vec3(_661.x, _661.y, vec2(r[1].z, r[1].w).x);
    internals[0] = vec4(_672.x, _672.y, _672.z, internals[0].w);
    vec3 _694 = fma(vec3(1.0), vec3(0.0) - vec3(r[2].x, r[2].y, r[2].z), vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[0] = vec4(_694.x, _694.y, _694.z, internals[0].w);
    vec3 _715 = fma(vec3(internals[0].z, internals[0].x, internals[0].y), vec3(r[3].y, r[3].z, r[3].x), vec3(0.0));
    internals[1] = vec4(_715.x, _715.y, _715.z, internals[1].w);
    vec3 _744 = fma(vec3(internals[0].y, internals[0].z, internals[0].x), vec3(0.0) - vec3(r[3].z, r[3].x, r[3].y), vec3(internals[1].x, internals[1].y, internals[1].z));
    internals[0] = vec4(_744.x, _744.y, _744.z, internals[0].w);
    vec4 _767 = internals[1];
    _767.x = dot(vec3(internals[0].x, internals[0].y, internals[0].z), vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[1] = _767;
    vec4 _776 = internals[1];
    _776.x = inversesqrt(internals[1].x);
    internals[1] = _776;
    vec3 _793 = fma(vec3(internals[1].x, internals[1].x, internals[1].x), vec3(internals[0].x, internals[0].y, internals[0].z), vec3(0.0));
    internals[0] = vec4(_793.x, _793.y, _793.z, internals[0].w);
    vec2 _805 = fma(vec2(internals[0].x, internals[0].y), vec2(0.5), vec2(0.5));
    r[0] = vec4(_805.x, _805.y, r[0].z, r[0].w);
    vec4 _817 = r[0];
    _817.z = fma(internals[0].z, 0.5, 0.5);
    r[0] = _817;
    vec2 _828 = vec2(r[0].x, r[0].y);
    vec2 _835 = vec2(r[0].z, r[0].w);
    vec4 _840 = vec4(_828.x, _828.y, _835.x, _835.y);
    vec2 _863 = vec2(pack2xF16(vec2(_840.x, _840.y)), pack2xF16(vec2(_840.z, _840.w)));
    pa[0] = vec4(_863.x, _863.y, pa[0].z, pa[0].w);
}

vec2 unpack2xF16(float _114)
{
    return unpackHalf2x16(floatBitsToUint(_114));
}

void frag_output_finalize()
{
    vec2 _111 = vec2(pa[0].x, pa[0].y);
    vec2 _120 = unpack2xF16(_111.x);
    vec2 _122 = unpack2xF16(_111.y);
    out_color = vec4(_120.x, _120.y, _122.x, _122.y);
}

void main()
{
    int i = 0;
    while (i < 1)
    {
        sa[i + 0] = fragmentData.buffer0[i];
        i++;
    }
    vec4 _144 = mix(vec4(0.0, 0.0, 0.0, 1.0), v_TexCoord0, bvec4((uint(renderFragInfo.iterator_written_mask) & 16u) != 0u));
    vec2 _147 = vec2(_144.x, _144.y);
    pa[0] = vec4(_147.x, _147.y, pa[0].z, pa[0].w);
    frag_init();
    primary_program();
    frag_output_finalize();
}


)WAVENORMALFRAG";
}
