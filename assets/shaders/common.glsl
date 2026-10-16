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
    vec4 lodWaterColor;
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
// Face "direction" of distant-terrain water (LodTerrain::WATER_TOP / WATER_NEAR): far water is opaque and
// composites its sea floor itself; near water is translucent over a real sea floor mesh, like real water.
const uint LOD_WATER_TOP = 7u;
const uint LOD_WATER_NEAR = 8u;
// Level-0 tiles (with near water) always cover this distance: LodTerrain's SPLIT_FACTOR x level-1 tile size.
const float LOD_NEAR_EXTENT = 1024.0;

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

// Shadow on terrain (0 = lit, 1 = fully shadowed), fading out toward the shadow map's edge.
float terrainShadow(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
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

// Whether a real chunk is drawn at this XZ (distant terrain steps aside there): anywhere in the main
// pass, only within fogRange.w (Planet::REFLECTION_RANGE) in the reflection. The mask wraps every
// 128 chunks, so it only applies within 63 chunks of the camera (real chunks never reach further).
bool realChunkAt(vec2 xz, bool reflection) {
    ivec2 chunk = ivec2(floor(xz / 32.0));
    ivec2 cameraChunk = ivec2(floor(frame.cameraPos.xz / 32.0));
    if (reflection && length((vec2(chunk) + 0.5) * 32.0 - frame.cameraPos.xz) > frame.fogRange.w) return false;
    return all(lessThanEqual(abs(chunk - cameraChunk), ivec2(63))) && texelFetch(chunkMask, chunk & 127, 0).r > 0.0; // Any non-zero byte (R8_UNORM).
}

// Walls lying exactly on a chunk border count as covered if either side is real.
bool coveredByRealChunk(vec3 worldPos, bool reflection) {
    return realChunkAt(worldPos.xz + 0.05, reflection) || realChunkAt(worldPos.xz - 0.05, reflection);
}

// Sun and ambient light on distant terrain, shadowed by real chunks within the shadow map.
vec3 distantLight(vec3 worldPos, vec3 normal) {
    vec3 lightDir = normalize(-frame.sunDirection.xyz);
    float diff = max(dot(normal, lightDir), 0.0);
    if (frame.simpleLighting != 1) diff *= 1.0 - terrainShadow(frame.lightSpace * vec4(worldPos, 1.0), normal, lightDir);
    return clamp(vec3(frame.ambientStrength) + diff * frame.sunColor.rgb, 0.0, 1.4);
}

// Water's own colour, as fluid.frag shades it before blending: lit texture mixed with the reflection.
vec3 waterSurface(vec3 worldPos, vec3 light) {
    return mix(light * frame.lodWaterColor.rgb, waterReflection(worldPos), waterFresnel(worldPos));
}
