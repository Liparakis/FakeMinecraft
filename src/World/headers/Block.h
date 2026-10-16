/**
 * @file Block.h
 * @brief Definition of the Block structure, the fundamental data unit of the world.
 */

#pragma once

#include <cstdio>
#include <cstring>

/**
 * This structure stores all the information about a block, like its name 
 * and what textures it uses.
 * 
 * It uses simple character arrays for names to keep things organized 
 * and to make it easier for the computer to load the data quickly.
 */
struct Block
{
public:
    /**
     * @enum BLOCK_TYPE
     * @brief Categorizes blocks by their physical and rendering behavior.
     */
	enum BLOCK_TYPE
	{
		SOLID,       ///< Regular blocks like Stone.
		TRANSPARENT, ///< Blocks you can see through like Glass.
		LEAVES,      ///< Leaf blocks (transparent but slightly different).
		BILLBOARD,   ///< Flat plants like flowers.
		LIQUID       ///< Water or Lava.
	};

    /** @name Texture Layer Cache
     * Cached indices for the TextureArray. These are resolved at runtime 
     * during initialization.
     * @{
     */
    mutable uint16_t topLayer = 0;    ///< Index for the top face texture.
    mutable uint16_t bottomLayer = 0; ///< Index for the bottom face texture.
    mutable uint16_t sideLayer = 0;   ///< Index for the side face textures.
    /** @} */

    BLOCK_TYPE blockType; ///< The physical behavior of this block.
    
    /** @name Fixed-Size Identifiers
     * Fixed arrays ensure strict structure locality and zero heap fragmentation.
     * @{
     */
    char blockName[32];     ///< Descriptive name (e.g., "GRASS_BLOCK").
    char topTexName[32];    ///< Unique name of the top texture file.
    char bottomTexName[32]; ///< Unique name of the bottom texture file.
    char sideTexName[32];   ///< Unique name of the side texture file.
    /** @} */

    uint8_t id = 0; ///< Internal runtime ID used for voxel mapping.

    /**
     * @brief Default Constructor. Initializes an empty block template.
     */
    Block() : blockType(SOLID) {
        blockName[0] = '\0';
        topTexName[0] = '\0';
        bottomTexName[0] = '\0';
        sideTexName[0] = '\0';
    }

    /**
     * @brief Builder-compatible Constructor.
     * @param name The unique identifier for this block.
     * @param type The physical and render category.
     */
    Block(const char* name, BLOCK_TYPE type) : blockType(type) {
         std::snprintf(blockName, sizeof(blockName), "%s", name);
         std::snprintf(topTexName, sizeof(topTexName), "%s", name);
         std::snprintf(bottomTexName, sizeof(bottomTexName), "%s", name);
         std::snprintf(sideTexName, sizeof(sideTexName), "%s", name);
    }
};