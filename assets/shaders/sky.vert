#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aTexCoord;

layout(push_constant) uniform Push {
    mat4 model;
    float brightness;
} push;

layout(location = 0) out vec2 TexCoord;

void main() {
    gl_Position = frame.projection * frame.view * push.model * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
