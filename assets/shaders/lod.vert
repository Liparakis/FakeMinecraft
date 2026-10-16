#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in uvec4 aColor; // rgb, face direction

layout(location = 0) out vec3 WorldPos;
layout(location = 1) out vec3 Color;
layout(location = 2) out vec3 Normal;

void main() {
    gl_Position = frame.projection * frame.view * vec4(aPos, 1.0);
    WorldPos = aPos;
    Color = vec3(aColor.rgb) / 255.0;
    Normal = FACE_NORMALS[min(aColor.a, 6u)];
}
