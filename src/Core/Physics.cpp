#include "Physics.h"
#include <iostream>
#include <cmath>
#include "Blocks.h"
#include "../World/headers/Planet.h"

/**
 * @file Physics.cpp
 * @brief This file handles physics and raycasting for our voxel world.
 */

namespace {
    /**
     * @brief A fast floor helper is used to avoid repeated calls to std::floor.
     */
    [[gnu::always_inline]]
    int fastFloor(float x) {
        return static_cast<int>(std::floor(x));
    }
    
    /**
     * @brief This helps us get the chunk index from world coordinates.
     * It handles negative coordinates correctly by using flooring.
     */
    [[gnu::always_inline]]
    int worldToChunkCoord(float worldCoord, int chunkSize) {
        return fastFloor(worldCoord / static_cast<float>(chunkSize));
    }

    /**
     * @brief Extracts the fractional part of a float (always in range [0, 1)).
     */
    [[gnu::always_inline]]
    float fract(float x) {
        return x - std::floor(x);
    }
}

/**
 * @brief The DDA algorithm is used for raycasting due to its efficiency with voxels.
 * 
 * This version is adjusted to handle:
 * 1. **Negative Coordinates**: floor() is used for consistency across coordinates.
 * 2. **Boundary Precision**: Distance to the next block boundary is calculated.
 * 3. **Chunk Caching**: Chunks are only re-queried when crossing into a new one.
 */
