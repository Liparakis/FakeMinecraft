#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 aPos;
layout(location = 1) in uvec4 aPacked; // texU, texV, unused, light
layout(location = 2) in uint aLayerIndex;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 TexCoord;
layout(location = 2) out float vSkyLight;

out float gl_ClipDistance[1];

void main() {
    vec4 worldPos = vec4(aPos, 1.0);
    gl_Position = frame.projection * frame.view * worldPos;
    gl_ClipDistance[0] = dot(worldPos, frame.clipPlane);
    FragPos = worldPos.xyz;
    TexCoord = vec3(vec2(aPacked.xy), float(aLayerIndex));
    vSkyLight = float((aPacked.w >> 4) & 0xFu) / 15.0;
}
