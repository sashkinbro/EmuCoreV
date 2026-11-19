#include <emucorev/paf/page_turn_shaders.h>
#include <array>

namespace emucorev::paf {
std::string_view page_turn_empty_back_vertex_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=0, std140) uniform PageEmptyVertex { vec4 p[9]; } vertex_material;
layout(binding=2, std140) uniform PageViewport {
    vec4 viewport_flip;
    float viewport_flag, screen_width, screen_height, z_offset, z_scale;
};
layout(location=0) in vec4 point;
layout(location=4) out vec4 v_TexCoord0;
layout(location=5) out vec4 v_TexCoord1;
void main() {
    float exponent_scale=(0.0-vertex_material.p[4].z)*1.44269502162933349609375;
    float shadow_level=-1.0+vertex_material.p[4].w;
    vec3 position=vec3(vertex_material.p[6].xy,vertex_material.p[6].w)*vec3(point.xy,1.0);
    vec2 base_uv=vertex_material.p[8].zw*point.xy;
    vec4 clip=vertex_material.p[0]*vec4(position.x);
    clip=fma(vec4(position.y),vertex_material.p[1],clip);
    clip=fma(vec4(position.z),vertex_material.p[3],clip);
    vec2 relative=vec2(0.0)-vertex_material.p[7].xy+point.xy;
    float distance=dot(vec3(vertex_material.p[7].zw,0.0),vec3(relative,1.0));
    float exponent=fma(abs(distance),exponent_scale,0.0);
    float fade=isnan(exponent)?1.0:exp2(exponent);
    vec2 uv=fma(vec2(base_uv.x,(0.0-base_uv.y)+1.0),vertex_material.p[5].xy,vertex_material.p[5].zw);
    float shadow=fma(fade,shadow_level,1.0);
    vec2 corner_uv=vec2(fma(base_uv.x,2.0,-1.0),fma(base_uv.y,2.0,-1.0));
    v_TexCoord0=vec4(uv,shadow,corner_uv.x);
    v_TexCoord1=vec4(corner_uv,0.0,0.0);
    if(viewport_flag<.5) {
        gl_Position=((clip*(vec4(2.0,-2.0,2.0,2.0)/vec4(screen_width,screen_height,1.0,1.0)))
            +vec4(-1.0,1.0,0.0,0.0))*viewport_flip;
        gl_Position.z=gl_Position.w;
    } else {
        gl_Position=clip*viewport_flip;
        gl_Position.z=(((((gl_Position.z/gl_Position.w)*z_scale)+z_offset)*2.0)-1.0)*gl_Position.w;
    }
}
)glsl";
}
std::string_view page_turn_copy_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding=0) uniform highp sampler2D source_texture;
layout(location=4) in vec4 v_TexCoord0;
layout(location=0) out mediump vec4 out_color;
vec2 half_pair(vec2 value) {
    precise vec2 bounded=mix(clamp(value,vec2(-65504.0),vec2(65504.0)),value,isnan(value));
    return unpackHalf2x16(packHalf2x16(bounded));
}
void main() {
    if((front_disabled!=0.0 && gl_FrontFacing) || (back_disabled!=0.0 && !gl_FrontFacing)) discard;
    precise vec4 image=texture(source_texture,v_TexCoord0.xy);
    out_color=vec4(half_pair(image.xy),half_pair(image.zw));
}
)glsl";
}
std::string_view page_turn_quad_vertex_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=0, std140) uniform PageQuadVertex { vec4 p[5]; } vertex_material;
layout(binding=2, std140) uniform PageViewport {
    vec4 viewport_flip;
    float viewport_flag, screen_width, screen_height, z_offset, z_scale;
};
layout(location=0) in vec4 point;
layout(location=4) out vec4 v_TexCoord0;
void main() {
    precise vec4 base=fma(vec4(point.y),vertex_material.p[1],vertex_material.p[3]);
    precise vec4 clip=fma(vec4(point.x),vertex_material.p[0],base);
    precise vec2 uv=fma(point.zw,vertex_material.p[4].zw,vertex_material.p[4].xy);
    if(viewport_flag<.5) {
        gl_Position=((clip*(vec4(2.0,-2.0,2.0,2.0)/vec4(screen_width,screen_height,1.0,1.0)))
            +vec4(-1.0,1.0,0.0,0.0))*viewport_flip;
        gl_Position.z=gl_Position.w;
    } else {
        gl_Position=clip*viewport_flip;
        gl_Position.z=(((((gl_Position.z/gl_Position.w)*z_scale)+z_offset)*2.0)-1.0)*gl_Position.w;
    }
    v_TexCoord0=vec4(uv,0.0,0.0);
}
)glsl";
}
std::string_view page_turn_shadow_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=1, std140) uniform PageShadowMaterial { vec4 p[2]; } fragment_material;
layout(binding=3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding=0) uniform highp sampler2D shadow_texture;
layout(location=4) in mediump vec4 v_TexCoord0;
layout(location=0) out mediump vec4 out_color;
vec2 half_pair(vec2 value) {
    precise vec2 bounded=mix(clamp(value,vec2(-65504.0),vec2(65504.0)),value,isnan(value));
    return unpackHalf2x16(packHalf2x16(bounded));
}
vec4 half_color(vec4 value) { return vec4(half_pair(value.xy),half_pair(value.zw)); }
void main() {
    if((front_disabled!=0.0 && gl_FrontFacing) || (back_disabled!=0.0 && !gl_FrontFacing)) discard;
    precise vec2 border=unpackHalf2x16(floatBitsToUint(fragment_material.p[0].x));
    precise vec2 border_scale=unpackHalf2x16(floatBitsToUint(fragment_material.p[0].y));
    precise float level=unpackHalf2x16(floatBitsToUint(fragment_material.p[0].z)).x;
    precise vec4 color=half_color(fragment_material.p[1]);
    precise vec4 shaded=half_color(color*vec4(level));
    precise vec4 inverse=half_color(fma(color,vec4(0.0)-vec4(level),vec4(1.0)));
    precise vec2 incoming=half_pair((uint(iterator_written_mask)&16u)!=0u?v_TexCoord0.xy:vec2(0.0));
    precise vec2 uv=half_pair(border_scale*abs(incoming));
    uv=half_pair(fma(border_scale,vec2(0.0)-border,uv));
    uv=half_pair(min(uv,vec2(1.0)));
    uv=half_pair(max(uv,vec2(0.0)));
    highp vec4 sampled=texture(shadow_texture,uv);
    precise float weight=half_pair(sampled.xy).x;
    out_color=half_color(fma(vec4(weight),inverse,shaded));
}
)glsl";
}
std::string_view page_turn_top_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=1, std140) uniform PageTopMaterial { vec4 p[9]; } fragment_material;
layout(binding=3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding=0) uniform highp sampler2D page_texture;
layout(location=4) in vec4 v_TexCoord0;
layout(location=6) in mediump vec4 v_TexCoord2;
layout(location=0) out mediump vec4 out_color;
vec2 half_pair(vec2 value) {
    precise vec2 bounded=mix(clamp(value,vec2(-65504.0),vec2(65504.0)),value,isnan(value));
    return unpackHalf2x16(packHalf2x16(bounded));
}
vec4 half_color(vec4 value) {
    return vec4(half_pair(value.xy),half_pair(value.zw));
}
void main() {
    if((front_disabled!=0.0 && gl_FrontFacing) || (back_disabled!=0.0 && !gl_FrontFacing)) discard;
    precise vec4 lighting=half_color((uint(iterator_written_mask)&64u)!=0u
        ?v_TexCoord2:vec4(0.0,0.0,0.0,1.0));
    precise vec4 image=half_color(texture(page_texture,v_TexCoord0.xy));
    precise vec4 tint=half_color(vec4(fragment_material.p[0].zw,fragment_material.p[1].xy));
    precise float back_opacity=half_pair(vec2(fragment_material.p[0].x,0.0)).x;
    precise vec4 light_color=half_color(vec4(fragment_material.p[4].zw,fragment_material.p[5].xy));
    precise vec4 base=half_color(tint*image);
    precise vec4 back=half_color(fma(base,vec4(0.0)-vec4(back_opacity),vec4(back_opacity)));
    precise vec4 combined=half_color(fma(image,tint,back));
    precise vec4 lit=half_color(light_color+lighting);
    out_color=half_color(fma(combined,lit,vec4(lighting.w)));
}
)glsl";
}
std::string_view page_turn_top_round_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=1, std140) uniform PageTopRoundMaterial { vec4 p[10]; } fragment_material;
layout(binding=3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding=0) uniform highp sampler2D page_texture;
layout(binding=1) uniform highp sampler2D corner_texture;
layout(location=4) in vec4 v_TexCoord0;
layout(location=6) in mediump vec4 v_TexCoord2;
layout(location=5) in mediump vec4 v_TexCoord1;
layout(location=0) out mediump vec4 out_color;
vec2 half_pair(vec2 value) {
    precise vec2 bounded=mix(clamp(value,vec2(-65504.0),vec2(65504.0)),value,isnan(value));
    return unpackHalf2x16(packHalf2x16(bounded));
}
vec4 half_color(vec4 value) {
    return vec4(half_pair(value.xy),half_pair(value.zw));
}
void main() {
    if((front_disabled!=0.0 && gl_FrontFacing) || (back_disabled!=0.0 && !gl_FrontFacing)) discard;
    precise vec4 lighting=half_color((uint(iterator_written_mask)&64u)!=0u
        ?v_TexCoord2:vec4(0.0,0.0,0.0,1.0));
    precise vec4 image=half_color(texture(page_texture,v_TexCoord0.xy));
    precise vec2 corner_position=half_pair((uint(iterator_written_mask)&32u)!=0u?v_TexCoord1.xy:vec2(0.0));
    precise vec2 corner_uv=max(vec2(0.0)-fragment_material.p[0].xy+abs(corner_position),vec2(0.0))*fragment_material.p[0].zw;
    precise vec4 corner=half_color(texture(corner_texture,corner_uv));
    precise vec4 tint=half_color(vec4(fragment_material.p[1].zw,fragment_material.p[2].xy));
    precise float back_opacity=half_pair(vec2(fragment_material.p[1].x,0.0)).x;
    precise vec4 light_color=half_color(vec4(fragment_material.p[5].zw,fragment_material.p[6].xy));
    precise vec4 base=half_color(tint*image);
    precise vec4 back=half_color(fma(base,vec4(0.0)-vec4(back_opacity),vec4(back_opacity)));
    precise vec4 combined=half_color(fma(image,tint,back));
    precise vec4 lit=half_color(light_color+lighting);
    precise vec4 color=half_color(fma(combined,lit,vec4(lighting.w)));
    color.rgb=half_color(vec4(vec3(corner.g)+color.rgb,0.0)).rgb;
    color.a=half_pair(vec2(corner.r*color.a,0.0)).x;
    out_color=color;
}
)glsl";
}
namespace {
template<std::size_t A, std::size_t B>
consteval auto shader_source(const char (&prefix)[A], const char (&body)[B]) {
    std::array<char,A+B-1> result{};
    for(std::size_t i=0;i<A-1;++i) result[i]=prefix[i];
    for(std::size_t i=0;i<B;++i) result[A-1+i]=body[i];
    return result;
}
constexpr char bottom_vertex_body[]=R"glsl(precision highp float;
precision highp int;
layout(binding=0, std140) uniform PageBottomVertex { vec4 p[12]; } vertex_material;
layout(binding=2, std140) uniform PageViewport {
    vec4 viewport_flip;
    float viewport_flag, screen_width, screen_height, z_offset, z_scale;
};
layout(binding=4) uniform highp sampler2D vertTex_ExpMinusTexture;
layout(location=0) in vec4 point;
layout(location=4) out vec4 v_TexCoord0;
#ifdef ROUNDED_EDGE
layout(location=5) out vec4 v_TexCoord1;
#endif
void main() {
    vec2 gap = vec2(0.0) - vertex_material.p[8].xy + vertex_material.p[8].zw;
    float extent = 1.0 / inversesqrt(dot(vec3(gap,0.0),vec3(gap,1.0)));
    extent = max(min(extent,1.0),0.0);
    float scale = fma(extent,.8,.2);
    float curl = fma(scale,vertex_material.p[9].x,0.0);
    float range_start = curl * .5;
    float shadow_scale = vertex_material.p[4].x * vertex_material.p[7].y;
    vec2 relative = vec2(0.0) - vertex_material.p[10].xy + point.xy;
    float distance = dot(vec3(vertex_material.p[10].zw,0.0),vec3(relative,1.0));
    distance = min(distance,.5);
    float argument = fma(1.0,.5,0.0-distance);
    float curve = 1.0 / inversesqrt(max(fma(argument,0.0-argument,.25),0.0));
    float height = fma(curve,0.0-curl,range_start);
    vec4 position = vec4(vertex_material.p[6].xy*point.xy,fma(vec2(height,1.0),vertex_material.p[6].zw,vec2(0.0)));
    float lookup = fma(height,shadow_scale,0.0);
    float sampled = textureLod(vertTex_ExpMinusTexture,vec2(lookup,.5),0.0).x;
    float shadow = fma(sampled,0.0-vertex_material.p[4].y,vertex_material.p[4].y);
    vec2 uv = vertex_material.p[11].zw * point.xy;
#ifdef ROUNDED_EDGE
    vec2 corner_uv=fma(uv,vec2(2.0),vec2(-1.0));
#endif
    uv.y = (0.0-uv.y)+1.0;
    uv = fma(uv,vertex_material.p[5].xy,vertex_material.p[5].zw);
#ifdef ROUNDED_EDGE
    v_TexCoord0 = vec4(uv,shadow+sampled,corner_uv.x);
    v_TexCoord1 = vec4(corner_uv,0.0,0.0);
#else
    v_TexCoord0 = vec4(uv,shadow+sampled,0.0);
#endif
    vec4 clip = vertex_material.p[0] * vec4(position.x);
    clip = fma(vec4(position.y),vertex_material.p[1],clip);
    clip = fma(vec4(position.z),vertex_material.p[2],clip);
    clip = fma(vec4(position.w),vertex_material.p[3],clip);
    if(viewport_flag<.5) {
        gl_Position=((clip*(vec4(2.0,-2.0,2.0,2.0)/vec4(screen_width,screen_height,1.0,1.0)))
            +vec4(-1.0,1.0,0.0,0.0))*viewport_flip;
        gl_Position.z=gl_Position.w;
    } else {
        gl_Position=clip*viewport_flip;
        gl_Position.z=(((((gl_Position.z/gl_Position.w)*z_scale)+z_offset)*2.0)-1.0)*gl_Position.w;
    }
}
)glsl";
constexpr auto bottom_vertex=shader_source("#version 320 es\n",bottom_vertex_body);
constexpr auto bottom_round_vertex=shader_source("#version 320 es\n#define ROUNDED_EDGE\n",bottom_vertex_body);
}
std::string_view page_turn_bottom_vertex_shader() noexcept {
    return {bottom_vertex.data(),bottom_vertex.size()-1};
}
std::string_view page_turn_bottom_round_vertex_shader() noexcept {
    return {bottom_round_vertex.data(),bottom_round_vertex.size()-1};
}
std::string_view page_turn_bottom_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding=0) uniform highp sampler2D page_texture;
layout(location=4) in mediump vec4 v_TexCoord0;
layout(location=0) out mediump vec4 out_color;
vec2 half_pair(vec2 value) {
    vec2 bounded=mix(clamp(value,vec2(-65504.0),vec2(65504.0)),value,isnan(value));
    return unpackHalf2x16(packHalf2x16(bounded));
}
void main() {
    if((front_disabled!=0.0 && gl_FrontFacing) || (back_disabled!=0.0 && !gl_FrontFacing)) discard;
    float shadow=half_pair(vec2((uint(iterator_written_mask)&16u)!=0u?v_TexCoord0.z:0.0,0.0)).x;
    highp vec4 sampled=texture(page_texture,v_TexCoord0.xy);
    vec4 image=vec4(half_pair(sampled.xy),half_pair(sampled.zw));
    vec4 color=vec4(shadow,shadow,shadow,1.0)*image;
    out_color=vec4(half_pair(color.xy),half_pair(color.zw));
}
)glsl";
}
std::string_view page_turn_bottom_round_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=1, std140) uniform PageBottomRoundMaterial { vec4 p[2]; } fragment_material;
layout(binding=3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding=0) uniform highp sampler2D page_texture;
layout(binding=1) uniform highp sampler2D corner_texture;
layout(location=4) in mediump vec4 v_TexCoord0;
layout(location=5) in mediump vec4 v_TexCoord1;
layout(location=0) out mediump vec4 out_color;
vec2 half_pair(vec2 value) {
    precise vec2 bounded=mix(clamp(value,vec2(-65504.0),vec2(65504.0)),value,isnan(value));
    return unpackHalf2x16(packHalf2x16(bounded));
}
vec4 half_color(vec4 value) { return vec4(half_pair(value.xy),half_pair(value.zw)); }
void main() {
    if((front_disabled!=0.0 && gl_FrontFacing) || (back_disabled!=0.0 && !gl_FrontFacing)) discard;
    precise vec4 incoming=half_color((uint(iterator_written_mask)&16u)!=0u
        ?v_TexCoord0:vec4(0.0,0.0,0.0,1.0));
    precise vec2 corner_position=half_pair((uint(iterator_written_mask)&32u)!=0u?v_TexCoord1.xy:vec2(0.0));
    precise vec2 corner_uv=max(vec2(0.0)-fragment_material.p[0].xy+abs(corner_position),vec2(0.0))
        *fragment_material.p[0].zw;
    precise vec4 corner=texture(corner_texture,corner_uv);
    precise vec2 uv=fma(vec2(corner.g),fragment_material.p[1].xy,incoming.xy);
    precise vec4 image=half_color(texture(page_texture,uv));
    precise vec4 color=half_color(fma(image,vec4(incoming.z),vec4(corner.g)));
    color.a=half_pair(vec2(corner.r*image.a,0.0)).x;
    out_color=color;
}
)glsl";
}
namespace {
constexpr char top_vertex_body[]=R"glsl(precision highp float;
precision highp int;
layout(binding=0, std140) uniform PageTopVertex { vec4 p[28]; } vertex_material;
layout(binding=2, std140) uniform PageViewport {
    vec4 viewport_flip;
    float viewport_flag, screen_width, screen_height, z_offset, z_scale;
};
layout(location=0) in vec4 point;
layout(location=4) out vec4 v_TexCoord0;
layout(location=6) out vec4 v_TexCoord2;
#ifdef ROUNDED_EDGE
layout(location=5) out vec4 v_TexCoord1;
#endif
void main() {
    precise vec2 relative = vec2(0.0) - vertex_material.p[17].xy + point.xy;
    precise vec2 tangent = vertex_material.p[17].wz * vec2(-1.0, 1.0);
    precise float distance = dot(vec3(vertex_material.p[17].zw, 0.0), vec3(relative, 1.0));
    precise float inverse_extent = 1.0 / vertex_material.p[14].x;
    precise float rate = fma(inverse_extent, distance, 0.0);
    precise float curve_argument = fma(rate, .98, -.5);
    precise float curve_squared = max(fma(curve_argument, -curve_argument, .25), 0.0);
    precise float curve = 1.0 / inversesqrt(curve_squared);
    precise float folded_rate = rate * .98;
    precise float along = dot(vec3(relative, 0.0), vec3(tangent, 1.0));
    precise float inverse_tangent = 1.0 / dot(vec3(tangent, 0.0), vec3(tangent, 1.0));
    if (folded_rate > .5) {
        precise float reverse = -curve + 1.0;
        curve = fma(-curve + reverse, vertex_material.p[16].y, curve);
    }
    precise vec2 planar = tangent * vec2(along);
    precise vec2 gap = vec2(0.0) - vertex_material.p[14].zw + vertex_material.p[15].xy;
    precise float gap_length = 1.0 / inversesqrt(dot(vec3(gap, 0.0), vec3(gap, 1.0)));
    precise float scale = fma(max(min(gap_length, 1.0), 0.0), .8, .2);
    precise float curl_scale = fma(scale, vertex_material.p[16].z, 0.0);
    precise float middle = scale * (.5 * vertex_material.p[16].x);
    precise float shifted = folded_rate + -.49;
    precise float next_curve = 1.0 / inversesqrt(max(fma(shifted, -shifted, .25), 0.0));
    precise float height = fma(curve, curl_scale, middle);
    precise vec2 on_fold = fma(planar, vec2(inverse_tangent), vertex_material.p[17].xy);
    precise vec2 reflected = (on_fold + on_fold) + (vec2(0.0) - point.xy);
    if (folded_rate + .01 > .5) {
        precise float reverse = -next_curve + 1.0;
        next_curve = fma(-next_curve + reverse, vertex_material.p[16].y, next_curve);
    }
    precise float slope = fma(next_curve, curl_scale, -height + middle);
    precise vec2 offset = vec2(0.0 - vertex_material.p[15].z) * vertex_material.p[17].zw;
#ifdef ROUNDED_EDGE
    precise vec3 normal = vec3(fma(vec2(slope),offset,vec2(0.0)),.01);
    precise float normal_inverse = inversesqrt(dot(normal,normal));
    normal = vec3(fma(vec2(normal_inverse),normal.xy,vec2(0.0)),normal.z*normal_inverse);
#else
    precise vec3 normal = vec3(vec2(slope) * offset, .01);
    normal = normal * vec3(inversesqrt(dot(normal, normal)));
#endif
#ifdef ROUNDED_EDGE
    precise vec4 position = vec4(fma(reflected,vertex_material.p[13].xy,vec2(0.0)),vec2(height,1.0)*vertex_material.p[13].zw);
#else
    precise vec4 position = vec4(vertex_material.p[13].xy * reflected, vec2(height, 1.0) * vertex_material.p[13].zw);
#endif
    precise vec3 world = fma(vec3(height), vertex_material.p[6].xyz, vertex_material.p[7].xyz);
    world = fma(vec3(reflected.y), vertex_material.p[5].xyz, world);
    world = fma(vec3(reflected.x), vertex_material.p[4].xyz, world);
    precise vec4 world_normal = vertex_material.p[4] * vec4(normal.x);
    world_normal = fma(vec4(normal.y), vertex_material.p[5], world_normal);
    world_normal = fma(vec4(normal.z), vertex_material.p[6], world_normal);
    precise float inverse_normal = inversesqrt(dot(world_normal, world_normal));
#ifdef ROUNDED_EDGE
    precise vec3 unit_normal = fma(world_normal.xyz,vec3(inverse_normal),vec3(0.0));
#else
    precise vec3 unit_normal = vec3(fma(world_normal.xy, vec2(inverse_normal), vec2(0.0)), world_normal.z * inverse_normal);
#endif
    precise vec3 eye = vertex_material.p[27].xyz + (vec3(0.0) - world);
    precise vec3 light = vertex_material.p[23].xyz + (vec3(0.0) - world);
    precise float inverse_ramp = 1.0 / (0.0 - vertex_material.p[10].y + 1.0);
    precise vec3 eye_direction = fma(eye, vec3(inversesqrt(dot(eye, eye))), vec3(0.0));
    precise float ramp = fma(dot(unit_normal, eye_direction), inverse_ramp, 0.0);
    ramp = fma(inverse_ramp, 0.0 - vertex_material.p[10].y, ramp);
    ramp = max(min(ramp, 1.0), 0.0);
    precise vec3 light_color = vertex_material.p[21].xyz * vec3(vertex_material.p[9].x);
#ifdef ROUNDED_EDGE
    precise vec3 light_direction = fma(light,vec3(inversesqrt(dot(light,light))),vec3(0.0));
#else
    precise vec3 light_direction = light * vec3(inversesqrt(dot(light, light)));
#endif
    precise float square_ramp = ramp * ramp;
    precise float diffuse = fma(dot(unit_normal, light_direction), .5, .5);
#ifdef ROUNDED_EDGE
    diffuse = fma(diffuse,diffuse,0.0);
#else
    diffuse = diffuse * diffuse;
#endif
    precise float cube_ramp = ramp * square_ramp;
    precise vec3 lighting = vec3(fma(vec2(diffuse), light_color.xy, vec2(0.0)), light_color.z * diffuse);
    precise float facing = fma(cube_ramp, -2.0, 3.0 * square_ramp);
    precise float opacity = fma(facing, 0.0 - vertex_material.p[10].z, vertex_material.p[10].z);
    precise vec4 clip = vertex_material.p[0] * vec4(position.x);
    clip = fma(vec4(position.y), vertex_material.p[1], clip);
    clip = fma(vec4(position.z), vertex_material.p[2], clip);
    clip = fma(vec4(position.w), vertex_material.p[3], clip);
    if (viewport_flag < .5) {
        gl_Position = ((clip * (vec4(2.0,-2.0,2.0,2.0) / vec4(screen_width,screen_height,1.0,1.0)))
            + vec4(-1.0,1.0,0.0,0.0)) * viewport_flip;
        gl_Position.z = gl_Position.w;
    } else {
        gl_Position = clip * viewport_flip;
        gl_Position.z = (((((gl_Position.z / gl_Position.w) * z_scale) + z_offset) * 2.0) - 1.0) * gl_Position.w;
    }
    precise vec2 uv = vertex_material.p[18].zw * point.xy;
#ifdef ROUNDED_EDGE
    precise vec2 corner_uv=fma(uv,vec2(2.0),vec2(-1.0));
#endif
    uv.y = (0.0 - uv.y) + 1.0;
    uv = fma(uv, vertex_material.p[12].xy, vertex_material.p[12].zw);
#ifdef ROUNDED_EDGE
    v_TexCoord0 = vec4(uv,corner_uv);
    v_TexCoord1 = vec4(corner_uv,lighting.xy);
#else
    v_TexCoord0 = vec4(uv, lighting.xy);
#endif
    v_TexCoord2 = vec4(lighting, opacity);
}
)glsl";
constexpr auto top_vertex=shader_source("#version 320 es\n",top_vertex_body);
constexpr auto top_round_vertex=shader_source("#version 320 es\n#define ROUNDED_EDGE\n",top_vertex_body);
}
std::string_view page_turn_top_vertex_shader() noexcept {
    return {top_vertex.data(),top_vertex.size()-1};
}
std::string_view page_turn_top_round_vertex_shader() noexcept {
    return {top_round_vertex.data(),top_round_vertex.size()-1};
}
std::string_view page_turn_empty_back_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding = 1, std140) uniform PageEmptyMaterial { vec4 p[4]; } fragment_material;
layout(binding = 3, std140) uniform PageDrawState {
    float back_disabled, front_disabled, writing_mask, use_raw_image;
    float res_multiplier, cast_sampler_mask, cast_phase_mask;
    float inv_frag_width, inv_frag_height, raw_cast_mask, iterator_written_mask;
};
layout(binding = 0) uniform highp sampler2D corner_texture;
layout(binding = 1) uniform highp sampler2D empty_background;
layout(location = 4) in mediump vec4 v_TexCoord0;
layout(location = 5) in mediump vec4 v_TexCoord1;
layout(location = 0) out mediump vec4 out_color;

