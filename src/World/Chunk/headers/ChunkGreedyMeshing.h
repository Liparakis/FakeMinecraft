#pragma once

/**
 * @file ChunkGreedyMeshing.h
 * @brief Logic for combining block faces to reduce vertex count.
 * Based on the "Greedy Meshing" algorithm by Mikola Lysenko:
 * http://0fps.net/2012/06/30/meshing-in-a-minecraft-game/
 */

#include <vector>
#include <glm/glm.hpp>
#include "WorldVertex.h"
#include "Block.h"
#include "BlockRegistry.h"

// Stores information about a group of combined block faces.
struct GreedyQuad {
    int x, y, z; // Position
    int width, height; // Size of the combined area
    uint16_t blockId; // The ID of the block type
    FACE_DIRECTION dir; // Which way the face is pointing
    uint8_t lightLevel; // Brightness of the face
};

// Adds a combined quad to the vertex list.
inline void emitGreedyQuad(const GreedyQuad &quad, const glm::vec3 &worldPos,
                           std::vector<WorldVertex> &vertices, std::vector<unsigned int> &indices,
                           unsigned int &currentVertex) {
    const Block &block = BlockRegistry::getInstance().getBlock(quad.blockId);

    // Get texture layer index based on face direction
    uint16_t layerIndex;
    if (quad.dir == BOTTOM) {
        layerIndex = block.bottomLayer;
    } else if (quad.dir == TOP) {
        layerIndex = block.topLayer;
    } else {
        layerIndex = block.sideLayer;
    }

    const char uMax = quad.width;
    const char vMax = quad.height;

    // x and z are chunk-local (int16 positions wrap past 32,767); the shader adds the chunk origin.
    const float wy = worldPos.y;

    const uint8_t light = quad.lightLevel;

    // Generate 4 vertices based on face direction
    switch (quad.dir) {
        case TOP:
            vertices.emplace_back(quad.x, quad.y + 1 + wy, quad.z + quad.height, 0, 0, TOP, layerIndex, light);
            vertices.emplace_back(quad.x + quad.width, quad.y + 1 + wy, quad.z + quad.height, uMax, 0, TOP,
                                  layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + 1 + wy, quad.z, 0, vMax, TOP, layerIndex, light);
            vertices.emplace_back(quad.x + quad.width, quad.y + 1 + wy, quad.z, uMax, vMax, TOP, layerIndex, light);
            break;
        case BOTTOM:
            vertices.emplace_back(quad.x + quad.width, quad.y + wy, quad.z + quad.height, uMax, vMax, BOTTOM,
                                  layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + wy, quad.z + quad.height, 0, vMax, BOTTOM, layerIndex, light);
            vertices.emplace_back(quad.x + quad.width, quad.y + wy, quad.z, uMax, 0, BOTTOM, layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + wy, quad.z, 0, 0, BOTTOM, layerIndex, light);
            break;
        case NORTH:
            vertices.emplace_back(quad.x + quad.width, quad.y + wy, quad.z, uMax, 0, NORTH, layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + wy, quad.z, 0, 0, NORTH, layerIndex, light);
            vertices.emplace_back(quad.x + quad.width, quad.y + quad.height + wy, quad.z, uMax, vMax, NORTH,
                                  layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + quad.height + wy, quad.z, 0, vMax, NORTH, layerIndex, light);
            break;
        case SOUTH:
            vertices.emplace_back(quad.x, quad.y + wy, quad.z + 1, 0, 0, SOUTH, layerIndex, light);
            vertices.emplace_back(quad.x + quad.width, quad.y + wy, quad.z + 1, uMax, 0, SOUTH, layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + quad.height + wy, quad.z + 1, 0, vMax, SOUTH, layerIndex, light);
            vertices.emplace_back(quad.x + quad.width, quad.y + quad.height + wy, quad.z + 1, uMax, vMax,
                                  SOUTH, layerIndex, light);
            break;
        case WEST:
            vertices.emplace_back(quad.x, quad.y + wy, quad.z, 0, 0, WEST, layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + wy, quad.z + quad.width, uMax, 0, WEST, layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + quad.height + wy, quad.z, 0, vMax, WEST, layerIndex, light);
            vertices.emplace_back(quad.x, quad.y + quad.height + wy, quad.z + quad.width, uMax, vMax, WEST,
                                  layerIndex, light);
            break;
        case EAST:
            vertices.emplace_back(quad.x + 1, quad.y + wy, quad.z + quad.width, uMax, 0, EAST, layerIndex, light);
            vertices.emplace_back(quad.x + 1, quad.y + wy, quad.z, 0, 0, EAST, layerIndex, light);
            vertices.emplace_back(quad.x + 1, quad.y + quad.height + wy, quad.z + quad.width, uMax, vMax,
                                  EAST, layerIndex, light);
            vertices.emplace_back(quad.x + 1, quad.y + quad.height + wy, quad.z, 0, vMax, EAST, layerIndex, light);
            break;
        default:
            break;
    }

    // Standard quad indices
    indices.push_back(currentVertex);
    indices.push_back(currentVertex + 3);
    indices.push_back(currentVertex + 1);
    indices.push_back(currentVertex);
    indices.push_back(currentVertex + 2);
    indices.push_back(currentVertex + 3);
    currentVertex += 4;
}