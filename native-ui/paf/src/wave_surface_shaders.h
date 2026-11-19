#pragma once
#include <string_view>
namespace emucorev::paf::detail {
inline constexpr std::string_view wave_surface_vertex_shader = R"WAVESURFACEVERT(
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

const bool _31[4] = bool[](false, false, false, false);
const int _34[2] = int[](0, 0);

layout(binding = 0, std430) restrict readonly buffer vertexDataType
{
    vec4 buffer0[9];
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

uniform highp sampler2D vertTex_PosTex;

layout(location = 0) in vec4 vin_uv;
layout(location = 4) out vec4 v_TexCoord0;
vec4 pa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 sa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 internals[3] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0));
vec4 r[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 outs[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));

void secondary_program()
{
    vec4 _190 = sa[10];
    _190.z = 0.0;
    sa[10] = _190;
}

void primary_program()
{
    vec4 _209 = pa[0];
    _209.y = (0.0 - pa[0].y) + sa[10].x;
    pa[0] = _209;
    vec2 _225 = (vec2(0.0) - vec2(sa[8].x, sa[8].y)) + vec2(pa[0].x, pa[0].y);
    outs[1] = vec4(_225.x, _225.y, outs[1].z, outs[1].w);
    r[0] = textureLod(vertTex_PosTex, vec2(outs[1].x, outs[1].y), sa[10].z);
    vec2 _268 = fma(vec2(r[0].z, r[0].z), vec2(sa[6].x, sa[6].y), vec2(sa[7].x, sa[7].y));
    internals[0] = vec4(_268.x, _268.y, internals[0].z, internals[0].w);
    vec4 _292 = internals[0];
    _292.z = fma(r[0].z, sa[6].z, sa[7].z);
    internals[0] = _292;
    vec4 _300 = internals[1];
    _300.x = r[0].y;
    internals[1] = _300;
    vec3 _325 = fma(vec3(internals[1].x, internals[1].x, internals[1].x), vec3(sa[5].x, sa[5].y, sa[5].z), vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[0] = vec4(_325.x, _325.y, _325.z, internals[0].w);
    vec4 _336 = internals[1];
    _336.x = r[0].x;
    internals[1] = _336;
    vec3 _361 = fma(vec3(internals[1].x, internals[1].x, internals[1].x), vec3(sa[4].x, sa[4].y, sa[4].z), vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[0] = vec4(_361.x, _361.y, _361.z, internals[0].w);
    vec3 _382 = fma(vec3(internals[0].x, internals[0].y, internals[0].z), vec3(sa[8].z, sa[8].z, sa[8].z), vec3(0.0));
    internals[0] = vec4(_382.x, _382.y, _382.z, internals[0].w);
    vec2 _396 = vec2(sa[3].x, sa[3].y);
    vec2 _403 = vec2(sa[3].z, sa[3].w);
    internals[1] = vec4(_396.x, _396.y, _403.x, _403.y);
    internals[1] = fma(vec4(internals[0].z, internals[0].z, internals[0].z, internals[0].z), sa[2], internals[1]);
    internals[1] = fma(vec4(internals[0].y, internals[0].y, internals[0].y, internals[0].y), sa[1], internals[1]);
    vec2 _469 = fma(vec2(internals[0].x, internals[0].x), vec2(sa[0].x, sa[0].y), vec2(internals[1].x, internals[1].y));
    outs[0] = vec4(_469.x, _469.y, outs[0].z, outs[0].w);
    vec2 _494 = fma(vec2(internals[0].x, internals[0].x), vec2(sa[0].z, sa[0].w), vec2(internals[1].z, internals[1].w));
    outs[0] = vec4(outs[0].x, outs[0].y, _494.x, _494.y);
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
    while (i < 9)
    {
        sa[i + 0] = vertexData.buffer0[i];
        i++;
    }
    vec4 _85 = sa[10];
    _85.x = 1.0;
    sa[10] = _85;
    pa[0] = vin_uv;
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
    secondary_program();
    primary_program();
    vert_output_finalize();
}


)WAVESURFACEVERT";
inline constexpr std::string_view wave_surface_fragment_shader = R"WAVESURFACEFRAG(
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

const vec4 _21[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
const vec4 _25[3] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0));
const vec4 _28[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
const bool _31[4] = bool[](false, false, false, false);
const int _34[2] = int[](0, 0);

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

uniform mediump sampler2D fragTex_LitTex;

layout(location = 4) in vec4 v_TexCoord0;
layout(location = 0) out mediump vec4 out_color;
vec4 pa[32] = _21;

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

float pack2xF16(vec2 _226)
{
    return uintBitsToFloat(packHalf2x16(mix(clamp(_226, vec2(-65504.0), vec2(65504.0)), _226, isnan(_226))));
}

void primary_program()
{
}

vec2 unpack2xF16(float _92)
{
    return unpackHalf2x16(floatBitsToUint(_92));
}

void frag_output_finalize()
{
    vec2 _89 = vec2(pa[0].x, pa[0].y);
    vec2 _98 = unpack2xF16(_89.x);
    vec2 _100 = unpack2xF16(_89.y);
    out_color = vec4(_98.x, _98.y, _100.x, _100.y);
}

void main()
{
    frag_init();
    bool _145 = (((uint(renderFragInfo.cast_sampler_mask) >> 0u) & 1u) != 0u) && (renderFragInfo.res_multiplier != 1.0);
    vec2 _149 = gl_FragCoord.xy * vec2(renderFragInfo.inv_frag_width, renderFragInfo.inv_frag_height);
    vec2 _150 = v_TexCoord0.xy - _149;
    vec2 _153 = _149 + (_150 / vec2(renderFragInfo.res_multiplier));
    vec2 _156 = abs(_150);
    float _157 = v_TexCoord0.xy.x;
    float _161 = _156.x;
    float _171 = float(textureSize(fragTex_LitTex, 0).x);
    vec2 _187 = vec2(_145 ? ((_161 > 4.9999998736893758177757263183594e-05) ? ((_161 <= 0.004999999888241291046142578125) ? _153.x : _157) : ((((floor((_157 * _171) * 0.5) * 2.0) + float((uint(renderFragInfo.cast_phase_mask) >> 0u) & 1u)) + 0.5) / _171)) : _157, ((_156.y <= 0.004999999888241291046142578125) && _145) ? _153.y : v_TexCoord0.xy.y);
    mediump vec4 _189 = texture(fragTex_LitTex, vec4(_187.x, _187.y, v_TexCoord0.z, v_TexCoord0.w).xy);
    vec4 hp_copy_189 = _189;
    vec4 _200 = round(hp_copy_189 * 65535.0);
    vec4 _201 = vec4(0.0);
    uvec4 _207 = uvec4(mix(clamp(_200, _201, vec4(65535.0)), _201, isnan(_200)));
    mediump vec4 _221 = mix(_189, vec4(uintBitsToFloat(_207.x | (_207.y << 16u)), uintBitsToFloat(_207.z | (_207.w << 16u)), 0.0, 0.0), bvec4(((uint(renderFragInfo.raw_cast_mask) >> 0u) & 1u) != 0u));
    vec2 _244 = vec2(pack2xF16(vec2(_221.x, _221.y)), pack2xF16(vec2(_221.z, _221.w)));
    pa[0] = vec4(_244.x, _244.y, pa[0].z, pa[0].w);
    primary_program();
    frag_output_finalize();
}


)WAVESURFACEFRAG";
} // namespace emucorev::paf::detail