/**
 * @file Planet.cpp
 * @brief Implementation for the Planet class. This handles everything from world gen to rendering.
 */

#include "Planet.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include "WorldGen.h"
#include "LoadStats.h"
#include "glm/gtc/matrix_transform.hpp"

// These macros help the CPU predict which way the code usually goes.
#if defined(__GNUC__) || defined(__clang__)
    #define LIKELY(x)   __builtin_expect(!!(x), 1)
    #define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
    #define LIKELY(x)   (x)
    #define UNLIKELY(x) (x)
#endif

Planet *Planet::planet = nullptr;

// Neighbor offsets are pre-calculated to avoid repetitive work.
static constexpr int NEIGHBOR_OFFSETS[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}
};

// Constants for reserving memory to avoid frequent reallocations.
static constexpr size_t EXPECTED_CHUNK_COUNT = 4000;
static constexpr size_t EXPECTED_VISIBLE_CHUNKS = 800;

Planet::Planet() {
    loadStart = loadstats::Clock::now();
    // Reserve some space upfront so it doesn't slow down the game later.
    chunks.reserve(EXPECTED_CHUNK_COUNT);
    chunkList.reserve(EXPECTED_CHUNK_COUNT);
    chunkData.reserve(EXPECTED_CHUNK_COUNT);
    chunkPool.reserve(200);

    renderChunks.reserve(EXPECTED_CHUNK_COUNT);
    solidChunks.reserve(EXPECTED_VISIBLE_CHUNKS);
    billboardChunks.reserve(EXPECTED_VISIBLE_CHUNKS / 2);
    waterChunks.reserve(EXPECTED_VISIBLE_CHUNKS / 4);
    frustumVisibleChunks.reserve(EXPECTED_VISIBLE_CHUNKS);
    toDeleteList.reserve(100);

    chunkThread = std::thread(&Planet::chunkThreadUpdate, this);
}

Planet::~Planet() {
    shouldEnd.store(true, std::memory_order_release);
    if (chunkThread.joinable()) {
        chunkThread.join();
    }
    chunkGenPool.clearPending();
    chunkGenPool.waitAll();

    // Each chunk hands its GPU meshes back to the renderer as it is destroyed.
    for (auto &[pos, chunk] : chunks) delete chunk;
    for (Chunk *chunk : chunkPool) delete chunk;
}

//==============================================================================
// MAIN UPDATE - Called every frame
//==============================================================================

