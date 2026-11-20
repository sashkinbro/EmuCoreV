#pragma once
#include <string_view>
namespace emucorev::paf::detail {
inline constexpr std::string_view wave_position_vertex_shader = R"WAVEPOSVERT(#version 320 es
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

layout(binding = 0, std140) uniform vertexDataType
{
    vec4 buffer0[4];
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
    vec4 _248 = internals[0];
    vec2 _252 = vec2(_248.z, _248.w);
    internals[1] = (sa[1] * vec4(internals[0].y, internals[0].y, internals[0].y, internals[0].y)) + internals[1];
    outs[1] = vec4(_252.x, _252.y, outs[1].z, outs[1].w);
    vec2 _278 = fma(vec2(internals[0].x, internals[0].x), vec2(sa[0].x, sa[0].y), vec2(internals[1].x, internals[1].y));
    outs[0] = vec4(_278.x, _278.y, outs[0].z, outs[0].w);
    vec2 _303 = fma(vec2(internals[0].x, internals[0].x), vec2(sa[0].z, sa[0].w), vec2(internals[1].z, internals[1].w));
    outs[0] = vec4(outs[0].x, outs[0].y, _303.x, _303.y);
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
    while (i < 4)
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


)WAVEPOSVERT";
inline constexpr std::string_view wave_position_fragment_shader = R"WAVEPOSFRAG(#version 320 es
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

layout(binding = 1, std140) uniform fragmentDataType
{
    vec4 buffer0[8];
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

layout(binding = 7) uniform highp sampler2D fragTex_HeightMap;

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

float pack2xF16(vec2 _478)
{
    return uintBitsToFloat(packHalf2x16(mix(clamp(_478, vec2(-65504.0), vec2(65504.0)), _478, isnan(_478))));
}

void secondary_program()
{
    vec4 _216 = sa[0];
    _216.x = fma(sa[4].w, sa[12].z, sa[13].z);
    sa[0] = _216;
    vec4 _230 = internals[0];
    _230.x = fract(sa[0].x);
    internals[0] = _230;
    vec4 _250 = internals[1];
    _250.x = fma(internals[0].x, sa[12].y, sa[12].x);
    internals[1] = _250;
    vec4 _264 = internals[2];
    _264.x = fma(internals[1].x, internals[1].x, 0.0);
    internals[2] = _264;
    vec4 _284 = internals[0];
    _284.x = fma(internals[2].x, sa[11].w, sa[11].z);
    internals[0] = _284;
    vec4 _305 = internals[0];
    _305.x = fma(internals[0].x, internals[2].x, 0.0 - sa[11].y);
    internals[0] = _305;
    vec4 _324 = internals[0];
    _324.x = fma(internals[0].x, internals[2].x, sa[13].z);
    internals[0] = _324;
    vec4 _338 = internals[1];
    _338.x = fma(internals[1].x, internals[0].x, 0.0);
    internals[1] = _338;
    vec4 _358 = internals[0];
    _358.x = fma(internals[2].x, sa[11].x, sa[10].w);
    internals[0] = _358;
    vec4 _378 = internals[0];
    _378.x = fma(internals[0].x, internals[2].x, 0.0 - sa[10].z);
    internals[0] = _378;
    vec4 _398 = internals[0];
    _398.x = fma(internals[0].x, internals[2].x, sa[13].x);
    internals[0] = _398;
    vec2 _405 = vec2(internals[1].x, internals[1].x);
    internals[2] = vec4(_405.x, _405.y, internals[2].z, internals[2].w);
    vec4 _426 = internals[1];
    _426.x = dot(vec3(internals[1].x, internals[1].x, 0.0), vec3(internals[2].x, internals[2].y, 1.0));
    internals[1] = _426;
    vec4 _444 = sa[0];
    _444.w = dot(vec3(internals[0].x, internals[0].x, 0.0), vec3(internals[2].x, internals[2].y, 1.0));
    sa[0] = _444;
    vec4 _454 = sa[1];
    _454.x = uintBitsToFloat(floatBitsToUint(sa[0].w));
    sa[1] = _454;
    vec4 _469 = sa[1];
    _469.y = fma(1.0, sa[13].x, 0.0 - internals[1].x);
    sa[1] = _469;
    vec2 _507 = vec2(uintBitsToFloat((floatBitsToUint(sa[4].x) & 4294901760u) | (floatBitsToUint(pack2xF16(vec2(sa[4].x, 0.0))) & 65535u)), sa[4].y);
    sa[4] = vec4(_507.x, _507.y, sa[4].z, sa[4].w);
    vec4 _513 = sa[10];
    _513.z = 1.0;
    sa[10] = _513;
    vec4 _516 = internals[0];
    _516.x = 1.0;
    internals[0] = _516;
    float _522 = 0.0 - 1.0;
    vec4 _526 = sa[3];
    _526.x = fma(internals[0].x, _522, 0.0);
    sa[3] = _526;
    vec2 _534 = vec2(pack2xF16(vec2(vec2(0.0).x, vec2(0.0).y)), sa[2].y);
    sa[2] = vec4(_534.x, _534.y, sa[2].z, sa[2].w);
    vec2 _565 = vec2(vec2(sa[6].x, sa[6].y).y, vec2(sa[6].z, sa[6].w).x);
    sa[0] = vec4(_565.x, _565.y, sa[0].z, sa[0].w);
    vec4 _578 = sa[3];
    _578.y = uintBitsToFloat(floatBitsToUint(sa[10].z));
    sa[3] = _578;
}

vec2 unpack2xF16(float _152)
{
    return unpackHalf2x16(floatBitsToUint(_152));
}

void primary_program()
{
    vec4 _600 = r[0];
    _600.x = fma(pa[0].y, sa[5].x, sa[5].y);
    r[0] = _600;
    vec4 _620 = internals[0];
    _620.x = fma(r[0].x, pa[0].y, sa[5].z);
    internals[0] = _620;
    vec4 _639 = internals[0];
    _639.x = fma(internals[0].x, pa[0].y, sa[5].w);
    internals[0] = _639;
    vec2 _646 = vec2(pa[0].y, pa[0].x);
    r[0] = vec4(_646.x, _646.y, r[0].z, r[0].w);
    vec2 _671 = fma(vec2(r[0].x, r[0].y), vec2(sa[13].y, sa[13].y), vec2(sa[12].w, sa[12].w));
    r[0] = vec4(r[0].x, r[0].y, _671.x, _671.y);
    vec4 _684 = r[0];
    _684.x = uintBitsToFloat(floatBitsToUint(r[0].z));
    r[0] = _684;
    vec4 _698 = r[0];
    _698.y = fma(internals[0].x, sa[13].y, 0.0);
    r[0] = _698;
    vec2 _713 = vec2(sa[6].y, sa[6].z) * vec2(r[0].x, r[0].y);
    r[2] = vec4(_713.x, _713.y, r[2].z, r[2].w);
    vec4 _730 = r[1];
    _730.z = r[0].w * sa[6].x;
    r[1] = _730;
    vec4 _749 = internals[0];
    _749.x = fma(r[2].y, sa[2].z, sa[3].z);
    internals[0] = _749;
    vec4 _768 = internals[0];
    _768.x = fma(r[2].x, sa[1].z, internals[0].x);
    internals[0] = _768;
    vec4 _787 = internals[0];
    _787.x = fma(r[1].z, sa[0].z, internals[0].x);
    internals[0] = _787;
    vec4 _807 = internals[0];
    _807.x = fma(internals[0].x, sa[7].z, sa[7].w);
    internals[0] = _807;
    vec4 _827 = r[1];
    _827.x = fma(internals[0].x, sa[7].x, sa[13].x);
    r[1] = _827;
    vec4 _842 = internals[0];
    _842.x = pa[0].x + sa[10].y;
    internals[0] = _842;
    vec4 _861 = pa[0];
    _861.x = fma(r[1].x, internals[0].x, sa[13].z);
    pa[0] = _861;
    vec2 _876 = vec2(pa[0].x, pa[0].y) + vec2(sa[10].x, sa[10].x);
    r[0] = vec4(_876.x, _876.y, r[0].z, r[0].w);
    vec2 _894 = vec2(sa[1].x, sa[1].y) * vec2(r[0].x, r[0].x);
    r[0] = vec4(r[0].x, r[0].y, _894.x, _894.y);
    vec4 _917 = r[0];
    _917.x = fma(r[0].y, 0.0 - sa[0].w, r[0].w);
    r[0] = _917;
    vec4 _936 = r[0];
    _936.y = fma(r[0].y, sa[1].y, r[0].z);
    r[0] = _936;
    vec2 _952 = fma(vec2(r[0].x, r[0].y), vec2(sa[4].z, sa[4].z), vec2(0.5));
    r[0] = vec4(_952.x, _952.y, r[0].z, r[0].w);
    vec2 _962 = vec2(r[0].x, r[0].y);
    bool _980 = (((uint(renderFragInfo.cast_sampler_mask) >> 7u) & 1u) != 0u) && (renderFragInfo.res_multiplier != 1.0);
    vec2 _984 = gl_FragCoord.xy * vec2(renderFragInfo.inv_frag_width, renderFragInfo.inv_frag_height);
    vec2 _985 = _962 - _984;
    vec2 _988 = _984 + (_985 / vec2(renderFragInfo.res_multiplier));
    vec2 _991 = abs(_985);
    float _992 = _962.x;
    float _996 = _991.x;
    float _1006 = float(textureSize(fragTex_HeightMap, 0).x);
    vec4 _1023 = texture(fragTex_HeightMap, vec2(_980 ? ((_996 > 4.9999998736893758177757263183594e-05) ? ((_996 <= 0.004999999888241291046142578125) ? _988.x : _992) : ((((floor((_992 * _1006) * 0.5) * 2.0) + float((uint(renderFragInfo.cast_phase_mask) >> 7u) & 1u)) + 0.5) / _1006)) : _992, ((_991.y <= 0.004999999888241291046142578125) && _980) ? _988.y : _962.y));
    vec4 _1033 = round(_1023 * 65535.0);
    vec4 _1034 = vec4(0.0);
    uvec4 _1039 = uvec4(mix(clamp(_1033, _1034, vec4(65535.0)), _1034, isnan(_1033)));
    mediump vec4 _1052 = mix(_1023, vec4(uintBitsToFloat(_1039.x | (_1039.y << 16u)), uintBitsToFloat(_1039.z | (_1039.w << 16u)), 0.0, 0.0), bvec4(((uint(renderFragInfo.raw_cast_mask) >> 7u) & 1u) != 0u));
    vec2 _1061 = vec2(pack2xF16(vec2(_1052.x, _1052.y)), pack2xF16(vec2(_1052.z, _1052.w)));
    r[0] = vec4(_1061.x, _1061.y, r[0].z, r[0].w);
    vec2 _1070 = unpack2xF16(r[0].y);
    vec2 _1080 = unpack2xF16(sa[9].w);
    vec2 _1089 = unpack2xF16(sa[9].z);
    vec2 _1093 = fma(vec2(_1070.y, _1070.y), vec2(_1080.x, _1080.x), vec2(_1089.x, _1089.x));
    vec2 _1101 = vec2(pack2xF16(vec2(_1093.x, _1093.y)), pa[0].y);
    pa[0] = vec4(_1101.x, _1101.y, pa[0].z, pa[0].w);
    vec2 _1134 = vec2(uintBitsToFloat((floatBitsToUint(r[0].x) & 4294901760u) | (floatBitsToUint(pack2xF16(vec2(unpack2xF16(sa[4].x).x * unpack2xF16(pa[0].x).x, 0.0))) & 65535u)), r[0].y);
    r[0] = vec4(_1134.x, _1134.y, r[0].z, r[0].w);
    vec4 _1152 = internals[1];
    _1152.x = sa[9].y + pa[0].y;
    internals[1] = _1152;
    vec4 _1171 = internals[0];
    _1171.x = fma(internals[1].x, sa[5].x, sa[5].y);
    internals[0] = _1171;
    vec4 _1190 = internals[0];
    _1190.x = fma(internals[0].x, internals[1].x, sa[5].z);
    internals[0] = _1190;
    vec4 _1209 = internals[1];
    _1209.x = fma(internals[0].x, internals[1].x, sa[5].w);
    internals[1] = _1209;
    vec4 _1228 = internals[0];
    _1228.x = fma(pa[0].y, sa[13].y, sa[9].x);
    internals[0] = _1228;
    vec4 _1242 = pa[0];
    _1242.x = fma(1.0, sa[12].w, internals[0].x);
    pa[0] = _1242;
    vec4 _1256 = pa[0];
    _1256.y = fma(internals[1].x, sa[13].y, 0.0);
    pa[0] = _1256;
    vec2 _1279 = fma(vec2(pa[0].x, pa[0].y), vec2(sa[0].x, sa[0].y), vec2(0.0) - vec2(r[2].x, r[2].y));
    pa[0] = vec4(_1279.x, _1279.y, pa[0].z, pa[0].w);
    vec2 _1297 = vec2(pa[0].y, pa[0].x) * vec2(sa[3].x, sa[3].y);
    internals[1] = vec4(_1297.x, _1297.y, internals[1].z, internals[1].w);
    vec4 _1318 = internals[0];
    _1318.x = dot(vec3(internals[1].x, internals[1].y, 0.0), vec3(internals[1].x, internals[1].y, 1.0));
    internals[0] = _1318;
    vec4 _1327 = internals[2];
    _1327.x = inversesqrt(internals[0].x);
    internals[2] = _1327;
    vec4 _1337 = pa[0];
    _1337.x = uintBitsToFloat(floatBitsToUint(sa[2].x));
    pa[0] = _1337;
    vec2 _1352 = vec2(internals[1].x, internals[1].y) * vec2(internals[2].x, internals[2].x);
    vec2 _1377 = vec2(uintBitsToFloat((floatBitsToUint(pa[0].x) & 65535u) | (floatBitsToUint(pack2xF16(vec2(0.0, _1352.x))) & 4294901760u)), uintBitsToFloat((floatBitsToUint(pa[0].y) & 4294901760u) | (floatBitsToUint(pack2xF16(vec2(_1352.y, 0.0))) & 65535u)));
    pa[0] = vec4(_1377.x, _1377.y, pa[0].z, pa[0].w);
    vec4 _1394 = internals[1];
    _1394.x = r[1].z * r[1].x;
    internals[1] = _1394;
    vec2 _1401 = vec2(r[2].x, r[2].y);
    internals[1] = vec4(internals[1].x, _1401.x, _1401.y, internals[1].w);
    vec2 _1410 = unpack2xF16(r[0].x);
    vec2 _1422 = vec2(pa[0].x, pa[0].y);
    vec2 _1424 = unpack2xF16(_1422.x);
    vec2 _1426 = unpack2xF16(_1422.y);
    vec4 _1436 = fma(vec4(_1410.x, _1410.x, _1410.x, _1410.x), vec4(_1424.x, _1424.y, _1426.x, _1426.y), internals[1]);
    vec2 _1445 = vec2(pack2xF16(vec2(_1436.x, _1436.y)), pack2xF16(vec2(_1436.z, _1436.w)));
    pa[0] = vec4(_1445.x, _1445.y, pa[0].z, pa[0].w);
    vec4 _1469 = pa[0];
    _1469.y = uintBitsToFloat((floatBitsToUint(pa[0].y) & 65535u) | (floatBitsToUint(pack2xF16(vec2(0.0, unpack2xF16(r[0].x).x))) & 4294901760u));
    pa[0] = _1469;
}

void frag_output_finalize()
{
    vec2 _149 = vec2(pa[0].x, pa[0].y);
    vec2 _158 = unpack2xF16(_149.x);
    vec2 _160 = unpack2xF16(_149.y);
    out_color = vec4(_158.x, _158.y, _160.x, _160.y);
}

void main()
{
    int i = 0;
    while (i < 8)
    {
        sa[i + 0] = fragmentData.buffer0[i];
        i++;
    }
    sa[9] = vec4(0.0199999995529651641845703125, 0.00999999977648258209228515625, 6.7441692491024795989417257384577e-41, 2.2958874039497802890014385492622e-41);
    sa[10] = vec4(-0.5, -0.5, 0.125, 0.00260416674427688121795654296875);
    sa[11] = vec4(-2.1701389414374716579914093017578e-05, 0.02083333395421504974365234375, 0.00026041668024845421314239501953125, -1.5500992276429315097630023956299e-06);
    sa[12] = vec4(-3.1415927410125732421875, 6.283185482025146484375, 0.15915493667125701904296875, -1.0);
    sa[13] = vec4(vec3(1.0, 2.0, 0.5).x, vec3(1.0, 2.0, 0.5).y, vec3(1.0, 2.0, 0.5).z, sa[13].w);
    vec4 _180 = mix(vec4(0.0, 0.0, 0.0, 1.0), v_TexCoord0, bvec4((uint(renderFragInfo.iterator_written_mask) & 16u) != 0u));
    vec2 _183 = vec2(_180.x, _180.y);
    pa[0] = vec4(_183.x, _183.y, pa[0].z, pa[0].w);
    frag_init();
    secondary_program();
    primary_program();
    frag_output_finalize();
}


)WAVEPOSFRAG";
} // namespace emucorev::paf::detail