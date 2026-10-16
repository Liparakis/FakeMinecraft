#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 WorldPos;
layout(location = 1) in vec3 Color;
layout(location = 2) in vec3 Normal;
layout(location = 3) flat in uint Kind;

layout(location = 0) out vec4 FragColor;

void main() {
    // Near water is translucent and drawn by lod_water.frag; the reflection mirrors only what is above water.
    bool reflection = frame.clipPlane.y != 0.0;
    if (Kind == LOD_WATER_NEAR || (reflection && Kind == LOD_WATER_TOP)) discard;
    if (coveredByRealChunk(WorldPos, reflection)) discard;

    vec3 light = distantLight(WorldPos, Normal);
    vec3 color = Color * light;
    // Far water composites itself exactly as real water blends over its sea floor (Color).
    if (Kind == LOD_WATER_TOP) color = mix(color, waterSurface(WorldPos, light), frame.lodWaterColor.a);
    FragColor = vec4(applyFog(color, WorldPos), 1.0);
}
