/**
 * @file Planet.h
 * @brief This header defines the Planet class which manages all our voxel chunks.
 */

#pragma once

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <glm/glm.hpp>
#include <thread>
#include <shared_mutex>
#include <atomic>
#include <memory_resource>

#include "ChunkPos.h"
#include "ChunkData.h"
#include "Chunk.h"
#include "ChunkPosHash.h"
#include "Frustum.h"
#include "ThreadPool.h"

/**
 * @enum OcclusionMethod
 * @brief Different ways to decide if a chunk is hidden or not based on GPU queries.
 */
enum class OcclusionMethod : uint8_t {
    VOTING,  ///< Discrete voting: we wait for N failures before hiding it (prevents flickering).
    EMA      ///< Exponential Moving Average: a smoother way to interpolate visibility.
};

/**
 * @class Planet
 * @brief The main class for our world system.
 * 
 * This class handles almost everything related to the world: generation in the background,
 * managing the chunks, frustum culling, and the parallel meshing. The project uses shared_mutex 
 * and atomics so it works well with the background threads without lagging the main game.
 */
class Planet
{
public:
    /**
     * @brief Constructor. Initializes shaders and reserves memory for chunk lists.
     */
    Planet(Shader* solidShader, Shader* waterShader, Shader* billboardShader, Shader* bboxShader);
    
    /**
     * @brief Destructor. Stops background threads and cleans up GPU resources.
     */
    ~Planet();

    /**
     * @brief The main update function called every frame.
     * 
     * Handles stuff like loading/unloading chunks, uploading new meshes to the GPU, 
     * and sorting chunks by distance.
     * @param cameraPos Where the player is in the world.
     * @param updateOcclusion Turn this off if you don't want to run occlusion queries.
     */
    void update(const glm::vec3& cameraPos, bool updateOcclusion = true);
    
    /**
     * @brief Renders the scene from the light's perspective to the shadow map.
     * @param shader The shadow depth shader.
     */
    void renderShadows(Shader* shader);

    // Stuff for shadows
    unsigned int depthMapFBO = 0;
    unsigned int depthMap = 0;
    const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;
    float shadowDistance = 256.0f; // Half-width of the shadow map in blocks; also limits which chunks cast shadows
    glm::mat4 lightSpaceMatrix;

    // Stuff for reflections
    unsigned int reflectionFBO = 0;
    unsigned int reflectionTexture = 0;
    unsigned int reflectionDepthRBO = 0;
    const unsigned int REFLECTION_WIDTH = 1024, REFLECTION_HEIGHT = 1024;
    glm::mat4 reflectionViewProjection;  // For water shader
    
    /**
     * @brief Renders the scene for water reflection.
     * @param cameraPos Current camera position.
     * @param cameraFront Current camera front vector.
     * @param aspectRatio Aspect ratio of the window.
     */
    void renderReflection(const glm::vec3& cameraPos, const glm::vec3& cameraFront, float aspectRatio);

    /**
     * @brief Updates the frustum state for culling.
     * @param frustumVP The view-projection matrix used for culling (can be frozen).
     * @param renderingVP The current view-projection matrix used for rendering.
     */
    void updateFrustum(const glm::mat4& frustumVP, const glm::mat4& renderingVP);

    /**
     * @brief Get a chunk based on its coordinates.
     * @return Pointer to the chunk, or nullptr if it's not actually loaded yet.
     */
    Chunk* getChunk(ChunkPos chunkPos);

    /**
     * @brief Clear the loading queue so we can rebuild it next frame.
     */
    void clearChunkQueue();

private:
    /**
     * @brief This is where the background thread starts doing its thing.
     */
    void chunkThreadUpdate();

    /** @name Internal Visibility Helpers @{ */
    [[nodiscard]] inline bool isOutOfRenderDistance(const ChunkPos& pos) const noexcept;
    [[nodiscard]] static std::pair<glm::vec3, glm::vec3> getChunkBounds(const ChunkPos& pos) noexcept;
    void cleanupUnusedChunkData();
    /** @} */

    /** @name Queue Logic @{ */
    void updateChunkQueue();
    void buildChunkQueue();
    void addChunkRing(int radius);
    inline void addChunkEdges(int radius, int edge, int y) noexcept;
    inline void addChunkCorners(int radius, int y) noexcept;
    inline void addChunkIfMissing(int x, int y, int z);
    /** @} */

