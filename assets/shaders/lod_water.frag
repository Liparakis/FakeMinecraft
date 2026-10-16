#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

// Distant water next to the real chunks: translucent over its own sea floor mesh, exactly as fluid.frag,
// because you see through the edge of the real water into it.
layout(location = 0) in vec3 WorldPos;
layout(location = 1) in vec3 Color;
layout(location = 2) in vec3 Normal;
layout(location = 3) flat in uint Kind;

layout(location = 0) out vec4 FragColor;

void main() {
    if (Kind != LOD_WATER_NEAR || coveredByRealChunk(WorldPos, false)) discard;
    vec3 light = distantLight(WorldPos, Normal);
    vec3 surface = waterSurface(WorldPos, light);

    // Beyond LOD_NEAR_EXTENT the water is opaque with no sea floor under it, so before that, turn into the
    // same opaque composite (lod.frag); otherwise you would see under it through this water.
    float a = frame.lodWaterColor.a;
    float t = smoothstep(LOD_NEAR_EXTENT * 0.75, LOD_NEAR_EXTENT * 0.95, length(WorldPos.xz - frame.cameraPos.xz));
    float alpha = mix(a, 1.0, t);
    vec3 color = (a * surface + t * (1.0 - a) * light * Color) / alpha; // Color: the sea floor's colour.
    FragColor = vec4(applyFog(color, WorldPos), alpha);
}
