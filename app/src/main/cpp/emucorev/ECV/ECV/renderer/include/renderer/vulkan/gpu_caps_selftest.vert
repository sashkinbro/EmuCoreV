#version 450
layout(push_constant) uniform PC { vec4 rect; vec4 color; float use_raw; float pad0; float pad1; float pad2; } pc;
void main() {
    vec2 p[4] = vec2[](vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0), vec2(1.0, 1.0));
    vec2 q = p[gl_VertexIndex];
    gl_Position = vec4(pc.rect.xy + q * pc.rect.zw, 0.5, 1.0);
}
