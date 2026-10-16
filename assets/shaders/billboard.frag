#version 450
#extension GL_GOOGLE_include_directive : require
#include "common.glsl"

layout(location = 0) in vec3 FragPos;
layout(location = 1) in vec3 TexCoord;
layout(location = 2) in float vSkyLight;

layout(location = 0) out vec4 FragColor;

void main() {
    if (dot(vec4(FragPos, 1.0), frame.clipPlane) < 0.0)
        discard;

    vec4 texResult = texture(tex, TexCoord);
    if (texResult.a < 0.1)
        discard;

    // Plants use a fixed upward normal and receive no shadows; shadows on grass looked noisy.
    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    float diff = max(lightDir.y, 0.0);
    vec3 diffuse = diff * frame.sunColor.rgb * (frame.simpleLighting == 1 ? vSkyLight : 1.0);
    vec3 ambient = frame.ambientStrength * frame.sunColor.rgb;

    FragColor = vec4((ambient + diffuse) * texResult.rgb, texResult.a);
}
