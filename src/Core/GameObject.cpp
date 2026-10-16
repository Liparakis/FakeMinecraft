#include "GameObject.h"
#include <iostream>
#include <algorithm>
#include <utility>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <SDL2/SDL_mouse.h>
#include "TextureManager.h"
#include "Blocks.h"
#include "ChunkUtils.h"
#include "../World/Generation/BiomeRegistry.h"
#include "SDL2/SDL_timer.h"

/**
 * @file GameObject.cpp
 * @brief Implementation of the central game controller and main engine logic.
 */

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================
namespace {
    /**
     * @brief String hashing implementation using the DJB2 algorithm at compile time.
     * This helps the game run faster when comparing block names.
     */
    constexpr uint32_t hashCString(const char *str) {
        uint32_t hash = 5381;
        while (*str) {
            hash = ((hash << 5) + hash) + static_cast<unsigned char>(*str++);
        }
        return hash;
    }

    // Block names are pre-hashed to avoid searching for them while the game is running.
    constexpr uint32_t HASH_WATER = hashCString("WATER");
    constexpr uint32_t HASH_LAVA = hashCString("LAVA");
    constexpr uint32_t HASH_TALL_GRASS_BOTTOM = hashCString("TALL_GRASS_BOTTOM");
    constexpr uint32_t HASH_TALL_GRASS_TOP = hashCString("TALL_GRASS_TOP");

    /**
     * @brief Converts simple block coordinates to chunk-relative coordinates.
     * This method correctly handles the negative coordinate wrapping logic.
     */
    [[gnu::always_inline]]
    int worldToChunkCoord(const int blockCoord, const int chunkSize) {
        return (blockCoord >= 0) ? (blockCoord / chunkSize) : ((blockCoord + 1) / chunkSize - 1);
    }

    /**
     * @brief A simple floor function for converting floats to block coordinates.
     */
    [[gnu::always_inline]]
    int fastBlockFloor(const float coord) {
        const int i = static_cast<int>(coord);
        return (coord < 0.0f && coord != i) ? i - 1 : i;
    }

    /**
     * @brief Utility structure to store and update chunk/local coordinates from world position.
     * Used to avoid repeated calculations in different parts of the UI layer.
     */
    struct ChunkLocalCoords {
        int chunkX, chunkY, chunkZ;
        int localX, localY, localZ;

        void update(const glm::vec3 &worldPos) {
            const int blockX = fastBlockFloor(worldPos.x);
            const int blockY = fastBlockFloor(worldPos.y);
            const int blockZ = fastBlockFloor(worldPos.z);

            chunkX = worldToChunkCoord(blockX, CHUNK_WIDTH);
            chunkY = worldToChunkCoord(blockY, CHUNK_HEIGHT);
            chunkZ = worldToChunkCoord(blockZ, CHUNK_WIDTH);

            localX = blockX - chunkX * CHUNK_WIDTH;
            localY = blockY - chunkY * CHUNK_HEIGHT;
            localZ = blockZ - chunkZ * CHUNK_WIDTH;
        }
    };
}

int convertToASCIIsum(const std::string &input) {
    int sum = 0;
    for (const char c: input) {
        const int charVal = static_cast<unsigned char>(c);
        //Use saturating arithmetic
        if (sum > INT_MAX - charVal) return INT_MAX;
        sum += charVal;
    }
    return sum;
}

// ============================================================================
// CONSTRUCTOR
// ============================================================================
GameObject::GameObject(const float x, const float y, std::string windowName)
    : windowX(x), windowY(y), lastX(x / 2), lastY(y / 2), fpsSlider(), renderDistanceSlider(), seedBox(),
      window_name(std::move(windowName)) {
}

GameObject &GameObject::getInstance(float x, float y, const std::string &windowName) {
    static GameObject instance(x, y, windowName);
    return instance;
}

// ============================================================================
// INITIALIZATION
// ============================================================================
void GameObject::init() {
    initializeGraphics();
    initializeUIElements();
    initializeOutlineMesh();
    initializePlayerModel();

    Planet::planet = new Planet();

    cachedWindowCenter.x = windowX * 0.5f;
    cachedWindowCenter.y = windowY * 0.5f;

    // FAKEMC_SCREENSHOT=<file.bmp> saves one frame after FAKEMC_SCREENSHOT_AT seconds (default 20) and quits.
    // FAKEMC_SCREENSHOT_PLAYING=1 closes the start menu first. Used for automated visual checks.
    const char *screenshotPath = std::getenv("FAKEMC_SCREENSHOT");
    const char *screenshotAt = std::getenv("FAKEMC_SCREENSHOT_AT");
    const float screenshotMs = (screenshotAt ? std::strtof(screenshotAt, nullptr) : 20.0f) * 1000.0f;
    bool screenshotTaken = false;
    if (screenshotPath && std::getenv("FAKEMC_SCREENSHOT_PLAYING")) gameState.state = PLAYING;
    // FAKEMC_CAMERA="x,y,z,yaw,pitch" starts the camera at a fixed view, for repeatable visual checks.
    if (const char *view = std::getenv("FAKEMC_CAMERA")) {
        float x, y, z, yaw, pitch;
        if (std::sscanf(view, "%f,%f,%f,%f,%f", &x, &y, &z, &yaw, &pitch) == 5) {
            camera.Position = glm::vec3(x, y, z);
            camera.Yaw = yaw;
            camera.Pitch = pitch;
            camera.processMouseMovement(0.0f, 0.0f);
            cachedMatrices.dirty = true;
        }
    }
    // FAKEMC_RESIZE="WxH" resizes the window halfway to the screenshot; FAKEMC_MSAA=0 starts with MSAA off.
    // Both exercise swapchain and pipeline rebuilds in automated checks.
    int resizeW = 0, resizeH = 0;
    int minimizeStep = std::getenv("FAKEMC_MINIMIZE") ? 1 : 0;
    // FAKEMC_FLY=<blocks per second> moves the camera along +X, so chunks stream in and out during a run.
    const float flySpeed = std::getenv("FAKEMC_FLY") ? std::strtof(std::getenv("FAKEMC_FLY"), nullptr) : 0.0f;
    if (const char *resize = std::getenv("FAKEMC_RESIZE")) std::sscanf(resize, "%dx%d", &resizeW, &resizeH);
    if (const char *msaa = std::getenv("FAKEMC_MSAA"); msaa && msaa[0] == '0') {
        msaaEnabled = false;
        msaaBox.setChecked(false);
    }
    // FAKEMC_RENDER_DISTANCE overrides the starting real-chunk render distance.
    if (const char *distance = std::getenv("FAKEMC_RENDER_DISTANCE")) Planet::planet->renderDistance = std::atoi(distance);
    // FAKEMC_FPS_CAP overrides the starting FPS setting (55 = VSync, above 360 = uncapped).
    if (const char *cap = std::getenv("FAKEMC_FPS_CAP")) fpsCap = std::atoi(cap);
    if (!screenshotPath) {
        platform::playMusic("../assets/sounds/songs/Minecraft Volume Alpha.ogg", GameConstants::MUSIC_VOLUME, true);
    }

    while (platform::pumpEvents()) {
        if (platform::consumeResized()) onResize();

        // FAKEMC_MINIMIZE=1 minimizes the window at a third of the run and restores it at two thirds.
        if (minimizeStep == 1 && platform::timeMs() >= screenshotMs / 3.0f) {
            SDL_MinimizeWindow(platform::window());
            minimizeStep = 2;
        } else if (minimizeStep == 2 && platform::timeMs() >= screenshotMs * 2.0f / 3.0f) {
            SDL_RestoreWindow(platform::window());
            minimizeStep = 0;
        }
        if (resizeW > 0 && platform::timeMs() >= screenshotMs * 0.5f) {
            SDL_SetWindowSize(platform::window(), resizeW, resizeH);
            resizeW = 0;
        }
        if (screenshotPath && platform::timeMs() >= screenshotMs) {
            if (screenshotTaken) break;
            Renderer::get().requestScreenshot(screenshotPath);
            screenshotTaken = true;
        }

        const float deltaMs = platform::deltaMs();
        if (flySpeed != 0.0f) {
            camera.Position.x += flySpeed * deltaMs * 0.001f;
            cachedMatrices.dirty = true;
        }
        keyboardCallBack(deltaMs);
        mouseCallBack();
        renderFrame();

        const bool capped = fpsCap != GameConstants::FPS_VSYNC_THRESHOLD && fpsCap <= GameConstants::FPS_MAX_CAPPED;
        platform::limitFrameRate(capped ? fpsCap : 0);
    }

    shutdown();
}

