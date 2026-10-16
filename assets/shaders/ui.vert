#version 450

layout(location = 0) in vec2 aPos;     // Window pixels, origin top-left.
layout(location = 1) in vec2 aTexCoord;
layout(location = 2) in vec4 aColor;

layout(push_constant) uniform Push {
    vec2 screenSize;
} push;

layout(location = 0) out vec2 TexCoord;
layout(location = 1) out vec4 Color;

void main() {
    gl_Position = vec4(aPos / push.screenSize * 2.0 - 1.0, 0.0, 1.0);
    TexCoord = aTexCoord;
    Color = aColor;
}
