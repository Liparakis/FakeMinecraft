#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 WorldPos;
layout(location = 1) in vec3 Color;
layout(location = 2) in vec3 Normal;
layout(location = 3) flat in uint IsWater;

layout(location = 0) out vec4 FragColor;

void main() {
    // The reflection pass mirrors everything above the water, so water itself is left out.
    bool reflection = frame.clipPlane.y != 0.0;
    if (reflection && IsWater != 0u) discard;

    // Real chunks win wherever they are drawn: anywhere in the main pass, only within
    // fogRange.w (Planet::REFLECTION_RANGE) in the reflection. The mask wraps every 128 chunks,
    // so it only applies within 63 chunks of the camera (real chunks never reach further).
    ivec2 chunk = ivec2(floor(WorldPos.xz / 32.0));
    ivec2 cameraChunk = ivec2(floor(frame.cameraPos.xz / 32.0));
    bool realDrawn = !reflection || length((vec2(chunk) + 0.5) * 32.0 - frame.cameraPos.xz) <= frame.fogRange.w;
    if (realDrawn && all(lessThanEqual(abs(chunk - cameraChunk), ivec2(63))) && texelFetch(chunkMask, chunk & 127, 0).r > 0.5)
        discard;

    // Same lighting as distant real terrain (beyond the shadow map): ambient plus sun.
    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    float diff = max(dot(Normal, lightDir), 0.0);
    vec3 light = clamp(vec3(frame.ambientStrength) + diff * frame.sunColor.rgb, 0.0, 1.4);
    vec3 color = Color * light;
    // As fluid.frag blended over its sea floor: only the opaque part of the water reflects.
    if (IsWater != 0u) color = mix(color, waterReflection(WorldPos), LOD_WATER_ALPHA * waterFresnel(WorldPos));
    FragColor = vec4(applyFog(color, WorldPos), 1.0);
}
