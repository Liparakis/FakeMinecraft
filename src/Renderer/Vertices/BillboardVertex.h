/**
 * @file BillboardVertex.h
 * @brief Vertex for plants and other cross-shaped billboards (20 bytes).
 *
 * Must match VertexLayout::Billboard in Renderer.cpp.
 */
#pragma once

#include "_Vertex.h"

struct BillboardVertex {
    float posX, posY, posZ; ///< Float positions so the crossed quads can sit off the block grid.
    uint8_t texU, texV, direction = 0, lightLevel;
    uint16_t layerIndex;
    uint16_t pad = 0;

    BillboardVertex(float _posX, float _posY, float _posZ, char _texU, char _texV, uint16_t _layerIndex, uint8_t _lightLevel = 15)
        : posX(_posX), posY(_posY), posZ(_posZ), texU(static_cast<uint8_t>(_texU)), texV(static_cast<uint8_t>(_texV)),
          lightLevel(_lightLevel), layerIndex(_layerIndex) {}
};
static_assert(sizeof(BillboardVertex) == 20);
