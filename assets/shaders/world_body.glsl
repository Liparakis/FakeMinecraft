// Fragment shader body shared by world.frag (cutout: leaves, alpha-tested) and world_opaque.frag.
// The opaque variant has no discard, so the GPU can reject hidden fragments before shading them.
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

    float shadow = 1.0 - sampleShadowLight(projCoords.xy, projCoords.z - bias);

    // Fade out towards the edge of the shadow map instead of cutting off in a hard line.
    vec2 edge = abs(projCoords.xy * 2.0 - 1.0);
    return shadow * (1.0 - smoothstep(0.8, 1.0, max(edge.x, edge.y)));
}

void main() {
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
#ifdef CUTOUT
    if (texResult.a == 0.0)
        discard;
#endif
    FragColor = vec4(applyFog(texResult.rgb * totalLight, FragPos), 1.0);
}
