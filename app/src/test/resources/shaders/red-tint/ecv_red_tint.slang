#version 450
// Single-pass GPU test fixture based on librashader test/basic.slang.
layout(set = 0, binding = 0, std140) uniform UBO
{
    mat4 MVP;
    float RedTint;
    uint FrameCount;
};

#pragma name ECVRedTint
#pragma format R8G8B8A8_UNORM
#pragma parameter RedTint "Red tint" 1.0 0.0 1.0 0.05

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main()
{
    gl_Position = MVP * Position;
    vTexCoord = TexCoord;
}

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(binding = 1) uniform sampler2D Source;
void main()
{
    vec4 original = texture(Source, vTexCoord);
    float luminance = dot(original.rgb, vec3(0.299, 0.587, 0.114));
    vec3 redTint = vec3(max(original.r, luminance), original.g * 0.04, original.b * 0.04);
    FragColor = vec4(mix(original.rgb, redTint, clamp(RedTint, 0.0, 1.0)), original.a);
}