Physics::RaycastResult Physics::raycast(const glm::vec3 &startPos, const glm::vec3 &direction, const float maxDistance)
{
    // Ensure direction is unit length for predictable distance accumulation
    const glm::vec3 dir = glm::normalize(direction);

    // Initial voxel coordinate (integer grid address)
    glm::ivec3 mapPos(fastFloor(startPos.x), fastFloor(startPos.y), fastFloor(startPos.z));

    // Step direction along each axis: +1 or -1
    const glm::ivec3 step(
        dir.x >= 0 ? 1 : -1,
        dir.y >= 0 ? 1 : -1,
        dir.z >= 0 ? 1 : -1
    );

    /**
     * Distance to the next block boundary is calculated here.
     * The 0.0f case is handled to prevent infinite loops.
     */
    auto calcBoundaryDist = [](float pos, float dir_component) -> float {
        if (dir_component >= 0) {
            const float frac = fract(pos);
            return (frac == 0.0f) ? 1.0f : (1.0f - frac);
        } else {
            const float frac = fract(pos);
            return (frac == 0.0f) ? 1.0f : frac;
        }
    };

    // Distances to the first voxel boundaries
    const float boundaryX = calcBoundaryDist(startPos.x, dir.x);
    const float boundaryY = calcBoundaryDist(startPos.y, dir.y);
    const float boundaryZ = calcBoundaryDist(startPos.z, dir.z);

    // Inverse directions are pre-calculated to save time inside the loop.
    const float invDirX = (std::abs(dir.x) > 1e-6f) ? (1.0f / std::abs(dir.x)) : 1e6f;
    const float invDirY = (std::abs(dir.y) > 1e-6f) ? (1.0f / std::abs(dir.y)) : 1e6f;
    const float invDirZ = (std::abs(dir.z) > 1e-6f) ? (1.0f / std::abs(dir.z)) : 1e6f;

    // tMax represents the ray distance (t) to the next boundary for each axis
    glm::vec3 tMax(
        boundaryX * invDirX,
        boundaryY * invDirY,
        boundaryZ * invDirZ
    );

    // tDelta represents the constant distance (t) required to move one full voxel along an axis
    const glm::vec3 tDelta(invDirX, invDirY, invDirZ);

    // Initial chunk coordinate lookup
    int currentChunkX = worldToChunkCoord(static_cast<float>(mapPos.x), CHUNK_WIDTH);
    int currentChunkY = worldToChunkCoord(static_cast<float>(mapPos.y), CHUNK_HEIGHT);
    int currentChunkZ = worldToChunkCoord(static_cast<float>(mapPos.z), CHUNK_WIDTH);

    Chunk* currentChunk = Planet::planet->getChunk(ChunkPos(currentChunkX, currentChunkY, currentChunkZ));
    const BlockRegistry& blockRegistry = BlockRegistry::getInstance();

    // Iterate until maxDistance is exceeded or a collision is found
    const int maxIterations = static_cast<int>(maxDistance * 2.0f) + 16; // Heuristic safety limit
    float currentDistance = 0.0f;

    for (int iteration = 0; iteration < maxIterations && currentDistance < maxDistance; ++iteration) {
        
        // Checking if the ray moved into a new chunk since the last step.
        const int chunkX = worldToChunkCoord(static_cast<float>(mapPos.x), CHUNK_WIDTH);
        const int chunkY = worldToChunkCoord(static_cast<float>(mapPos.y), CHUNK_HEIGHT);
        const int chunkZ = worldToChunkCoord(static_cast<float>(mapPos.z), CHUNK_WIDTH);

        if (chunkX != currentChunkX || chunkY != currentChunkY || chunkZ != currentChunkZ) [[unlikely]] {
            currentChunkX = chunkX;
            currentChunkY = chunkY;
            currentChunkZ = chunkZ;
            currentChunk = Planet::planet->getChunk(ChunkPos(currentChunkX, currentChunkY, currentChunkZ));
        }

        // Only query voxels if the chunk is loaded/exists
        if (currentChunk != nullptr) [[likely]] {
            // Transform global mapPos to local chunk relative coordinates
            const int localBlockX = mapPos.x - currentChunkX * CHUNK_WIDTH;
            const int localBlockY = mapPos.y - currentChunkY * CHUNK_HEIGHT;
            const int localBlockZ = mapPos.z - currentChunkZ * CHUNK_WIDTH;

            // Safety check against CHUNK bounds
            if (localBlockX >= 0 && localBlockX < CHUNK_WIDTH &&
                localBlockY >= 0 && localBlockY < CHUNK_HEIGHT &&
                localBlockZ >= 0 && localBlockZ < CHUNK_WIDTH) [[likely]] {

                const unsigned int block = currentChunk->getBlockAtPos(localBlockX, localBlockY, localBlockZ);

                // Check for block solidity (ignore air and liquids)
                if (block != 0) [[unlikely]] {
                    const Block& blockData = blockRegistry.getBlock(block);
                    if (blockData.blockType != Block::LIQUID) {
                        
                        // Precise world-space hit position
                        const glm::vec3 resultPos = startPos + dir * currentDistance;

                        return {
                            true, resultPos, currentChunk,
                            mapPos.x, mapPos.y, mapPos.z,
                            localBlockX, localBlockY, localBlockZ
                        };
                    }
                }
            }
        }

        /**
         * @brief DDA Step logic.
         * The closest axis is selected, and the position moves one block in that direction.
         */
        if (tMax.x < tMax.y) {
            if (tMax.x < tMax.z) {
                currentDistance = tMax.x;
                mapPos.x += step.x;
                tMax.x += tDelta.x;
            } else {
                currentDistance = tMax.z;
                mapPos.z += step.z;
                tMax.z += tDelta.z;
            }
        } else {
            if (tMax.y < tMax.z) {
                currentDistance = tMax.y;
                mapPos.y += step.y;
                tMax.y += tDelta.y;
            } else {
                currentDistance = tMax.z;
                mapPos.z += step.z;
                tMax.z += tDelta.z;
            }
        }
    }

    // Default return: Ray exceeded range without hit
    return {
        false, glm::vec3(0), nullptr,
        0, 0, 0,
        0, 0, 0
    };
}

// ============================================================================
// COLLISION DETECTION
// ============================================================================