void Planet::prepare(const glm::vec3 &cameraPos) {
    // Relaxed atomics are used here because they are faster and suitable for simple seed checks.
    const long currentSeed = SEED;
    const long cachedSeed = last_seed.load(std::memory_order_relaxed);

    // Check if the seed changed early on.
    if (UNLIKELY(currentSeed != cachedSeed)) {
        // Workers bail out once they see the new seed; wait for them before freeing their chunks.
        chunkGenPool.clearPending();
        chunkGenPool.waitAll();
        std::unique_lock lock(chunkMutex);

        if (currentSeed != last_seed.load(std::memory_order_relaxed)) {
            last_seed.store(currentSeed, std::memory_order_relaxed);

            for (auto &[pos, chunk]: chunks) {
                delete chunk;
            }
            chunks.clear();
            
            for (Chunk* chunk : chunkPool) {
                delete chunk;
            }
            chunkPool.clear();

            chunkData.clear();
            chunkList.clear();

            renderChunks.clear();
            solidChunks.clear();
            billboardChunks.clear();
            waterChunks.clear();
            frustumVisibleChunks.clear();
            toDeleteList.clear();
            renderChunksDirty.store(true, std::memory_order_release);

            chunkQueue = {};
            regenQueue = {};
            regenPending.clear();
            inFlight.clear();
            loadStart = loadstats::Clock::now();
            nearReported = allReported = false;
            loadstats::generationMicros = 0;
            loadstats::meshingMicros = 0;
            loadstats::generatedChunks = 0;
            loadstats::meshedChunks = 0;
        }
        return;
    }

    const int newCamChunkX = static_cast<int>(std::floor(cameraPos.x / CHUNK_WIDTH));
    const int newCamChunkY = static_cast<int>(std::floor(cameraPos.y / CHUNK_HEIGHT));
    const int newCamChunkZ = static_cast<int>(std::floor(cameraPos.z / CHUNK_WIDTH));

    // Use atomics for lock-free updates.
    camChunkX.store(newCamChunkX, std::memory_order_relaxed);
    camChunkY.store(newCamChunkY, std::memory_order_relaxed);
    camChunkZ.store(newCamChunkZ, std::memory_order_relaxed);

    chunksLoading.store(0, std::memory_order_relaxed);
    numChunks.store(0, std::memory_order_relaxed);
    numChunksRendered.store(0, std::memory_order_relaxed);

    // Clear without deallocation (capacity preserved).
    solidChunks.clear();
    billboardChunks.clear();
    waterChunks.clear();
    frustumVisibleChunks.clear();
    toDeleteList.clear();

    // Use atomic load for lock-free check.
    if (UNLIKELY(renderChunksDirty.load(std::memory_order_acquire))) {
        std::unique_lock lock(chunkMutex);
        if (renderChunksDirty.load(std::memory_order_relaxed)) {
            renderChunks = chunkList;
            renderChunksDirty.store(false, std::memory_order_release);
        }
    }

    int chunksUploadedThisFrame = 0;
    chunkMaskNext.fill(0);
    int readyChunks = 0, readyNear = 0;

    // Cache commonly used values to avoid recomputation in loops.
    const float maxDist = static_cast<float>(renderDistance) * CHUNK_WIDTH;
    const float maxDistSq = maxDist * maxDist;
    cameraCache.maxDistSq = maxDistSq;

    constexpr float billboardMaxDist = 10.0f * CHUNK_WIDTH;
    constexpr float billboardMaxDistSq = billboardMaxDist * billboardMaxDist;

    const float camPosX = cameraPos.x;
    const float camPosY = cameraPos.y;
    const float camPosZ = cameraPos.z;

    // Loop through all active chunks. NO LOCK HELD during iteration.
    // size_t is used here for better compatibility with 64-bit systems.
    const size_t renderChunksSize = renderChunks.size();
    for (size_t i = 0; i < renderChunksSize; ++i) {
        Chunk * const chunk = renderChunks[i];
        const ChunkPos &pos = chunk->chunkPos;
        numChunks.fetch_add(1, std::memory_order_relaxed);

        if (UNLIKELY(isOutOfRenderDistance(pos))) {
            toDeleteList.push_back(pos);
            continue;
        }
        // Real chunks are drawn out to a horizontal radius; the mask tells distant terrain exactly which ones,
        // so it fills the square's corners and never leaves a gap (or overlaps) at the seam.
        const float horizontalDx = chunk->cullingCenter.x - camPosX;
        const float horizontalDz = chunk->cullingCenter.z - camPosZ;
        const bool withinRadius = horizontalDx * horizontalDx + horizontalDz * horizontalDz <= maxDistSq;
        if (chunk->hasMesh() && withinRadius) chunkMaskNext[(pos.z & (MASK - 1)) * MASK + (pos.x & (MASK - 1))] = 1;

        if (chunk->ready) {
            ++readyChunks;
            if (std::abs(pos.x - newCamChunkX) <= NEAR_RADIUS && std::abs(pos.z - newCamChunkZ) <= NEAR_RADIUS) ++readyNear;
        }

        const bool needsUpload = chunk->generated && !chunk->ready;
        if (UNLIKELY(needsUpload)) {
            // Uploads are capped per frame so a burst of finished chunks can't stall one frame.
            if (chunksUploadedThisFrame < MAX_UPLOADS_PER_FRAME && chunk->uploadMesh()) {
                ++chunksUploadedThisFrame;
                if (std::abs(pos.x - newCamChunkX) <= shadowRadiusChunks() && std::abs(pos.z - newCamChunkZ) <= shadowRadiusChunks()) {
                    shadowDirty = true;
                }
            }
            chunksLoading.fetch_add(1, std::memory_order_relaxed);
        }

        if (LIKELY(chunk->hasMesh() && frustum.isBoxVisible(chunk->cullingCenter, chunk->cullingExtents))) {
            const float dx = chunk->cullingCenter.x - camPosX;
            const float dy = chunk->cullingCenter.y - camPosY;
            const float dz = chunk->cullingCenter.z - camPosZ;

            const float distSq = dx*dx + dy*dy + dz*dz;

            if (UNLIKELY(!withinRadius)) continue;

            chunk->cachedDistSq = distSq;
            frustumVisibleChunks.push_back(chunk);

            {
                numChunksRendered.fetch_add(1, std::memory_order_relaxed);

                if (chunk->hasSolid()) solidChunks.push_back(chunk);

                // Billboard LOD: Only render decorations within a specific range.
                if (chunk->hasBillboard() && distSq <= billboardMaxDistSq) {
                    billboardChunks.push_back(chunk);
                }

                if (chunk->hasWater()) waterChunks.push_back(chunk);
            }
        }
    }

    reportLoadProgress(readyNear, readyChunks);
    if (chunkMaskNext != chunkMask) {
        chunkMask = chunkMaskNext;
        chunkMaskChanged = true;
    }

    // We need to sort these lists so that depth testing and blending works right.
    const bool cameraMoved = (newCamChunkX != prevSortCamX) || (newCamChunkZ != prevSortCamZ);
    const bool shouldSort = cameraMoved || !toDeleteList.empty();

    if (LIKELY(shouldSort)) {
        prevSortCamX = newCamChunkX;
        prevSortCamZ = newCamChunkZ;

        auto frontToBackSort = [](const Chunk * const a, const Chunk * const b) noexcept {
            return a->cachedDistSq < b->cachedDistSq;
        };

        auto backToFrontSort = [](const Chunk * const a, const Chunk * const b) noexcept {
            return a->cachedDistSq > b->cachedDistSq;
        };

        std::sort(frustumVisibleChunks.begin(), frustumVisibleChunks.end(), frontToBackSort);
        std::sort(solidChunks.begin(), solidChunks.end(), frontToBackSort);
        std::sort(waterChunks.begin(), waterChunks.end(), backToFrontSort);
        std::sort(billboardChunks.begin(), billboardChunks.end(), frontToBackSort);
    }

    if (UNLIKELY(!toDeleteList.empty())) {
        std::unique_lock lock(chunkMutex);

        for (const ChunkPos &pos: toDeleteList) {
            auto it = chunks.find(pos);
            if (it != chunks.end()) {
                Chunk* const chunk = it->second;

                // This is a neat trick to remove things from the list in constant time:
                // swap the one we want with the last one, then pop the back.
                const size_t index = chunk->listIndex;
                if (LIKELY(index < chunkList.size())) {
                    Chunk* const lastChunk = chunkList.back();
                    chunkList[index] = lastChunk;
                    lastChunk->listIndex = index;
                    chunkList.pop_back();
                }

                chunkPool.push_back(chunk);
                chunks.erase(it);
                shadowDirty = true;
            }
        }

        // Pooled chunks may be reset by the chunk thread at any moment, so the shadow and
        // reflection passes later this frame must not see them in the snapshot.
        renderChunks = chunkList;
        renderChunksDirty.store(false, std::memory_order_release);
    }

}