void GameObject::initializeGraphics() {
    if (!platform::init(static_cast<int>(windowX), static_cast<int>(windowY), "Fake Minecraft")) {
        throw std::runtime_error("Failed to create the game window");
    }
    Renderer::get().init(platform::window(), true);
    ui::init("../assets/fonts/arial.ttf");

    camera = Camera(glm::vec3(0.0f, MAX_HEIGHT / 2 + 10, 0.0f));
    TextureManager::getInstance().loadTextures("../assets/sprites/blocks");
    Blocks::init();
    BiomeRegistry::getInstance().init();
    sky.init();
    lodTerrain.init();
}

void GameObject::shutdown() {
    delete Planet::planet;
    Planet::planet = nullptr;

    Renderer &renderer = Renderer::get();
    renderer.releaseMesh(playerModel);
    lodTerrain.shutdown();
    VkContext::get().destroyLater(outlineVertices);
    sky.shutdown();
    renderer.shutdown();
    platform::shutdown();
}

void GameObject::initializeUIElements() {
    cachedWindowCenter.x = windowX * 0.5f;
    cachedWindowCenter.y = windowY * 0.5f;

    fullscreenCheckBox = Checkbox(cachedWindowCenter.x, cachedWindowCenter.y - 100.0f, 50);
    fpsSlider = Slider(cachedWindowCenter.x - 100, cachedWindowCenter.y, 200.0f, 20.0f, 55.0f, 361.0f, 144.0f);
    renderDistanceSlider = Slider(cachedWindowCenter.x - 100, cachedWindowCenter.y - 80.0f, 200.0f, 20.0f, 4.0f, 30.f,
                                  16.f);
    seedBox = TypeBox(cachedWindowCenter.x - 100, cachedWindowCenter.y + 65.0f, 200.0f, 20.0f);
    uncapSpeedBox = Checkbox(cachedWindowCenter.x + 80, cachedWindowCenter.y + 110.0f, 30);
    superJumpBox = Checkbox(cachedWindowCenter.x + 80, cachedWindowCenter.y + 150.0f, 30);

    videoSettingsBtn = Button(cachedWindowCenter.x, cachedWindowCenter.y - 30, 200, 40, "Video Settings");
    gameSettingsBtn = Button(cachedWindowCenter.x, cachedWindowCenter.y + 30, 200, 40, "Game Settings");
    backBtn = Button(cachedWindowCenter.x, cachedWindowCenter.y + 220, 200, 40, "Back");
    quitBtn = Button(cachedWindowCenter.x, cachedWindowCenter.y + 90, 200, 40, "Quit Game");

    dynamicShadowsBox = Checkbox(cachedWindowCenter.x + 80, cachedWindowCenter.y - 20, 30);
    msaaBox = Checkbox(cachedWindowCenter.x + 80, cachedWindowCenter.y + 40, 30);
    fancyLeavesBox = Checkbox(cachedWindowCenter.x + 80, cachedWindowCenter.y + 100, 30);
    distantTerrainBox = Checkbox(cachedWindowCenter.x + 80, cachedWindowCenter.y + 160, 30);
    distantTerrainBox.setChecked(true);
    dynamicShadowsBox.setChecked(true);
    msaaBox.setChecked(true);
}

void GameObject::initializeOutlineMesh() {
    outlineVertices = Renderer::get().createVertexBuffer(GameConstants::OUTLINE_VERTICES,
                                                         sizeof(GameConstants::OUTLINE_VERTICES));
}

void GameObject::onResize() {
    int width = 0, height = 0;
    SDL_GetWindowSize(platform::window(), &width, &height);
    if (width == 0 || height == 0) return;
    windowX = static_cast<float>(width);
    windowY = static_cast<float>(height);

    cachedWindowCenter.x = windowX * 0.5f;
    cachedWindowCenter.y = windowY * 0.5f;

    renderDistanceSlider.setDimensions(cachedWindowCenter.x - 100, cachedWindowCenter.y - 80.0f);
    fullscreenCheckBox.setDimensions(cachedWindowCenter.x + 50, cachedWindowCenter.y - 100.0f);
    fpsSlider.setDimensions(cachedWindowCenter.x - 100, cachedWindowCenter.y);
    seedBox.setDimensions(cachedWindowCenter.x, cachedWindowCenter.y + 65.0f);
    uncapSpeedBox.setDimensions(cachedWindowCenter.x + 80, cachedWindowCenter.y + 110.0f);
    superJumpBox.setDimensions(cachedWindowCenter.x + 80, cachedWindowCenter.y + 150.0f);

    videoSettingsBtn.setPosition(cachedWindowCenter.x, cachedWindowCenter.y - 30);
    gameSettingsBtn.setPosition(cachedWindowCenter.x, cachedWindowCenter.y + 30);
    quitBtn.setPosition(cachedWindowCenter.x, cachedWindowCenter.y + 90);
    backBtn.setPosition(cachedWindowCenter.x, cachedWindowCenter.y + 220);

    dynamicShadowsBox.setDimensions(cachedWindowCenter.x + 80, cachedWindowCenter.y - 20);
    msaaBox.setDimensions(cachedWindowCenter.x + 80, cachedWindowCenter.y + 40);
    fancyLeavesBox.setDimensions(cachedWindowCenter.x + 80, cachedWindowCenter.y + 100);
    distantTerrainBox.setDimensions(cachedWindowCenter.x + 80, cachedWindowCenter.y + 160);

    cachedMatrices.dirty = true;
    Renderer::get().onResize();
}

