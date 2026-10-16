/**
 * @file _Vertex.h
 * @brief Basic vertex structure and face direction definitions.
 */
#pragma once

#include <cstdint>

/**
 * @enum FACE_DIRECTION
 * @brief Directions for block faces.
 */
enum FACE_DIRECTION {
    NORTH,
    SOUTH,
    WEST,
    EAST,
    BOTTOM,
    TOP,
    PLACEHOLDER_VALUE
};

/**
 * @struct Vertex
 * @brief Vertex layout for chunk geometry, 16 bytes with every attribute naturally aligned.
 *
 * Must match VertexLayout::Chunk in Renderer.cpp.
 */
struct Vertex {
    int16_t posX, posY, posZ, posPad = 0;
    uint8_t texU, texV;      ///< Texture coordinates (greedy quads repeat the texture).
    uint8_t direction;       ///< FACE_DIRECTION, used to look up the normal.
    uint8_t lightLevel;      ///< Sky light in the high nibble, block light in the low nibble.
    uint16_t layerIndex;     ///< Layer in the block texture array.
    uint8_t top = 0;         ///< Fluids only: 1 on the animated top surface.
    uint8_t pad = 0;

    Vertex(int16_t _posX, int16_t _posY, int16_t _posZ, char _texU, char _texV, char _direction, uint16_t _layerIndex, uint8_t _lightLevel = 15)
        : posX(_posX), posY(_posY), posZ(_posZ), texU(static_cast<uint8_t>(_texU)), texV(static_cast<uint8_t>(_texV)),
          direction(static_cast<uint8_t>(_direction)), lightLevel(_lightLevel), layerIndex(_layerIndex) {}
};
static_assert(sizeof(Vertex) == 16);
