#pragma once

#include "../../Platform/Platform.h"
#include "Renderer.h"
#include "Canvas.h"
#include "Camera.h"
#include "Planet.h"
#include <string>
#include <glm/gtc/type_ptr.hpp>
#include "CheckBox.h"
#include "Physics.h"
#include "Slider.h"
#include "TypeBox.h"
#include "Button.h"
#include "Sky.h"

// Forward declarations
class Chunk;

// Utility function moved to implementation
int convertToASCIIsum(const std::string &input);

// Game state enum
enum GameStateEnum {
    PLAYING,
    PAUSED
};

// Menu state enum
enum class MenuState {
    MAIN,
    VIDEO,
    GAME
};

struct GameState {
    GameStateEnum state = PAUSED;
    MenuState menuState = MenuState::MAIN;
    uint16_t selectedBlock = 1;
};

// ============================================================================
// GAME OBJECT CLASS
// ============================================================================
/**
 * @class GameObject
 * @brief The central controller class for the game engine, following the Singleton pattern.
 * 
 * GameObject manages the main game loop, initialization of all subsystems (window, renderer, UI, Planet),
 * and high-level logic like input handling, rendering orchestration, and block interaction.
 * 
 * The class organizes variables so that frequently used data (like the Camera)
 * is easier for the program to find quickly.
 */
class GameObject {
public:
    /**
     * @brief Gets the singleton instance of the GameObject.
     * @param x Initial window width.
     * @param y Initial window height.
     * @param windowName Title of the game window.
     * @return Reference to the GameObject instance.
     */
    static GameObject &getInstance(float x, float y, const std::string &windowName);

    /**
     * @brief Initializes the entire game world, runs the main loop until the window closes,
     * then shuts every subsystem down in reverse order.
     */
    void init();

    // Prevent copying to maintain Singleton integrity
    GameObject(const GameObject &) = delete;
    GameObject &operator=(const GameObject &) = delete;

private:
    /**
     * @brief Constructor for GameObject.
     * @param x Initial window width.
     * @param y Initial window height.
     * @param windowName Name of the window.
     */
    GameObject(float x, float y, std::string windowName);
    
    ~GameObject() = default;

    /** @name Initialization Helpers @{ */
    void initializeGraphics();
    void initializeUIElements();
    void initializeOutlineMesh();
    void initializePlayerModel();
    void shutdown();
    /** @} */

    /** @name Live Update Systems @{ */
    /// Records and submits one frame: shadow, reflection, main and UI passes.
    void renderFrame();
    /// Light-space matrix for the shadow map, snapped to texels so shadows don't shimmer.
    [[nodiscard]] glm::mat4 computeLightSpace() const;
    /// Re-lays out the UI after the window changed size.
    void onResize();
    /// Updates the window title with performance metrics (FPS, Chunk Count).
    static void updateWindowTitle();
    /// Applies the FPS cap / VSync choice from the settings menu.
    void updateFPSSettings() const;
    /// Updates player physics (gravity, collision) when noclip is disabled.
    void updatePlayerPhysics(float deltaTime);
    /** @} */

    /** @name Input Callbacks @{ */
    void mouseCallBack();
    void keyboardCallBack(float deltaTime);
    void handlePlayingMouseInput(const platform::MouseState &ms);
    void handlePausedMouseInput(const platform::MouseState &ms);
    /** @} */

    /** @name Rendering Layers @{ */
    void renderUI() const;
    void renderBlockOutline();
    void renderFluidOverlay() const;
    void renderCrosshair() const;
    void renderDebugInfo() const;
    void renderPauseMenu() const;
    void renderMainMenu() const;
    void renderVideoSettings() const;
    void renderGameSettings() const;
    void renderPlayerModel();
    /** @} */

    /** @name Block Interaction Logic @{ */
    static void handleBlockBreak(const Physics::RaycastResult &result);
    void handleBlockPlace(const Physics::RaycastResult &result) const;
    void handleBlockPick(const Physics::RaycastResult &result);
    /** @} */

    /** @name Audio Utilities @{ */
    static void playSound(uint16_t block_id);
    /** @} */

    // ========================================================================
    // MEMBER VARIABLES
    // ========================================================================

    /// @brief Primary camera instance. Most frequently accessed during matrix updates.
    Camera camera;
    
