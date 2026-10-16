/**
 * @file Planet.cpp
 * @brief Implementation for the Planet class. This handles everything from world gen to rendering.
 */

#include "Planet.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <GL/glew.h>
#include "WorldGen.h"
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

Planet::Planet(Shader *solidShader, Shader *waterShader, Shader *billboardShader, Shader* bboxShader)
    : solidShader(solidShader), waterShader(waterShader), billboardShader(billboardShader), bboxShader(bboxShader) {

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

    // Initialize Shadow Map
    glGenFramebuffers(1, &depthMapFBO);
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Initialize Reflection FBO
    glGenFramebuffers(1, &reflectionFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, reflectionFBO);
    glGenTextures(1, &reflectionTexture);
    glBindTexture(GL_TEXTURE_2D, reflectionTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, REFLECTION_WIDTH, REFLECTION_HEIGHT, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, reflectionTexture, 0);
    
    // Depth renderbuffer for correct depth testing
    glGenRenderbuffers(1, &reflectionDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, reflectionDepthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, REFLECTION_WIDTH, REFLECTION_HEIGHT);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, reflectionDepthRBO);
    
    // Check if FBO is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "ERROR::FRAMEBUFFER:: Reflection Framebuffer is not complete!" << std::endl;
        
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

Planet::~Planet() {
    glDeleteFramebuffers(1, &depthMapFBO);
    glDeleteTextures(1, &depthMap);
    
    glDeleteFramebuffers(1, &reflectionFBO);
    glDeleteTextures(1, &reflectionTexture);
    glDeleteRenderbuffers(1, &reflectionDepthRBO);

    shouldEnd.store(true, std::memory_order_release);
    if (chunkThread.joinable()) {
        chunkThread.join();
    }
}

//==============================================================================
// MAIN UPDATE - Called every frame
//==============================================================================

