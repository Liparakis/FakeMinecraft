#include "Sky.h"
#include <cmath>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/**
 * @file Sky.cpp
 * @brief This file handles the sky rendering, including the day/night cycle and atmospheric colors.
 */

namespace {
    /** @name Math Constants @{ */
    constexpr float TWO_PI = 2.0f * static_cast<float>(M_PI);
    constexpr float INV_TWO_PI = 1.0f / TWO_PI;
    /** @} */

    /** @name Color Palettes
 * Colors are defined for different times of the day. Linear interpolation 
 * is used to transition between them during the update.
 * @{
 */
    const glm::vec3 DAY_SKY_COLOR(0.5f, 0.75f, 1.0f);
    const glm::vec3 NIGHT_SKY_COLOR(0.02f, 0.02f, 0.05f);
    const glm::vec3 SUNSET_SKY_COLOR(0.9f, 0.5f, 0.2f);
    const glm::vec3 SUN_COLOR_BRIGHT(1.0f, 0.95f, 0.85f);
    const glm::vec3 SUN_COLOR_SUNSET(1.0f, 0.4f, 0.1f);
    /** @} */

    /** @name Lighting Thresholds @{ */
    constexpr float SUNSET_THRESHOLD = 0.3f;
    constexpr float INV_SUNSET_THRESHOLD = 1.0f / SUNSET_THRESHOLD;
    /** @} */

    /** @name Static Geometry
 * A simple quad is used for rendering the sky. Vertex data is stored in the GPU 
 * during initialization for efficiency.
 * @{
 */
    constexpr float QUAD_VERTICES[] = {
        -0.5f, -0.5f, 0.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, 0.0f,  1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,  0.0f, 1.0f,
    };
    /** @} */
}

Sky::Sky() {}

Sky::~Sky() = default;

void Sky::shutdown() {
    VkContext::get().destroyLater(quad);
}

void Sky::init() {
    sunTexture = loadTexture("../assets/sprites/blocks/sun.png");
    moonTexture = loadTexture("../assets/sprites/blocks/moon.png");
    quad = Renderer::get().createVertexBuffer(QUAD_VERTICES, sizeof(QUAD_VERTICES));
    updateCachedValues();
}

TextureId Sky::loadTexture(const char* path) {
    int width, height, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path, &width, &height, &channels, 4);
    if (!data) [[unlikely]] {
        std::cerr << "[Sky] Failed to load texture: " << path << std::endl;
        return 0;
    }
    const TextureId id = Renderer::get().createTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, data, false);
    stbi_image_free(data);
    ready = true;
    return id;
}


void Sky::update(float deltaTime) {
    if (paused) [[unlikely]] return;

    // Advance time and loop at 1.0 (Midnight/Sunrise cycle).
    timeOfDay += deltaTime / dayLengthSeconds;
    if (timeOfDay >= 1.0f) [[unlikely]] {
        timeOfDay -= 1.0f;
    }

    // All the lighting values are updated in one go here.
    updateCachedValues();
}

