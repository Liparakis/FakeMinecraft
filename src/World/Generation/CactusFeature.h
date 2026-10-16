#pragma once

#include "Feature.h"
#include "../headers/WorldConstants.h"

/**
 * @class CactusFeature
 * @brief A 1-3 block cactus column on sand, with no direct horizontal neighbours.
 */
class CactusFeature final : public Feature {
public:
    explicit constexpr CactusFeature(float chance) noexcept : spawnChance(chance) {}

    [[nodiscard]] float getSpawnChance() const noexcept override { return spawnChance; }

    [[nodiscard]] bool canPlace(const uint8_t* chunkData, int localX, int localY, int localZ) const noexcept override {
        if (localY <= 0 || localY + 3 >= static_cast<int>(CHUNK_HEIGHT)) return false;
        if (localX < 1 || localX >= static_cast<int>(CHUNK_WIDTH) - 1 || localZ < 1 || localZ >= static_cast<int>(CHUNK_WIDTH) - 1) return false;
        const uint8_t below = chunkData[index(localX, localZ, localY - 1)];
        if (below != Blocks::SAND && below != Blocks::RED_SAND) return false;
        return chunkData[index(localX + 1, localZ, localY)] == Blocks::AIR && chunkData[index(localX - 1, localZ, localY)] == Blocks::AIR &&
               chunkData[index(localX, localZ + 1, localY)] == Blocks::AIR && chunkData[index(localX, localZ - 1, localY)] == Blocks::AIR;
    }

    [[nodiscard]] bool place(uint8_t* chunkData, int localX, int localY, int localZ,
                             int worldX, int worldZ, uint64_t seed) noexcept override {
        const int height = 1 + static_cast<int>((seed ^ (static_cast<uint64_t>(worldX) * 31 + static_cast<uint64_t>(worldZ) * 17)) % 3);
        for (int y = 0; y < height; ++y) chunkData[index(localX, localZ, localY + y)] = Blocks::CACTUS;
        return true;
    }

private:
    const float spawnChance;

    static constexpr int index(int x, int z, int y) noexcept {
        return (x * static_cast<int>(CHUNK_WIDTH) + z) * static_cast<int>(CHUNK_HEIGHT) + y;
    }
};
