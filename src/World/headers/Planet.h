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
#include <array>
#include <atomic>
#include <chrono>
#include <memory_resource>

#include "ChunkPos.h"
#include "ChunkData.h"
#include "Chunk.h"
#include "ChunkPosHash.h"
#include "Frustum.h"
#include "ThreadPool.h"

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
     * @brief Constructor. Starts the chunk thread and reserves memory for chunk lists.
     */
    Planet();

    /**
     * @brief Destructor. Stops background threads; chunk meshes are released to the renderer.
     */
    ~Planet();

    /**
     * @brief Per-frame world update, called after Renderer::beginFrame and before any pass.
     *
     * Loads/unloads chunks, uploads new meshes, and builds the culled, sorted draw lists.
     * @param cameraPos Where the player is in the world.
     */
    void prepare(const glm::vec3& cameraPos);

    /// Where real chunks are drawn (see Renderer::uploadChunkMask); null if unchanged since the last call.
    [[nodiscard]] const uint8_t *chunkMaskData() const { return chunkMask.data(); }
    const uint8_t *takeChunkMaskIfChanged() {
        if (!chunkMaskChanged) return nullptr;
        chunkMaskChanged = false;
        return chunkMask.data();
    }

    /// Rebuilds every chunk's mesh in the background (e.g. after a graphics setting changed).
    /// The old meshes keep drawing until the new ones are uploaded.
    void remeshAll();

    /// Draws solid blocks, billboards and water from prepare()'s lists; must run inside the main pass.
    void render();

    /// Draws shadow casters near the player; must run inside the shadow pass.
    void renderShadows(const glm::mat4& lightSpace);

    float shadowDistance = 256.0f; // Half-width of the shadow map in blocks; also limits which chunks cast shadows
    [[nodiscard]] int shadowRadiusChunks() const { return static_cast<int>(shadowDistance / CHUNK_WIDTH) + 1; }

    /// True (once) if a shadow caster near the camera was uploaded or a chunk was removed since the last call.
    bool consumeShadowDirty() { const bool dirty = shadowDirty; shadowDirty = false; return dirty; }

    /// View and projection of the camera mirrored below the water plane.
    struct ReflectionCamera {
        glm::mat4 view;
        glm::mat4 projection;
    };
    [[nodiscard]] static ReflectionCamera reflectionCamera(const glm::vec3& cameraPos, const glm::vec3& cameraFront, const glm::mat4& projection);

    /// Clip plane that keeps only geometry above the water surface in the reflection.
    static inline const glm::vec4 REFLECTION_CLIP_PLANE{0.0f, 1.0f, 0.0f, -64.5f};
    /// Real chunks drawn in the reflection (distant reflections are tiny and distorted by the water shader).
    static constexpr float REFLECTION_RANGE = 8.0f * CHUNK_WIDTH;

    /// True when water was visible last frame, i.e. the reflection is worth rendering.
    [[nodiscard]] bool hasVisibleWater() const { return !waterChunks.empty(); }

    /// Draws nearby chunks from the reflection camera; must run inside the reflection pass.
    void renderReflection(const glm::vec3& cameraPos, const glm::mat4& reflectionViewProjection);

    /**
     * @brief Updates the frustum used for culling.
     * @param frustumVP The view-projection matrix used for culling (frozen while in freecam).
     */
    void updateFrustum(const glm::mat4& frustumVP);

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
    /// Moves queued builds and remeshes into the worker pool. Returns false when there was nothing to do.
    bool dispatchWork();
    /// Worker job: fetch (or generate) block data for a chunk and its neighbours, mesh it, publish it.
    void buildChunk(ChunkPos chunkPos);
    /// Worker job: (re)build an existing chunk's mesh, recomputing its light from lit neighbours.
    void remeshChunk(ChunkPos chunkPos);
    /// True when every horizontal neighbour is lit or outside the render distance. Needs chunkMutex.
    [[nodiscard]] bool neighborsLit(ChunkPos pos) const;
    /// Queues the chunk's first mesh once its neighbours are lit. Needs chunkMutex (exclusive).
    void requestMeshIfReady(Chunk *chunk);
    bool populateChunkData(Chunk* chunk, const ChunkPos& chunkPos);
    std::shared_ptr<ChunkData> getOrCreateChunkData(const ChunkPos& pos);
    /** @} */

public:
    static Planet* planet; ///< A global pointer so we can access the world from anywhere.

    /** @name Render Stats @{ */
    std::atomic<unsigned int> numChunks{0};         ///< How many chunks we have in memory.
    std::atomic<unsigned int> numChunksRendered{0}; ///< How many chunks we actually drew this frame.
    /** @} */

    int renderDistance = 16; ///< Radius of real (block) chunks around the player; distant terrain covers the rest.
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
    std::unordered_set<ChunkPos, ChunkPosHash> inFlight; ///< Chunks currently being built by a worker.
    std::queue<ChunkPos> regenQueue;    ///< Chunks that need their 3D mesh rebuilt.
    std::unordered_set<ChunkPos, ChunkPosHash> regenPending; ///< Dedupes regenQueue so a chunk is remeshed once per batch of neighbour loads.
    std::atomic<unsigned int> chunksLoading{0}; ///< How many things are currently loading.
    /** @} */

    /** @name Synchronization & State @{ */
    std::atomic<int> lastCamX{-100}, lastCamY{-100}, lastCamZ{-100};
    std::atomic<int> camChunkX{-100}, camChunkY{-100}, camChunkZ{-100};
    std::atomic<long> last_seed{0};

    std::thread chunkThread;                   ///< The thread that handles the world in the background.
    mutable std::shared_mutex chunkMutex;      ///< A lock so multiple threads don't mess up our chunk maps.
    std::atomic<bool> shouldEnd{false};        ///< A flag to tell the threads to stop.

    Frustum frustum;                           ///< Used to see what's actually in front of the camera.
    /// Generates and meshes chunks in parallel; two hardware threads stay free for rendering and dispatch.
    ThreadPool chunkGenPool{std::max(1u, std::thread::hardware_concurrency() - 2)};
    static constexpr int MAX_UPLOADS_PER_FRAME = 24;
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
    std::vector<Chunk*> shadowCasters;         ///< Scratch list for the shadow pass.
    bool shadowDirty = true;                   ///< See consumeShadowDirty(); render thread only.
    static constexpr int MASK = static_cast<int>(Renderer::CHUNK_MASK_SIZE);
    std::array<uint8_t, MASK * MASK> chunkMask{}, chunkMaskNext{};
    bool chunkMaskChanged = true;

    /** @name Load timing (see LoadStats.h) @{ */
    static constexpr int NEAR_RADIUS = 4;      ///< The 9x9 chunks around the camera count as "nearby".
    std::chrono::steady_clock::time_point loadStart;
    bool nearReported = false, allReported = false;
    void reportLoadProgress(int readyNear, int readyChunks);
    /** @} */
    std::vector<Chunk*> reflectedChunks;       ///< Scratch list for the reflection pass.

    std::vector<Chunk*> chunkPool;             ///< A place where we put old chunks so we can reuse them.
    std::vector<Chunk*> renderChunks;          ///< A list we use for rendering so it doesn't change while we're drawing.
    std::atomic<bool> renderChunksDirty{false};
    /** @} */

    /**
     * @brief Caches computed values to avoid redundant per-chunk recomputation.
     */
    struct CameraCache {
        float maxDistSq{0.0f};
        int framesSinceMove{0};
    } cameraCache;
};
