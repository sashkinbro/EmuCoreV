#pragma once
#include <string_view>
namespace emucorev::paf::detail {
inline constexpr std::string_view wave_skydome_vertex_shader = R"WAVESKYVERT(
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
    vec4 buffer0[22];
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

layout(binding = 16) uniform highp sampler2D vertTex_WaveNorTex;

layout(location = 0) in vec4 IN_pos;
layout(location = 4) out vec4 v_TexCoord0;
vec4 pa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 sa[32] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 internals[3] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0));
vec4 r[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));
vec4 outs[20] = vec4[](vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0), vec4(0.0));

void secondary_program()
{
    vec4 _245 = sa[11];
    _245.z = 0.0;
    sa[11] = _245;
    sa[4] = vec4(sa[4].x, sa[4].y, sa[4].z, vec2(1.0 / sa[13].x).x);
    vec4 _273 = sa[11];
    _273.x = (0.0 - sa[12].w) + sa[23].y;
    sa[11] = _273;
}

void primary_program()
{
    vec2 _291 = vec2(1.0, sa[24].z) * vec2(pa[0].x, pa[0].y);
    outs[0] = vec4(_291.x, _291.y, outs[0].z, outs[0].w);
    vec4 _309 = textureLod(vertTex_WaveNorTex, vec2(outs[0].x, outs[0].y), sa[11].z);
    r[0] = vec4(r[0].x, r[0].y, _309.x, _309.y);
    r[1] = vec4(_309.z, _309.w, r[1].z, r[1].w);
    vec4 _329 = internals[0];
    _329.x = sa[4].w * pa[0].y;
    internals[0] = _329;
    vec4 _345 = internals[0];
    _345.x = min(internals[0].x, sa[26].z);
    internals[0] = _345;
    vec4 _354 = internals[1];
    _354.x = max(internals[0].x, 0.0);
    internals[1] = _354;
    vec4 _368 = internals[0];
    _368.x = fma(internals[1].x, internals[1].x, 0.0);
    internals[0] = _368;
    vec4 _383 = internals[1];
    vec4 _388 = internals[0];
    vec4 _394 = internals[1];
    _394.x = sa[24].y * internals[0].x;
    internals[1] = _394;
    vec4 _397 = r[0];
    _397.x = _383.x * _388.x;
    r[0] = _397;
    vec4 _417 = internals[0];
    _417.x = fma(r[0].x, 0.0 - sa[24].z, internals[1].x);
    internals[0] = _417;
    vec4 _432 = internals[0];
    _432.x = fma(1.0, sa[26].z, 0.0 - internals[0].x);
    internals[0] = _432;
    vec4 _447 = r[2];
    _447.x = fma(1.0, sa[26].z, 0.0 - internals[0].x);
    r[2] = _447;
    vec4 _463 = internals[0];
    _463.x = (0.0 - sa[11].x) + sa[12].w;
    internals[0] = _463;
    vec4 _482 = internals[0];
    _482.x = fma(internals[0].x, pa[0].x, sa[11].x);
    internals[0] = _482;
    vec4 _502 = internals[0];
    _502.x = fma(internals[0].x, sa[26].x, sa[26].y);
    internals[0] = _502;
    vec4 _516 = internals[0];
    _516.x = fract(internals[0].x);
    internals[0] = _516;
    vec4 _536 = internals[1];
    _536.x = fma(internals[0].x, sa[25].w, sa[25].z);
    internals[1] = _536;
    vec4 _550 = internals[2];
    _550.x = fma(internals[1].x, internals[1].x, 0.0);
    internals[2] = _550;
    vec4 _570 = internals[0];
    _570.x = fma(internals[2].x, sa[25].y, sa[25].x);
    internals[0] = _570;
    vec4 _590 = internals[0];
    _590.x = fma(internals[0].x, internals[2].x, 0.0 - sa[24].w);
    internals[0] = _590;
    vec4 _609 = internals[0];
    _609.x = fma(internals[0].x, internals[2].x, sa[26].y);
    internals[0] = _609;
    vec4 _623 = internals[1];
    _623.x = fma(internals[1].x, internals[0].x, 0.0);
    internals[1] = _623;
    vec4 _644 = internals[0];
    _644.x = fma(internals[2].x, sa[24].x, sa[23].w);
    internals[0] = _644;
    vec4 _664 = internals[0];
    _664.x = fma(internals[0].x, internals[2].x, 0.0 - sa[23].z);
    internals[0] = _664;
    vec4 _683 = internals[0];
    _683.x = fma(internals[0].x, internals[2].x, sa[26].z);
    internals[0] = _683;
    vec2 _690 = vec2(internals[1].x, internals[1].x);
    internals[2] = vec4(_690.x, _690.y, internals[2].z, internals[2].w);
    vec4 _711 = outs[0];
    _711.x = dot(vec3(internals[1].x, internals[1].x, 0.0), vec3(internals[2].x, internals[2].y, 1.0));
    outs[0] = _711;
    vec4 _722 = outs[0];
    _722.y = uintBitsToFloat(floatBitsToUint(pa[0].y));
    outs[0] = _722;
    vec2 _738 = (vec2(0.0) - vec2(outs[0].y, outs[0].x)) + vec2(sa[26].z, sa[26].z);
    r[1] = vec4(r[1].x, r[1].y, _738.x, _738.y);
    vec4 _759 = internals[0];
    _759.x = dot(vec3(internals[0].x, internals[0].x, 0.0), vec3(internals[2].x, internals[2].y, 1.0));
    internals[0] = _759;
    vec4 _773 = internals[1];
    _773.x = pa[0].x + sa[23].x;
    internals[1] = _773;
    vec4 _794 = internals[1];
    _794.x = fma(internals[1].x, 0.0 - internals[1].x, sa[22].w);
    internals[1] = _794;
    vec4 _803 = internals[1];
    _803.x = inversesqrt(internals[1].x);
    internals[1] = _803;
    vec4 _812 = internals[1];
    _812.x = 1.0 / internals[1].x;
    internals[1] = _812;
    vec4 _827 = internals[1];
    _827.x = (0.0 - sa[13].y) * internals[1].x;
    internals[1] = _827;
    vec4 _846 = internals[2];
    _846.x = fma(internals[1].x, r[1].z, sa[12].z);
    internals[2] = _846;
    vec4 _861 = internals[1];
    _861.x = (0.0 - sa[12].z) + sa[22].z;
    internals[1] = _861;
    vec4 _880 = internals[1];
    _880.x = fma(internals[1].x, pa[0].y, internals[2].x);
    internals[1] = _880;
    vec4 _899 = internals[1];
    _899.x = fma(internals[1].x, sa[26].x, sa[26].y);
    internals[1] = _899;
    vec4 _913 = internals[1];
    _913.x = fract(internals[1].x);
    internals[1] = _913;
    vec4 _932 = internals[2];
    _932.x = fma(internals[1].x, sa[25].w, sa[25].z);
    internals[2] = _932;
    vec4 _946 = pa[0];
    _946.x = fma(internals[2].x, internals[2].x, 0.0);
    pa[0] = _946;
    vec4 _965 = internals[1];
    _965.x = fma(pa[0].x, sa[25].y, sa[25].x);
    internals[1] = _965;
    vec4 _985 = internals[1];
    _985.x = fma(internals[1].x, pa[0].x, 0.0 - sa[24].w);
    internals[1] = _985;
    vec4 _1004 = internals[1];
    _1004.x = fma(internals[1].x, pa[0].x, sa[26].y);
    internals[1] = _1004;
    vec4 _1018 = internals[2];
    _1018.x = fma(internals[2].x, internals[1].x, 0.0);
    internals[2] = _1018;
    vec4 _1037 = internals[1];
    _1037.x = fma(pa[0].x, sa[24].x, sa[23].w);
    internals[1] = _1037;
    vec4 _1057 = internals[1];
    _1057.x = fma(internals[1].x, pa[0].x, 0.0 - sa[23].z);
    internals[1] = _1057;
    vec4 _1076 = r[0];
    _1076.x = fma(internals[1].x, pa[0].x, sa[26].z);
    r[0] = _1076;
    vec2 _1094 = vec2(internals[2].x, internals[2].x);
    vec4 _1097 = pa[0];
    _1097.x = r[0].x * internals[2].x;
    pa[0] = _1097;
    internals[1] = vec4(_1094.x, _1094.y, internals[1].z, internals[1].w);
    vec4 _1118 = internals[1];
    _1118.x = dot(vec3(internals[2].x, internals[2].x, 0.0), vec3(internals[1].x, internals[1].y, 1.0));
    internals[1] = _1118;
    vec4 _1133 = internals[1];
    _1133.x = fma(1.0, sa[26].z, 0.0 - internals[1].x);
    internals[1] = _1133;
    vec4 _1152 = internals[1];
    _1152.y = fma(r[0].x, internals[2].x, pa[0].x);
    internals[1] = _1152;
    vec2 _1167 = vec2(internals[0].x, internals[0].x) * vec2(internals[1].x, internals[1].y);
    outs[0] = vec4(_1167.x, _1167.y, outs[0].z, outs[0].w);
    vec4 _1178 = internals[1];
    _1178.x = r[0].w;
    internals[1] = _1178;
    vec3 _1195 = vec3(sa[8].x, sa[8].y, sa[8].z) * vec3(r[0].z, r[0].z, r[0].z);
    internals[2] = vec4(_1195.x, _1195.y, _1195.z, internals[2].w);
    vec3 _1224 = fma(vec3(internals[1].x, internals[1].x, internals[1].x), vec3(sa[9].x, sa[9].y, sa[9].z), vec3(internals[2].x, internals[2].y, internals[2].z));
    internals[1] = vec4(_1224.x, _1224.y, _1224.z, internals[1].w);
    vec4 _1235 = internals[2];
    _1235.x = r[1].x;
    internals[2] = _1235;
    vec3 _1261 = fma(vec3(internals[2].x, internals[2].x, internals[2].x), vec3(sa[10].x, sa[10].y, sa[10].z), vec3(internals[1].x, internals[1].y, internals[1].z));
    internals[1] = vec4(_1261.x, _1261.y, _1261.z, internals[1].w);
    vec3 _1281 = fma(vec3(internals[1].x, internals[1].y, internals[1].z), vec3(r[1].z, r[1].z, r[1].z), vec3(0.0));
    internals[1] = vec4(_1281.x, _1281.y, _1281.z, internals[1].w);
    vec2 _1300 = vec2(sa[7].x, sa[7].y);
    vec3 _1312 = vec3(_1300.x, _1300.y, vec2(sa[7].z, sa[7].w).x);
    internals[2] = vec4(_1312.x, _1312.y, _1312.z, internals[2].w);
    vec3 _1342 = (vec3(sa[20].z, sa[20].z, sa[20].z) * vec3(internals[1].x, internals[1].y, internals[1].z)) + vec3(internals[2].x, internals[2].y, internals[2].z);
    vec4 _1345 = internals[1];
    vec2 _1349 = vec2(_1345.z, _1345.z);
    internals[2] = vec4(_1342.x, _1342.y, _1342.z, internals[2].w);
    r[0] = vec4(_1349.x, _1349.y, r[0].z, r[0].w);
    vec2 _1370 = vec2(sa[6].x, sa[6].y);
    vec3 _1381 = vec3(_1370.x, _1370.y, vec2(sa[6].z, sa[6].w).x);
    internals[0] = vec4(_1381.x, _1381.y, _1381.z, internals[0].w);
    vec2 _1391 = vec2(outs[0].x, outs[0].y);
    internals[1] = vec4(_1391.x, _1391.y, internals[1].z, internals[1].w);
    vec3 _1420 = fma(vec3(internals[1].x, internals[1].x, internals[1].x), vec3(0.0) - vec3(internals[0].x, internals[0].y, internals[0].z), vec3(internals[2].x, internals[2].y, internals[2].z));
    internals[0] = vec4(_1420.x, _1420.y, _1420.z, internals[0].w);
    vec3 _1448 = fma(vec3(internals[1].y, internals[1].y, internals[1].y), vec3(sa[5].x, sa[5].y, sa[5].z), vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[0] = vec4(_1448.x, _1448.y, _1448.z, internals[0].w);
    vec2 _1473 = fma(vec2(r[1].w, r[1].w), vec2(sa[4].x, sa[4].y), vec2(internals[0].x, internals[0].y));
    outs[0] = vec4(_1473.x, _1473.y, outs[0].z, outs[0].w);
    vec4 _1495 = outs[0];
    _1495.z = fma(r[1].w, sa[4].z, internals[0].z);
    outs[0] = _1495;
    vec2 _1511 = vec2(sa[13].z, sa[13].w);
    vec3 _1523 = vec3(_1511.x, _1511.y, vec2(sa[14].x, sa[14].y).x);
    internals[1] = vec4(_1523.x, _1523.y, _1523.z, internals[1].w);
    vec2 _1543 = (vec2(0.0) - vec2(outs[0].x, outs[0].y)) + vec2(sa[14].z, sa[14].w);
    outs[1] = vec4(_1543.x, _1543.y, outs[1].z, outs[1].w);
    vec4 _1562 = outs[1];
    _1562.z = (0.0 - outs[0].z) + sa[15].x;
    outs[1] = _1562;
    vec4 _1578 = internals[0];
    _1578.x = (0.0 - sa[20].x) + sa[20].y;
    internals[0] = _1578;
    vec3 _1596 = (vec3(0.0) - vec3(outs[0].x, outs[0].y, outs[0].z)) + vec3(internals[1].x, internals[1].y, internals[1].z);
    vec4 _1599 = internals[0];
    internals[1] = vec4(_1596.x, _1596.y, _1596.z, internals[1].w);
    vec4 _1608 = pa[0];
    _1608.x = 1.0 / _1599.x;
    pa[0] = _1608;
    vec4 _1627 = internals[0];
    _1627.x = fma(r[0].x, sa[20].w, pa[0].y);
    internals[0] = _1627;
    vec4 _1641 = internals[0];
    _1641.x = fma(internals[0].x, pa[0].x, 0.0);
    internals[0] = _1641;
    vec4 _1661 = internals[0];
    _1661.x = fma(pa[0].x, 0.0 - sa[20].x, internals[0].x);
    internals[0] = _1661;
    vec4 _1675 = internals[0];
    _1675.x = min(internals[0].x, sa[26].z);
    internals[0] = _1675;
    vec4 _1684 = r[1];
    _1684.x = max(internals[0].x, 0.0);
    r[1] = _1684;
    vec4 _1698 = r[1];
    _1698.z = r[1].x * r[1].x;
    r[1] = _1698;
    vec2 _1714 = vec2(sa[19].x, sa[19].y);
    vec3 _1726 = vec3(_1714.x, _1714.y, vec2(sa[19].z, sa[19].w).x);
    internals[0] = vec4(_1726.x, _1726.y, _1726.z, internals[0].w);
    vec3 _1749 = fma(vec3(1.0), vec3(sa[15].z, sa[15].w, sa[16].x), vec3(0.0) - vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[2] = vec4(_1749.x, _1749.y, _1749.z, internals[2].w);
    vec3 _1778 = fma(vec3(internals[2].x, internals[2].y, internals[2].z), vec3(sa[16].y, sa[16].y, sa[16].y), vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[0] = vec4(_1778.x, _1778.y, _1778.z, internals[0].w);
    vec3 _1799 = vec3(outs[0].x, outs[0].y, outs[0].z) * vec3(sa[12].x, sa[12].x, sa[12].x);
    internals[2] = vec4(_1799.x, _1799.y, _1799.z, internals[2].w);
    vec4 _1816 = r[0];
    _1816.z = r[2].x * r[2].x;
    r[0] = _1816;
    vec4 _1832 = pa[0];
    _1832.x = abs(r[0].x) * sa[21].x;
    pa[0] = _1832;
    vec4 _1846 = pa[0];
    _1846.x = min(pa[0].x, sa[26].z);
    pa[0] = _1846;
    vec4 _1855 = pa[0];
    _1855.x = max(pa[0].x, 0.0);
    pa[0] = _1855;
    vec4 _1875 = r[0];
    _1875.x = fma(r[0].z, 0.0 - pa[0].x, pa[0].x);
    r[0] = _1875;
    vec4 _1895 = pa[0];
    _1895.x = dot(vec3(internals[1].x, internals[1].y, internals[1].z), vec3(internals[1].x, internals[1].y, internals[1].z));
    pa[0] = _1895;
    vec4 _1904 = pa[0];
    _1904.x = inversesqrt(pa[0].x);
    pa[0] = _1904;
    vec2 _1919 = fma(vec2(internals[1].x, internals[1].y), vec2(pa[0].x, pa[0].x), vec2(0.0));
    outs[0] = vec4(_1919.x, _1919.y, outs[0].z, outs[0].w);
    vec4 _1936 = outs[0];
    _1936.z = internals[1].z * pa[0].x;
    outs[0] = _1936;
    vec2 _1951 = vec2(outs[1].x, outs[1].y);
    vec3 _1962 = vec3(_1951.x, _1951.y, vec2(outs[1].z, outs[1].w).x);
    internals[1] = vec4(_1962.x, _1962.y, _1962.z, internals[1].w);
    vec4 _1985 = internals[1];
    _1985.x = dot(vec3(internals[1].x, internals[1].y, internals[1].z), vec3(internals[1].x, internals[1].y, internals[1].z));
    internals[1] = _1985;
    vec4 _1994 = internals[1];
    _1994.x = inversesqrt(internals[1].x);
    internals[1] = _1994;
    vec3 _2011 = fma(vec3(internals[1].x, internals[1].x, internals[1].x), vec3(outs[1].x, outs[1].y, outs[1].z), vec3(0.0));
    internals[1] = vec4(_2011.x, _2011.y, _2011.z, internals[1].w);
    vec4 _2034 = internals[1];
    _2034.x = dot(vec3(outs[0].x, outs[0].y, outs[0].z), vec3(internals[1].x, internals[1].y, internals[1].z));
    internals[1] = _2034;
    vec4 _2043 = internals[1];
    _2043.x = max(internals[1].x, 0.0);
    internals[1] = _2043;
    vec4 _2052 = internals[1];
    _2052.x = log2(internals[1].x);
    internals[1] = _2052;
    vec4 _2067 = internals[1];
    _2067.x = fma(internals[1].x, sa[17].y, 0.0);
    internals[1] = _2067;
    float _2072 = internals[1].x;
    vec4 _2078 = internals[1];
    _2078.x = isnan(_2072) ? 1.0 : exp2(_2072);
    internals[1] = _2078;
    vec4 _2093 = internals[1];
    _2093.x = fma(internals[1].x, sa[17].z, 0.0);
    internals[1] = _2093;
    vec2 _2116 = fma(vec2(internals[1].x, internals[1].x), vec2(sa[16].z, sa[16].w), vec2(internals[0].x, internals[0].y));
    outs[0] = vec4(_2116.x, _2116.y, outs[0].z, outs[0].w);
    vec4 _2138 = outs[0];
    _2138.z = fma(internals[1].x, sa[17].x, internals[0].z);
    outs[0] = _2138;
    vec4 _2152 = pa[0];
    _2152.x = r[1].x * r[1].z;
    pa[0] = _2152;
    vec4 _2166 = internals[1];
    _2166.x = sa[24].y * r[1].z;
    internals[1] = _2166;
    vec4 _2186 = internals[1];
    _2186.x = fma(pa[0].x, 0.0 - sa[24].z, internals[1].x);
    internals[1] = _2186;
    vec3 _2205 = fma(vec3(1.0), vec3(sa[18].x, sa[18].y, sa[18].z), vec3(0.0) - vec3(internals[0].x, internals[0].y, internals[0].z));
    internals[0] = vec4(_2205.x, _2205.y, _2205.z, internals[0].w);
    vec2 _2230 = fma(vec2(internals[1].x, internals[1].x), vec2(internals[0].x, internals[0].y), vec2(outs[0].x, outs[0].y));
    outs[1] = vec4(_2230.x, _2230.y, outs[1].z, outs[1].w);
    vec4 _2252 = outs[1];
    _2252.z = fma(internals[1].x, internals[0].z, outs[0].z);
    outs[1] = _2252;
    vec4 _2272 = outs[1];
    _2272.w = fma(r[2].x, 0.0 - r[0].x, r[2].x);
    outs[1] = _2272;
    vec2 _2283 = vec2(sa[3].x, sa[3].y);
    vec2 _2290 = vec2(sa[3].z, sa[3].w);
    internals[0] = vec4(_2283.x, _2283.y, _2290.x, _2290.y);
    internals[0] = fma(vec4(internals[2].z, internals[2].z, internals[2].z, internals[2].z), sa[2], internals[0]);
    internals[0] = fma(vec4(internals[2].y, internals[2].y, internals[2].y, internals[2].y), sa[1], internals[0]);
    vec2 _2356 = fma(vec2(internals[2].x, internals[2].x), vec2(sa[0].x, sa[0].y), vec2(internals[0].x, internals[0].y));
    outs[0] = vec4(_2356.x, _2356.y, outs[0].z, outs[0].w);
    vec2 _2381 = fma(vec2(internals[2].x, internals[2].x), vec2(sa[0].z, sa[0].w), vec2(internals[0].z, internals[0].w));
    outs[0] = vec4(outs[0].x, outs[0].y, _2381.x, _2381.y);
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
    while (i < 21)
    {
        sa[i + 0] = vertexData.buffer0[i];
        i++;
    }
    sa[21].x = vertexData.buffer0[21].x;
    sa[21].y = vertexData.buffer0[21].y;
    sa[22] = vec4(sa[22].x, sa[22].y, vec4(1.57079637050628662109375, 0.25, -0.5, 3.1415927410125732421875).x, vec4(1.57079637050628662109375, 0.25, -0.5, 3.1415927410125732421875).y);
    sa[23] = vec4(vec4(1.57079637050628662109375, 0.25, -0.5, 3.1415927410125732421875).z, vec4(1.57079637050628662109375, 0.25, -0.5, 3.1415927410125732421875).w, sa[23].z, sa[23].w);
    sa[23] = vec4(sa[23].x, sa[23].y, vec4(0.125, 0.00260416674427688121795654296875, -2.1701389414374716579914093017578e-05, 3.0).x, vec4(0.125, 0.00260416674427688121795654296875, -2.1701389414374716579914093017578e-05, 3.0).y);
    sa[24] = vec4(vec4(0.125, 0.00260416674427688121795654296875, -2.1701389414374716579914093017578e-05, 3.0).z, vec4(0.125, 0.00260416674427688121795654296875, -2.1701389414374716579914093017578e-05, 3.0).w, sa[24].z, sa[24].w);
    sa[24] = vec4(sa[24].x, sa[24].y, vec4(2.0, 0.02083333395421504974365234375, 0.00026041668024845421314239501953125, -1.5500992276429315097630023956299e-06).x, vec4(2.0, 0.02083333395421504974365234375, 0.00026041668024845421314239501953125, -1.5500992276429315097630023956299e-06).y);
    sa[25] = vec4(vec4(2.0, 0.02083333395421504974365234375, 0.00026041668024845421314239501953125, -1.5500992276429315097630023956299e-06).z, vec4(2.0, 0.02083333395421504974365234375, 0.00026041668024845421314239501953125, -1.5500992276429315097630023956299e-06).w, sa[25].z, sa[25].w);
    sa[25] = vec4(sa[25].x, sa[25].y, vec4(-3.1415927410125732421875, 6.283185482025146484375, 0.15915493667125701904296875, 0.5).x, vec4(-3.1415927410125732421875, 6.283185482025146484375, 0.15915493667125701904296875, 0.5).y);
    sa[26] = vec4(vec4(-3.1415927410125732421875, 6.283185482025146484375, 0.15915493667125701904296875, 0.5).z, vec4(-3.1415927410125732421875, 6.283185482025146484375, 0.15915493667125701904296875, 0.5).w, sa[26].z, sa[26].w);
    vec4 _140 = sa[26];
    _140.z = 1.0;
    sa[26] = _140;
    pa[0] = IN_pos;
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


)WAVESKYVERT";
inline constexpr std::string_view wave_skydome_fragment_shader = R"WAVESKYFRAG(
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

layout(location = 4) in mediump vec4 v_TexCoord0;
layout(location = 0) out mediump vec4 out_color;
vec4 pa[32] = _21;

float pack2xF16(vec2 _121)
{
    return uintBitsToFloat(packHalf2x16(mix(clamp(_121, vec2(-65504.0), vec2(65504.0)), _121, isnan(_121))));
}

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

void primary_program()
{
}

vec2 unpack2xF16(float _86)
{
    return unpackHalf2x16(floatBitsToUint(_86));
}

void frag_output_finalize()
{
    vec2 _83 = vec2(pa[0].x, pa[0].y);
    vec2 _92 = unpack2xF16(_83.x);
    vec2 _94 = unpack2xF16(_83.y);
    out_color = vec4(_92.x, _92.y, _94.x, _94.y);
}

void main()
{
    vec4 _116 = mix(vec4(0.0, 0.0, 0.0, 1.0), v_TexCoord0, bvec4((uint(renderFragInfo.iterator_written_mask) & 16u) != 0u));
    vec2 _140 = vec2(pack2xF16(vec2(_116.x, _116.y)), pack2xF16(vec2(_116.z, _116.w)));
    pa[0] = vec4(_140.x, _140.y, pa[0].z, pa[0].w);
    frag_init();
    primary_program();
    frag_output_finalize();
}


)WAVESKYFRAG";
}