// Retain the original half-register write boundaries, including saturation.
vec2 half_pair(vec2 value) {
    precise vec2 clamped = mix(clamp(value, vec2(-65504.0), vec2(65504.0)), value, isnan(value));
    return unpackHalf2x16(packHalf2x16(clamped));
}
vec4 half_color(vec4 value) {
    return vec4(half_pair(value.xy), half_pair(value.zw));
}
void main() {
    if ((front_disabled != 0.0 && gl_FrontFacing) ||
        (back_disabled != 0.0 && !gl_FrontFacing)) discard;
    precise vec4 input_value = half_color((uint(iterator_written_mask) & 16u) != 0u
        ? v_TexCoord0 : vec4(0.0, 0.0, 0.0, 1.0));
    precise vec2 corner_position = half_pair((uint(iterator_written_mask) & 32u) != 0u
        ? v_TexCoord1.xy : vec2(0.0));
    precise vec2 corner_uv = max(abs(corner_position) - fragment_material.p[0].xy, vec2(0.0)) * fragment_material.p[0].zw;
    mediump vec4 corner_sample = texture(corner_texture, corner_uv);
    precise vec4 corner = half_color(corner_sample);
    precise float minimum_shadow = half_pair(vec2(fragment_material.p[1].x, 0.0)).x;
    precise float shadow_range = half_pair(vec2(1.0 - minimum_shadow, 0.0)).x;
    precise float corner_shadow = half_pair(vec2(fma(corner.b, shadow_range, minimum_shadow))).x;
    precise float shadow = half_pair(vec2(fma(input_value.z, -corner_shadow, 1.0))).x;
    precise vec2 background_uv = fma(input_value.xy, fragment_material.p[3].zw, vec2(0.0));
    mediump vec4 background_sample = texture(empty_background, background_uv);
    precise vec4 background = half_color(background_sample);
    precise vec3 tint = half_color(vec4(fragment_material.p[1].zw, fragment_material.p[2].xy)).rgb;
    precise vec3 shaded = half_color(vec4(background.rgb * tint, 0.0)).rgb;
    precise vec3 shadow_tint = half_color(vec4(fragment_material.p[2].zw, fragment_material.p[3].x, fragment_material.p[2].z)).rgb;
    shaded = half_color(vec4(fma(vec3(shadow), -shadow_tint, shaded), 0.0)).rgb;
    shaded = half_color(vec4(max(shaded, vec3(0.0)), 0.0)).rgb;
    shaded = half_color(vec4(vec3(corner.g) + shaded, 0.0)).rgb;
    precise float opacity = half_pair(vec2(fragment_material.p[2].y, 0.0)).x;
    precise float alpha = half_pair(vec2(opacity * corner.r, 0.0)).x;
    out_color = vec4(shaded, alpha);
}
)glsl";
}
}
