#version 450

// FXAA 3.11 (quality), after Timothy Lottes' algorithm as explained by Simon Rodriguez:
// http://blog.simonrodriguez.fr/articles/2016/07/implementing_fxaa.html
// Runs on the resolved scene, so it smooths what 4x MSAA leaves: sub-sample edges, distant
// terrain silhouettes and the steps between blocks. The UI is drawn afterwards, untouched.
layout(set = 1, binding = 0) uniform sampler2D scene;

layout(location = 0) out vec4 FragColor;

const float EDGE_THRESHOLD_MIN = 0.0312;
const float EDGE_THRESHOLD_MAX = 0.125;
const float SUBPIXEL_QUALITY = 0.75;
const int ITERATIONS = 12;
const float QUALITY[12] = float[](1.0, 1.0, 1.0, 1.0, 1.0, 1.5, 2.0, 2.0, 2.0, 2.0, 4.0, 8.0);

float luma(vec3 rgb) {
    return dot(rgb, vec3(0.299, 0.587, 0.114));
}

float lumaAt(vec2 uv) {
    return luma(textureLod(scene, uv, 0.0).rgb);
}

// A macro: textureLodOffset needs a compile-time constant offset.
#define LUMA_OFFSET(uv, dx, dy) luma(textureLodOffset(scene, uv, 0.0, ivec2(dx, dy)).rgb)

void main() {
    vec2 texel = 1.0 / vec2(textureSize(scene, 0));
    vec2 uv = gl_FragCoord.xy * texel;
    vec3 center = textureLod(scene, uv, 0.0).rgb;

    float lumaCenter = luma(center);
    float lumaDown = LUMA_OFFSET(uv, 0, -1);
    float lumaUp = LUMA_OFFSET(uv, 0, 1);
    float lumaLeft = LUMA_OFFSET(uv, -1, 0);
    float lumaRight = LUMA_OFFSET(uv, 1, 0);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;
    // Flat area: nothing to smooth.
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * EDGE_THRESHOLD_MAX)) {
        FragColor = vec4(center, 1.0);
        return;
    }

    float lumaDownLeft = LUMA_OFFSET(uv, -1, -1);
    float lumaUpRight = LUMA_OFFSET(uv, 1, 1);
    float lumaUpLeft = LUMA_OFFSET(uv, -1, 1);
    float lumaDownRight = LUMA_OFFSET(uv, 1, -1);

    float lumaDownUp = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;
    float lumaLeftCorners = lumaDownLeft + lumaUpLeft;
    float lumaDownCorners = lumaDownLeft + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners = lumaUpRight + lumaUpLeft;

    // Is the edge horizontal or vertical?
    float edgeHorizontal = abs(-2.0 * lumaLeft + lumaLeftCorners) + abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 +
                           abs(-2.0 * lumaRight + lumaRightCorners);
    float edgeVertical = abs(-2.0 * lumaUp + lumaUpCorners) + abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 +
                         abs(-2.0 * lumaDown + lumaDownCorners);
    bool isHorizontal = edgeHorizontal >= edgeVertical;

    // Which side of the pixel the edge lies on.
    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;
    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    float stepLength = isHorizontal ? texel.y : texel.x;
    float lumaLocalAverage;
    if (is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    // Walk along the edge in both directions until its ends.
    vec2 edgeUv = uv;
    if (isHorizontal) edgeUv.y += stepLength * 0.5;
    else edgeUv.x += stepLength * 0.5;
    vec2 offset = isHorizontal ? vec2(texel.x, 0.0) : vec2(0.0, texel.y);
    vec2 uv1 = edgeUv - offset;
    vec2 uv2 = edgeUv + offset;

    float lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
    float lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    for (int i = 2; i < ITERATIONS && !(reached1 && reached2); ++i) {
        if (!reached1) lumaEnd1 = lumaAt(uv1) - lumaLocalAverage;
        if (!reached2) lumaEnd2 = lumaAt(uv2) - lumaLocalAverage;
        reached1 = abs(lumaEnd1) >= gradientScaled;
        reached2 = abs(lumaEnd2) >= gradientScaled;
        if (!reached1) uv1 -= offset * QUALITY[i];
        if (!reached2) uv2 += offset * QUALITY[i];
    }

    // Shift toward the edge by how close this pixel is to the nearer end.
    float distance1 = isHorizontal ? (uv.x - uv1.x) : (uv.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - uv.x) : (uv2.y - uv.y);
    bool isDirection1 = distance1 < distance2;
    float pixelOffset = -min(distance1, distance2) / (distance1 + distance2) + 0.5;
    bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != (lumaCenter < lumaLocalAverage);
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Sub-pixel aliasing (thin features smaller than a pixel).
    float lumaAverage = (1.0 / 12.0) * (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    float subPixel = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    subPixel = (-2.0 * subPixel + 3.0) * subPixel * subPixel;
    finalOffset = max(finalOffset, subPixel * subPixel * SUBPIXEL_QUALITY);

    vec2 finalUv = uv;
    if (isHorizontal) finalUv.y += finalOffset * stepLength;
    else finalUv.x += finalOffset * stepLength;
    FragColor = vec4(textureLod(scene, finalUv, 0.0).rgb, 1.0);
}