void Planet::update(const glm::vec3 &cameraPos, bool updateOcclusion) {
    // Relaxed atomics are used here because they are faster and suitable for simple seed checks.
    const long currentSeed = SEED;
    const long cachedSeed = last_seed.load(std::memory_order_relaxed);

    // Check if the seed changed early on.
    if (UNLIKELY(currentSeed != cachedSeed)) {
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
            chunkDataQueue = {};
            regenQueue = {};
            regenPending.clear();
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

    glDisable(GL_BLEND);

    // Use atomic load for lock-free check.
    if (UNLIKELY(renderChunksDirty.load(std::memory_order_acquire))) {
        std::unique_lock lock(chunkMutex);
        if (renderChunksDirty.load(std::memory_order_relaxed)) {
            renderChunks = chunkList;
            renderChunksDirty.store(false, std::memory_order_release);
        }
    }

    int chunksUploadedThisFrame = 0;

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

        const bool needsUpload = chunk->generated && !chunk->ready;
        if (UNLIKELY(needsUpload)) {
            // Uploads are limited to 4 per frame to prevent stuttering.
            if (chunksUploadedThisFrame < 4) {
                chunk->uploadMesh();
                ++chunksUploadedThisFrame;
            }
            chunksLoading.fetch_add(1, std::memory_order_relaxed);
        }

        if (LIKELY(chunk->ready && frustum.isBoxVisible(chunk->cullingCenter, chunk->cullingExtents))) {
            const float dx = chunk->cullingCenter.x - camPosX;
            const float dy = chunk->cullingCenter.y - camPosY;
            const float dz = chunk->cullingCenter.z - camPosZ;

            const float distSq = dx*dx + dy*dy + dz*dz;

            if (UNLIKELY(distSq > maxDistSq)) continue;

            chunk->cachedDistSq = distSq;
            frustumVisibleChunks.push_back(chunk);

            // Chunks in this 3x3x3 zone around the camera are always drawn to prevent blocks from flickering.
            const int distCX = std::abs(pos.x - newCamChunkX);
            const int distCY = std::abs(pos.y - newCamChunkY);
            const int distCZ = std::abs(pos.z - newCamChunkZ);
            const bool inSafeZone = (distCX <= 1) & (distCY <= 1) & (distCZ <= 1);

            if (LIKELY(chunk->occlusionVisible || inSafeZone)) {
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
                renderChunksDirty.store(true, std::memory_order_release);
            }
        }
    }

    // Hardware Occlusion Query result processing.
    static int frameCounter = 0;
    if (updateOcclusion) {
        ++frameCounter;

        const size_t visibleCount = frustumVisibleChunks.size();
        for (size_t i = 0; i < visibleCount; ++i) {
            Chunk * const chunk = frustumVisibleChunks[i];

            const size_t chunkHash = chunk->chunkPos.x * 73856093 ^
                                    chunk->chunkPos.y * 19349663 ^
                                    chunk->chunkPos.z * 83492791;
            const bool shouldQuery = (chunkHash + frameCounter) % 3 == 0;

            if (chunk->queryID != 0 && chunk->queryIssued && shouldQuery) {
                GLint available = GL_FALSE;
                glGetQueryObjectiv(chunk->queryID, GL_QUERY_RESULT_AVAILABLE, &available);

                if (LIKELY(available == GL_TRUE)) {
                    GLuint result = 0;
                    glGetQueryObjectuiv(chunk->queryID, GL_QUERY_RESULT, &result);
                    const bool rawVisible = (result > 0);

                    // A voting system (hysteresis) is used to prevent flickering in/out.
                    if (LIKELY(rawVisible)) {
                        chunk->occlusionVisible = true;
                        chunk->occlusionCounter = 0;
                    } else {
                        if (++chunk->occlusionCounter >= 4) {
                            chunk->occlusionVisible = false;
                        }
                    }
                }
            }
        }
    }

    // Solid rendering pass.
    solidShader->use();
    const size_t solidCount = solidChunks.size();
    for (size_t i = 0; i < solidCount; ++i) {
        solidChunks[i]->renderAllSolid();
    }

    // Hardware Occlusion Query issuance pass.
    if (updateOcclusion) {
        bboxShader->use();
        (*bboxShader)["viewProjection"] = lastViewProjection;

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GEQUAL);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        if (UNLIKELY(bboxVAO == 0)) initBoundingBoxMesh();
        glBindVertexArray(bboxVAO);

        const int safeZoneMinX = newCamChunkX - 1;
        const int safeZoneMaxX = newCamChunkX + 1;
        const int safeZoneMinY = newCamChunkY - 1;
        const int safeZoneMaxY = newCamChunkY + 1;
        const int safeZoneMinZ = newCamChunkZ - 1;
        const int safeZoneMaxZ = newCamChunkZ + 1;

        const size_t visibleCount = frustumVisibleChunks.size();
        for (size_t i = 0; i < visibleCount; ++i) {
            Chunk * const chunk = frustumVisibleChunks[i];

            if (chunk->queryID != 0) {
                const int posX = chunk->chunkPos.x;
                const int posY = chunk->chunkPos.y;
                const int posZ = chunk->chunkPos.z;

                const bool inSafeZone = (posX >= safeZoneMinX && posX <= safeZoneMaxX) &&
                                       (posY >= safeZoneMinY && posY <= safeZoneMaxY) &&
                                       (posZ >= safeZoneMinZ && posZ <= safeZoneMaxZ);

                if (inSafeZone) continue;

                const size_t chunkHash = posX * 73856093 ^ posY * 19349663 ^ posZ * 83492791;

                if ((chunkHash + frameCounter) % 3 == 0) {
                    glBeginQuery(GL_SAMPLES_PASSED, chunk->queryID);

                    glm::mat4 model = glm::mat4(1.0f);
                    model = glm::translate(model, chunk->cullingCenter);
                    model = glm::scale(model, (chunk->cullingExtents - 0.01f) * 2.0f);
                    (*bboxShader)["model"] = model;
                    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);

                    glEndQuery(GL_SAMPLES_PASSED);
                    chunk->queryIssued = true;
                }
            }
        }

        glBindVertexArray(0);
        glEnable(GL_CULL_FACE);
        glDepthMask(GL_TRUE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_GEQUAL);
    }

    // Billboard rendering pass.
    billboardShader->use();
    glDisable(GL_CULL_FACE);
    const size_t billboardCount = billboardChunks.size();
    for (size_t i = 0; i < billboardCount; ++i) {
        billboardChunks[i]->renderAllBillboard();
    }
    glEnable(GL_CULL_FACE);

    // Water rendering pass.
    glEnable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    waterShader->use();
    const size_t waterCount = waterChunks.size();
    for (size_t i = 0; i < waterCount; ++i) {
        waterChunks[i]->renderAllWater();
    }
    glEnable(GL_CULL_FACE);
}

