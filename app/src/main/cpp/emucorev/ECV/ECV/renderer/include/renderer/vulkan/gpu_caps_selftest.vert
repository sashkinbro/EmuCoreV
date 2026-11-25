#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float use_raw; float batch; float pad1; float pad2; } pc;
layout(set = 0, binding = 2) uniform Quads { vec4 rect[128]; vec4 color[128]; } quads;
layout(location = 0) flat out vec4 v_color;
void main() {
    vec2 p[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vec2 q = p[gl_VertexIndex];
    vec4 r = pc.batch > 0.5 ? quads.rect[gl_InstanceIndex] : pc.rect;
    v_color = pc.batch > 0.5 ? quads.color[gl_InstanceIndex] : pc.color;
    gl_Position = vec4(r.xy + q * r.zw, 0.5, 1.0);
}
