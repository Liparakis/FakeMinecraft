#version 450

// R8 font atlas (coverage in .r); solid shapes sample a fully white texel.
layout(set = 1, binding = 0) uniform sampler2D atlas;

layout(location = 0) in vec2 TexCoord;
layout(location = 1) in vec4 Color;
layout(location = 0) out vec4 FragColor;

void main() {
    FragColor = vec4(Color.rgb, Color.a * texture(atlas, TexCoord).r);
}
