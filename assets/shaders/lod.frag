#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 WorldPos;
layout(location = 1) in vec3 Color;
layout(location = 2) in vec3 Normal;

layout(location = 0) out vec4 FragColor;

void main() {
    // Real chunks win wherever they are drawn. The mask wraps every 128 chunks,
    // so it only applies within 63 chunks of the camera (real chunks never reach further).
    ivec2 chunk = ivec2(floor(WorldPos.xz / 32.0));
    ivec2 cameraChunk = ivec2(floor(frame.cameraPos.xz / 32.0));
    if (all(lessThanEqual(abs(chunk - cameraChunk), ivec2(63))) && texelFetch(chunkMask, chunk & 127, 0).r > 0.5)
        discard;

    // Same lighting as distant real terrain (beyond the shadow map): ambient plus sun.
    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    float diff = max(dot(Normal, lightDir), 0.0);
    vec3 light = clamp(vec3(frame.ambientStrength) + diff * frame.sunColor.rgb, 0.0, 1.4);
    FragColor = vec4(applyFog(Color * light, WorldPos), 1.0);
}
