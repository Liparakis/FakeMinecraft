#pragma once

#include "Feature.h"
#include "../headers/WorldConstants.h"

/**
 * @class ConiferTreeFeature
 * @brief A tall spruce-style tree whose leaf layers widen in a jagged cone towards the ground.
 */
class ConiferTreeFeature final : public Feature {
public:
    constexpr ConiferTreeFeature(uint8_t log, uint8_t leaves, int minH, int maxH, float chance) noexcept
        : logBlock(log), leavesBlock(leaves), minHeight(minH), maxHeight(maxH), spawnChance(chance) {}

    [[nodiscard]] float getSpawnChance() const noexcept override { return spawnChance; }

    [[nodiscard]] bool canPlace(const uint8_t* chunkData, int localX, int localY, int localZ) const noexcept override {
        if (localY <= 0 || localY + maxHeight + 2 >= static_cast<int>(CHUNK_HEIGHT)) return false;
        if (localX < MAX_RADIUS || localX >= static_cast<int>(CHUNK_WIDTH) - MAX_RADIUS) return false;
        if (localZ < MAX_RADIUS || localZ >= static_cast<int>(CHUNK_WIDTH) - MAX_RADIUS) return false;
        return Blocks::isSoil(chunkData[index(localX, localZ, localY - 1)]);
    }

    [[nodiscard]] bool place(uint8_t* chunkData, int localX, int localY, int localZ,
                             int worldX, int worldZ, uint64_t seed) noexcept override {
        uint64_t h = seed ^ (static_cast<uint64_t>(worldX) * 0x9E3779B97F4A7C15ULL) ^ (static_cast<uint64_t>(worldZ) * 0xC2B2AE3D27D4EB4FULL);
        h ^= h >> 31;
        const int height = minHeight + static_cast<int>(h % static_cast<uint64_t>(maxHeight - minHeight + 1));

        for (int y = 0; y < height; ++y) chunkData[index(localX, localZ, localY + y)] = logBlock;

        // Layers count down from the tip; radius grows every other layer and alternates for a jagged edge.
        const int top = localY + height;
        for (int layer = 0; layer <= height - 2; ++layer) {
            const int ly = top - layer;
            const int r = std::min(MAX_RADIUS, (layer + 1) / 2 - ((layer % 4 == 3) ? 1 : 0));
            for (int dx = -r; dx <= r; ++dx) {
                for (int dz = -r; dz <= r; ++dz) {
                    if (r > 0 && std::abs(dx) == r && std::abs(dz) == r) continue;
                    uint8_t &b = chunkData[index(localX + dx, localZ + dz, ly)];
                    if (b == Blocks::AIR) b = leavesBlock;
                }
            }
        }
        chunkData[index(localX, localZ, top)] = leavesBlock;
        return true;
    }

private:
    static constexpr int MAX_RADIUS = 3;
    const uint8_t logBlock, leavesBlock;
    const int minHeight, maxHeight;
    const float spawnChance;

    static constexpr int index(int x, int z, int y) noexcept {
        return (x * static_cast<int>(CHUNK_WIDTH) + z) * static_cast<int>(CHUNK_HEIGHT) + y;
    }
};
