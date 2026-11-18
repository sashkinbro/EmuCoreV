#include <emucorev/paf/bubble_shaders.h>

namespace emucorev::paf {

std::string_view opaque_bubble_vertex_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;
layout(binding = 0, std140) uniform BubbleTransform { vec4 p[24]; };
layout(binding = 2, std140) uniform BubbleViewport {
    vec4 flip;
    float projected;
    float screen_width;
    float screen_height;
    float depth_offset;
    float depth_scale;
};
layout(location = 0) in vec4 profile;
layout(location = 1) in vec4 texture_profile;
layout(location = 4) out vec4 diffuse;
layout(location = 5) out vec4 reflection;
layout(location = 6) out vec4 texture_and_light;

void main() {
    precise vec4 radial = profile.xxzz * texture_profile.zwzw;
    precise vec4 world = fma(vec4(profile.y), p[6], p[7]);
    world = (p[5] * vec4(radial.y)) + world;
    world = fma(vec4(radial.x), p[4], world);
    precise vec3 normal = fma(vec3(radial.z), p[12].xyz, vec3(0));
    normal = fma(vec3(radial.w), p[13].xyz, normal);
    normal = fma(vec3(profile.w), p[14].xyz, normal);
    normal = fma(vec3(inversesqrt(dot(normal, normal))), normal, vec3(0));

    precise vec3 eye = fma(vec3(1), -world.xyz, p[23].xyz);
    eye = fma(vec3(inversesqrt(dot(eye, eye))), eye, vec3(0));
    precise float eye_cosine = dot(normal, eye);
    precise float reflection_scale = fma(abs(eye_cosine * 2.0), 0.949999988079071, 0.0500000007450581);
    precise vec3 reflected = fma(vec3(reflection_scale), normal, -eye);
    precise float inverse_radius = inversesqrt(dot(vec3(reflected.xy, 0), vec3(reflected.xy, 1)));
    precise float radius = 1.0 / inverse_radius;

    // The original angular mapping is this cubic approximation, not atan().
    precise float ratio;
    precise float angle;
    if (reflected.z < 0.0) {
        ratio = fma(abs(radius) + reflected.z, 1.0 / (-reflected.z + abs(radius)), 0.0);
        angle = fma(ratio, fma(fma(ratio, 0.196299999952316, 0.0), ratio, -0.981700003147125), 2.35619449615479);
    } else {
        ratio = fma(1.0 / (abs(radius) + reflected.z), -abs(radius) + reflected.z, 0.0);
        angle = fma(ratio, fma(fma(ratio, 0.196299999952316, 0.0), ratio, -0.981700003147125), 0.785398185253143);
    }
    angle = radius < 0.0 ? -angle : angle;

    precise vec3 light = fma(vec3(1), -world.xyz, p[22].xyz);
    precise float inverse_light_length = inversesqrt(dot(light, light));
    precise vec3 half_vector = fma(light, vec3(inverse_light_length), eye);
    precise float inverse_half_length = inversesqrt(dot(half_vector, half_vector));
    light *= vec3(inverse_light_length);
    precise float diffuse_factor = fma(dot(normal, light), 0.5, 0.5);
    diffuse_factor = fma(diffuse_factor, diffuse_factor, 0.0);

    precise vec2 uv = p[16].xy * vec2(normal.x);
    uv = fma(vec2(normal.y), p[17].xy, uv);
    uv = fma(vec2(normal.z), p[18].xy, uv);
    uv = fma(uv, vec2(p[11].z), vec2(p[11].x * 0.5) * radial.xy);
    uv += vec2(0.5);

    precise float facing = fma(1.0 / (-p[10].y + 1.0), -p[10].y,
        eye_cosine * (1.0 / (-p[10].y + 1.0)));
    facing = max(min(facing, 1.0), 0.0);
    precise float facing_squared = facing * facing;
    diffuse = (p[21] * vec4(p[9].x)) * vec4(diffuse_factor);
    diffuse.xy = fma(diffuse.xy, p[8].xy, vec2(0));
    diffuse.zw = p[8].zw * diffuse.zw;

    precise float back_height = fma(p[20].y, -p[20].x, p[20].y * profile.y);
    back_height = fma(p[20].w, -p[20].z, fma(back_height, p[20].w, 0.0));
    precise vec2 reflected_uv = fma(vec2(angle * inverse_radius), reflected.xy, vec2(0));
    reflected_uv = fma(reflected_uv, vec2(0.159154936671257), vec2(0.5));
    reflection = vec4(reflected_uv, 0, max(min(back_height, 1.0), 0.0));

    precise float specular = dot(normal, half_vector * vec3(inverse_half_length));
    specular = exp2(log2(max(specular, 0.0)) * p[9].z);
    specular = isnan(specular) ? 1.0 : specular;
    specular = fma(2.0 + p[9].z, specular, 0.0);
    specular = fma(specular, 0.125, 0.0);
    precise float complement = -diffuse_factor + 1.0;
    precise float complement_cubed = (complement * complement) * complement;
    precise float facing_cubed = facing * facing_squared;
    precise float smooth_facing = fma(facing_cubed, -2.0, 3.0 * facing_squared);
    texture_and_light = vec4(uv.x, -uv.y + 1.0,
        fma(specular, -complement_cubed, specular), fma(smooth_facing, -p[10].z, p[10].z));

    precise vec4 clip = p[0] * vec4(world.x);
    clip = fma(vec4(world.y), p[1], clip);
    clip = fma(vec4(world.z), p[2], clip);
    clip = fma(vec4(world.w), p[3], clip);
    if (projected < 0.5) {
        gl_Position = (clip * (vec4(2, -2, 2, 2) / vec4(screen_width, screen_height, 1, 1))
            + vec4(-1, 1, 0, 0)) * flip;
        gl_Position.z = gl_Position.w;
    } else {
        gl_Position = clip * flip;
        gl_Position.z = (((gl_Position.z / gl_Position.w) * depth_scale + depth_offset) * 2.0 - 1.0) * gl_Position.w;
    }
}
)glsl";
}

