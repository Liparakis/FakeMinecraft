#pragma once

#include <cstdint>

/// Every biome the generator can produce; picked by TerrainSampler::classify.
enum class BiomeID : uint8_t {
    OCEAN, DEEP_OCEAN, FROZEN_OCEAN, WARM_OCEAN,
    RIVER, FROZEN_RIVER,
    BEACH, SNOWY_BEACH, STONY_SHORE,
    PLAINS, FLOWER_PLAINS, SNOWY_PLAINS,
    FOREST, BIRCH_FOREST, DARK_FOREST,
    TAIGA, SNOWY_TAIGA,
    DESERT, SAVANNA, BADLANDS,
    SWAMP, JUNGLE,
    WINDSWEPT_HILLS, SNOWY_PEAKS, STONY_PEAKS,
    COUNT
};
