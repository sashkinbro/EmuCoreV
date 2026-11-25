#version 450
layout(input_attachment_index = 0, set = 0, binding = 0) uniform subpassInput last_frag_data;
layout(input_attachment_index = 1, set = 0, binding = 1) uniform usubpassInput last_frag_data_rawUI;
layout(location = 0) flat in vec4 v_color;
layout(location = 0) out vec4 out_color;
layout(location = 1) out uvec4 out_raw;
layout(push_constant) uniform PC { vec4 rect; vec4 color; float use_raw; float batch; float pad1; float pad2; } pc;
vec4 from_raw(uvec4 u) {
    vec2 a = unpackHalf2x16(u.x | (u.y << 16u));
    vec2 b = unpackHalf2x16(u.z | (u.w << 16u));
    return vec4(a, b);
}
uvec4 to_raw(vec4 c) {
    uint a = packHalf2x16(c.xy);
    uint b = packHalf2x16(c.zw);
    return uvec4(a & 0xFFFFu, a >> 16u, b & 0xFFFFu, b >> 16u);
}
void main() {
    vec4 old = pc.use_raw < 0.5 ? subpassLoad(last_frag_data) : from_raw(subpassLoad(last_frag_data_rawUI));
    vec4 res = old * 0.75 + v_color * 0.25;
    out_color = res;
    out_raw = to_raw(res);
}