// ============================================================================
// FRAME
// ============================================================================
glm::mat4 GameObject::computeLightSpace() const {
    const float shadowDist = Planet::planet->shadowDistance;
    glm::vec3 sunTo = -sky.getSunDirection();

    // The sun's angle is snapped to 0.5 degree steps so the light-space basis doesn't
    // rotate continuously, which would make shadows crawl.
    const float angleQuantization = glm::radians(0.5f);
    const float sunLength = glm::length(sunTo);
    if (sunLength > 0.0001f) {
        const glm::vec3 sunNorm = sunTo / sunLength;
        const float azimuth = std::round(std::atan2(sunNorm.z, sunNorm.x) / angleQuantization) * angleQuantization;
        const float elevation = std::round(std::asin(sunNorm.y) / angleQuantization) * angleQuantization;
        const float cosElev = std::cos(elevation);
        // Unit length on purpose: scaling by sunLength would let float noise leak back in, changing the
        // matrix every frame and defeating the shadow cache.
        sunTo = glm::vec3(std::cos(azimuth) * cosElev, std::sin(elevation), std::sin(azimuth) * cosElev);
    }

    // Choose an up vector that avoids degeneracy when the sun is near the zenith.
    glm::vec3 lightUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(glm::normalize(sunTo), lightUp)) > 0.99f) lightUp = glm::vec3(0.0f, 0.0f, 1.0f);

    // Snap the shadow centre to a 16-block grid (a whole number of texels) so shadows stay stable
    // while moving and the cached shadow map only needs re-rendering when the camera crosses a step.
    const float worldUnitsPerTexel = (2.0f * shadowDist) / static_cast<float>(Renderer::SHADOW_SIZE);
    const float snapStep = std::round(16.0f / worldUnitsPerTexel) * worldUnitsPerTexel;
    const glm::vec3 lightDir = glm::normalize(-sunTo);
    const glm::vec3 lightRight = glm::normalize(glm::cross(lightUp, lightDir));
    const glm::vec3 lightFinalUp = glm::cross(lightDir, lightRight);

    const float centerX = glm::floor(glm::dot(camera.Position, lightRight) / snapStep) * snapStep;
    const float centerY = glm::floor(glm::dot(camera.Position, lightFinalUp) / snapStep) * snapStep;
    const float centerZ = glm::floor(glm::dot(camera.Position, lightDir) / snapStep) * snapStep;
    const glm::vec3 snappedCenter = lightRight * centerX + lightFinalUp * centerY + lightDir * centerZ;

    const glm::mat4 lightView = glm::lookAt(snappedCenter + sunTo * 100.0f, snappedCenter, lightUp);
    const glm::mat4 lightProjection = glm::ortho(-shadowDist, shadowDist, -shadowDist, shadowDist, -4000.0f, 4000.0f);
    return lightProjection * lightView;
}

void GameObject::renderFrame() {
    Renderer &renderer = Renderer::get();

    sky.update(platform::deltaMs() * 0.001f);
    updateWindowTitle();
    updateFPSSettings();
    renderer.setMsaa(msaaEnabled);
    SEED = convertToASCIIsum(seedBox.getText());

    if (!renderer.beginFrame()) return;

    if (cachedMatrices.dirty) [[unlikely]] {
        cachedMatrices.view = camera.getViewMatrix();
        // Reverse-Z: near and far are swapped so depth precision is spent close to the camera.
        // The far distance (first argument) covers the distant terrain's ~16 km horizon.
        cachedMatrices.projection = glm::perspective(glm::radians(90.0f), windowX / windowY,
                                                     LodTerrain::VIEW_DISTANCE * 1.5f, 0.1f);
        cachedMatrices.dirty = false;
    }
    const glm::mat4 &view = cachedMatrices.view;
    const glm::mat4 &projection = cachedMatrices.projection;
    Planet::planet->updateFrustum(freecamActive ? savedViewProjection : projection * view);

    const Planet::ReflectionCamera reflection = Planet::reflectionCamera(camera.Position, camera.Front, windowX / windowY);
    const glm::mat4 lightSpace = toVulkanClip(computeLightSpace());

    FrameUniforms mainUniforms;
    mainUniforms.view = view;
    mainUniforms.projection = toVulkanClip(projection);
    mainUniforms.lightSpace = lightSpace;
    mainUniforms.reflectionViewProjection = toVulkanClip(reflection.projection) * reflection.view;
    mainUniforms.sunDirection = glm::vec4(sky.getSunDirection(), 0.0f);
    mainUniforms.sunColor = glm::vec4(sky.getSunColor(), 0.0f);
    mainUniforms.cameraPos = glm::vec4(camera.Position, 1.0f);
    mainUniforms.ambientStrength = sky.getAmbientStrength();
    mainUniforms.time = static_cast<float>(SDL_GetTicks()) * 0.001f;
    mainUniforms.simpleLighting = simpleLightingEnabled ? 1 : 0;
    // Fog hides the edge of whatever is drawn: the horizon, or the last real chunks without distant terrain.
    const float viewEnd = lodTerrain.enabled() ? LodTerrain::VIEW_DISTANCE
                                               : static_cast<float>(Planet::planet->renderDistance * CHUNK_WIDTH);
    mainUniforms.fogColor = glm::vec4(sky.getSkyColor(), 1.0f);
    // Over 16 km the haze starts early, like real air; the short real-chunk range only hides its edge.
    mainUniforms.fogRange = glm::vec4(viewEnd * (lodTerrain.enabled() ? 0.08f : 0.55f), viewEnd, 0.0f, 0.0f);

    FrameUniforms reflectionUniforms = mainUniforms;
    reflectionUniforms.view = reflection.view;
    reflectionUniforms.projection = toVulkanClip(reflection.projection);
    reflectionUniforms.clipPlane = Planet::REFLECTION_CLIP_PLANE;
    renderer.setUniforms(mainUniforms, reflectionUniforms);

    // Chunk uploads are recorded here, before any pass begins.
    const Uint64 prepareStart = SDL_GetPerformanceCounter();
    Planet::planet->prepare(freecamActive ? savedPlayerPosition : camera.Position);
    if (const uint8_t *mask = Planet::planet->takeChunkMaskIfChanged()) renderer.uploadChunkMask(mask);
    {
        const glm::vec3 &center = freecamActive ? savedPlayerPosition : camera.Position;
        const float nearRadius = static_cast<float>(Planet::planet->renderDistance * CHUNK_WIDTH);
        lodTerrain.update(camera.Position, glm::vec2(center.x, center.z), nearRadius, SEED);
    }
    renderer.finishUploads();
    renderer.mark("uploads");
    const Uint64 recordStart = SDL_GetPerformanceCounter();

    // The shadow map is cached: it is re-rendered only when the snapped light matrix changes, a shadow
    // caster near the camera was (re)uploaded or removed, or shadows were toggled. Turning shadows off
    // renders one empty pass, which leaves a cleared (shadow-free) map.
    const bool shadowsOn = dynamicShadowsEnabled && !simpleLightingEnabled;
    const bool castersChanged = Planet::planet->consumeShadowDirty();
    if (!shadowCacheValid || shadowsOn != cachedShadowsOn || (shadowsOn && (lightSpace != cachedLightSpace || castersChanged))) {
        renderer.beginShadowPass();
        if (shadowsOn) Planet::planet->renderShadows(lightSpace);
        renderer.endPass();
        cachedLightSpace = lightSpace;
        cachedShadowsOn = shadowsOn;
        shadowCacheValid = true;
    }

    if (Planet::planet->hasVisibleWater()) {
        renderer.beginReflectionPass(glm::vec3(0.5f, 0.7f, 1.0f));
        Planet::planet->renderReflection(camera.Position, reflection.projection * reflection.view);
        renderer.endPass();
    }

    renderer.beginMainPass(sky.getSkyColor());
    sky.render(view, camera.Position);
    Planet::planet->render();
    {
        Frustum frustum;
        frustum.update(projection * view);
        lodTerrain.render(frustum);
        renderer.mark("main: distant");
    }
    renderBlockOutline();
    if (freecamActive) [[unlikely]] renderPlayerModel();
    renderer.endPass();

    renderer.beginUIPass();
    renderUI();
    ui::flush(windowX, windowY);
    renderer.endPass();

    const Uint64 recordEnd = SDL_GetPerformanceCounter();
    const double toMs = 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    renderer.addCpuTime("prepare", static_cast<double>(recordStart - prepareStart) * toMs);
    renderer.addCpuTime("record", static_cast<double>(recordEnd - recordStart) * toMs);
    renderer.addCpuTime("frame", platform::deltaMs());

    const Uint64 submitStart = SDL_GetPerformanceCounter();
    renderer.endFrame();
    renderer.addCpuTime("submit+present", static_cast<double>(SDL_GetPerformanceCounter() - submitStart) * toMs);
}