    /** @name Background Tasks @{ */
    void processChunkDataQueue();
    void processChunkQueue();
    void processRegenQueue();
    bool populateChunkData(Chunk* chunk, const ChunkPos& chunkPos);
    std::shared_ptr<ChunkData> getOrCreateChunkData(const ChunkPos& pos);
    /** @} */

public:
    static Planet* planet; ///< A global pointer so we can access the world from anywhere.

    /** @name Render Stats @{ */
    std::atomic<unsigned int> numChunks{0};         ///< How many chunks we have in memory.
    std::atomic<unsigned int> numChunksRendered{0}; ///< How many chunks we actually drew this frame.
    /** @} */

    int renderDistance = 30; ///< Radius of loaded chunks around the player.
    int renderHeight = 2;   ///< Vertical extent of chunk loading.

private:
    /** @name Data Structures
     * Data structures for keeping track of our chunks.
     * @{
     */
    std::unordered_map<ChunkPos, Chunk*, ChunkPosHash> chunks;         ///< Our main map that has all the chunks we're using.
    std::vector<Chunk*> chunkList;                                    ///< A list that helps us loop through chunks really fast.
    std::unordered_map<ChunkPos, std::shared_ptr<ChunkData>, ChunkPosHash> chunkData; ///< Data for the voxels, shared between chunks.
    /** @} */

    /** @name Queues
     * Queues that our background thread works on.
     * @{
     */
    std::queue<ChunkPos> chunkQueue;     ///< Chunks that need to be created.
    std::queue<ChunkPos> chunkDataQueue; ///< Chunks that need our noise algorithm to run.
    std::queue<ChunkPos> regenQueue;    ///< Chunks that need their 3D mesh rebuilt.
    std::unordered_set<ChunkPos, ChunkPosHash> regenPending; ///< Dedupes regenQueue so a chunk is remeshed once per batch of neighbour loads.
    std::atomic<unsigned int> chunksLoading{0}; ///< How many things are currently loading.
    /** @} */

    /** @name Synchronization & State @{ */
    std::atomic<int> lastCamX{-100}, lastCamY{-100}, lastCamZ{-100};
    std::atomic<int> camChunkX{-100}, camChunkY{-100}, camChunkZ{-100};
    std::atomic<long> last_seed{0};

    Shader* solidShader;
    Shader* waterShader;
    Shader* billboardShader;
    Shader* bboxShader;

    std::thread chunkThread;                   ///< The thread that handles the world in the background.
    mutable std::shared_mutex chunkMutex;      ///< A lock so multiple threads don't mess up our chunk maps.
    std::atomic<bool> shouldEnd{false};        ///< A flag to tell the threads to stop.

    Frustum frustum;                           ///< Used to see what's actually in front of the camera.
    glm::mat4 lastViewProjection = glm::mat4(1.0f);
    ThreadPool chunkGenPool;                   ///< A pool of threads to generate world data in parallel.
    int visibleCount = 0;
    /** @} */

    /** @name Render Caching
     * Lists we pre-allocate so we don't have to keep creating them every frame.
     * @{
     */
    int prevSortCamX = -1000, prevSortCamZ = -1000;
    std::vector<Chunk*> solidChunks;
    std::vector<Chunk*> billboardChunks;
    std::vector<Chunk*> waterChunks;
    std::vector<Chunk*> frustumVisibleChunks;
    std::vector<ChunkPos> toDeleteList;

    std::vector<Chunk*> chunkPool;             ///< A place where we put old chunks so we can reuse them.
    std::vector<Chunk*> renderChunks;          ///< A list we use for rendering so it doesn't change while we're drawing.
    std::atomic<bool> renderChunksDirty{false};
    /** @} */

    OcclusionMethod occlusionMethod = OcclusionMethod::VOTING;

    /** @name Hardware Occlusion Resources @{ */
    unsigned int bboxVAO = 0, bboxVBO = 0, bboxEBO = 0;
    void initBoundingBoxMesh();
    /** @} */

    /**
     * @brief Caches computed values to avoid redundant per-chunk recomputation.
     */
    struct CameraCache {
        float maxDistSq{0.0f};
        int framesSinceMove{0};
    } cameraCache;
    
public:
    /**
     * @brief Renders a debug bounding box.
     */
    void drawBoundingBox(const glm::vec3& center, const glm::vec3& extents);
};