    /// @brief Holds the current play state (Paused/Playing) and UI selection.
    GameState gameState;

    /** @brief Matrix caching system to avoid redundant perspective/view calculations. */
    struct CachedMatrices {
        glm::mat4 view;
        glm::mat4 projection;
        bool dirty = true; ///< Set to true whenever the camera moves or window resizes.
    } cachedMatrices;

    /** @brief Cached UI center points to avoid repeated division in rendering code. */
    struct {
        float x = 0.0f;
        float y = 0.0f;
    } cachedWindowCenter;

    /** @name Screen Dimensions @{ */
    float windowX, windowY;
    float lastX, lastY;
    /** @} */

    /// @brief Target frame rate (55 = VSync, 361 = Uncapped).
    int fpsCap = 55;

    /** @name Debug/Freecam State @{ */
    bool freecamActive = false;
    glm::vec3 savedPlayerPosition;
    glm::mat4 savedViewProjection;
    /** @} */

    /** @name Player Physics State @{ */
    bool noclipEnabled = true;          ///< If true, player can fly through blocks (Creative/Spectator).
    glm::vec3 playerVelocity{0.0f};     ///< Current velocity vector for physics simulation.
    bool playerOnGround = false;        ///< True when the player is standing on solid ground.
    bool superJumpEnabled = false;
    bool uncapSpeedEnabled = false;
    /** @} */

    /** @name GPU Meshes @{ */
    GpuMesh playerModel;
    GpuBuffer outlineVertices;
    /** @} */

    /// @brief Manages the day/night cycle and atmospheric rendering.
    Sky sky;

    /** @name UI Components @{ */
    Checkbox fullscreenCheckBox;
    Checkbox superJumpBox;
    Checkbox uncapSpeedBox;
    Slider fpsSlider;
    Slider renderDistanceSlider;
    TypeBox seedBox;
    
    // New Menu UI
    Button videoSettingsBtn;
    Button gameSettingsBtn;
    Button backBtn;
    Button quitBtn; // Optional
    
    Checkbox dynamicShadowsBox;
    Checkbox msaaBox;
    Checkbox fancyLeavesBox;

    bool dynamicShadowsEnabled = true;
    bool msaaEnabled = true;
    bool simpleLightingEnabled = false;

    /** @name Shadow map cache (see renderFrame) @{ */
    glm::mat4 cachedLightSpace{1.0f};
    bool cachedShadowsOn = false;
    bool shadowCacheValid = false;
    /** @} */
    /** @} */

    std::string window_name;
};

// ============================================================================
// CONSTANTS - constexpr for compile-time evaluation
// ============================================================================
namespace GameConstants {
    constexpr float CROSSHAIR_SIZE = 5.0f;
    constexpr float CROSSHAIR_THICKNESS = 25.0f;
    constexpr float MOVEMENT_SPEED_MULTIPLIER = 0.02f;
    constexpr float RAYCAST_DISTANCE = 5.0f;
    constexpr float MUSIC_VOLUME = 0.8f;
    constexpr int FPS_VSYNC_THRESHOLD = 55;
    constexpr int FPS_MAX_CAPPED = 360;

    // Outline vertices as constexpr array
    constexpr float OUTLINE_VERTICES[] = {
        -.001f, -.001f, -.001f, 1.001f, -.001f, -.001f,
        1.001f, -.001f, -.001f, 1.001f, 1.001f, -.001f,
        1.001f, 1.001f, -.001f, -.001f, 1.001f, -.001f,
        -.001f, 1.001f, -.001f, -.001f, -.001f, -.001f,

        -.001f, -.001f, -.001f, -.001f, -.001f, 1.001f,
        -.001f, -.001f, 1.001f, -.001f, 1.001f, 1.001f,
        -.001f, 1.001f, 1.001f, -.001f, 1.001f, -.001f,

        1.001f, -.001f, -.001f, 1.001f, -.001f, 1.001f,
        1.001f, -.001f, 1.001f, 1.001f, 1.001f, 1.001f,
        1.001f, 1.001f, 1.001f, 1.001f, 1.001f, -.001f,

        -.001f, -.001f, 1.001f, 1.001f, -.001f, 1.001f,
        -.001f, 1.001f, 1.001f, 1.001f, 1.001f, 1.001f,
    };
}