// ============================================================================
// UPDATE METHODS
// ============================================================================
void GameObject::updateWindowTitle() {
    // The title is only updated if the values actually changed to save processing time.
    static int lastFPS = -1;
    static int lastChunkCount = -1;

    const int currentFPS = static_cast<int>(platform::fps());
    const int currentChunks = Planet::planet->numChunks;

    if (currentFPS != lastFPS || currentChunks != lastChunkCount) [[unlikely]] {
        static char titleBuffer[128];
        snprintf(titleBuffer, sizeof(titleBuffer), "Fake Minecraft / FPS: %d Total Chunks: %d",
                 currentFPS, currentChunks);
        platform::setTitle(titleBuffer);

        lastFPS = currentFPS;
        lastChunkCount = currentChunks;
    }
}

void GameObject::updateFPSSettings() const {
    // The slider's lowest position means VSync; anything above FPS_MAX_CAPPED means uncapped.
    Renderer::get().setVsync(fpsCap == GameConstants::FPS_VSYNC_THRESHOLD);
}

// ============================================================================
// RENDERING
// ============================================================================
void GameObject::renderBlockOutline() {
    const auto result = Physics::raycast(camera.Position, camera.Front, GameConstants::RAYCAST_DISTANCE);
    if (!result.hit) [[unlikely]] return;

    Renderer &renderer = Renderer::get();
    renderer.bindPipeline(PipelineId::Outline);
    const glm::vec4 blockPos(result.blockX, result.blockY, result.blockZ, 1.0f);
    renderer.pushConstants(&blockPos, sizeof(blockPos));
    renderer.setLineWidth(2.0f);
    renderer.draw(outlineVertices, 24);
    renderer.setLineWidth(1.0f);
}

void GameObject::renderUI() const {
    renderFluidOverlay();

    if (gameState.state == PLAYING) [[likely]] {
        renderCrosshair();
        renderDebugInfo();
    } else {
        renderPauseMenu();
    }
}

void GameObject::renderFluidOverlay() const {
    // Use helper for coordinate conversion
    ChunkLocalCoords coords;
    coords.update(camera.Position);

    const Chunk *chunk = Planet::planet->getChunk(ChunkPos(coords.chunkX, coords.chunkY, coords.chunkZ));
    if (!chunk) [[unlikely]] return;

    const uint32_t blockType = chunk->getBlockAtPos(coords.localX, coords.localY, coords.localZ);
    const char *blockName = BlockRegistry::getInstance().getBlock(blockType).blockName;
    const uint32_t blockHash = hashCString(blockName);

    ui::Brush overlay;
    overlay.outline_opacity = 0.0f;

    if (blockHash == HASH_WATER) [[unlikely]] {
        overlay.fill_color[0] = 0.0f;
        overlay.fill_color[1] = 0.0f;
        overlay.fill_color[2] = 0.45f;
        overlay.fill_opacity = 0.6f;
        ui::drawRect(cachedWindowCenter.x, cachedWindowCenter.y, windowX, windowY, overlay);
    } else if (blockHash == HASH_LAVA) [[unlikely]] {
        overlay.fill_color[0] = 1.0f;
        overlay.fill_color[1] = 0.5f;
        overlay.fill_color[2] = 0.0f;
        overlay.fill_opacity = 0.4f;
        ui::drawRect(cachedWindowCenter.x, cachedWindowCenter.y, windowX, windowY, overlay);
    }
}

void GameObject::renderCrosshair() const {
    static const ui::Brush crosshair = []() {
        ui::Brush b;
        b.fill_color[0] = 0.7f;
        b.fill_color[1] = 0.7f;
        b.fill_color[2] = 0.7f;
        b.fill_opacity = 1.0f;
        b.outline_opacity = 0.0f;
        return b;
    }();

    ui::drawRect(cachedWindowCenter.x, cachedWindowCenter.y, GameConstants::CROSSHAIR_SIZE,
             GameConstants::CROSSHAIR_THICKNESS, crosshair);
    ui::drawRect(cachedWindowCenter.x, cachedWindowCenter.y, GameConstants::CROSSHAIR_THICKNESS,
             GameConstants::CROSSHAIR_SIZE, crosshair);
}

void GameObject::renderDebugInfo() const {
    static const ui::Brush textBrush = []() {
        ui::Brush b;
        b.fill_color[0] = 0.0f;
        b.fill_color[1] = 0.0f;
        b.fill_color[2] = 0.0f;
        return b;
    }();

    // A static buffer is used for text rendering.
    static char coordsBuffer[64];
    snprintf(coordsBuffer, sizeof(coordsBuffer), "COORDS: %d, %d, %d",
             static_cast<int>(camera.Position.x),
             static_cast<int>(camera.Position.y),
             static_cast<int>(camera.Position.z));

    static char fpsBuffer[32];
    snprintf(fpsBuffer, sizeof(fpsBuffer), "FPS: %d",
             static_cast<int>(platform::fps()));

    ui::drawText(10, 30, 15, fpsBuffer, textBrush);
    ui::drawText(10, 50, 15, coordsBuffer, textBrush);

    // Cache block registry reference
    static const BlockRegistry &registry = BlockRegistry::getInstance();
    const char *blockName = registry.getBlock(gameState.selectedBlock).blockName;

    static char blockBuffer[128];
    snprintf(blockBuffer, sizeof(blockBuffer), "SELECTED BLOCK: %s", blockName);
    ui::drawText(10, 70, 15, blockBuffer, textBrush);

    // Mode indicator
    {
        static const ui::Brush modeBrush = []() {
            ui::Brush b;
            b.fill_color[0] = 0.2f;
            b.fill_color[1] = 0.8f;
            b.fill_color[2] = 0.2f;
            return b;
        }();
        static const ui::Brush noclipBrush = []() {
            ui::Brush b;
            b.fill_color[0] = 0.8f;
            b.fill_color[1] = 0.4f;
            b.fill_color[2] = 0.1f;
            return b;
        }();

        if (noclipEnabled) {
            ui::drawText(10, 90, 15, "CREATIVE (F3 to toggle)", noclipBrush);
        } else {
            ui::drawText(10, 90, 15, "SURVIVAL (F3 to toggle)", modeBrush);
        }
    }

    if (freecamActive) [[unlikely]] {
        static const ui::Brush freecamBrush = []() {
            ui::Brush b;
            b.fill_color[0] = 1.0f;
            b.fill_color[1] = 0.5f;
            b.fill_color[2] = 0.0f;
            return b;
        }();
        ui::drawText(10, 110, 15, "FREECAM ON (F1 to exit)", freecamBrush);
    }
}

