/**
 * @file Blocks.h
 * @brief Utilities for finding blocks and adding them to the game.
 */

#pragma once

#include "BlockRegistry.h"
#include "BlockBuilder.h"
#include "../../Renderer/headers/TextureManager.h"

/**
 * @namespace Blocks
 * @brief Provides a simple way to get block IDs and register new blocks.
 */
namespace Blocks {
    /** @name Global Block IDs
     * Cached numeric identifiers for high-speed voxel access. These are 
     * populated during the init() call.
     * @{
     */
    inline uint8_t AIR = 0;
    inline uint8_t DIRT = 0;
    inline uint8_t GRASS = 0; ///< The short grass plant entity.
    inline uint8_t GRASS_BLOCK = 0; ///< The standard dirt block with grass on top.
    inline uint8_t STONE = 0;
    inline uint8_t OAK_LOG = 0;
    inline uint8_t OAK_LEAVES = 0;
    inline uint8_t TALL_GRASS_BOTTOM = 0;
    inline uint8_t TALL_GRASS_TOP = 0;
    inline uint8_t POPPY = 0;
    inline uint8_t WHITE_TULIP = 0;
    inline uint8_t PINK_TULIP = 0;
    inline uint8_t ORANGE_TULIP = 0;
    inline uint8_t WATER = 0;
    inline uint8_t LAVA = 0;
    inline uint8_t SAND = 0;
    inline uint8_t COAL_ORE = 0;
    inline uint8_t IRON_ORE = 0;
    inline uint8_t GOLD_ORE = 0;
    inline uint8_t DIAMOND_ORE = 0;
    inline uint8_t EMERALD_ORE = 0;
    inline uint8_t GRAVEL = 0;
    inline uint8_t BEDROCK = 0;

    // Biome blocks (textures from the Faithful 64x pack, see tools/fetch_faithful_textures.py)
    inline uint8_t SNOW = 0;
    inline uint8_t SNOWY_GRASS_BLOCK = 0;
    inline uint8_t ICE = 0;
    inline uint8_t PACKED_ICE = 0;
    inline uint8_t RED_SAND = 0;
    inline uint8_t SANDSTONE = 0;
    inline uint8_t CLAY = 0;
    inline uint8_t COARSE_DIRT = 0;
    inline uint8_t PODZOL = 0;
    inline uint8_t DRY_GRASS_BLOCK = 0;
    inline uint8_t SWAMP_GRASS_BLOCK = 0;
    inline uint8_t LUSH_GRASS_BLOCK = 0;
    inline uint8_t COLD_GRASS_BLOCK = 0;
    inline uint8_t TERRACOTTA = 0;
    inline uint8_t ORANGE_TERRACOTTA = 0;
    inline uint8_t YELLOW_TERRACOTTA = 0;
    inline uint8_t WHITE_TERRACOTTA = 0;
    inline uint8_t RED_TERRACOTTA = 0;
    inline uint8_t BROWN_TERRACOTTA = 0;
    inline uint8_t LIGHT_GRAY_TERRACOTTA = 0;
    inline uint8_t SPRUCE_LOG = 0;
    inline uint8_t SPRUCE_LEAVES = 0;
    inline uint8_t BIRCH_LOG = 0;
    inline uint8_t BIRCH_LEAVES = 0;
    inline uint8_t JUNGLE_LOG = 0;
    inline uint8_t JUNGLE_LEAVES = 0;
    inline uint8_t ACACIA_LOG = 0;
    inline uint8_t ACACIA_LEAVES = 0;
    inline uint8_t DARK_OAK_LOG = 0;
    inline uint8_t DARK_OAK_LEAVES = 0;
    inline uint8_t CACTUS = 0;
    inline uint8_t DEAD_BUSH = 0;
    inline uint8_t DRY_SHRUB = 0;
    inline uint8_t FERN = 0;
    inline uint8_t CORNFLOWER = 0;
    inline uint8_t DANDELION = 0;
    inline uint8_t AZURE_BLUET = 0;
    inline uint8_t BLUE_ORCHID = 0;
    /** @} */

    /// Blocks that trees and plants can grow on.
    inline bool isSoil(const uint8_t id) {
        return id == GRASS_BLOCK || id == DIRT || id == SNOWY_GRASS_BLOCK || id == DRY_GRASS_BLOCK ||
               id == SWAMP_GRASS_BLOCK || id == LUSH_GRASS_BLOCK || id == COLD_GRASS_BLOCK ||
               id == PODZOL || id == COARSE_DIRT;
    }

