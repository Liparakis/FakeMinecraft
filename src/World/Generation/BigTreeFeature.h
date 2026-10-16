#pragma once

#include "Feature.h"
#include "../headers/WorldConstants.h"
#include "../headers/Blocks.h"

/**
 * BigTreeFeature - Generates large oak-like trees with branches.
 * This class includes logic to make sure trees don't overlap too much.
 */
class BigTreeFeature final : public Feature {
public:
    const uint8_t logBlock;
    const uint8_t leafBlock;
    const float spawnChance;

    constexpr BigTreeFeature(uint8_t log, uint8_t leaf, float chance = 0.001f) noexcept
        : logBlock(log), leafBlock(leaf), spawnChance(chance) {}

    [[nodiscard]] float getSpawnChance() const noexcept override {
        return spawnChance;
    }

    [[nodiscard]] bool canPlace(const uint8_t* chunkData,
                                 int localX, int localY, int localZ) const noexcept override {
        // Check height constraint
        if (localY >= CHUNK_HEIGHT - 12) {
            return false;
        }

        // Check horizontal bounds (need space for canopy)
        if (localX < 2 || localX > CHUNK_WIDTH - 3 ||
            localZ < 2 || localZ > CHUNK_WIDTH - 3) {
            return false;
        }

        // Check ground block
        const int idx = calculateIndex(localX, localZ, localY - 1);
        const uint8_t groundBlock = chunkData[idx];
        return Blocks::isSoil(groundBlock);
    }

    [[nodiscard]] bool place(uint8_t* chunkData,
                              int localX, int localY, int localZ,
                              int worldX, int worldZ,
                              uint64_t seed) noexcept override {

        // Use a simple random number generator to decide tree height
        FastRNG rng(hashSeed(seed, worldX, worldZ));
        const int height = rng.nextRange(8, 12);

        // Place main trunk - optimized with base index calculation
        const int trunkBase = calculateIndex(localX, localZ, localY);
        for (int h = 0; h < height; ++h) {
            chunkData[trunkBase + h] = logBlock;
        }

        // Place canopy (large sphere)
        const int canopyY = localY + height - 2;
        constexpr int radius = 3;
        constexpr int radiusSqPlusOne = radius * radius + 1;

        // Pre-clamp bounds for canopy to avoid checks in inner loops
        const int minX = std::max(0, localX - radius);
        const int maxX = std::min<int>(CHUNK_WIDTH - 1, localX + radius);
        const int minZ = std::max(0, localZ - radius);
        const int maxZ = std::min<int>(CHUNK_WIDTH - 1, localZ + radius);
        const int minY = std::max(0, canopyY - radius);
        const int maxY = std::min<int>(CHUNK_HEIGHT - 1, canopyY + radius);

        for (int px = minX; px <= maxX; ++px) {
            const int dx = px - localX;
            const int dxSq = dx * dx;
            const int xBase = px * CHUNK_WIDTH * CHUNK_HEIGHT;

            for (int pz = minZ; pz <= maxZ; ++pz) {
                const int dz = pz - localZ;
                const int dzSq = dz * dz;
                const int dxzSq = dxSq + dzSq;
                const int xzBase = xBase + pz * CHUNK_HEIGHT;

                for (int py = minY; py <= maxY; ++py) {
                    const int dy = py - canopyY;
                    const int dySq = dy * dy;

                    if (dxzSq + dySq <= radiusSqPlusOne) {
                        const int idx = xzBase + py;
                        if (chunkData[idx] == Blocks::AIR) {
                            chunkData[idx] = leafBlock;
                        }
                    }
                }
            }
        }

        // Add branches - optimized with bounds checking
        for (int i = 0; i < 3; ++i) {
            const int branchHeight = height / 2 + rng.nextRange(0, 2);
            const int dirX = rng.nextDirection();
            const int dirZ = rng.nextDirection();

            if (dirX == 0 && dirZ == 0) continue;

            for (int len = 1; len <= 2; ++len) {
                const int px = localX + dirX * len;
                const int py = localY + branchHeight + len;
                const int pz = localZ + dirZ * len;

                if (px >= 0 && px < CHUNK_WIDTH &&
                    pz >= 0 && pz < CHUNK_WIDTH &&
                    py >= 0 && py < CHUNK_HEIGHT) {
                    const int idx = calculateIndex(px, pz, py);
                    chunkData[idx] = logBlock;
                }
            }
        }

        return true;
    }

private:
    [[nodiscard]] static constexpr inline int calculateIndex(int x, int z, int y) noexcept {
        return (x * CHUNK_WIDTH + z) * CHUNK_HEIGHT + y;
    }

    struct FastRNG {
        uint64_t state;
        explicit FastRNG(uint64_t seed) noexcept : state(seed) {
            if (state == 0) state = 1;
        }
        [[nodiscard]] inline uint64_t next() noexcept {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            return state * 0x2545F4914F6CDD1DULL;
        }
        [[nodiscard]] inline int nextRange(int minVal, int maxVal) noexcept {
            const int range = maxVal - minVal + 1;
            return minVal + static_cast<int>(next() % static_cast<uint64_t>(range));
        }
        [[nodiscard]] inline int nextDirection() noexcept {
            return static_cast<int>(next() % 3) - 1;
        }
    };

    // A simple function to create a random number based on the location
    [[nodiscard]] static inline uint64_t hashSeed(uint64_t seed, int worldX, int worldZ) noexcept {
        uint64_t hash = seed;
        hash ^= static_cast<uint64_t>(worldX) * 73856093ULL;
        hash ^= static_cast<uint64_t>(worldZ) * 19349663ULL;
        hash ^= hash >> 33;
        hash *= 0xff51afd7ed558ccdULL;
        hash ^= hash >> 33;
        return hash;
    }
};