// ============================================================================
// MENU RENDERING
// ============================================================================

void GameObject::renderPauseMenu() const {
    static const ui::Brush overlay = []() {
        ui::Brush b;
        b.outline_opacity = 0.0f;
        b.fill_color[0] = 0.1f;
        b.fill_color[1] = 0.1f;
        b.fill_color[2] = 0.1f;
        b.fill_opacity = 0.85f;
        return b;
    }();

    // Draw generic overlay
    ui::drawRect(cachedWindowCenter.x, cachedWindowCenter.y, windowX, windowY, overlay);

    switch (gameState.menuState) {
        case MenuState::MAIN:
            renderMainMenu();
            break;
        case MenuState::VIDEO:
            renderVideoSettings();
            break;
        case MenuState::GAME:
            renderGameSettings();
            break;
    }
}

void GameObject::renderMainMenu() const {
    static const ui::Brush titleBrush = []() {
        ui::Brush b;
        b.fill_color[0] = 1.0f;
        b.fill_color[1] = 1.0f;
        b.fill_color[2] = 1.0f;
        return b;
    }();

    ui::drawText(cachedWindowCenter.x - 60, cachedWindowCenter.y - 120, 25, "Game Menu", titleBrush);

    videoSettingsBtn.draw();
    gameSettingsBtn.draw();
    quitBtn.draw();
}

void GameObject::renderVideoSettings() const {
    static const ui::Brush textBrush = []() {
        ui::Brush b;
        b.fill_color[0] = 1.0f;
        b.fill_color[1] = 1.0f;
        b.fill_color[2] = 1.0f;
        return b;
    }();

    ui::drawText(cachedWindowCenter.x - 80, cachedWindowCenter.y - 140, 25, "Video Settings", textBrush);

    // Render Distance
    static char renderDistBuffer[64];
    snprintf(renderDistBuffer, sizeof(renderDistBuffer), "Render Distance: %d", Planet::planet->renderDistance);
    ui::drawText(cachedWindowCenter.x - 90, cachedWindowCenter.y - 105, 18, renderDistBuffer, textBrush);
    renderDistanceSlider.draw();

    // Dynamic Shadows & Lighting
    ui::drawText(cachedWindowCenter.x - 130, cachedWindowCenter.y - 25, 18, "Dynamic Lighting", textBrush);
    dynamicShadowsBox.draw();

    // AA
    ui::drawText(cachedWindowCenter.x - 130, cachedWindowCenter.y + 35, 18, "Anti-Aliasing", textBrush);
    msaaBox.draw();

    // Leaves
    ui::drawText(cachedWindowCenter.x - 130, cachedWindowCenter.y + 95, 18, "Fancy Leaves", textBrush);
    fancyLeavesBox.draw();

    // Horizon
    ui::drawText(cachedWindowCenter.x - 130, cachedWindowCenter.y + 155, 18, "Distant Terrain", textBrush);
    distantTerrainBox.draw();

    backBtn.draw();
}

void GameObject::renderGameSettings() const {
    static const ui::Brush textBrush = []() {
        ui::Brush b;
        b.fill_color[0] = 1.0f;
        b.fill_color[1] = 1.0f;
        b.fill_color[2] = 1.0f;
        return b;
    }();

    ui::drawText(cachedWindowCenter.x - 80, cachedWindowCenter.y - 140, 25, "Game Settings", textBrush);

    // Fullscreen
    ui::drawText(cachedWindowCenter.x - 80, cachedWindowCenter.y - 90, 18, "Fullscreen", textBrush);
    fullscreenCheckBox.draw();

    // FPS
    static char fpsBuffer[32];
    if (fpsCap == GameConstants::FPS_VSYNC_THRESHOLD) {
        snprintf(fpsBuffer, sizeof(fpsBuffer), "Max FPS: VSYNC");
    } else if (fpsCap <= GameConstants::FPS_MAX_CAPPED) {
        snprintf(fpsBuffer, sizeof(fpsBuffer), "Max FPS: %d", fpsCap);
    } else {
        snprintf(fpsBuffer, sizeof(fpsBuffer), "Max FPS: UNCAPPED");
    }
    ui::drawText(cachedWindowCenter.x - 90, cachedWindowCenter.y - 25, 18, fpsBuffer, textBrush);
    fpsSlider.draw();

    // Seed
    ui::drawText(cachedWindowCenter.x - 90, cachedWindowCenter.y + 40, 18, "World Seed", textBrush);
    seedBox.draw();

    // Cap Velocity
    ui::drawText(cachedWindowCenter.x - 90, cachedWindowCenter.y + 110, 18, "Uncap Speed", textBrush);
    uncapSpeedBox.draw();

    // Super Jump
    ui::drawText(cachedWindowCenter.x - 90, cachedWindowCenter.y + 150, 18, "Super Jump", textBrush);
    superJumpBox.draw();

    backBtn.draw();
}

// ============================================================================
// INPUT HANDLING
// ============================================================================
void GameObject::mouseCallBack() {
    const platform::MouseState &ms = platform::mouse();

    if (gameState.state == PLAYING) [[likely]] {
        handlePlayingMouseInput(ms);
        cachedMatrices.dirty = true;
    } else {
        handlePausedMouseInput(ms);
    }
}

void GameObject::handlePlayingMouseInput(const platform::MouseState &ms) {
    const auto xOffset = static_cast<float>(ms.relX);
    const auto yOffset = static_cast<float>(-ms.relY);
    camera.processMouseMovement(xOffset, yOffset, true);

    if (ms.leftPressed || ms.middlePressed || ms.rightPressed) [[unlikely]] {
        const auto result = Physics::raycast(camera.Position, camera.Front,
                                             GameConstants::RAYCAST_DISTANCE);
        if (!result.hit) [[unlikely]] return;

        if (ms.leftPressed) {
            handleBlockBreak(result);
        } else if (ms.middlePressed) {
            handleBlockPick(result);
        } else {
            handleBlockPlace(result);
        }
    }
}

