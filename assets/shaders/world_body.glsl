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

void main() {
    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    float diff = max(dot(Normal, lightDir), 0.0);

    vec3 diffuse;
    if (frame.simpleLighting == 1) {
        // Classic voxel lighting: sun * sky light level * face shading.
        diffuse = diff * frame.sunColor.rgb * vSkyLight;
    } else {
        diffuse = diff * frame.sunColor.rgb * (1.0 - terrainShadow(FragPosLightSpace, Normal, lightDir));
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