void Sky::updateCachedValues() {
    // 1. Figure out the sun's angle
    cachedAngle = timeOfDay * TWO_PI;
    cachedCosAngle = std::cos(cachedAngle);
    cachedSinAngle = std::sin(cachedAngle);
    cachedSunHeight = -cachedCosAngle; // Maps cos range [-1, 1] to height range [1, -1] (noon=1)

    // 2. Figure out the direction of the sunlight (Sun -> Ground)
    cachedSunDirection = glm::normalize(glm::vec3(0.3f, cachedSunHeight, cachedSinAngle));

    // 3. Fading the sun's color based on height
    if (cachedSunHeight > 0.0f) [[unlikely]] {
        // Night phase
        cachedSunColor = glm::vec3(0.0f);
    } else {
        const float intensity = -cachedSunHeight; // 0 at horizon, 1 at noon zenith

        if (intensity < SUNSET_THRESHOLD) [[unlikely]] {
            // Smoothly lerp towards a warm orange as the sun approaches the horizon.
            const float t = intensity * INV_SUNSET_THRESHOLD;
            cachedSunColor = glm::mix(SUN_COLOR_SUNSET, SUN_COLOR_BRIGHT, t);
        } else {
            cachedSunColor = SUN_COLOR_BRIGHT * intensity;
        }
    }

    // 4. Decide how much ambient light we have based on time of day.
    if (cachedSunHeight > 0.0f) [[unlikely]] {
        // Night: slight ambient boost when moon is high
        cachedAmbientStrength = 0.15f + 0.05f * (1.0f - cachedSunHeight);
    } else {
        // Day: peak ambient at noon
        cachedAmbientStrength = 0.3f + 0.15f * (-cachedSunHeight);
    }

    // 5. Pick the sky color based on how low the sun is.
    if (cachedSunHeight > 0.1f) [[unlikely]] {
        cachedSkyColor = NIGHT_SKY_COLOR;
    } else if (cachedSunHeight > -0.1f) [[unlikely]] {
        // Night -> Sunset transition
        const float t = (cachedSunHeight + 0.1f) * 5.0f;
        cachedSkyColor = glm::mix(SUNSET_SKY_COLOR, NIGHT_SKY_COLOR, t);
    } else if (cachedSunHeight > -0.3f) [[unlikely]] {
        // Sunset -> Day transition
        const float t = (cachedSunHeight + 0.3f) * 5.0f;
        cachedSkyColor = glm::mix(DAY_SKY_COLOR, SUNSET_SKY_COLOR, t);
    } else {
        cachedSkyColor = DAY_SKY_COLOR;
    }
}

void Sky::render(const glm::mat4& view, const glm::vec3& cameraPos) {
    if (!ready || !quad) [[unlikely]] return;

    const glm::vec3& sunDir = cachedSunDirection;
    const glm::vec3 moonDir = -sunDir;

    // Fade the bodies in and out around the horizon.
    const float sunBrightness = (cachedSunHeight < 0.0f)
        ? glm::clamp(-cachedSunHeight * 2.0f, 0.0f, 1.0f) : 0.0f;
    const float moonBrightness = (cachedSunHeight > 0.0f)
        ? glm::clamp(cachedSunHeight * 2.0f, 0.0f, 0.8f) : 0.0f;

    if (sunBrightness <= 0.01f && moonBrightness <= 0.01f) [[unlikely]] return;

    // Additive blending so they look like they're glowing; no depth so the world draws over them.
    Renderer::get().bindPipeline(PipelineId::Sky);
    if (sunBrightness > 0.01f) {
        renderBillboard(sunTexture, -sunDir, 80.0f, view, cameraPos, sunBrightness);
    }
    if (moonBrightness > 0.01f) {
        renderBillboard(moonTexture, -moonDir, 60.0f, view, cameraPos, moonBrightness);
    }
}

void Sky::renderBillboard(const TextureId texture, const glm::vec3& direction, const float size,
                          const glm::mat4& view, const glm::vec3& cameraPos, const float brightness) {
    const glm::vec3 billboardPos = cameraPos + direction * 500.0f;

    // The camera's vectors are extracted so the sun and moon always face the player.
    const glm::vec3 right(view[0][0], view[1][0], view[2][0]);
    const glm::vec3 up(view[0][1], view[1][1], view[2][1]);
    const glm::vec3 forward = glm::normalize(cameraPos - billboardPos);

    // Build the model matrix manually (Column-major logic).
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(right * size, 0.0f);
    model[1] = glm::vec4(up * size, 0.0f);
    model[2] = glm::vec4(forward * size, 0.0f);
    model[3] = glm::vec4(billboardPos, 1.0f);

    struct {
        glm::mat4 model;
        float brightness;
    } push{model, brightness};

    Renderer& renderer = Renderer::get();
    renderer.bindTexture(texture);
    renderer.pushConstants(&push, sizeof(push));
    renderer.draw(quad, 6);
}