bool Physics::isSolidBlock(int x, int y, int z) {
    const int chunkX = worldToChunkCoord(static_cast<float>(x), CHUNK_WIDTH);
    const int chunkY = worldToChunkCoord(static_cast<float>(y), CHUNK_HEIGHT);
    const int chunkZ = worldToChunkCoord(static_cast<float>(z), CHUNK_WIDTH);

    Chunk* chunk = Planet::planet->getChunk(ChunkPos(chunkX, chunkY, chunkZ));
    if (!chunk) return false;

    const int localX = x - chunkX * CHUNK_WIDTH;
    const int localY = y - chunkY * CHUNK_HEIGHT;
    const int localZ = z - chunkZ * CHUNK_WIDTH;

    if (localX < 0 || localX >= CHUNK_WIDTH ||
        localY < 0 || localY >= CHUNK_HEIGHT ||
        localZ < 0 || localZ >= CHUNK_WIDTH) {
        return false;
    }

    const unsigned int block = chunk->getBlockAtPos(localX, localY, localZ);
    if (block == 0) return false;

    const Block& blockData = BlockRegistry::getInstance().getBlock(block);
    // Only SOLID, TRANSPARENT, and LEAVES cause collision
    return (blockData.blockType != Block::LIQUID && blockData.blockType != Block::BILLBOARD);
}

/**
 * @brief Resolves player AABB collisions one axis at a time.
 *
 * This approach prevents the player from tunneling through corners by
 * checking each axis independently and correcting the position before
 * moving to the next axis.
 */
Physics::CollisionResult Physics::resolveCollisions(
    const glm::vec3& oldPos, const glm::vec3& newPos, const glm::vec3& halfExtents)
{
    CollisionResult result;
    result.position = newPos;
    result.onGround = false;
    result.hitCeiling = false;
    result.hitWallX = false;
    result.hitWallZ = false;

    // Helper lambda: check all voxels overlapping an AABB for solidity
    auto checkAABB = [&](const glm::vec3& pos) -> bool {
        const int minX = fastFloor(pos.x - halfExtents.x + 0.001f);
        const int maxX = fastFloor(pos.x + halfExtents.x - 0.001f);
        const int minY = fastFloor(pos.y - halfExtents.y + 0.001f);
        const int maxY = fastFloor(pos.y + halfExtents.y - 0.001f);
        const int minZ = fastFloor(pos.z - halfExtents.z + 0.001f);
        const int maxZ = fastFloor(pos.z + halfExtents.z - 0.001f);

        for (int bx = minX; bx <= maxX; ++bx) {
            for (int by = minY; by <= maxY; ++by) {
                for (int bz = minZ; bz <= maxZ; ++bz) {
                    if (isSolidBlock(bx, by, bz)) return true;
                }
            }
        }
        return false;
    };

    // Resolve Y axis first (gravity is the most important)
    glm::vec3 testPos = glm::vec3(oldPos.x, newPos.y, oldPos.z);
    if (checkAABB(testPos)) {
        // Collision on Y axis - snap to nearest block boundary
        if (newPos.y < oldPos.y) {
            // Falling down - snap feet to top of block below
            const int groundBlockY = fastFloor(newPos.y - halfExtents.y + 0.001f);
            result.position.y = static_cast<float>(groundBlockY + 1) + halfExtents.y;
            result.onGround = true;
        } else {
            // Moving up - snap head to bottom of block above
            const int ceilBlockY = fastFloor(newPos.y + halfExtents.y - 0.001f);
            result.position.y = static_cast<float>(ceilBlockY) - halfExtents.y;
            result.hitCeiling = true;
        }
    }

    // Resolve X axis
    testPos = glm::vec3(newPos.x, result.position.y, oldPos.z);
    if (checkAABB(testPos)) {
        result.position.x = oldPos.x;
        result.hitWallX = true;
    }

    // Resolve Z axis
    testPos = glm::vec3(result.position.x, result.position.y, newPos.z);
    if (checkAABB(testPos)) {
        result.position.z = oldPos.z;
        result.hitWallZ = true;
    }

    // Final ground check (for when standing still or moving horizontally)
    if (!result.onGround) {
        glm::vec3 groundTest = result.position;
        groundTest.y -= 0.01f;
        result.onGround = checkAABB(groundTest);
    }

    return result;
}
