#include <emucorev/paf/page_turn_shaders.h>

namespace emucorev::paf {
std::string_view page_turn_empty_back_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding = 1, std140) uniform PageEmptyMaterial { vec4 p[4]; };
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
    precise vec2 corner_uv = max(abs(corner_position) - p[0].xy, vec2(0.0)) * p[0].zw;
    mediump vec4 corner_sample = texture(corner_texture, corner_uv);
    precise vec4 corner = half_color(corner_sample);
    precise float minimum_shadow = half_pair(vec2(p[1].x, 0.0)).x;
    precise float shadow_range = half_pair(vec2(1.0 - minimum_shadow, 0.0)).x;
    precise float corner_shadow = half_pair(vec2(fma(corner.b, shadow_range, minimum_shadow))).x;
    precise float shadow = half_pair(vec2(fma(input_value.z, -corner_shadow, 1.0))).x;
    precise vec2 background_uv = fma(input_value.xy, p[3].zw, vec2(0.0));
    mediump vec4 background_sample = texture(empty_background, background_uv);
    precise vec4 background = half_color(background_sample);
    precise vec3 tint = half_color(vec4(p[1].zw, p[2].xy)).rgb;
    precise vec3 shaded = half_color(vec4(background.rgb * tint, 0.0)).rgb;
    precise vec3 shadow_tint = half_color(vec4(p[2].zw, p[3].x, p[2].z)).rgb;
    shaded = half_color(vec4(fma(vec3(shadow), -shadow_tint, shaded), 0.0)).rgb;
    shaded = half_color(vec4(max(shaded, vec3(0.0)), 0.0)).rgb;
    shaded = half_color(vec4(vec3(corner.g) + shaded, 0.0)).rgb;
    precise float opacity = half_pair(vec2(p[2].y, 0.0)).x;
    precise float alpha = half_pair(vec2(opacity * corner.r, 0.0)).x;
    out_color = vec4(shaded, alpha);
}
)glsl";
}
}