std::string_view opaque_bubble_fragment_shader() noexcept {
    return R"glsl(#version 320 es
precision highp float;
precision highp int;

layout(binding = 1, std140) uniform BubbleMaterial {
    precise vec4 parameters[3];
};
layout(binding = 0) uniform highp sampler2D icon_map;
layout(binding = 1) uniform highp sampler2D environment_map;
layout(location = 4) in highp vec4 diffuse;
layout(location = 5) in highp vec4 reflection;
layout(location = 6) in highp vec4 texture_and_light;
layout(location = 0) out mediump vec4 color;

vec2 half_round(vec2 value) {
    // The original pack saturates finite overflow and preserves NaNs.
    precise vec2 packed_value = mix(clamp(value, vec2(-65504.0), vec2(65504.0)), value, isnan(value));
    return unpackHalf2x16(packHalf2x16(packed_value));
}
vec4 half_round(vec4 value) {
    return vec4(half_round(value.xy), half_round(value.zw));
}

void main() {
    precise vec4 lighting = half_round(diffuse);
    precise vec4 reflected = half_round(reflection);
    precise vec4 factors = half_round(texture_and_light);
    // Keep interpolation and filtering at full precision, then explicitly
    // round at the material's half boundaries. Implicit mediump interpolation
    // or sampling changes pixels on actual GPUs.
    precise vec2 icon_uv = vec2(texture_and_light.x, texture_and_light.y);
    precise vec2 environment_uv = vec2(reflection.x, reflection.y);
    highp vec4 icon_sample = texture(icon_map, icon_uv);
    highp vec4 environment_sample = texture(environment_map, environment_uv);
    precise vec4 icon = half_round(vec4(icon_sample));
    precise vec4 environment = half_round(vec4(environment_sample));
    precise vec4 background = half_round(vec4(parameters[0].zw, parameters[1].x, parameters[0].z));
    precise vec4 specular = half_round(vec4(parameters[1].zw, parameters[2].x, parameters[1].z));

    // Preserve the distinct multiply/MAD rounding steps, including icon alpha.
    icon.rgb = half_round(vec4(icon.rgb * lighting.rgb, icon.a)).rgb;
    precise vec4 result = half_round(fma(vec4(factors.w), environment, background));
    result = half_round(fma(vec4(factors.z), specular, result));
    result = half_round(fma(vec4(reflected.w), -background, result));
    result = half_round(fma(icon, vec4(reflected.w), result));
    result.a = half_round(vec2(parameters[0].x)).x;
    color = result;
}
)glsl";
}

} // namespace emucorev::paf
