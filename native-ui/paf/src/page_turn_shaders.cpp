#include <emucorev/paf/page_turn_shaders.h>

namespace emucorev::paf {
std::string_view page_turn_top_vertex_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding=0, std140) uniform PageTopVertex { vec4 p[28]; };
layout(binding=2, std140) uniform PageViewport {
    vec4 viewport_flip;
    float viewport_flag, screen_width, screen_height, z_offset, z_scale;
};
layout(location=0) in vec4 point;
layout(location=4) out vec4 v_TexCoord0;
layout(location=6) out vec4 v_TexCoord2;
void main() {
    precise vec2 relative = vec2(0.0) - p[17].xy + point.xy;
    precise vec2 tangent = p[17].wz * vec2(-1.0, 1.0);
    precise float distance = dot(vec3(p[17].zw, 0.0), vec3(relative, 1.0));
    precise float inverse_extent = 1.0 / p[14].x;
    precise float rate = fma(inverse_extent, distance, 0.0);
    precise float curve_argument = fma(rate, .98, -.5);
    precise float curve_squared = max(fma(curve_argument, -curve_argument, .25), 0.0);
    precise float curve = 1.0 / inversesqrt(curve_squared);
    precise float folded_rate = rate * .98;
    precise float along = dot(vec3(relative, 0.0), vec3(tangent, 1.0));
    precise float inverse_tangent = 1.0 / dot(vec3(tangent, 0.0), vec3(tangent, 1.0));
    if (folded_rate > .5) {
        precise float reverse = -curve + 1.0;
        curve = fma(-curve + reverse, p[16].y, curve);
    }
    precise vec2 planar = tangent * vec2(along);
    precise vec2 gap = vec2(0.0) - p[14].zw + p[15].xy;
    precise float gap_length = 1.0 / inversesqrt(dot(vec3(gap, 0.0), vec3(gap, 1.0)));
    precise float scale = fma(max(min(gap_length, 1.0), 0.0), .8, .2);
    precise float curl_scale = fma(scale, p[16].z, 0.0);
    precise float middle = scale * (.5 * p[16].x);
    precise float shifted = folded_rate + -.49;
    precise float next_curve = 1.0 / inversesqrt(max(fma(shifted, -shifted, .25), 0.0));
    precise float height = fma(curve, curl_scale, middle);
    precise vec2 on_fold = fma(planar, vec2(inverse_tangent), p[17].xy);
    precise vec2 reflected = (on_fold + on_fold) + (vec2(0.0) - point.xy);
    if (folded_rate + .01 > .5) {
        precise float reverse = -next_curve + 1.0;
        next_curve = fma(-next_curve + reverse, p[16].y, next_curve);
    }
    precise float slope = fma(next_curve, curl_scale, -height + middle);
    precise vec2 offset = vec2(0.0 - p[15].z) * p[17].zw;
    precise vec3 normal = vec3(vec2(slope) * offset, .01);
    normal = normal * vec3(inversesqrt(dot(normal, normal)));
    precise vec4 position = vec4(p[13].xy * reflected, vec2(height, 1.0) * p[13].zw);
    precise vec3 world = fma(vec3(height), p[6].xyz, p[7].xyz);
    world = fma(vec3(reflected.y), p[5].xyz, world);
    world = fma(vec3(reflected.x), p[4].xyz, world);
    precise vec4 world_normal = p[4] * vec4(normal.x);
    world_normal = fma(vec4(normal.y), p[5], world_normal);
    world_normal = fma(vec4(normal.z), p[6], world_normal);
    precise float inverse_normal = inversesqrt(dot(world_normal, world_normal));
    precise vec3 unit_normal = vec3(fma(world_normal.xy, vec2(inverse_normal), vec2(0.0)), world_normal.z * inverse_normal);
    precise vec3 eye = p[27].xyz + (vec3(0.0) - world);
    precise vec3 light = p[23].xyz + (vec3(0.0) - world);
    precise float inverse_ramp = 1.0 / (0.0 - p[10].y + 1.0);
    precise vec3 eye_direction = fma(eye, vec3(inversesqrt(dot(eye, eye))), vec3(0.0));
    precise float ramp = fma(dot(unit_normal, eye_direction), inverse_ramp, 0.0);
    ramp = fma(inverse_ramp, 0.0 - p[10].y, ramp);
    ramp = max(min(ramp, 1.0), 0.0);
    precise vec3 light_color = p[21].xyz * vec3(p[9].x);
    precise vec3 light_direction = light * vec3(inversesqrt(dot(light, light)));
    precise float square_ramp = ramp * ramp;
    precise float diffuse = fma(dot(unit_normal, light_direction), .5, .5);
    diffuse = diffuse * diffuse;
    precise float cube_ramp = ramp * square_ramp;
    precise vec3 lighting = vec3(fma(vec2(diffuse), light_color.xy, vec2(0.0)), light_color.z * diffuse);
    precise float facing = fma(cube_ramp, -2.0, 3.0 * square_ramp);
    precise float opacity = fma(facing, 0.0 - p[10].z, p[10].z);
    precise vec4 clip = p[0] * vec4(position.x);
    clip = fma(vec4(position.y), p[1], clip);
    clip = fma(vec4(position.z), p[2], clip);
    clip = fma(vec4(position.w), p[3], clip);
    if (viewport_flag < .5) {
        gl_Position = ((clip * (vec4(2.0,-2.0,2.0,2.0) / vec4(screen_width,screen_height,1.0,1.0)))
            + vec4(-1.0,1.0,0.0,0.0)) * viewport_flip;
        gl_Position.z = gl_Position.w;
    } else {
        gl_Position = clip * viewport_flip;
        gl_Position.z = (((((gl_Position.z / gl_Position.w) * z_scale) + z_offset) * 2.0) - 1.0) * gl_Position.w;
    }
    precise vec2 uv = p[18].zw * point.xy;
    uv.y = (0.0 - uv.y) + 1.0;
    uv = fma(uv, p[12].xy, p[12].zw);
    v_TexCoord0 = vec4(uv, lighting.xy);
    v_TexCoord2 = vec4(lighting, opacity);
}
)glsl";
}
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
