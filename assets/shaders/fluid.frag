#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 Normal;
layout(location = 2) in vec3 TexCoord;
layout(location = 3) in float vSkyLight;
layout(location = 4) in float vBlockLight;
layout(location = 5) in vec4 FragPosLightSpace;
layout(location = 6) in vec4 FragPosReflectionSpace;

layout(location = 0) out vec4 FragColor;

float calculateShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = targetCoords(fragPosLightSpace);
    if (projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
    float bias = max(0.005 * (1.0 - dot(normal, lightDir)), 0.0005);
    return 1.0 - sampleShadowLight(projCoords.xy, projCoords.z - bias);
}

void main() {
    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    vec3 normal = normalize(Normal);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 diffuse = diff * frame.sunColor.rgb * vSkyLight;
    if (frame.simpleLighting != 1) {
        diffuse *= 1.0 - calculateShadow(FragPosLightSpace, normal, lightDir);
    }

    vec3 ambient = vec3(frame.ambientStrength);
    vec3 blockLight = vBlockLight * vec3(1.0, 0.85, 0.6);
    vec3 totalLight = clamp(ambient + max(diffuse, blockLight), 0.0, 1.4);

    vec4 texResult = texture(tex, TexCoord);
    if (texResult.a < 0.1)
        discard;

    // Where this fragment sits in the reflection camera's view, with a little wave distortion.
    vec2 reflectionUV = targetCoords(FragPosReflectionSpace).xy;
    reflectionUV += vec2(sin(FragPos.x * 0.8 + frame.time * 0.7), sin(FragPos.z * 1.2 + frame.time * 0.5)) * 0.003;
    reflectionUV = clamp(reflectionUV, 0.001, 0.999);

    vec3 reflectionColor = texture(reflectionMap, reflectionUV).rgb;
    reflectionColor = mix(reflectionColor, reflectionColor * vec3(0.3, 0.5, 0.7), 0.3);

    // Water reflects more at grazing angles (Schlick's approximation).
    // Ref: https://en.wikipedia.org/wiki/Schlick%27s_approximation
    vec3 viewDir = normalize(frame.cameraPos.xyz - FragPos);
    float fresnel = clamp(pow(1.0 - max(dot(viewDir, normal), 0.0), 2.0), 0.15, 0.65);
    fresnel *= clamp(length(frame.sunColor.rgb), 0.0, 1.0); // No reflections at night.

    vec3 waterColor = totalLight * texResult.rgb;
    if (texResult.a > 0.5 && fresnel > 0.01) {
        FragColor = vec4(mix(waterColor, reflectionColor, fresnel), texResult.a);
    } else {
        FragColor = vec4(waterColor, texResult.a);
    }
}
