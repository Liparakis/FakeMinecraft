/**
 * @file Biome.h
 * @brief Definition of different biomes in the game.
 */
#pragma once

#include <vector>
#include <cstdint>
#include "Feature.h"
#include "BiomeID.h"

/**
 * @struct Biome
 * @brief How a biome dresses the terrain: surface blocks and decorations.
 *
 * Terrain height is not a biome property (see Terrain.h), which is what keeps
 * biome borders from turning into cliffs.
 */
struct Biome {
    BiomeID id = BiomeID::PLAINS;
    char name[32] = {};

    uint8_t surfaceBlock = 0;    ///< Top block above the water line (Grass, Sand).
    uint8_t underBlock = 0;      ///< The few blocks below the surface (Dirt, Sand).
    uint8_t underwaterBlock = 0; ///< Top block of columns below the water line.
    uint8_t steepBlock = 0;      ///< Replaces the surface on steep slopes (usually Stone).
    int underDepth = 3;
    bool frozenWater = false;    ///< Water surface freezes to ice.
    bool terracottaBands = false;///< Badlands: layered terracotta instead of stone near the surface.

    /// Placement order matters: big features first so small plants don't block them.
    std::vector<Feature*> features;
};
