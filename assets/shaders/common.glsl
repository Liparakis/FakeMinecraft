// Shared by the 3D shaders. Must match FrameUniforms in src/Renderer/Renderer.h.
layout(set = 0, binding = 0) uniform FrameUniforms {
    mat4 view;
    mat4 projection;
    mat4 lightSpace;
    mat4 reflectionViewProjection;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 cameraPos;
    vec4 clipPlane;
    vec4 fogColor;
    vec4 fogRange;
    float ambientStrength;
    float time;
    int simpleLighting;
    int pad;
} frame;

layout(set = 0, binding = 1) uniform sampler2DArray tex;
// Depth-compare sampler with linear filtering: each lookup is a 2x2 bilinear PCF in hardware.
layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;
layout(set = 0, binding = 3) uniform sampler2D reflectionMap;
// Non-zero where a real chunk is drawn; indexed by chunk coordinate mod 128 (see Renderer::uploadChunkMask).
layout(set = 0, binding = 4) uniform sampler2D chunkMask;

const vec3 FACE_NORMALS[7] = vec3[](
    vec3(0, 0, -1), // NORTH
    vec3(0, 0, 1),  // SOUTH
    vec3(-1, 0, 0), // WEST
    vec3(1, 0, 0),  // EAST
    vec3(0, -1, 0), // BOTTOM
    vec3(0, 1, 0),  // TOP
    vec3(0, 0, 0)   // PLACEHOLDER
);
// Face "direction" of distant-terrain water tops (LodTerrain::WATER_TOP).
const uint LOD_WATER_TOP = 7u;
// Opacity of real water (TextureManager raises the water texture's alpha to 180).
const float LOD_WATER_ALPHA = 180.0 / 255.0;

// Fraction of light reaching a point (0 = fully shadowed) from four filtered shadow-map lookups,
// which together cover the same ~4x4 texel footprint as 16 point samples at a quarter of the fetches.
float sampleShadowLight(vec2 uv, float depth) {
    vec2 texel = 1.0 / vec2(textureSize(shadowMap, 0));
    float lit = texture(shadowMap, vec3(uv + vec2(-1.25, -1.25) * texel, depth))
              + texture(shadowMap, vec3(uv + vec2( 1.25, -1.25) * texel, depth))
              + texture(shadowMap, vec3(uv + vec2(-1.25,  1.25) * texel, depth))
              + texture(shadowMap, vec3(uv + vec2( 1.25,  1.25) * texel, depth));
    return lit * 0.25;
}

// Fades distant geometry into the sky colour (horizontal distance, so looking down doesn't fog).
vec3 applyFog(vec3 color, vec3 worldPos) {
    float distance = length(worldPos.xz - frame.cameraPos.xz);
    return mix(color, frame.fogColor.rgb, smoothstep(frame.fogRange.x, frame.fogRange.y, distance));
}

// Clip-space position -> texture coordinates of an offscreen target rendered with the
// flipped viewport: clip y = +1 lands in the first row, so v runs opposite to y.
vec3 targetCoords(vec4 clipPos) {
    vec3 ndc = clipPos.xyz / clipPos.w;
    return vec3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}

// Water reflects more at grazing angles (Schlick's approximation), and nothing at night.
// Ref: https://en.wikipedia.org/wiki/Schlick%27s_approximation
float waterFresnel(vec3 worldPos) {
    vec3 viewDir = normalize(frame.cameraPos.xyz - worldPos);
    float fresnel = clamp(pow(1.0 - max(viewDir.y, 0.0), 2.0), 0.15, 0.65);
    return fresnel * clamp(length(frame.sunColor.rgb), 0.0, 1.0);
}

// The reflection pass's image at this water point, with a little wave distortion, tinted by the water.
vec3 waterReflection(vec3 worldPos) {
    vec2 uv = targetCoords(frame.reflectionViewProjection * vec4(worldPos, 1.0)).xy;
    uv += vec2(sin(worldPos.x * 0.8 + frame.time * 0.7), sin(worldPos.z * 1.2 + frame.time * 0.5)) * 0.003;
    vec3 color = texture(reflectionMap, clamp(uv, 0.001, 0.999)).rgb;
    return mix(color, color * vec3(0.3, 0.5, 0.7), 0.3);
}
