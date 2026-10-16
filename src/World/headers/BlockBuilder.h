/**
 * @file BlockBuilder.h
 * @brief Helper to create Block definitions easily.
 */

#pragma once

#include <cstdio>

#include "Block.h"

/**
 * @class BlockBuilder
 * @brief A helper class that makes it easy to set up new types of blocks.
 * 
 * It allows us to set the name, textures, and type of a block one after 
 * another before creating the final block object.
 */
class BlockBuilder {
public:
    /**
     * @brief Starts a new block definition.
     * @param name The unique name of the block.
     */
    BlockBuilder(const char* name) {
        std::snprintf(block.blockName, sizeof(block.blockName), "%s", name);
        block.blockType = Block::SOLID; // Default
        
        // Default texture name = block name
        std::snprintf(block.topTexName, sizeof(block.topTexName), "%s", name);
        std::snprintf(block.bottomTexName, sizeof(block.bottomTexName), "%s", name);
        std::snprintf(block.sideTexName, sizeof(block.sideTexName), "%s", name);
    }

    /**
     * @brief Sets all 6 faces to use the same texture.
     */
    BlockBuilder& setTexture(const char* texName) {
        std::snprintf(block.topTexName, sizeof(block.topTexName), "%s", texName);
        std::snprintf(block.bottomTexName, sizeof(block.bottomTexName), "%s", texName);
        std::snprintf(block.sideTexName, sizeof(block.sideTexName), "%s", texName);
        return *this;
    }

    /**
     * @brief Manually sets different textures for Top, Bottom, and Sides.
     */
    BlockBuilder& setTextures(const char* top, const char* bottom, const char* side) {
        std::snprintf(block.topTexName, sizeof(block.topTexName), "%s", top);
        std::snprintf(block.bottomTexName, sizeof(block.bottomTexName), "%s", bottom);
        std::snprintf(block.sideTexName, sizeof(block.sideTexName), "%s", side);
        return *this;
    }

    /**
     * @brief Higher-level helper for grass-like blocks.
     */
    BlockBuilder& setGrassPattern(const char* top, const char* dirt, const char* side) {
        return setTextures(top, dirt, side);
    }
    
    /**
     * @brief Higher-level helper for cylindrical log blocks.
     */
    BlockBuilder& setLogPattern(const char* topBottom, const char* side) {
        return setTextures(topBottom, topBottom, side);
    }

    /// @brief Marks the block as a standard solid cube.
    BlockBuilder& setSolid() {
        block.blockType = Block::SOLID;
        return *this;
    }

    /// @brief Marks the block as transparent (e.g., Glass).
    BlockBuilder& setTransparent() {
        block.blockType = Block::TRANSPARENT;
        return *this;
    }

    /// @brief Marks the block as leaves (optimized alpha-testing).
    BlockBuilder& setLeaves() {
        block.blockType = Block::LEAVES;
        return *this;
    }

    /// @brief Marks the block as a plant/billboard (cross-quad).
    BlockBuilder& setBillboard() {
        block.blockType = Block::BILLBOARD;
        return *this;
    }

    /// @brief Marks the block as a liquid (lava/water).
    BlockBuilder& setLiquid() {
        block.blockType = Block::LIQUID;
        return *this;
    }
    
    /**
     * @brief Finalizes the construction.
     * @return The configured Block object.
     */
    Block build() const {
        return block;
    }

private:
    Block block{"temp", Block::SOLID}; ///< Internal workspace for the builder.
};