void Planet::reportLoadProgress(const int readyNear, const int readyChunks) {
    using namespace loadstats;
    const auto ms = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - loadStart).count();
    };
    constexpr int nearCount = (2 * NEAR_RADIUS + 1) * (2 * NEAR_RADIUS + 1);
    const int allCount = (2 * renderDistance + 1) * (2 * renderDistance + 1);

    if (!nearReported && readyNear >= nearCount) {
        nearReported = true;
        std::cout << "[LOAD] nearest " << nearCount << " chunks drawable after " << ms() << " ms" << std::endl;
    }
    if (!allReported && readyChunks >= allCount) {
        allReported = true;
        const int64_t gen = generatedChunks.load(), meshed = meshedChunks.load();
        std::cout << "[LOAD] all " << allCount << " chunks drawable after " << ms() << " ms; generation "
                  << generationMicros.load() / 1000 << " ms CPU for " << gen << " chunks ("
                  << (gen ? generationMicros.load() / gen : 0) << " us each), meshing "
                  << meshingMicros.load() / 1000 << " ms CPU for " << meshed << " meshes ("
                  << (meshed ? meshingMicros.load() / meshed : 0) << " us each)" << std::endl;
    }
}

void Planet::render() {
    Renderer &renderer = Renderer::get();

    renderer.mark("main: sky");
    // Opaque terrain first, front to back, so hidden fragments are rejected before shading;
    // then the alpha-tested leaves.
    renderer.bindPipeline(PipelineId::WorldOpaque);
    for (Chunk *const chunk : solidChunks) chunk->renderAllSolid();
    renderer.mark("main: opaque");
    renderer.bindPipeline(PipelineId::World);
    for (Chunk *const chunk : solidChunks) chunk->renderAllCutout();
    renderer.mark("main: cutout");

    renderer.bindPipeline(PipelineId::Billboard);
    for (Chunk *const chunk : billboardChunks) chunk->renderAllBillboard();
    renderer.mark("main: plants");

    renderer.bindPipeline(PipelineId::Fluid);
    for (Chunk *const chunk : waterChunks) chunk->renderAllWater();
    renderer.mark("main: water");
}