void Planet::renderShadows(Shader* shader) {
    shader->use();
    
    // Shadow culling is implemented here to only render shadows for chunks near the player.
    const int camX = camChunkX.load(std::memory_order_relaxed);
    const int camY = camChunkY.load(std::memory_order_relaxed);
    const int camZ = camChunkZ.load(std::memory_order_relaxed);
    const int shadowRadius = static_cast<int>(shadowDistance / CHUNK_WIDTH) + 1;
    
    // We iterate over the main chunk list as renderChunks might be overly culled for shadows
    // Optimization: In a real scenario, we'd maintain a separate "shadow visible" list
    // For now, we iterate all chunks but only draw close ones.
    const size_t chunkSize = chunkList.size();
    for(size_t i = 0; i < chunkSize; ++i) {
        Chunk* const chunk = chunkList[i];
        if(!chunk || !chunk->ready) continue;
        
        const ChunkPos& pos = chunk->chunkPos;
        if (abs(pos.x - camX) > shadowRadius || abs(pos.z - camZ) > shadowRadius) continue;
        
        // Draw solid
        if(chunk->hasSolid()) {
             chunk->renderAllSolid();
        }
        
        // Draw billboards (trees, etc cast shadows too)
        if(chunk->hasBillboard()) {
             chunk->renderAllBillboard();
        }
    }
}


