#pragma once

#include "Feature.h"
#include "../../Chunk/headers/Chunk.h"
#include "../headers/WorldConstants.h"

/**
 * Tree feature - handles how trees are placed in the world.
 * You can change what blocks are used for wood and leaves.
 */
class TreeFeature final : public Feature {
public:
    const uint8_t logBlock;
    const uint8_t leavesBlock;
    const int minHeight;
    const int maxHeight;
    const int canopyRadius;
    const float spawnChance;

    constexpr TreeFeature(uint8_t log, uint8_t leaves,
                          int minH = 4, int maxH = 7,
                          int canopy = 2, float chance = 0.02f) noexcept
        : logBlock(log), leavesBlock(leaves),
          minHeight(minH), maxHeight(maxH),
          canopyRadius(canopy), spawnChance(chance) {}

    [[nodiscard]] float getSpawnChance() const noexcept override {
        return spawnChance;
    }

    [[nodiscard]] bool canPlace(const uint8_t* chunkData,
                                 int localX, int localY, int localZ) const noexcept override {
        // Early exit on invalid Y position
        if (localY <= 0 || localY >= CHUNK_HEIGHT - maxHeight - 3) {
            return false;
        }

        // Check block below is suitable for tree growth
        const int belowIdx = calculateIndex(localX, localZ, localY - 1);
        const uint8_t belowBlock = chunkData[belowIdx];

        return Blocks::isSoil(belowBlock);
    }

    [[nodiscard]] bool place(uint8_t* chunkData,
                              int localX, int localY, int localZ,
                              int worldX, int worldZ,
                              uint64_t seed) noexcept override {

        // Pick a random height for the tree based on the coordinates.
        const int treeHeight = fastRandomRange(seed, worldX, worldZ, minHeight, maxHeight);

        // Bounds validation - check all constraints upfront
        if (localY + treeHeight + 2 >= CHUNK_HEIGHT) return false;
        if (localX < canopyRadius || localX >= CHUNK_WIDTH - canopyRadius) return false;
        if (localZ < canopyRadius || localZ >= CHUNK_WIDTH - canopyRadius) return false;

        // Set the wood blocks for the trunk.
        const int trunkBaseIdx = calculateIndex(localX, localZ, localY);
        for (int y = 0; y < treeHeight; ++y) {
            chunkData[trunkBaseIdx + y] = logBlock;
        }

        // Place leaves - optimized loop with reduced calculations
        const int leafStartY = localY + treeHeight - 2;
        const int leafEndY = localY + treeHeight + 1;
        const int treeTopY = localY + treeHeight;

        for (int ly = leafStartY; ly <= leafEndY; ++ly) {
            if (ly < 0 || ly >= CHUNK_HEIGHT) continue;

            const int yOffset = ly - treeTopY;
            const int radius = (yOffset < 0) ? canopyRadius : (canopyRadius - 1);

            if (radius < 1) continue;

            const int minX = localX - radius;
            const int maxX = localX + radius;
            const int minZ = localZ - radius;
            const int maxZ = localZ + radius;

            const int startX = (minX < 0) ? 0 : minX;
            const int endX = (maxX >= CHUNK_WIDTH) ? CHUNK_WIDTH - 1 : maxX;
            const int startZ = (minZ < 0) ? 0 : minZ;
            const int endZ = (maxZ >= CHUNK_WIDTH) ? CHUNK_WIDTH - 1 : maxZ;

            for (int px = startX; px <= endX; ++px) {
                const int dx = px - localX;
                const int absDx = (dx < 0) ? -dx : dx;
                if (absDx > radius) continue;

                const int rowBaseIdx = px * CHUNK_WIDTH * CHUNK_HEIGHT + ly;

                for (int pz = startZ; pz <= endZ; ++pz) {
                    const int dz = pz - localZ;
                    const int absDz = (dz < 0) ? -dz : dz;

                    if (absDz > radius || (absDx == radius && absDz == radius)) continue;

                    const int idx = rowBaseIdx + pz * CHUNK_HEIGHT;
                    const uint8_t currentBlock = chunkData[idx];
                    if (currentBlock == Blocks::AIR || currentBlock == leavesBlock) {
                        chunkData[idx] = leavesBlock;
                    }
                }
            }
        }

        return true;
    }

private:
    [[nodiscard]] static constexpr inline int calculateIndex(int x, int z, int y) noexcept {
        return (x * CHUNK_WIDTH + z) * CHUNK_HEIGHT + y;
    }

    [[nodiscard]] static inline int fastRandomRange(uint64_t seed, int worldX, int worldZ,
                                                      int minVal, int maxVal) noexcept {
        uint64_t hash = seed;
        hash ^= static_cast<uint64_t>(worldX) * 73856093ULL;
        hash ^= static_cast<uint64_t>(worldZ) * 19349663ULL;
        hash ^= hash >> 33;
        hash *= 0xff51afd7ed558ccdULL;
        hash ^= hash >> 33;
        hash *= 0xc4ceb9fe1a85ec53ULL;
        hash ^= hash >> 33;

        const int range = maxVal - minVal + 1;
        return minVal + static_cast<int>(hash % static_cast<uint64_t>(range));
    }
};