void Planet::renderShadows(const glm::mat4 &lightSpace) {
    Renderer &renderer = Renderer::get();
    
    // Shadow culling is implemented here to only render shadows for chunks near the player.
    const int camX = camChunkX.load(std::memory_order_relaxed);
    const int camZ = camChunkZ.load(std::memory_order_relaxed);
    const int shadowRadius = shadowRadiusChunks();
    
    // renderChunks is the render thread's snapshot; chunkList belongs to the chunk thread.
    shadowCasters.clear();
    for (Chunk *const chunk : renderChunks) {
        if (!chunk->hasMesh()) continue;
        
        const ChunkPos& pos = chunk->chunkPos;
        if (abs(pos.x - camX) > shadowRadius || abs(pos.z - camZ) > shadowRadius) continue;
        
        shadowCasters.push_back(chunk);
    }

    renderer.bindPipeline(PipelineId::ShadowWorld);
    renderer.pushConstants(&lightSpace, sizeof(lightSpace));
    for (Chunk *const chunk : shadowCasters) {
        chunk->renderAllSolid();
        chunk->renderAllCutout();
    }

    // Plants and leaves cast shadows too.
    renderer.bindPipeline(PipelineId::ShadowBillboard);
    renderer.pushConstants(&lightSpace, sizeof(lightSpace));
    for (Chunk *const chunk : shadowCasters) chunk->renderAllBillboard();
}


Planet::ReflectionCamera Planet::reflectionCamera(const glm::vec3 &cameraPos, const glm::vec3 &cameraFront, const glm::mat4 &projection) {
    const float waterLevel = static_cast<float>(WATER_LEVEL);

    // Mirror the camera below the water plane and flip its pitch.
    glm::vec3 reflectPos = cameraPos;
    reflectPos.y = waterLevel - (cameraPos.y - waterLevel);
    glm::vec3 reflectFront = cameraFront;
    reflectFront.y = -reflectFront.y;

    return {
        glm::lookAt(reflectPos, reflectPos + reflectFront, glm::vec3(0, 1, 0)),
        projection,
    };
}

void Planet::renderReflection(const glm::vec3 &cameraPos, const glm::mat4 &reflectionViewProjection) {
    // Distant reflections are tiny and distorted by the water shader, so only nearby chunks are drawn.
    constexpr float REFLECTION_RANGE = 8.0f * CHUNK_WIDTH;
    Frustum reflectionFrustum;
    reflectionFrustum.update(reflectionViewProjection);
    reflectedChunks.clear();
    for (Chunk *const chunk : renderChunks) {
        if (!chunk->hasMesh()) continue;
        const float dx = chunk->cullingCenter.x - cameraPos.x;
        const float dz = chunk->cullingCenter.z - cameraPos.z;
        if (dx * dx + dz * dz > REFLECTION_RANGE * REFLECTION_RANGE) continue;
        if (reflectionFrustum.isBoxVisible(chunk->cullingCenter, chunk->cullingExtents)) reflectedChunks.push_back(chunk);
    }

    Renderer &renderer = Renderer::get();
    renderer.bindPipeline(PipelineId::WorldOpaque);
    for (Chunk *const chunk : reflectedChunks) chunk->renderAllSolid();
    renderer.bindPipeline(PipelineId::World);
    for (Chunk *const chunk : reflectedChunks) chunk->renderAllCutout();
    renderer.bindPipeline(PipelineId::Billboard);
    for (Chunk *const chunk : reflectedChunks) chunk->renderAllBillboard();
}

