#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in ivec4 aPos;
layout(location = 1) in uvec4 aPacked; // texU, texV, direction, light
layout(location = 2) in uint aLayerIndex;
layout(location = 3) in uint aTop;

layout(location = 0) out vec3 FragPos;
layout(location = 1) out vec3 Normal;
layout(location = 2) out vec3 TexCoord;
layout(location = 3) out float vSkyLight;
layout(location = 4) out float vBlockLight;
layout(location = 5) out vec4 FragPosLightSpace;
layout(location = 6) out vec4 FragPosReflectionSpace;

// Chunk vertices are chunk-local in x and z (int16 would wrap past 32,767); this is the chunk's world origin.
layout(push_constant) uniform Push {
    layout(offset = 64) ivec4 chunkOrigin;
} push;

const int FRAMES = 32;
const float ANIMATION_SECONDS = 5.0;

void main() {
    vec3 pos = vec3(aPos.xyz + push.chunkOrigin.xyz);
    if (aTop != 0u) {
        pos.y -= 0.1;
        pos.y += (sin(pos.x * 1.5708 + frame.time) + sin(pos.z * 1.5708 + frame.time * 1.5)) * 0.05;
    }

    gl_Position = frame.projection * frame.view * vec4(pos, 1.0);
    FragPos = pos;

    int currentLayer = int(aLayerIndex) + int(floor(mod(frame.time / ANIMATION_SECONDS, 1.0) * FRAMES));
    TexCoord = vec3(vec2(aPacked.xy), float(currentLayer));
    Normal = FACE_NORMALS[min(aPacked.z, 6u)];
    FragPosLightSpace = frame.lightSpace * vec4(pos, 1.0);
    FragPosReflectionSpace = frame.reflectionViewProjection * vec4(pos, 1.0);

    vSkyLight = float((aPacked.w >> 4) & 0xFu) / 15.0;
    vBlockLight = float(aPacked.w & 0xFu) / 15.0;
}