void Planet::renderReflection(const glm::vec3 &cameraPos, const glm::vec3 &cameraFront, float aspectRatio) {
    // waterChunks is last frame's visible water; with none on screen the reflection is never sampled.
    if (waterChunks.empty()) return;

    // Distant reflections are tiny and distorted by the water shader, so only nearby chunks are drawn.
    constexpr float REFLECTION_RANGE = 8.0f * CHUNK_WIDTH;
    auto inReflectionRange = [&](const Chunk *chunk) {
        const float dx = chunk->cullingCenter.x - cameraPos.x;
        const float dz = chunk->cullingCenter.z - cameraPos.z;
        return dx * dx + dz * dz <= REFLECTION_RANGE * REFLECTION_RANGE;
    };

    glViewport(0, 0, REFLECTION_WIDTH, REFLECTION_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, reflectionFBO);
    
    // Clear with Sky Color
    glClearColor(0.5f, 0.7f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    const float waterLevel = 64.0f; // HARDCODED for now, matching WorldConstants
    
    // Calculate Reflection Camera
    glm::vec3 reflectPos = cameraPos;
    reflectPos.y = waterLevel - (cameraPos.y - waterLevel);
    
    // Reflect front vector: invert Y component
    glm::vec3 reflectFront = cameraFront;
    reflectFront.y = -reflectFront.y;
    
    glm::mat4 view = glm::lookAt(reflectPos, reflectPos + reflectFront, glm::vec3(0, 1, 0));
    // The same Reverse-Z setup as the main camera is used so everything lines up.
    glm::mat4 projection = glm::perspective(glm::radians(90.0f), aspectRatio, 10000.0f, 0.1f);
    
    // Store for water shader to use during main pass
    reflectionViewProjection = projection * view;
    
    // Create Reflection Frustum for Culling
    Frustum reflectionFrustum;
    reflectionFrustum.update(projection * view);

    // A clip plane is set so only reflections above the water level are rendered.
    glm::vec4 clipPlane = glm::vec4(0.0f, 1.0f, 0.0f, -(waterLevel + 0.5f));
    
    // Update Shaders
    
    // 1. Render Solid Chunks
    solidShader->use();
    (*solidShader)["view"] = view;
    (*solidShader)["projection"] = projection;
    (*solidShader)["clipPlane"] = clipPlane;
    (*solidShader)["lightSpaceMatrix"] = lightSpaceMatrix;
    
    // Set up Reverse-Z Depth Testing for Reflection Pass
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_GEQUAL);
    glClearDepth(0.0f);
    glClear(GL_DEPTH_BUFFER_BIT); // Clear purely the depth buffer (color already cleared)
    
    glCullFace(GL_BACK); 
    
    // Iterate over ALL active chunks (renderChunks), not just the ones visible to the main camera
    const size_t chunkSize = renderChunks.size();
    for (size_t i = 0; i < chunkSize; ++i) {
        Chunk *const chunk = renderChunks[i];
        
        // Skip if not ready or no solid mesh
        if (!chunk->ready || !chunk->hasSolid()) continue;

        // Skip chunks that are entirely below water level (prevent underwater geometry in reflection)
        const float chunkMaxY = chunk->chunkPos.y * CHUNK_HEIGHT + CHUNK_HEIGHT;
        if (chunkMaxY < waterLevel - 2.0f) continue;  // 2 block margin

        // Perform Frustum Culling against Reflection Frustum
        if (inReflectionRange(chunk) && reflectionFrustum.isBoxVisible(chunk->cullingCenter, chunk->cullingExtents)) {
            chunk->renderAllSolid();
        }
    }
    
    // 2. Render Billboards
    billboardShader->use();
    (*billboardShader)["view"] = view;
    (*billboardShader)["projection"] = projection;
    (*billboardShader)["clipPlane"] = clipPlane;
    
    for (size_t i = 0; i < chunkSize; ++i) {
        Chunk *const chunk = renderChunks[i];
        
        if (!chunk->ready || !chunk->hasBillboard()) continue;

        // Skip chunks that are entirely below water level
        const float chunkMaxY = chunk->chunkPos.y * CHUNK_HEIGHT + CHUNK_HEIGHT;
        if (chunkMaxY < waterLevel - 2.0f) continue;

        if (inReflectionRange(chunk) && reflectionFrustum.isBoxVisible(chunk->cullingCenter, chunk->cullingExtents)) {
            chunk->renderAllBillboard();
        }
    }
    
    // Restore State
    glCullFace(GL_BACK);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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

void Planet::updateFrustum(const glm::mat4 &frustumVP, const glm::mat4 &renderingVP) {
    lastViewProjection = renderingVP;
    frustum.update(frustumVP);
}

//==============================================================================
// CHUNK THREAD - Background processing
//==============================================================================

void Planet::chunkThreadUpdate() {
    static int cleanupCounter = 0;

    while (!shouldEnd.load(std::memory_order_acquire)) {
        // If the seed changes, we need to stop what we're doing.
        if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Throttling the cleanup of old chunk data to avoid hitting the lock too much.
        if (++cleanupCounter > 5000) {
            cleanupUnusedChunkData();
            cleanupCounter = 0;
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
            // Process task queues if camera is stationary.
            const bool allQueuesEmpty = chunkQueue.empty() &&
                                       chunkDataQueue.empty() &&
                                       regenQueue.empty();

            if (UNLIKELY(allQueuesEmpty)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } else {
                processChunkDataQueue();
                processRegenQueue();
                processChunkQueue();
            }
        }
    }
}

void Planet::processRegenQueue() {
    if (regenQueue.empty()) return;

    std::unique_lock lock(chunkMutex);

    const ChunkPos chunkPos = regenQueue.front();
    regenQueue.pop();
    regenPending.erase(chunkPos);

    auto it = chunks.find(chunkPos);
    if (it != chunks.end()) {
        Chunk* const chunk = it->second;

        lock.unlock();
        chunk->generateChunkMesh();
        lock.lock();

        if (chunks.find(chunkPos) != chunks.end()) {
            chunk->ready = false;
        }
    }
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

void Planet::processChunkDataQueue() {
    std::unique_lock lock(chunkMutex);

    if (chunkQueue.empty()) return;

    // Process multiple chunks in parallel to saturate generator worker threads.
    const size_t maxBatch = std::min(chunkDataQueue.size(),
                                    chunkGenPool.workerCount() * 2);

    // Use static thread_local to avoid repeated allocations of the generation task list.
    static thread_local std::vector<ChunkPos> toGenerate;
    toGenerate.clear();
    toGenerate.reserve(maxBatch);

    for (size_t i = 0; i < maxBatch && !chunkDataQueue.empty(); ++i) {
        const ChunkPos chunkPos = chunkDataQueue.front();
        chunkDataQueue.pop();

        if (chunkData.find(chunkPos) == chunkData.end()) {
            toGenerate.push_back(chunkPos);
        }
    }

    lock.unlock();

    if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) return;

    // Sending all these chunks to be generated in parallel so it's faster.
    for (const ChunkPos &pos: toGenerate) {
        chunkGenPool.submit([this, pos]() {
            if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) return;

            // Allocating memory for the chunk data and storing it in a shared_ptr.
            auto *d = new uint8_t[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH];
            WorldGen::generateChunkData(pos, d, SEED);
            const auto data = std::make_shared<ChunkData>(d);

            std::unique_lock dataLock(chunkMutex);
            if (chunkData.find(pos) == chunkData.end()) {
                chunkData[pos] = data;
            }
        });
    }
}