//==============================================================================
// HELPER METHODS - Inlined for performance
//==============================================================================

inline bool Planet::isOutOfRenderDistance(const ChunkPos &pos) const noexcept {
    const int camX = camChunkX.load(std::memory_order_relaxed);
    const int camZ = camChunkZ.load(std::memory_order_relaxed);

    // Bitwise operations are used for abs() where possible to improve efficiency.
    const int diffX = pos.x - camX;
    const int diffZ = pos.z - camZ;
    const int absX = (diffX < 0) ? -diffX : diffX;
    const int absZ = (diffZ < 0) ? -diffZ : diffZ;

    return (absX > renderDistance) | (absZ > renderDistance);
}

std::pair<glm::vec3, glm::vec3> Planet::getChunkBounds(const ChunkPos &pos) noexcept {
    const float worldX = static_cast<float>(pos.x) * CHUNK_WIDTH;
    const float worldY = static_cast<float>(pos.y) * CHUNK_HEIGHT;
    const float worldZ = static_cast<float>(pos.z) * CHUNK_WIDTH;
    return {
        glm::vec3(worldX, worldY, worldZ),
        glm::vec3(worldX + CHUNK_WIDTH, worldY + CHUNK_HEIGHT, worldZ + CHUNK_WIDTH)
    };
}

void Planet::updateFrustum(const glm::mat4 &frustumVP) {
    frustum.update(frustumVP);
}

//==============================================================================
// CHUNK THREAD - Background processing
//==============================================================================

