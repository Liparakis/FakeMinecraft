/**
 * @file Sky.h
 * @brief Logic for the sky, day/night cycle, and sun/moon.
 */

#pragma once

#include <glm/glm.hpp>
#include "Renderer.h"

/**
 * @class Sky
 * @brief Manages the sky color and positions of the sun and moon.
 * 
 * This class keeps track of the time of day and updates the light 
 * and colors in the world to match.
 */
class Sky {
public:
    /**
     * @brief Constructor. Sets default time and cycle parameters.
     */
    Sky();
    
    /**
     * @brief Destructor. GPU resources are owned by the renderer and freed with it.
     */
    ~Sky();

    /**
     * @brief Uploads the sun/moon textures and the quad geometry. Needs an initialised renderer.
     */
    void init();

    /**
     * @brief Advances the internal clock and triggers a recalculation of cached lighting values.
     * @param deltaTime Time elapsed since last frame in seconds.
     */
    void update(float deltaTime);

    /**
     * @brief Renders the sun and moon billboards into the scene.
     * 
     * Uses additive blending and high-distance translation to ensure celestials appear 
     * behind all world geometry while following the camera.
     * 
     * Must run inside the main pass; the projection comes from the frame uniforms.
     *
     * @param view The current camera view matrix (used to face the camera).
     * @param cameraPos The world-space position of the camera (used for billboard anchoring).
     */
    void render(const glm::mat4& view, const glm::vec3& cameraPos);

    /// Frees the quad buffer; call before the renderer shuts down.
    void shutdown();

    /** @name Environmental Lighting Parameters (Cached)
     * These values are computed once per frame during update() to minimize CPU overhead.
     * @{
     */
    [[nodiscard]] inline glm::vec3 getSunDirection() const noexcept { return cachedSunDirection; }
    [[nodiscard]] inline glm::vec3 getSunColor() const noexcept { return cachedSunColor; }
    [[nodiscard]] inline float getAmbientStrength() const noexcept { return cachedAmbientStrength; }
    [[nodiscard]] inline glm::vec3 getSkyColor() const noexcept { return cachedSkyColor; }
    /** @} */

    /**
     * @brief The current clock state as a normalized float.
     * 0.0 = Noon, 0.25 = Sunset, 0.5 = Midnight, 0.75 = Sunrise.
     */
    float timeOfDay = 0.75f;

    /// @brief Duration of a full 24-hour cycle in real-world seconds.
    float dayLengthSeconds = 600.0f;

    /** @name Time Control @{ */
    void togglePause() noexcept { paused = !paused; }
    [[nodiscard]] bool isPaused() const noexcept { return paused; }
    /** @} */

private:
    /**
     * Frequent variables are kept together to help with speed.
     */

    /** @name Cached Lighting Results @{ */
    glm::vec3 cachedSunDirection{0.3f, -1.0f, 0.0f}; ///< Direction vector for shaders.
    glm::vec3 cachedSunColor{1.0f, 0.95f, 0.85f};     ///< Sunbeam intensity/color.
    glm::vec3 cachedSkyColor{0.5f, 0.75f, 1.0f};      ///< Clear-color/Fog color.
    float cachedAmbientStrength = 0.45f;              ///< Minimum scene brightness.
    /** @} */

    /** @name Cached Trigonometric Results @{ */
    float cachedAngle = 0.0f;       ///< Current celestial orbital angle in radians.
    float cachedSunHeight = -1.0f;  ///< Normalized Y component of sun position.
    float cachedCosAngle = -1.0f;   ///< cos(cachedAngle)
    float cachedSinAngle = 0.0f;    ///< sin(cachedAngle)
    /** @} */

    bool paused = false; ///< If true, time progression is halted.

    /** @name Resource Data
     * Handles for textures and geometry.
     * @{
     */
    TextureId sunTexture = 0;
    TextureId moonTexture = 0;
    GpuBuffer quad;
    bool ready = false;

    TextureId loadTexture(const char* path);
    void updateCachedValues();

    /**
     * @brief Internal helper to render a single billboard at a fixed distance.
     * Performs camera-facing rotation and applies brightness scaling.
     */
    void renderBillboard(TextureId texture, const glm::vec3& direction, float size,
                         const glm::mat4& view, const glm::vec3& cameraPos, float brightness);
};