void GameObject::handlePausedMouseInput(const platform::MouseState &ms) {
    if (ms.leftPressed) {
        if (gameState.menuState == MenuState::MAIN) {
            videoSettingsBtn.handleClick(ms.x, ms.y, [this]() {
                gameState.menuState = MenuState::VIDEO;
            });
            gameSettingsBtn.handleClick(ms.x, ms.y, [this]() {
                gameState.menuState = MenuState::GAME;
            });
            quitBtn.handleClick(ms.x, ms.y, [this]() {
                platform::quit();
            });
        } else if (gameState.menuState == MenuState::VIDEO) {
            backBtn.handleClick(ms.x, ms.y, [this]() {
                gameState.menuState = MenuState::MAIN;
            });

            dynamicShadowsBox.handleClick(ms.x, ms.y,
                                          [this]() {
                                              dynamicShadowsEnabled = true;
                                              simpleLightingEnabled = false;
                                          },
                                          [this]() {
                                              dynamicShadowsEnabled = false;
                                              simpleLightingEnabled = true;
                                          });

            msaaBox.handleClick(ms.x, ms.y,
                                [this]() { msaaEnabled = true; },
                                [this]() { msaaEnabled = false; });

            // Leaves are baked into chunk meshes, so changing the style remeshes the world.
            fancyLeavesBox.handleClick(ms.x, ms.y,
                                       [] { setFancyLeaves(true); Planet::planet->remeshAll(); },
                                       [] { setFancyLeaves(false); Planet::planet->remeshAll(); });

            distantTerrainBox.handleClick(ms.x, ms.y,
                                          [this] { lodTerrain.setEnabled(true); },
                                          [this] { lodTerrain.setEnabled(false); });
        } else if (gameState.menuState == MenuState::GAME) {
            backBtn.handleClick(ms.x, ms.y, [this]() {
                gameState.menuState = MenuState::MAIN;
            });

            fullscreenCheckBox.handleClick(ms.x, ms.y,
                                           [] { platform::setFullscreen(true); },
                                           [] { platform::setFullscreen(false); });

            uncapSpeedBox.handleClick(ms.x, ms.y,
                                       [this] { uncapSpeedEnabled = true; },
                                       [this] { uncapSpeedEnabled = false; });

            superJumpBox.handleClick(ms.x, ms.y,
                                     [this] { superJumpEnabled = true; },
                                     [this] { superJumpEnabled = false; });
        }
    }

    // Sliders handling (Drag)
    if (ms.leftDown) {
        if (gameState.menuState == MenuState::VIDEO) {
            renderDistanceSlider.startDragging(ms.x, ms.y);
        } else if (gameState.menuState == MenuState::GAME) {
            fpsSlider.startDragging(ms.x, ms.y);
        }
    } else {
        renderDistanceSlider.stopDragging();
        fpsSlider.stopDragging();
    }

    if (gameState.menuState == MenuState::VIDEO) {
        renderDistanceSlider.update(ms.x, Planet::planet->renderDistance);
    } else if (gameState.menuState == MenuState::GAME) {
        fpsSlider.update(ms.x, fpsCap);
    }
}

void GameObject::keyboardCallBack(const float deltaTime) {
    // Toggle pause / Menu Navigation
    static bool escPressed = false;
    if (platform::keyDown(SDL_SCANCODE_ESCAPE)) {
        if (!escPressed) {
            escPressed = true;
            if (gameState.state == PLAYING) {
                gameState.state = PAUSED;
                gameState.menuState = MenuState::MAIN;
                SDL_SetRelativeMouseMode(SDL_FALSE);
            } else {
                // In Menu
                if (gameState.menuState == MenuState::MAIN) {
                    gameState.state = PLAYING;
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                } else {
                    // Go back to main menu
                    gameState.menuState = MenuState::MAIN;
                }
            }
        }
    } else {
        escPressed = false;
    }

    if (gameState.state == PAUSED) [[unlikely]] {
        if (gameState.menuState == MenuState::GAME) {
            seedBox.handleInput();
        }
        return;
    }

    // Pre-calculate movement speed
    const float movementSpeed = deltaTime * GameConstants::MOVEMENT_SPEED_MULTIPLIER;

    bool moved = false;

    if (noclipEnabled) {
        // Noclip mode: fly freely through everything (original behavior)
        if (platform::keyDown(SDL_SCANCODE_W)) {
            camera.processKeyboard(FORWARD, movementSpeed);
            moved = true;
        }
        if (platform::keyDown(SDL_SCANCODE_S)) {
            camera.processKeyboard(BACKWARD, movementSpeed);
            moved = true;
        }
        if (platform::keyDown(SDL_SCANCODE_A)) {
            camera.processKeyboard(LEFT, movementSpeed);
            moved = true;
        }
        if (platform::keyDown(SDL_SCANCODE_D)) {
            camera.processKeyboard(RIGHT, movementSpeed);
            moved = true;
        }
        if (platform::keyDown(SDL_SCANCODE_SPACE)) {
            camera.processKeyboard(UP, movementSpeed);
            moved = true;
        }
        if (platform::keyDown(SDL_SCANCODE_LSHIFT)) {
            camera.processKeyboard(DOWN, movementSpeed);
            moved = true;
        }
    } else {
        // Collision mode: horizontal movement only via WASD, gravity and collision handled by physics
        glm::vec3 moveDir(0.0f);

        // Calculate horizontal-only front and right vectors
        glm::vec3 flatFront = glm::normalize(glm::vec3(camera.Front.x, 0.0f, camera.Front.z));
        glm::vec3 flatRight = camera.Right; // Right is always horizontal if Up is (0,1,0)

        if (platform::keyDown(SDL_SCANCODE_W)) moveDir += flatFront;
        if (platform::keyDown(SDL_SCANCODE_S)) moveDir -= flatFront;
        if (platform::keyDown(SDL_SCANCODE_A)) moveDir -= flatRight;
        if (platform::keyDown(SDL_SCANCODE_D)) moveDir += flatRight;

        // Apply movement input to velocity
        // Convert per-ms speed multiplier to per-second velocity: (units/ms) * 1000 = units/s
        const float walkSpeed = GameConstants::MOVEMENT_SPEED_MULTIPLIER * 1000.0f;

        // Uncapped Momentum Logic
        // "As long as you are not stopped by an object u gain momentum constantly"

        // Convert per-ms speed multiplier to just use it as a base acceleration unit if needed,
        // but user wants acceleration. Let's use the explicit acceleration values.

        moveDir = (glm::length(moveDir) > 0.001f) ? glm::normalize(moveDir) : glm::vec3(0.0f);

        // Ground: Fast acceleration (20.0f)
        // Air: Very high acceleration (50.0f)
        float accelerationSpeed = playerOnGround ? 20.0f : 50.0f;
        float dtSec = deltaTime * 0.001f;

        if (glm::length(moveDir) > 0.1f) {
            // Arcade Momentum:
            // 1. "Uncapped": Speed constantly increases while moving.
            // 2. "Awesome Strafes": Direction snaps instantly to input (no drift).

            // Get current horizontal speed
            float currentHorizontalSpeed = glm::length(glm::vec2(playerVelocity.x, playerVelocity.z));

            // Ensure a minimum start speed so we don't get stuck at 0
            if (currentHorizontalSpeed < 5.0f) currentHorizontalSpeed = 5.0f;

            // Add acceleration (Uncapped)
            currentHorizontalSpeed += accelerationSpeed * dtSec;

            if (!uncapSpeedEnabled && currentHorizontalSpeed > 20.0f) {
                currentHorizontalSpeed = 20.0f;
            }

            // Apply speed in new direction instantly
            playerVelocity.x = moveDir.x * currentHorizontalSpeed;
            playerVelocity.z = moveDir.z * currentHorizontalSpeed;
        } else {
            // Friction/Stop
            // Ground stops quickly, Air keeps momentum
            float friction = playerOnGround ? 10.0f : 0.5f;

            float frictionFactor = friction * dtSec;
            if (frictionFactor > 1.0f) frictionFactor = 1.0f;

            playerVelocity.x = glm::mix(playerVelocity.x, 0.0f, frictionFactor);
            playerVelocity.z = glm::mix(playerVelocity.z, 0.0f, frictionFactor);

            if (glm::length(glm::vec2(playerVelocity.x, playerVelocity.z)) < 0.01f) {
                playerVelocity.x = 0.0f;
                playerVelocity.z = 0.0f;
            }
        }


        // Jump (only when on ground)
        if (platform::keyDown(SDL_SCANCODE_SPACE) && playerOnGround) {
            playerVelocity.y = superJumpEnabled ? 100.0f : 8.5f; // Jump impulse
            playerOnGround = false;
        }

        // Apply physics (gravity + collision)
        updatePlayerPhysics(deltaTime * 0.001f); // deltaTime is in ms from MOVEMENT_SPEED_MULTIPLIER usage
        moved = true;
    }

    if (moved) [[likely]] {
        cachedMatrices.dirty = true;
    }

    // F-key toggles with debouncing
    static bool f1Pressed = false;
    static bool wasNoclipEnabled = true;

    if (platform::keyDown(SDL_SCANCODE_F1)) {
        if (!f1Pressed) [[unlikely]] {
            f1Pressed = true;
            freecamActive = !freecamActive;

            if (freecamActive) {
                // Save state to allow detached investigation of the world
                savedPlayerPosition = camera.Position;
                savedViewProjection = cachedMatrices.projection * cachedMatrices.view;

                // Force creative flight (noclip) in freecam
                wasNoclipEnabled = noclipEnabled;
                noclipEnabled = true;
            } else {
                // Teleport back to player state
                camera.Position = savedPlayerPosition;
                cachedMatrices.dirty = true;

                // Restore previous mode
                noclipEnabled = wasNoclipEnabled;
                playerVelocity = glm::vec3(0.0f); // Reset physics velocity
            }
        }
    } else {
        f1Pressed = false;
    }

    static bool f2Pressed = false;
    if (platform::keyDown(SDL_SCANCODE_F2)) {
        if (!f2Pressed) [[unlikely]] {
            f2Pressed = true;
            sky.togglePause();
            std::cout << "Sky Time " << (sky.isPaused() ? "PAUSED" : "RESUMED") << std::endl;
        }
    } else {
        f2Pressed = false;
    }

    // F3: Toggle noclip / collision mode
    static bool f3Pressed = false;
    if (platform::keyDown(SDL_SCANCODE_F3)) {
        if (!f3Pressed) [[unlikely]] {
            f3Pressed = true;
            noclipEnabled = !noclipEnabled;
            if (noclipEnabled) {
                playerVelocity = glm::vec3(0.0f);
                std::cout << "[MODE] Phase-Through Creative (Noclip ON)" << std::endl;
            } else {
                playerVelocity = glm::vec3(0.0f);
                std::cout << "[MODE] Collision Oriented (Noclip OFF)" << std::endl;
            }
        }
    } else {
        f3Pressed = false;
    }

    static bool f11Pressed = false;
    if (platform::keyDown(SDL_SCANCODE_F11)) {
        if (!f11Pressed) [[unlikely]] {
            f11Pressed = true;
            const bool isChecked = fullscreenCheckBox.isChecked();
            fullscreenCheckBox.setChecked(!isChecked);
            platform::setFullscreen(!isChecked);
        }
    } else {
        f11Pressed = false;
    }
}