    /**
     * @brief Adds all the blocks to the game system.
     * 
     * 1. Teaches the registry about every block type.
     * 2. Saves the block numbers for later use.
     * 3. Finds the right textures for each block.
     */
    inline void init() {
        BlockRegistry& registry = BlockRegistry::getInstance();
        
        // --- Registration Phase ---
        
        AIR = registry.registerBlock(
            BlockBuilder("AIR").setTransparent().setTexture("air").build()
        );

        DIRT = registry.registerBlock(
            BlockBuilder("DIRT").setSolid().setTexture("dirt").build()
        );

        GRASS_BLOCK = registry.registerBlock(
            BlockBuilder("GRASS_BLOCK")
                .setGrassPattern("grass_block_top", "dirt", "grass_block_side")
                .setSolid()
                .build()
        );

        STONE = registry.registerBlock(
            BlockBuilder("STONE").setSolid().setTexture("stone").build()
        );

        OAK_LOG = registry.registerBlock(
            BlockBuilder("LOG")
                .setLogPattern("oak_log_top", "oak_log")
                .setSolid()
                .build()
        );

        OAK_LEAVES = registry.registerBlock(
            BlockBuilder("LEAVES").setLeaves().setTexture("oak_leaves").build()
        );

        GRASS = registry.registerBlock(
            BlockBuilder("GRASS").setBillboard().setTexture("short_grass").build()
        );

        TALL_GRASS_BOTTOM = registry.registerBlock(
            BlockBuilder("TALL_GRASS_BOTTOM").setBillboard().setTexture("tall_grass_bottom").build()
        );

        TALL_GRASS_TOP = registry.registerBlock(
            BlockBuilder("TALL_GRASS_TOP").setBillboard().setTexture("tall_grass_top").build()
        );

        POPPY = registry.registerBlock(
            BlockBuilder("POPPY").setBillboard().setTexture("poppy").build()
        );

        WHITE_TULIP = registry.registerBlock(
            BlockBuilder("WHITE_TULIP").setBillboard().setTexture("white_tulip").build()
        );

        PINK_TULIP = registry.registerBlock(
            BlockBuilder("PINK_TULIP").setBillboard().setTexture("pink_tulip").build()
        );

        ORANGE_TULIP = registry.registerBlock(
            BlockBuilder("ORANGE_TULIP").setBillboard().setTexture("orange_tulip").build()
        );

        WATER = registry.registerBlock(
            BlockBuilder("WATER").setLiquid().setTexture("water_still").build()
        );

        LAVA = registry.registerBlock(
            BlockBuilder("LAVA").setLiquid().setTexture("lava_still").build()
        );

        SAND = registry.registerBlock(
            BlockBuilder("SAND").setSolid().setTexture("sand").build()
        );
        
        COAL_ORE = registry.registerBlock(
            BlockBuilder("COAL_ORE").setSolid().setTexture("coal_ore").build()
        );
        IRON_ORE = registry.registerBlock(
            BlockBuilder("IRON_ORE").setSolid().setTexture("iron_ore").build()
        );
        GOLD_ORE = registry.registerBlock(
            BlockBuilder("GOLD_ORE").setSolid().setTexture("gold_ore").build()
        );
        DIAMOND_ORE = registry.registerBlock(
            BlockBuilder("DIAMOND_ORE").setSolid().setTexture("diamond_ore").build()
        );
        EMERALD_ORE = registry.registerBlock(
            BlockBuilder("EMERALD_ORE").setSolid().setTexture("emerald_ore").build()
        );

        GRAVEL = registry.registerBlock(
            BlockBuilder("GRAVEL").setSolid().setTexture("gravel").build()
        );

        BEDROCK = registry.registerBlock(
            BlockBuilder("BEDROCK").setSolid().setTexture("bedrock").build()
        );

        // New blocks are appended so the IDs above (used by the sound table) stay stable.
        auto solid = [&](const char* name, const char* tex) {
            return registry.registerBlock(BlockBuilder(name).setSolid().setTexture(tex).build());
        };
        auto sided = [&](const char* name, const char* top, const char* bottom, const char* side) {
            return registry.registerBlock(BlockBuilder(name).setSolid().setTextures(top, bottom, side).build());
        };
        auto leaves = [&](const char* name, const char* tex) {
            return registry.registerBlock(BlockBuilder(name).setLeaves().setTexture(tex).build());
        };
        auto plant = [&](const char* name, const char* tex) {
            return registry.registerBlock(BlockBuilder(name).setBillboard().setTexture(tex).build());
        };

        SNOW = solid("SNOW", "snow");
        SNOWY_GRASS_BLOCK = sided("SNOWY_GRASS_BLOCK", "snow", "dirt", "grass_block_snow");
        ICE = solid("ICE", "ice");
        PACKED_ICE = solid("PACKED_ICE", "packed_ice");
        RED_SAND = solid("RED_SAND", "red_sand");
        SANDSTONE = sided("SANDSTONE", "sandstone_top", "sandstone_bottom", "sandstone");
        CLAY = solid("CLAY", "clay");
        COARSE_DIRT = solid("COARSE_DIRT", "coarse_dirt");
        PODZOL = sided("PODZOL", "podzol_top", "dirt", "podzol_side");
        DRY_GRASS_BLOCK = sided("DRY_GRASS_BLOCK", "grass_block_top_dry", "dirt", "grass_block_side_dry");
        SWAMP_GRASS_BLOCK = sided("SWAMP_GRASS_BLOCK", "grass_block_top_swamp", "dirt", "grass_block_side_swamp");
        LUSH_GRASS_BLOCK = sided("LUSH_GRASS_BLOCK", "grass_block_top_lush", "dirt", "grass_block_side_lush");
        COLD_GRASS_BLOCK = sided("COLD_GRASS_BLOCK", "grass_block_top_cold", "dirt", "grass_block_side_cold");
        TERRACOTTA = solid("TERRACOTTA", "terracotta");
        ORANGE_TERRACOTTA = solid("ORANGE_TERRACOTTA", "orange_terracotta");
        YELLOW_TERRACOTTA = solid("YELLOW_TERRACOTTA", "yellow_terracotta");
        WHITE_TERRACOTTA = solid("WHITE_TERRACOTTA", "white_terracotta");
        RED_TERRACOTTA = solid("RED_TERRACOTTA", "red_terracotta");
        BROWN_TERRACOTTA = solid("BROWN_TERRACOTTA", "brown_terracotta");
        LIGHT_GRAY_TERRACOTTA = solid("LIGHT_GRAY_TERRACOTTA", "light_gray_terracotta");
        SPRUCE_LOG = sided("SPRUCE_LOG", "spruce_log_top", "spruce_log_top", "spruce_log");
        SPRUCE_LEAVES = leaves("SPRUCE_LEAVES", "spruce_leaves");
        BIRCH_LOG = sided("BIRCH_LOG", "birch_log_top", "birch_log_top", "birch_log");
        BIRCH_LEAVES = leaves("BIRCH_LEAVES", "birch_leaves");
        JUNGLE_LOG = sided("JUNGLE_LOG", "jungle_log_top", "jungle_log_top", "jungle_log");
        JUNGLE_LEAVES = leaves("JUNGLE_LEAVES", "jungle_leaves");
        ACACIA_LOG = sided("ACACIA_LOG", "acacia_log_top", "acacia_log_top", "acacia_log");
        ACACIA_LEAVES = leaves("ACACIA_LEAVES", "acacia_leaves");
        DARK_OAK_LOG = sided("DARK_OAK_LOG", "dark_oak_log_top", "dark_oak_log_top", "dark_oak_log");
        DARK_OAK_LEAVES = leaves("DARK_OAK_LEAVES", "dark_oak_leaves");
        CACTUS = sided("CACTUS", "cactus_top", "cactus_top", "cactus_side");
        DEAD_BUSH = plant("DEAD_BUSH", "dead_bush");
        DRY_SHRUB = plant("DRY_SHRUB", "short_dry_grass");
        FERN = plant("FERN", "fern");
        CORNFLOWER = plant("CORNFLOWER", "cornflower");
        DANDELION = plant("DANDELION", "dandelion");
        AZURE_BLUET = plant("AZURE_BLUET", "azure_bluet");
        BLUE_ORCHID = plant("BLUE_ORCHID", "blue_orchid");

        // --- Resolution Phase ---
        const TextureManager& tm = TextureManager::getInstance();
        for (const auto& block : registry.getAllBlocks()) {
             const Block& b = registry.getBlockMutable(block.id);
             if (b.blockName == "AIR") continue;
             
             // Map string names to numeric texture indices
             b.topLayer = tm.getLayerIndex(b.topTexName);
             b.bottomLayer = tm.getLayerIndex(b.bottomTexName);
             b.sideLayer = tm.getLayerIndex(b.sideTexName);
        }
    }
}
