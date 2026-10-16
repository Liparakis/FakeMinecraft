#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in ivec4 aPos;
layout(location = 1) in uvec4 aPacked; // texU, texV, direction, light
layout(location = 2) in uint aLayerIndex;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 TexCoord;
layout(location = 3) out float vSkyLight;
layout(location = 4) out float vBlockLight;
layout(location = 5) out vec4 FragPosLightSpace;

void main() {
    vec4 worldPos = vec4(vec3(aPos.xyz), 1.0);
    gl_Position = frame.projection * frame.view * worldPos;
    FragPos = worldPos.xyz;
    TexCoord = vec3(vec2(aPacked.xy), float(aLayerIndex));
    Normal = FACE_NORMALS[min(aPacked.z, 6u)];
    FragPosLightSpace = frame.lightSpace * worldPos;

    vSkyLight = float((aPacked.w >> 4) & 0xFu) / 15.0;
    vBlockLight = float(aPacked.w & 0xFu) / 15.0;
}
