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

// Chunk vertices are chunk-local in x and z (int16 would wrap past 32,767); this is the chunk's world origin.
layout(push_constant) uniform Push {
    layout(offset = 64) ivec4 chunkOrigin;
} push;

// Hardware clipping against the reflection plane, so the fragment shader needs no discard for it.
out float gl_ClipDistance[1];

void main() {
    vec4 worldPos = vec4(vec3(aPos.xyz + push.chunkOrigin.xyz), 1.0);
    gl_Position = frame.projection * frame.view * worldPos;
    gl_ClipDistance[0] = dot(worldPos, frame.clipPlane);
    FragPos = worldPos.xyz;
    TexCoord = vec3(vec2(aPacked.xy), float(aLayerIndex));
    Normal = FACE_NORMALS[min(aPacked.z, 6u)];
    FragPosLightSpace = frame.lightSpace * worldPos;

    vSkyLight = float((aPacked.w >> 4) & 0xFu) / 15.0;
    vBlockLight = float(aPacked.w & 0xFu) / 15.0;
}