void Planet::processChunkQueue() {
    std::unique_lock lock(chunkMutex);

    if (chunkQueue.empty()) {
        lock.unlock();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return;
    }

    const ChunkPos chunkPos = chunkQueue.front();

    // Skip if already exists
    if (chunks.find(chunkPos) != chunks.end()) {
        chunkQueue.pop();
        return;
    }

    lock.unlock();

    // Check seed before expensive generation
    if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) return;

    // Get chunk from pool or create if needed. Using an object pool reduces frequent new/delete operations.
    Chunk *chunk;
    {
        std::unique_lock poolLock(chunkMutex);
        if (!chunkPool.empty()) {
            chunk = chunkPool.back();
            chunkPool.pop_back();
            chunk->reset(chunkPos);
        } else {
            chunk = new Chunk(chunkPos);
        }
    }

    if (!populateChunkData(chunk, chunkPos)) {
        std::unique_lock poolLock(chunkMutex);
        chunkPool.push_back(chunk);
        return;
    }

    chunk->generateChunkMesh();

    lock.lock();
    if (chunks.find(chunkPos) == chunks.end()) {
        chunks[chunkPos] = chunk;
        chunk->listIndex = chunkList.size();
        chunkList.push_back(chunk);
        renderChunksDirty.store(true, std::memory_order_release);

        // Batched neighbor notifications: Alert neighbors that a new data source is available.
        constexpr int neighborOffsets[6][3] = {
            {1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, -1, 0}
        };

        std::shared_ptr<ChunkData> Chunk::* const dataPtrs[6] = {
            &Chunk::westData, &Chunk::eastData, &Chunk::northData,
            &Chunk::southData, &Chunk::downData, &Chunk::upData
        };

        for (int i = 0; i < 6; ++i) {
            const ChunkPos nPos(chunkPos.x + neighborOffsets[i][0],
                               chunkPos.y + neighborOffsets[i][1],
                               chunkPos.z + neighborOffsets[i][2]);
            auto it = chunks.find(nPos);
            if (it != chunks.end()) {
                it->second->*dataPtrs[i] = chunk->chunkData;
                if (regenPending.insert(nPos).second) regenQueue.push(nPos);
            }
        }
    } else {
        delete chunk;
    }
    chunkQueue.pop();
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
    std::unique_lock lock(chunkMutex);

    auto it = chunkData.find(pos);
    if (it != chunkData.end()) {
        return it->second;
    }

    lock.unlock();

    if (UNLIKELY(SEED != last_seed.load(std::memory_order_relaxed))) return nullptr;

    // Perform expensive procedural generation outside of the mutex lock.
    auto *d = new uint8_t[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH];
    WorldGen::generateChunkData(pos, d, SEED);
    auto data = std::make_shared<ChunkData>(d);

    lock.lock();
    chunkData[pos] = data;
    return data;
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

//==============================================================================
// Hardware Occlusion Helper Methods
//==============================================================================

void Planet::initBoundingBoxMesh() {
    // Use constexpr for compile-time data to avoid runtime initialization overhead.
    constexpr float vertices[] = {
        -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,
        -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
    };

    constexpr unsigned int indices[] = {
        0, 1, 2, 2, 3, 0, 4, 5, 6, 6, 7, 4,
        0, 1, 5, 5, 4, 0, 2, 3, 7, 7, 6, 2,
        0, 3, 7, 7, 4, 0, 1, 2, 6, 6, 5, 1
    };

    glGenVertexArrays(1, &bboxVAO);
    glGenBuffers(1, &bboxVBO);
    glGenBuffers(1, &bboxEBO);

    glBindVertexArray(bboxVAO);

    glBindBuffer(GL_ARRAY_BUFFER, bboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bboxEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    glBindVertexArray(0);
}

void Planet::drawBoundingBox(const glm::vec3& center, const glm::vec3& extents) {
    if (UNLIKELY(bboxVAO == 0)) initBoundingBoxMesh();

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, center);
    model = glm::scale(model, extents * 2.0f);

    (*bboxShader)["model"] = model;

    glBindVertexArray(bboxVAO);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}