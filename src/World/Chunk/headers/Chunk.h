#pragma once

#include "Renderer.h"
#include <thread>
#include <vector>
#include <glm/glm.hpp>
#include "WorldVertex.h"
#include "FluidVertex.h"
#include "BillboardVertex.h"
#include "Block.h"
#include "ChunkPos.h"
#include "ChunkData.h"
#include "WorldConstants.h"
#include <memory>

constexpr int SUBCHUNK_HEIGHT = 16;
constexpr int NUM_SUBCHUNKS = 16; // CHUNK_HEIGHT / SUBCHUNK_HEIGHT

struct LiquidFace {
    char x, y, z;
    FACE_DIRECTION dir;
    const Block *block;
    char waterTopValue;
};

// Divides the chunk into smaller sections to help with performance.
struct SubChunk {
    unsigned int numTrianglesWorld{}, numTrianglesLiquid{}, numTrianglesBillboard{};
    glm::vec3 cullingCenter;
    glm::vec3 cullingExtents;
    bool ready = false;
};

class Chunk {
public:
    explicit Chunk(ChunkPos chunkPos);
    ~Chunk();

    // Logic for creating the visual mesh for the blocks.
    void generateChunkMesh();

    // Light map computation (BFS flood-fill from emissive blocks)
    void computeLightMap();

    // Functions for combining multiple block faces to save processing time.
    void greedyMergeTopFaces(int y, int subChunkIndex, uint16_t topMask[][CHUNK_WIDTH],
                            bool processed[][CHUNK_WIDTH], unsigned int* currentVertex);
    void greedyMergeBottomFaces(int y, int subChunkIndex, uint16_t bottomMask[][CHUNK_WIDTH],
                               bool processed[][CHUNK_WIDTH], unsigned int* currentVertex);

    // Vertical faces (NORTH/SOUTH/EAST/WEST) with greedy meshing
    void generateHorizontalFaces(unsigned int* currentVertex);
    void generateVerticalFaces(unsigned int* currentVertex);
    void generateZAxisFaces(unsigned int* currentVertex, uint16_t maskA[][CHUNK_HEIGHT],
                           uint16_t maskB[][CHUNK_HEIGHT], bool processedSide[][CHUNK_HEIGHT]);
    void generateXAxisFaces(unsigned int* currentVertex, uint16_t maskA[][CHUNK_HEIGHT],
                           uint16_t maskB[][CHUNK_HEIGHT], bool processedSide[][CHUNK_HEIGHT]);
    void greedyMergeNorthFaces(int z, uint16_t maskA[][CHUNK_HEIGHT],
                              bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex);
    void greedyMergeSouthFaces(int z, uint16_t maskB[][CHUNK_HEIGHT],
                              bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex);
    void greedyMergeWestFaces(int x, uint16_t maskA[][CHUNK_HEIGHT],
                             bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex);
    void greedyMergeEastFaces(int x, uint16_t maskB[][CHUNK_HEIGHT],
                             bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex);

    // Special blocks (liquids, billboards, transparent)
    void generateSpecialBlocks(unsigned int* currentVertex, unsigned int* currentLiquidVertex,
                              unsigned int* currentBillboardVertex);

    // ========================================================================
    // FACE GENERATION (ChunkFaceGeneration.cpp)
    // ========================================================================
    void generateWorldFaces(int x, int y, int z, FACE_DIRECTION faceDirection, const Block *block,
                            unsigned int &currentVertex, int subChunkIndex);

    void generateBillboardFaces(int x, int y, int z, const Block *block, unsigned int &currentVertex, int subChunkIndex);

    void generateLiquidFaces(int x, int y, int z, FACE_DIRECTION faceDirection, const Block *block,
                             unsigned int &currentVertex, char liquidTopValue, uint8_t light, int subChunkIndex);

    // ========================================================================
    // RENDERING (ChunkRendering.cpp)
    // ========================================================================
    void uploadMesh();
    // Helper functions to draw everything in the chunk at once.
    void renderAllSolid();
    void renderAllBillboard() const;
    void renderAllWater() const;
    /// Frees the GPU meshes once in-flight frames are done with them.
    void releaseMeshes();

    // ========================================================================
    // BLOCK OPERATIONS (Chunk.cpp)
    // ========================================================================
    [[nodiscard]] uint16_t getBlockAtPos(int x, int y, int z) const;
    void updateBlock(int x, int y, int z, uint8_t newBlock);
    void updateChunk();

    // Reset chunk for reuse in object pool
    void reset(ChunkPos newPos);

    [[nodiscard]] uint8_t getLightLevel(int x, int y, int z) const {
        if (x < 0 || x >= CHUNK_WIDTH || y < 0 || y >= CHUNK_HEIGHT || z < 0 || z >= CHUNK_WIDTH) return 0;
        return lightMap[x * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT + y];
    }

    // ========================================================================
    // UTILITY FUNCTIONS
    // ========================================================================
    [[nodiscard]] bool hasSolid() const { return static_cast<bool>(worldMesh); }
    [[nodiscard]] bool hasBillboard() const { return static_cast<bool>(billboardMesh); }
    [[nodiscard]] bool hasWater() const { return static_cast<bool>(waterMesh); }
    [[nodiscard]] bool hasMesh() const { return hasSolid() || hasBillboard() || hasWater(); }

    // ========================================================================
    // PUBLIC MEMBER VARIABLES
    // ========================================================================
    std::shared_ptr<ChunkData> chunkData{};
    std::shared_ptr<ChunkData> northData{};
    std::shared_ptr<ChunkData> southData{};
    std::shared_ptr<ChunkData> upData{};
    std::shared_ptr<ChunkData> downData{};
    std::shared_ptr<ChunkData> eastData{};
    std::shared_ptr<ChunkData> westData{};
    ChunkPos chunkPos;
    bool ready;
    bool generated;
    size_t listIndex = 0; // For O(1) removal from Planet::chunkList
    float cachedDistSq = 0.0f;  // Cached distance squared for sorting (updated once per frame)

    // Chunk-level culling data (for frustum culling)
    glm::vec3 cullingCenter;
    glm::vec3 cullingExtents;

    // One GPU mesh per geometry type, so each is a single draw call.
    GpuMesh worldMesh, billboardMesh, waterMesh;

    // Sub-chunk data (kept for potential future per-sub-chunk culling)
    SubChunk subChunks[NUM_SUBCHUNKS];

private:
    glm::vec3 worldPos;
    std::thread chunkThread;

    // Per-block light level (0-15), same indexing as ChunkData
    uint8_t lightMap[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH] = {};

    // Neighbor chunks cache (updated in generateChunkMesh)
    // 0: North (-Z), 1: South (+Z), 2: West (-X), 3: East (+X)
    const Chunk* neighborChunks[4] = { nullptr, nullptr, nullptr, nullptr };

    // Temporary buffers per sub-chunk (used during generation, cleared after upload)
    std::vector<WorldVertex> worldVertices[NUM_SUBCHUNKS];
    std::vector<unsigned int> worldIndices[NUM_SUBCHUNKS];
    std::vector<FluidVertex> liquidVertices[NUM_SUBCHUNKS];
    std::vector<unsigned int> liquidIndices[NUM_SUBCHUNKS];
    std::vector<BillboardVertex> billboardVertices[NUM_SUBCHUNKS];
    std::vector<unsigned int> billboardIndices[NUM_SUBCHUNKS];
};