void Planet::chunkThreadUpdate() {
    auto lastCleanup = std::chrono::steady_clock::now();

    while (!shouldEnd.load(std::memory_order_acquire)) {
        // If the seed changes, we need to stop what we're doing.
        if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Free block data nothing references any more (chunks that left the render distance).
        // Time-based, so memory stays bounded however fast the camera moves.
        if (const auto now = std::chrono::steady_clock::now(); now - lastCleanup > std::chrono::seconds(1)) {
            cleanupUnusedChunkData();
            lastCleanup = now;
        }

        // Atomic loads are used here to minimize locking when reading camera positions.
        const int currentCamX = camChunkX.load(std::memory_order_relaxed);
        const int currentCamY = camChunkY.load(std::memory_order_relaxed);
        const int currentCamZ = camChunkZ.load(std::memory_order_relaxed);
        const int prevCamX = lastCamX.load(std::memory_order_relaxed);
        const int prevCamY = lastCamY.load(std::memory_order_relaxed);
        const int prevCamZ = lastCamZ.load(std::memory_order_relaxed);

        // Check if camera moved to rebuild queue
        if ((currentCamX != prevCamX) | (currentCamY != prevCamY) | (currentCamZ != prevCamZ)) {
            updateChunkQueue();
        } else {
            // Hand queued work to the pool; the dispatcher itself never generates or meshes.
            if (!dispatchWork()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

bool Planet::dispatchWork() {
    std::vector<ChunkPos> remesh;
    std::vector<ChunkPos> build;
    {
        std::unique_lock lock(chunkMutex);
        while (!regenQueue.empty()) {
            remesh.push_back(regenQueue.front());
            regenPending.erase(regenQueue.front());
            regenQueue.pop();
        }

        // A bounded number of builds in flight keeps the pool working nearest-first, and lets
        // a camera move re-prioritise the queue instead of finishing a long backlog.
        const size_t maxInFlight = chunkGenPool.workerCount() * 2;
        while (inFlight.size() < maxInFlight && !chunkQueue.empty()) {
            const ChunkPos pos = chunkQueue.front();
            chunkQueue.pop();
            if (chunks.contains(pos) || !inFlight.insert(pos).second) continue;
            build.push_back(pos);
        }
    }

    if (remesh.empty() && build.empty()) {
        std::unique_lock lock(chunkMutex);
        if (chunkQueue.empty() && inFlight.empty()) {
            for (Chunk *chunk : chunkList) {
                if (chunk->meshRequested) continue;
                chunk->meshRequested = true;
                remesh.push_back(chunk->chunkPos);
            }
        }
    }

    for (const ChunkPos &pos : remesh) chunkGenPool.submit([this, pos] { remeshChunk(pos); });
    for (const ChunkPos &pos : build) chunkGenPool.submit([this, pos] { buildChunk(pos); });
    return !remesh.empty() || !build.empty();
}

void Planet::remeshAll() {
    std::unique_lock lock(chunkMutex);
    for (Chunk *chunk : chunkList) {
        if (chunk->meshRequested && regenPending.insert(chunk->chunkPos).second) regenQueue.push(chunk->chunkPos);
    }
}

bool Planet::neighborsLit(const ChunkPos pos) const {
    constexpr int offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (const auto &[dx, dz] : offsets) {
        const ChunkPos n(pos.x + dx, pos.y, pos.z + dz);
        if (!chunks.contains(n) && !isOutOfRenderDistance(n)) return false;
    }
    return true;
}

void Planet::requestMeshIfReady(Chunk *chunk) {
    if (chunk->meshRequested || !neighborsLit(chunk->chunkPos)) return;
    chunk->meshRequested = true;
    if (regenPending.insert(chunk->chunkPos).second) regenQueue.push(chunk->chunkPos);
}

void Planet::remeshChunk(const ChunkPos pos) {
    Chunk *const chunk = getChunk(pos);
    if (!chunk) return;

    std::lock_guard meshLock(chunk->meshMutex);
    // The chunk may have been pooled or reused for another position while we waited for the lock.
    if (getChunk(pos) != chunk || !(chunk->chunkPos == pos)) return;
    chunk->generateChunkMesh();
    chunk->ready = false;
}
//==============================================================================
// CHUNK DATA CLEANUP
//==============================================================================

void Planet::cleanupUnusedChunkData() {
    std::unique_lock lock(chunkMutex);

    // Use static thread_local to avoid repeated allocations in the cleanup thread.
    static thread_local std::vector<ChunkPos> toRemove;
    toRemove.clear();
    toRemove.reserve(chunkData.size() / 10); // Estimate 10% cleanup

    for (const auto &[pos, data]: chunkData) {
        // Check reference count: 1 ref = held by chunkData map ONLY.
        if (LIKELY(data.use_count() == 1)) {
            toRemove.push_back(pos);
        }
    }

    for (const ChunkPos &pos: toRemove) {
        chunkData.erase(pos);
    }
}

//==============================================================================
// CHUNK QUEUE MANAGEMENT
//==============================================================================

void Planet::updateChunkQueue() {
    const int currentCamX = camChunkX.load(std::memory_order_relaxed);
    const int currentCamY = camChunkY.load(std::memory_order_relaxed);
    const int currentCamZ = camChunkZ.load(std::memory_order_relaxed);

    lastCamX.store(currentCamX, std::memory_order_relaxed);
    lastCamY.store(currentCamY, std::memory_order_relaxed);
    lastCamZ.store(currentCamZ, std::memory_order_relaxed);

    std::unique_lock lock(chunkMutex);
    buildChunkQueue();
}

void Planet::buildChunkQueue() {
    chunkQueue = {};

    const int camX = lastCamX.load(std::memory_order_relaxed);
    const int camZ = lastCamZ.load(std::memory_order_relaxed);

    // Add camera chunk (clamped to Y=0 for this single-layer world).
    addChunkIfMissing(camX, 0, camZ);

    // Build in expanding rings (closest first) to prioritize player vicinity.
    for (int r = 1; r <= renderDistance; ++r) {
        addChunkRing(r);
    }
}

void Planet::addChunkRing(int radius) {
    const int camX = lastCamX.load(std::memory_order_relaxed);
    const int camZ = lastCamZ.load(std::memory_order_relaxed);
    constexpr int actualY = 0;

    // Cardinal directions
    addChunkIfMissing(camX, actualY, camZ + radius);
    addChunkIfMissing(camX, actualY, camZ - radius);
    addChunkIfMissing(camX + radius, actualY, camZ);
    addChunkIfMissing(camX - radius, actualY, camZ);

    // Edges (between corners)
    for (int e = 1; e < radius; ++e) {
        addChunkEdges(radius, e, actualY);
    }

    // Corners
    addChunkCorners(radius, actualY);
}

inline void Planet::addChunkEdges(int radius, int edge, int y) noexcept {
    const int camX = lastCamX.load(std::memory_order_relaxed);
    const int camZ = lastCamZ.load(std::memory_order_relaxed);

    addChunkIfMissing(camX + edge, y, camZ + radius);
    addChunkIfMissing(camX - edge, y, camZ + radius);
    addChunkIfMissing(camX + radius, y, camZ + edge);
    addChunkIfMissing(camX + radius, y, camZ - edge);
    addChunkIfMissing(camX + edge, y, camZ - radius);
    addChunkIfMissing(camX - edge, y, camZ - radius);
    addChunkIfMissing(camX - radius, y, camZ + edge);
    addChunkIfMissing(camX - radius, y, camZ - edge);
}

inline void Planet::addChunkCorners(int radius, int y) noexcept {
    const int camX = lastCamX.load(std::memory_order_relaxed);
    const int camZ = lastCamZ.load(std::memory_order_relaxed);

    addChunkIfMissing(camX + radius, y, camZ + radius);
    addChunkIfMissing(camX + radius, y, camZ - radius);
    addChunkIfMissing(camX - radius, y, camZ + radius);
    addChunkIfMissing(camX - radius, y, camZ - radius);
}

inline void Planet::addChunkIfMissing(int x, int y, int z) {
    const ChunkPos pos(x, y, z);
    if (chunks.find(pos) == chunks.end()) {
        chunkQueue.push(pos);
    }
}

//==============================================================================
// CHUNK DATA PROCESSING
//==============================================================================


void Planet::buildChunk(const ChunkPos chunkPos) {
    // Whatever happens below, this position is no longer in flight afterwards.
    struct InFlightGuard {
        Planet *planet;
        ChunkPos pos;
        ~InFlightGuard() {
            std::unique_lock lock(planet->chunkMutex);
            planet->inFlight.erase(pos);
        }
    } guard{this, chunkPos};

    if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed)) || isOutOfRenderDistance(chunkPos)) return;

    // Reuse a pooled chunk when possible. reset() takes the chunk's mesh lock, so it runs
    // after chunkMutex is released (lock order: meshMutex before chunkMutex).
    Chunk *chunk = nullptr;
    {
        std::unique_lock lock(chunkMutex);
        if (!chunkPool.empty()) {
            chunk = chunkPool.back();
            chunkPool.pop_back();
        }
    }
    if (chunk) chunk->reset(chunkPos);
    else chunk = new Chunk(chunkPos);

    if (!populateChunkData(chunk, chunkPos)) {
        std::unique_lock lock(chunkMutex);
        chunkPool.push_back(chunk);
        return;
    }

    // Only light here. The chunk is meshed once all four neighbours are lit too, so its single mesh
    // already includes their lava light and no neighbour has to be remeshed for it.
    {
        std::lock_guard meshLock(chunk->meshMutex);
        chunk->computeLightMap();
    }

    std::unique_lock lock(chunkMutex);
    if (chunks.contains(chunkPos) || SEED != last_seed.load(std::memory_order_relaxed)) {
        chunkPool.push_back(chunk);
        return;
    }
    chunks[chunkPos] = chunk;
    chunk->listIndex = chunkList.size();
    chunkList.push_back(chunk);
    renderChunksDirty.store(true, std::memory_order_release);

    // Tell existing neighbours about the new data, and mesh any whose four neighbours are now all lit.
    constexpr int neighborOffsets[6][3] = {
        {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}
    };
    // Border of the new chunk facing each offset (East, West, South, North; -1 for up/down).
    constexpr int facingBorder[6] = {3, 2, 1, 0, -1, -1};
    std::shared_ptr<ChunkData> Chunk::* const dataPtrs[6] = {
        &Chunk::westData, &Chunk::eastData, &Chunk::northData,
        &Chunk::southData, &Chunk::downData, &Chunk::upData
    };
    for (int i = 0; i < 6; ++i) {
        const ChunkPos nPos(chunkPos.x + neighborOffsets[i][0], chunkPos.y + neighborOffsets[i][1],
                            chunkPos.z + neighborOffsets[i][2]);
        if (auto it = chunks.find(nPos); it != chunks.end()) {
            Chunk *const neighbor = it->second;
            neighbor->*dataPtrs[i] = chunk->chunkData;
            if (facingBorder[i] < 0) continue;
            if (!neighbor->meshRequested) {
                requestMeshIfReady(neighbor);
            } else if (chunk->borderBlockLight[facingBorder[i]] && regenPending.insert(nPos).second) {
                // Already meshed before this chunk existed (e.g. the camera moved): refresh its lava light.
                regenQueue.push(nPos);
            }
        }
    }
    requestMeshIfReady(chunk);
}
bool Planet::populateChunkData(Chunk *chunk, const ChunkPos &chunkPos) {
    // Use stack array for better cache locality during neighbor data acquisition.
    struct NeighborInfo {
        int dx, dy, dz;
        std::shared_ptr<ChunkData> *target;
    };

    constexpr NeighborInfo neighbors[] = {
        {0, 0, 0, nullptr},    // Will be assigned below
        {0, 1, 0, nullptr},
        {0, -1, 0, nullptr},
        {0, 0, -1, nullptr},
        {0, 0, 1, nullptr},
        {1, 0, 0, nullptr},
        {-1, 0, 0, nullptr}
    };

    std::shared_ptr<ChunkData>* targets[] = {
        &chunk->chunkData, &chunk->upData, &chunk->downData,
        &chunk->northData, &chunk->southData, &chunk->eastData, &chunk->westData
    };

    for (size_t i = 0; i < 7; ++i) {
        if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) return false;

        const ChunkPos neighborPos(chunkPos.x + neighbors[i].dx,
                                  chunkPos.y + neighbors[i].dy,
                                  chunkPos.z + neighbors[i].dz);
        *targets[i] = getOrCreateChunkData(neighborPos);

        if (!*targets[i]) return false;
    }

    return true;
}

