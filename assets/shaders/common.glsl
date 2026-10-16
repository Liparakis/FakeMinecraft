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
    float ambientStrength;
    float time;
    int simpleLighting;
    int pad;
} frame;

layout(set = 0, binding = 1) uniform sampler2DArray tex;
layout(set = 0, binding = 2) uniform sampler2D shadowMap;
layout(set = 0, binding = 3) uniform sampler2D reflectionMap;

const vec3 FACE_NORMALS[7] = vec3[](
    vec3(0, 0, -1), // NORTH
    vec3(0, 0, 1),  // SOUTH
    vec3(-1, 0, 0), // WEST
    vec3(1, 0, 0),  // EAST
    vec3(0, -1, 0), // BOTTOM
    vec3(0, 1, 0),  // TOP
    vec3(0, 0, 0)   // PLACEHOLDER
);

// Clip-space position -> texture coordinates of an offscreen target rendered with the
// flipped viewport: clip y = +1 lands in the first row, so v runs opposite to y.
vec3 targetCoords(vec4 clipPos) {
    vec3 ndc = clipPos.xyz / clipPos.w;
    return vec3(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5, ndc.z);
}
