#version 450

layout(set = 1, binding = 0) uniform sampler2D skyTex;

layout(push_constant) uniform Push {
    mat4 model;
    float brightness;
} push;

layout(location = 0) in vec2 TexCoord;
layout(location = 0) out vec4 FragColor;

void main() {
    vec4 texResult = texture(skyTex, TexCoord);
    if (texResult.a < 0.01)
        discard;
    FragColor = vec4(texResult.rgb * push.brightness, texResult.a);
}