// ============================================================================
// BLOCK OPERATIONS
// ============================================================================
void GameObject::handleBlockBreak(const Physics::RaycastResult &result) {
    const uint32_t blockType = result.chunk->getBlockAtPos(
        result.localBlockX, result.localBlockY, result.localBlockZ);

    // Dependent breaking logic for tall structures
    const Block &currentBlock = BlockRegistry::getInstance().getBlock(blockType);
    const uint32_t blockHash = hashCString(currentBlock.blockName);

    if (blockHash == HASH_TALL_GRASS_BOTTOM) [[unlikely]] {
        // Automatically destroy the top half of tall grass if the base is removed
        const uint32_t aboveBlock = result.chunk->getBlockAtPos(
            result.localBlockX, result.localBlockY + 1, result.localBlockZ);

        const Block &aboveBlockData = BlockRegistry::getInstance().getBlock(aboveBlock);
        const uint32_t aboveHash = hashCString(aboveBlockData.blockName);

        if (aboveHash == HASH_TALL_GRASS_TOP) {
            result.chunk->updateBlock(result.localBlockX, result.localBlockY + 1, result.localBlockZ, 0);
        }
    }

    playSound(blockType);
    result.chunk->updateBlock(result.localBlockX, result.localBlockY, result.localBlockZ, 0);
}

void GameObject::handleBlockPick(const Physics::RaycastResult &result) {
    gameState.selectedBlock = result.chunk->getBlockAtPos(
        result.localBlockX, result.localBlockY, result.localBlockZ);
}

void GameObject::handleBlockPlace(const Physics::RaycastResult &result) const {
    //Batch absolute value calculations
    const float distX = result.hitPos.x - (result.blockX + 0.5f);
    const float distY = result.hitPos.y - (result.blockY + 0.5f);
    const float distZ = result.hitPos.z - (result.blockZ + 0.5f);

    const float absDX = std::abs(distX);
    const float absDY = std::abs(distY);
    const float absDZ = std::abs(distZ);

    int blockX = result.blockX;
    int blockY = result.blockY;
    int blockZ = result.blockZ;

    //Branchless selection
    const bool xMax = (absDX > absDY) & (absDX > absDZ);
    const bool yMax = (absDY > absDX) & (absDY > absDZ);

    blockX += xMax ? (distX > 0 ? 1 : -1) : 0;
    blockY += yMax ? (distY > 0 ? 1 : -1) : 0;
    blockZ += (!xMax & !yMax) ? (distZ > 0 ? 1 : -1) : 0;

    // Get chunk coordinates
    const int chunkX = worldToChunkCoord(blockX, CHUNK_WIDTH);
    const int chunkY = worldToChunkCoord(blockY, CHUNK_HEIGHT);
    const int chunkZ = worldToChunkCoord(blockZ, CHUNK_WIDTH);

    const int localBlockX = blockX - chunkX * CHUNK_WIDTH;
    const int localBlockY = blockY - chunkY * CHUNK_HEIGHT;
    const int localBlockZ = blockZ - chunkZ * CHUNK_WIDTH;

    Chunk *chunk = Planet::planet->getChunk(ChunkPos(chunkX, chunkY, chunkZ));
    if (!chunk) [[unlikely]] return;

    const uint16_t blockToReplace = chunk->getBlockAtPos(localBlockX, localBlockY, localBlockZ);
    const Block &blockToReplaceData = BlockRegistry::getInstance().getBlock(blockToReplace);

    if (blockToReplace != 0 && blockToReplaceData.blockType != Block::LIQUID) [[unlikely]] {
        return;
    }

    // Billboard validation
    const Block &selectedBlockData = BlockRegistry::getInstance().getBlock(gameState.selectedBlock);
    if (selectedBlockData.blockType == Block::BILLBOARD) [[unlikely]] {
        const uint16_t blockBelow = chunk->getBlockAtPos(localBlockX, localBlockY - 1, localBlockZ);
        if (!Blocks::isSoil(static_cast<uint8_t>(blockBelow))) {
            return;
        }
    }

    playSound(gameState.selectedBlock);
    chunk->updateBlock(localBlockX, localBlockY, localBlockZ, gameState.selectedBlock);
}

