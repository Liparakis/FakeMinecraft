#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 TexCoord;
layout(location = 3) in float vSkyLight;
layout(location = 4) in float vBlockLight;
layout(location = 5) in vec4 FragPosLightSpace;

layout(location = 0) out vec4 FragColor;

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = targetCoords(fragPosLightSpace);
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;

    // A tiny offset keeps surfaces from shadowing themselves (shadow acne).
    float bias = max(0.0005 * (1.0 - dot(normal, lightDir)), 0.0001);

    // Poisson-disk PCF softens the edges.
    // Learned this from: https://learnopengl.com/Advanced-Lighting/Shadows/Point-Shadows
    const vec2 poissonDisk[16] = vec2[](
        vec2(-0.94201624, -0.39906216), vec2(0.94558609, -0.76890725),
        vec2(-0.094184101, -0.92938870), vec2(0.34495938, 0.29387760),
        vec2(-0.91588581, 0.45771432), vec2(-0.81544232, -0.87912464),
        vec2(-0.38277543, 0.27676845), vec2(0.97484398, 0.75648379),
        vec2(0.44323325, -0.97511554), vec2(0.53742981, -0.47373420),
        vec2(-0.26496911, -0.41893023), vec2(0.79197514, 0.19090188),
        vec2(-0.24188840, 0.99706507), vec2(-0.81409955, 0.91437590),
        vec2(0.19984126, 0.78641367), vec2(0.14383161, -0.14100790)
    );

    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for (int i = 0; i < 16; ++i) {
        float pcfDepth = texture(shadowMap, projCoords.xy + poissonDisk[i] * texelSize * 2.5).r;
        shadow += projCoords.z - bias > pcfDepth ? 1.0 : 0.0;
    }
    shadow /= 16.0;

    // Fade out towards the edge of the shadow map instead of cutting off in a hard line.
    vec2 edge = abs(projCoords.xy * 2.0 - 1.0);
    return shadow * (1.0 - smoothstep(0.8, 1.0, max(edge.x, edge.y)));
}

void main() {
    // Reflections only show what is above the water line.
    if (dot(vec4(FragPos, 1.0), frame.clipPlane) < 0.0)
        discard;

    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    float diff = max(dot(Normal, lightDir), 0.0);

    vec3 diffuse;
    if (frame.simpleLighting == 1) {
        // Classic voxel lighting: sun * sky light level * face shading.
        diffuse = diff * frame.sunColor.rgb * vSkyLight;
    } else {
        diffuse = diff * frame.sunColor.rgb * (1.0 - calculateShadow(FragPosLightSpace, Normal, lightDir));
    }

    vec3 ambient = vec3(frame.ambientStrength);
    vec3 blockLight = vBlockLight * vec3(1.0, 0.85, 0.6); // Warm glow from lava.
    vec3 totalLight = clamp(ambient + max(diffuse, blockLight), 0.0, 1.4);

    vec4 texResult = texture(tex, TexCoord);
    if (texResult.a == 0.0)
        discard;
    FragColor = vec4(texResult.rgb * totalLight, 1.0);
}