std::shared_ptr<ChunkData> Planet::getOrCreateChunkData(const ChunkPos &pos) {
    // The world is a single chunk tall, so everything above or below it is air. All of those
    // positions share one read-only block instead of allocating 256 KB of air each.
    if (pos.y != 0) {
        static const std::shared_ptr<ChunkData> emptyData =
            std::make_shared<ChunkData>(new uint8_t[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH]());
        return emptyData;
    }

    std::unique_lock lock(chunkMutex);

    auto it = chunkData.find(pos);
    if (it != chunkData.end()) {
        return it->second;
    }

    lock.unlock();

    if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) return nullptr;

    // Perform expensive procedural generation outside of the mutex lock.
    auto *d = new uint8_t[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH];
    {
        loadstats::ScopedTimer timer(loadstats::generationMicros);
        WorldGen::generateChunkData(pos, d, SEED);
    }
    loadstats::generatedChunks.fetch_add(1, std::memory_order_relaxed);
    auto data = std::make_shared<ChunkData>(d);

    lock.lock();
    return chunkData.emplace(pos, data).first->second;
}

//==============================================================================
// PUBLIC UTILITIES
//==============================================================================

Chunk *Planet::getChunk(const ChunkPos chunkPos) {
    // Use shared_lock for read-only access to allow multiple concurrent readers.
    std::shared_lock lock(chunkMutex);
    const auto it = chunks.find(chunkPos);
    return (it != chunks.end()) ? it->second : nullptr;
}

void Planet::clearChunkQueue() {
    lastCamX.fetch_add(1, std::memory_order_relaxed);
}
