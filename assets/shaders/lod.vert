#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in uvec4 aColor; // rgb, face direction

layout(location = 0) out vec3 WorldPos;
layout(location = 1) out vec3 Color;
layout(location = 2) out vec3 Normal;
layout(location = 3) flat out uint Kind; // Face direction, or LOD_WATER_TOP / LOD_WATER_NEAR.
out float gl_ClipDistance[1];

void main() {
    gl_Position = frame.projection * frame.view * vec4(aPos, 1.0);
    gl_ClipDistance[0] = dot(vec4(aPos, 1.0), frame.clipPlane);
    WorldPos = aPos;
    Color = vec3(aColor.rgb) / 255.0;
    Kind = aColor.a;
    Normal = Kind >= LOD_WATER_TOP ? vec3(0, 1, 0) : FACE_NORMALS[min(Kind, 6u)];
}