// ============================================================================
// UTILITY
// ============================================================================
void GameObject::playSound(const uint16_t block_id) {
    // A lookup table is used for identifying common blocks.
    static constexpr struct {
        uint16_t id;
        const char *sound;
        float volume;
    } soundMap[] = {
                {1, "../assets/sounds/DIRT.ogg", 0.4f},
                {2, "../assets/sounds/GRASS_BLOCK.ogg", 0.4f},
                {3, "../assets/sounds/STONE_BLOCK.ogg", 0.4f},
                {4, "../assets/sounds/LOG.ogg", 0.4f},
                {15, "../assets/sounds/SAND.ogg", 1.0f},
                {16, "../assets/sounds/STONE_BLOCK.ogg", 0.4f},
                {17, "../assets/sounds/STONE_BLOCK.ogg", 0.4f},
            };

    // Fast path for grass blocks (5-12)
    if (block_id >= 5 && block_id <= 12) [[likely]] {
        platform::playSound("../assets/sounds/GRASS.ogg", 0.4f);
        return;
    }

    // Linear search is fast for small tables
    for (const auto &[id, sound, volume]: soundMap) {
        if (id == block_id) {
            platform::playSound(sound, volume);
            return;
        }
    }
}

// ============================================================================
// PHYSICS & PLAYER MODEL
// ============================================================================

void GameObject::updatePlayerPhysics(const float deltaTime) {
    // Gravity
    playerVelocity.y -= 25.0f * deltaTime; // Gravity constant

    // Terminal velocity
    if (playerVelocity.y < -50.0f) playerVelocity.y = -50.0f;

    // Predicted position
    glm::vec3 newPos = camera.Position + playerVelocity * deltaTime;

    // Player AABB dimensions (approx 0.6 width, 1.8 height)
    const glm::vec3 halfExtents(0.3f, 0.9f, 0.3f);

    // Resolve collisions
    const auto result = Physics::resolveCollisions(camera.Position, newPos, halfExtents);

    // Update position and state
    camera.Position = result.position;
    playerOnGround = result.onGround;

    if (result.onGround || result.hitCeiling) {
        playerVelocity.y = 0.0f;
    }

    if (result.hitWallX) {
        playerVelocity.x = 0.0f;
    }
    if (result.hitWallZ) {
        playerVelocity.z = 0.0f;
    }
}

void GameObject::initializePlayerModel() {
    struct Vertex {
        float x, y, z;
        float r, g, b;
    };

    // Simple Steve-like model (normalized coordinates relative to center)
    // Head (color: skin), Body (color: cyan), Arms/Legs (color: purple/blue)
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    unsigned int indexOffset = 0;

    auto addBox = [&](float x, float y, float z, float w, float h, float d, float r, float g, float b) {
        float minX = x - w / 2;
        float maxX = x + w / 2;
        float minY = y - h / 2;
        float maxY = y + h / 2;
        float minZ = z - d / 2;
        float maxZ = z + d / 2;

        Vertex v[] = {
            {minX, minY, minZ, r, g, b}, {maxX, minY, minZ, r, g, b},
            {maxX, maxY, minZ, r, g, b}, {minX, maxY, minZ, r, g, b}, // Front
            {minX, minY, maxZ, r, g, b}, {maxX, minY, maxZ, r, g, b},
            {maxX, maxY, maxZ, r, g, b}, {minX, maxY, maxZ, r, g, b} // Back
        };

        for (const auto &vert: v) vertices.push_back(vert);

        unsigned int idx[] = {
            0, 1, 2, 2, 3, 0, // Front
            4, 5, 6, 6, 7, 4, // Back
            0, 4, 7, 7, 3, 0, // Left
            1, 5, 6, 6, 2, 1, // Right
            3, 2, 6, 6, 7, 3, // Top
            0, 1, 5, 5, 4, 0 // Bottom
        };

        for (unsigned int i: idx) indices.push_back(indexOffset + i);
        indexOffset += 8;
    };

    // Body parts relative to ground (0,0,0 is feet center)
    // Scale: 1 unit = 1 block. Player height ~1.8
    // Head: 0.25 size, at 1.6
    addBox(0.0f, 1.6f, 0.0f, 0.5f, 0.5f, 0.5f, 0.9f, 0.7f, 0.5f);
    // Body: 0.3 width, 0.6 height, at 1.05
    addBox(0.0f, 1.05f, 0.0f, 0.5f, 0.6f, 0.25f, 0.0f, 0.7f, 0.8f);
    // Left Arm
    addBox(-0.35f, 1.05f, 0.0f, 0.2f, 0.6f, 0.25f, 0.6f, 0.4f, 0.2f);
    // Right Arm
    addBox(0.35f, 1.05f, 0.0f, 0.2f, 0.6f, 0.25f, 0.6f, 0.4f, 0.2f);
    // Left Leg
    addBox(-0.13f, 0.375f, 0.0f, 0.24f, 0.75f, 0.25f, 0.2f, 0.2f, 0.8f);
    // Right Leg
    addBox(0.13f, 0.375f, 0.0f, 0.24f, 0.75f, 0.25f, 0.2f, 0.2f, 0.8f);

    Renderer::get().uploadMesh(playerModel, vertices.data(), vertices.size() * sizeof(Vertex), indices.data(),
                               static_cast<uint32_t>(indices.size()));
}

void GameObject::renderPlayerModel() {
    // The model is built from the feet up; savedPlayerPosition is at eye level.
    const glm::vec3 feetPos = savedPlayerPosition - glm::vec3(0.0f, 1.6f, 0.0f);
    const struct {
        glm::mat4 model;
        glm::vec4 color;
    } push{glm::translate(glm::mat4(1.0f), feetPos), glm::vec4(0.9f, 0.2f, 0.2f, 1.0f)}; // Bright red for visibility

    Renderer &renderer = Renderer::get();
    renderer.bindPipeline(PipelineId::Model);
    renderer.pushConstants(&push, sizeof(push));
    renderer.draw(playerModel);
}
