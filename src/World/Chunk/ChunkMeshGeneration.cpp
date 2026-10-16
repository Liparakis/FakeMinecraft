#include "Chunk.h"
#include "ChunkUtils.h"
#include "ChunkGreedyMeshing.h"
#include "Blocks.h"
#include "Planet.h"
#include "LoadStats.h"


void Chunk::generateChunkMesh() {
    if (!chunkData) return;
    loadstats::ScopedTimer timer(loadstats::meshingMicros);
    loadstats::meshedChunks.fetch_add(1, std::memory_order_relaxed);

    // Work out how light spreads from glowing blocks like lava.
    computeLightMap();

    // Cache neighbor chunks for lighting at boundaries
    neighborChunks[0] = Planet::planet->getChunk({chunkPos.x, chunkPos.y, chunkPos.z - 1}); // North
    neighborChunks[1] = Planet::planet->getChunk({chunkPos.x, chunkPos.y, chunkPos.z + 1}); // South
    neighborChunks[2] = Planet::planet->getChunk({chunkPos.x - 1, chunkPos.y, chunkPos.z}); // West
    neighborChunks[3] = Planet::planet->getChunk({chunkPos.x + 1, chunkPos.y, chunkPos.z}); // East

    computeNeighborSkyTops();

    // Everything above the highest non-air block is air, so the face passes stop there.
    {
        const uint8_t *raw = chunkData->data;
        int top = 0;
        for (int column = 0; column < static_cast<int>(CHUNK_WIDTH * CHUNK_WIDTH); ++column) {
            const uint8_t *col = raw + column * CHUNK_HEIGHT;
            for (int y = CHUNK_HEIGHT - 1; y >= top; --y) {
                if (col[y] != 0) {
                    top = y + 1;
                    break;
                }
            }
        }
        meshTopY = top;
    }

    // Build into this thread's scratch buffers; they keep their capacity between meshes.
    thread_local std::vector<WorldVertex> scratchWorldVertices[NUM_SUBCHUNKS];
    thread_local std::vector<unsigned int> scratchWorldIndices[NUM_SUBCHUNKS];
    thread_local std::vector<WorldVertex> scratchCutoutVertices[NUM_SUBCHUNKS];
    thread_local std::vector<unsigned int> scratchCutoutIndices[NUM_SUBCHUNKS];
    thread_local std::vector<FluidVertex> scratchLiquidVertices[NUM_SUBCHUNKS];
    thread_local std::vector<unsigned int> scratchLiquidIndices[NUM_SUBCHUNKS];
    thread_local std::vector<BillboardVertex> scratchBillboardVertices[NUM_SUBCHUNKS];
    thread_local std::vector<unsigned int> scratchBillboardIndices[NUM_SUBCHUNKS];
    worldVertices = scratchWorldVertices;
    worldIndices = scratchWorldIndices;
    cutoutVertices = scratchCutoutVertices;
    cutoutIndices = scratchCutoutIndices;
    liquidVertices = scratchLiquidVertices;
    liquidIndices = scratchLiquidIndices;
    billboardVertices = scratchBillboardVertices;
    billboardIndices = scratchBillboardIndices;
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        worldVertices[i].clear();
        worldIndices[i].clear();
        cutoutVertices[i].clear();
        cutoutIndices[i].clear();
        liquidVertices[i].clear();
        liquidIndices[i].clear();
        billboardVertices[i].clear();
        billboardIndices[i].clear();
    }

    // Per-subchunk vertex counters
    unsigned int currentVertex[NUM_SUBCHUNKS] = {};
    unsigned int currentLiquidVertex[NUM_SUBCHUNKS] = {};
    unsigned int currentBillboardVertex[NUM_SUBCHUNKS] = {};

    // Combine flat surfaces to save time.
    generateHorizontalFaces(currentVertex);

    // Combine vertical surfaces to save time.
    generateVerticalFaces(currentVertex);

    // Generate non-solid blocks (fluids, billboards, transparent)
    generateSpecialBlocks(currentVertex, currentLiquidVertex, currentBillboardVertex);

    mergeMeshes();
    worldVertices = nullptr;
    worldIndices = nullptr;
    cutoutVertices = nullptr;
    cutoutIndices = nullptr;
    liquidVertices = nullptr;
    liquidIndices = nullptr;
    billboardVertices = nullptr;
    billboardIndices = nullptr;
    generated = true;
}

void Chunk::emitQuad(const GreedyQuad &quad, const int subChunkIndex) {
    const bool cutout = meshBlockTable().cutout[quad.blockId];
    auto &vertices = cutout ? cutoutVertices[subChunkIndex] : worldVertices[subChunkIndex];
    auto &indices = cutout ? cutoutIndices[subChunkIndex] : worldIndices[subChunkIndex];
    // Indices are relative to the buffer the quad lands in, so the base is that buffer's size.
    unsigned int base = static_cast<unsigned int>(vertices.size());
    emitGreedyQuad(quad, worldPos, vertices, indices, base);
}

void Chunk::computeNeighborSkyTops() {
    const BlockRegistry &registry = BlockRegistry::getInstance();
    const ChunkData *sides[4] = {northData.get(), southData.get(), westData.get(), eastData.get()};
    for (int side = 0; side < 4; ++side) {
        const ChunkData *data = sides[side];
        for (int a = 0; a < static_cast<int>(CHUNK_WIDTH); ++a) {
            int top = -1;
            if (data) {
                // The neighbour's border column that touches this chunk.
                const int x = side == 0 || side == 1 ? a : (side == 2 ? CHUNK_WIDTH - 1 : 0);
                const int z = side == 2 || side == 3 ? a : (side == 0 ? CHUNK_WIDTH - 1 : 0);
                for (int y = CHUNK_HEIGHT - 1; y >= 0; --y) {
                    const auto type = registry.getBlock(static_cast<uint8_t>(data->getBlock(x, y, z))).blockType;
                    if (type == Block::SOLID || type == Block::LEAVES || type == Block::LIQUID) {
                        top = y;
                        break;
                    }
                }
            }
            neighborSkyTop[side][a] = static_cast<int16_t>(top);
        }
    }
}

uint8_t Chunk::neighborLight(const int side, const int a, const int y) const {
    if (const Chunk *neighbor = neighborChunks[side]) {
        const int x = side == 0 || side == 1 ? a : (side == 2 ? CHUNK_WIDTH - 1 : 0);
        const int z = side == 2 || side == 3 ? a : (side == 0 ? CHUNK_WIDTH - 1 : 0);
        return neighbor->getLightLevel(x, y, z);
    }
    // Same rule as the sky pass in computeLightMap: lit (15) down to and including the highest blocker.
    return y >= neighborSkyTop[side][a] ? static_cast<uint8_t>(15 << 4) : 0;
}

void Chunk::generateHorizontalFaces(unsigned int* currentVertex) {
    // Mask arrays for greedy meshing (reused per slice)
    thread_local uint16_t topMask[CHUNK_WIDTH][CHUNK_WIDTH];
    thread_local uint16_t bottomMask[CHUNK_WIDTH][CHUNK_WIDTH];
    thread_local bool processed[CHUNK_WIDTH][CHUNK_WIDTH];
    
    // Thread-safe capture
    auto localData = chunkData;
    if (!localData) return;

    const uint8_t* rawData = localData->data;
    const int strideZ = CHUNK_HEIGHT;
    const int strideX = CHUNK_WIDTH * CHUNK_HEIGHT;

    const MeshBlockTable &table = meshBlockTable();

    // Helper to build masks for a single Y slice
    auto buildMasksForY = [&](int y, bool isYMin, bool isYMax) {
         // Clear masks
         memset(topMask, 0, sizeof(topMask));
         memset(bottomMask, 0, sizeof(bottomMask));
         memset(processed, 0, sizeof(processed));

         for (int x = 0; x < CHUNK_WIDTH; x++) {
             int xOffset = x * strideX;
             for (int z = 0; z < CHUNK_WIDTH; z++) {
                 int idx = xOffset + z * strideZ + y;
                 uint8_t blockId = rawData[idx];

                 if (!table.greedy[blockId]) continue;

                 // Check TOP face
                 uint8_t topBlockId;
                 if (!isYMax) {
                     topBlockId = rawData[idx + 1];
                 } else {
                     topBlockId = upData ? upData->getBlock(x, 0, z) : Blocks::AIR;
                 }
                 
                 if (table.faceVisible(blockId, topBlockId)) topMask[x][z] = blockId;

                 // Check BOTTOM face
                 uint8_t bottomBlockId;
                 if (!isYMin) {
                     bottomBlockId = rawData[idx - 1];
                 } else {
                     bottomBlockId = downData ? downData->getBlock(x, CHUNK_HEIGHT - 1, z) : Blocks::AIR;
                 }

                 if (table.faceVisible(blockId, bottomBlockId)) bottomMask[x][z] = blockId;
             }
         }
    };

    for (int y = 0; y < meshTopY; y++) {
        int subChunkIndex = y / SUBCHUNK_HEIGHT;

        bool isYMin = (y == 0);
        bool isYMax = (y == CHUNK_HEIGHT - 1);

        // Build masks with optimized access
        buildMasksForY(y, isYMin, isYMax);

        // Greedy merge TOP faces
        greedyMergeTopFaces(y, subChunkIndex, topMask, processed, currentVertex);

        // Greedy merge BOTTOM faces
        greedyMergeBottomFaces(y, subChunkIndex, bottomMask, processed, currentVertex);
    }
}

void Chunk::greedyMergeTopFaces(int y, int subChunkIndex, uint16_t topMask[][CHUNK_WIDTH],
                                bool processed[][CHUNK_WIDTH], unsigned int* currentVertex) {
    memset(processed, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_WIDTH);

    for (int x = 0; x < CHUNK_WIDTH; x++) {
        for (int z = 0; z < CHUNK_WIDTH; z++) {
            if (topMask[x][z] == 0 || processed[x][z]) continue;

            uint16_t blockId = topMask[x][z];
            int width = 1, height = 1;

            // Extend in X direction
            while (x + width < CHUNK_WIDTH &&
                   topMask[x + width][z] == blockId &&
                   !processed[x + width][z]) {
                width++;
            }

            // Extend in Z direction
            bool canExtend = true;
            while (z + height < CHUNK_WIDTH && canExtend) {
                for (int dx = 0; dx < width; dx++) {
                    if (topMask[x + dx][z + height] != blockId ||
                        processed[x + dx][z + height]) {
                        canExtend = false;
                        break;
                    }
                }
                if (canExtend) height++;
            }

            // Mark as processed
            for (int dx = 0; dx < width; dx++) {
                for (int dz = 0; dz < height; dz++) {
                    processed[x + dx][z + dz] = true;
                }
            }

            // Emit merged quad
            // Get light level from adjacent air block (above this face)
            uint8_t light = (y + 1 < CHUNK_HEIGHT) ? lightMap[x * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT + (y + 1)] : 0;
            GreedyQuad quad{x, y, z, width, height, blockId, TOP, light};
            emitQuad(quad, subChunkIndex);
        }
    }
}

void Chunk::greedyMergeBottomFaces(int y, int subChunkIndex, uint16_t bottomMask[][CHUNK_WIDTH],
                                   bool processed[][CHUNK_WIDTH], unsigned int* currentVertex) {
    memset(processed, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_WIDTH);

    for (int x = 0; x < CHUNK_WIDTH; x++) {
        for (int z = 0; z < CHUNK_WIDTH; z++) {
            if (bottomMask[x][z] == 0 || processed[x][z]) continue;

            uint16_t blockId = bottomMask[x][z];
            int width = 1, height = 1;

            while (x + width < CHUNK_WIDTH &&
                   bottomMask[x + width][z] == blockId &&
                   !processed[x + width][z]) {
                width++;
            }

            bool canExtend = true;
            while (z + height < CHUNK_WIDTH && canExtend) {
                for (int dx = 0; dx < width; dx++) {
                    if (bottomMask[x + dx][z + height] != blockId ||
                        processed[x + dx][z + height]) {
                        canExtend = false;
                        break;
                    }
                }
                if (canExtend) height++;
            }

            for (int dx = 0; dx < width; dx++) {
                for (int dz = 0; dz < height; dz++) {
                    processed[x + dx][z + dz] = true;
                }
            }

            uint8_t light = (y - 1 >= 0) ? lightMap[x * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT + (y - 1)] : 0;
            GreedyQuad quad{x, y, z, width, height, blockId, BOTTOM, light};
            emitQuad(quad, subChunkIndex);
        }
    }
}

void Chunk::generateVerticalFaces(unsigned int* currentVertex) {
    static thread_local uint16_t maskA[CHUNK_WIDTH][CHUNK_HEIGHT];
    static thread_local uint16_t maskB[CHUNK_WIDTH][CHUNK_HEIGHT];
    static thread_local bool processedSide[CHUNK_WIDTH][CHUNK_HEIGHT];

    // Z-AXIS PASS (North/South Faces)
    generateZAxisFaces(currentVertex, maskA, maskB, processedSide);

    // X-AXIS PASS (West/East Faces)
    generateXAxisFaces(currentVertex, maskA, maskB, processedSide);
}

void Chunk::generateZAxisFaces(unsigned int* currentVertex, uint16_t maskA[][CHUNK_HEIGHT],
                               uint16_t maskB[][CHUNK_HEIGHT], bool processedSide[][CHUNK_HEIGHT]) {
    NeighborData neighbor{};
    
    auto localData = chunkData;
    if (!localData) return;

    const MeshBlockTable &table = meshBlockTable();

    // Direct pointer access setup
    const uint8_t* rawData = localData->data;
    const int strideZ = CHUNK_HEIGHT; // Stride for Z is just CHUNK_HEIGHT
    const int strideX = CHUNK_WIDTH * CHUNK_HEIGHT;

    // Helper lambda for boundary/complex cases
    auto handleBoundaryZ = [&](int z) {
        memset(maskA, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(maskB, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);

        for (int x = 0; x < CHUNK_WIDTH; x++) {
            for (int y = 0; y < meshTopY; y++) {
                uint16_t blockId = localData->getBlock(x, y, z);
                if (blockId == 0) continue;
                const Block* block = &BlockRegistry::getInstance().getBlock(blockId);
                if (block->blockType != Block::SOLID && block->blockType != Block::LEAVES) continue;

                fetchNorthNeighbor(neighbor, x, y, z, localData, northData, upData);
                if (shouldRenderFace(neighbor, false)) maskA[x][y] = blockId;

                fetchSouthNeighbor(neighbor, x, y, z, localData, southData, upData);
                if (shouldRenderFace(neighbor, false)) maskB[x][y] = blockId;
            }
        }
        
        greedyMergeNorthFaces(z, maskA, processedSide, currentVertex);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);
        greedyMergeSouthFaces(z, maskB, processedSide, currentVertex);
    };

    // 1. Handle North Boundary (Z=0)
    handleBoundaryZ(0);

    // 2. Handle Internal Slices (Z=1 to 30)
    for (int z = 1; z < CHUNK_WIDTH - 1; z++) {
        memset(maskA, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(maskB, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);

        // Calculate base offset for this Z plane
        // idx = x * strideX + z * strideZ + y
        // We will iterate x, then update base pointer
        int zOffset = z * strideZ;

        for (int x = 0; x < CHUNK_WIDTH; x++) {
            int xOffset = x * strideX;
            int currentConstOffset = xOffset + zOffset; // Base offset for (x, *, z)

            for (int y = 0; y < meshTopY; y++) {
                int idx = currentConstOffset + y;
                uint8_t blockId = rawData[idx];

                if (!table.greedy[blockId]) continue;

                // --- North Neighbor (Z-1) ---
                // Internal Z means Z-1 is in this chunk.
                // Neighbor index = idx - strideZ
                // Neighbor Top index = idx - strideZ + 1 (if y < MAX)
                {
                    const int nIdx = idx - strideZ;
                    if (table.faceVisible(blockId, rawData[nIdx])) maskA[x][y] = blockId;
                }

                // --- South Neighbor (Z+1) ---
                // Internal Z means Z+1 is in this chunk.
                // Neighbor index = idx + strideZ
                {
                    int sIdx = idx + strideZ;
                    if (table.faceVisible(blockId, rawData[sIdx])) maskB[x][y] = blockId;
                }
            }
        }

        greedyMergeNorthFaces(z, maskA, processedSide, currentVertex);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);
        greedyMergeSouthFaces(z, maskB, processedSide, currentVertex);
    }

    // 3. Handle South Boundary (Z=31)
    handleBoundaryZ(CHUNK_WIDTH - 1);
}

void Chunk::generateXAxisFaces(unsigned int* currentVertex, uint16_t maskA[][CHUNK_HEIGHT],
                               uint16_t maskB[][CHUNK_HEIGHT], bool processedSide[][CHUNK_HEIGHT]) {
    NeighborData neighbor{};
    
    auto localData = chunkData;
    if (!localData) return;

    const MeshBlockTable &table = meshBlockTable();

    // Direct pointer access setup
    const uint8_t* rawData = localData->data;
    const int strideZ = CHUNK_HEIGHT;
    const int strideX = CHUNK_WIDTH * CHUNK_HEIGHT;

    auto handleBoundaryX = [&](int x) {
        memset(maskA, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(maskB, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);

        for (int z = 0; z < CHUNK_WIDTH; z++) {
            for (int y = 0; y < meshTopY; y++) {
                uint16_t blockId = localData->getBlock(x, y, z);
                if (blockId == 0) continue;
                const Block* block = &BlockRegistry::getInstance().getBlock(blockId);
                if (block->blockType != Block::SOLID && block->blockType != Block::LEAVES) continue;

                fetchWestNeighbor(neighbor, x, y, z, localData, westData, upData);
                if (shouldRenderFace(neighbor, false)) maskA[z][y] = blockId;

                fetchEastNeighbor(neighbor, x, y, z, localData, eastData, upData);
                if (shouldRenderFace(neighbor, false)) maskB[z][y] = blockId;
            }
        }
        
        greedyMergeWestFaces(x, maskA, processedSide, currentVertex);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);
        greedyMergeEastFaces(x, maskB, processedSide, currentVertex);
    };

    // 1. West Boundary (X=0)
    handleBoundaryX(0);

    // 2. Internal Slices (X=1 to 30)
    for (int x = 1; x < CHUNK_WIDTH - 1; x++) {
        memset(maskA, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(maskB, 0, sizeof(uint16_t) * CHUNK_WIDTH * CHUNK_HEIGHT);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);
        
        int xOffset = x * strideX;

        for (int z = 0; z < CHUNK_WIDTH; z++) {
            int currentConstOffset = xOffset + z * strideZ;

            for (int y = 0; y < meshTopY; y++) {
                int idx = currentConstOffset + y;
                uint8_t blockId = rawData[idx];

                if (!table.greedy[blockId]) continue;

                // --- West Neighbor (X-1) ---
                // Neighbor index = idx - strideX
                {
                    int wIdx = idx - strideX;
                    if (table.faceVisible(blockId, rawData[wIdx])) maskA[z][y] = blockId;
                }

                // --- East Neighbor (X+1) ---
                // Neighbor index = idx + strideX
                {
                    int eIdx = idx + strideX;
                    if (table.faceVisible(blockId, rawData[eIdx])) maskB[z][y] = blockId;
                }
            }
        }

        greedyMergeWestFaces(x, maskA, processedSide, currentVertex);
        memset(processedSide, 0, sizeof(bool) * CHUNK_WIDTH * CHUNK_HEIGHT);
        greedyMergeEastFaces(x, maskB, processedSide, currentVertex);
    }

    // 3. East Boundary (X=31)
    handleBoundaryX(CHUNK_WIDTH - 1);
}

void Chunk::greedyMergeNorthFaces(int z, uint16_t maskA[][CHUNK_HEIGHT],
                                  bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex) {
    for (int x = 0; x < CHUNK_WIDTH; x++) {
        for (int y = 0; y < meshTopY; y++) {
            if (maskA[x][y] == 0 || processedSide[x][y]) continue;

            uint16_t id = maskA[x][y];
            int width = 1, height = 1;

            while (x + width < CHUNK_WIDTH && maskA[x + width][y] == id && !processedSide[x + width][y]) width++;

            bool canExtend = true;
            while (y + height < meshTopY && canExtend) {
                for (int dx = 0; dx < width; dx++) {
                    if (maskA[x + dx][y + height] != id || processedSide[x + dx][y + height]) {
                        canExtend = false; break;
                    }
                }
                if (canExtend) height++;
            }

            for (int dx = 0; dx < width; dx++)
                for (int dy = 0; dy < height; dy++)
                    processedSide[x + dx][y + dy] = true;

            int subIdx = y / SUBCHUNK_HEIGHT;
            if (subIdx >= NUM_SUBCHUNKS) subIdx = NUM_SUBCHUNKS - 1;
            
            uint8_t light = 0;
            if (z - 1 >= 0) {
                 light = lightMap[x * CHUNK_WIDTH * CHUNK_HEIGHT + (z - 1) * CHUNK_HEIGHT + y];
            } else {
                 light = neighborLight(0, x, y);
            }
            
            GreedyQuad quad{x, y, z, width, height, id, NORTH, light};
            emitQuad(quad, subIdx);
        }
    }
}

void Chunk::greedyMergeSouthFaces(int z, uint16_t maskB[][CHUNK_HEIGHT],
                                  bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex) {
    for (int x = 0; x < CHUNK_WIDTH; x++) {
        for (int y = 0; y < meshTopY; y++) {
            if (maskB[x][y] == 0 || processedSide[x][y]) continue;

            uint16_t id = maskB[x][y];
            int width = 1, height = 1;
            while (x + width < CHUNK_WIDTH && maskB[x + width][y] == id && !processedSide[x + width][y]) width++;
            bool canExtend = true;
            while (y + height < meshTopY && canExtend) {
                for (int dx = 0; dx < width; dx++) {
                    if (maskB[x + dx][y + height] != id || processedSide[x + dx][y + height]) {
                        canExtend = false; break;
                    }
                }
                if (canExtend) height++;
            }
            for (int dx = 0; dx < width; dx++)
                for (int dy = 0; dy < height; dy++)
                    processedSide[x + dx][y + dy] = true;

            int subIdx = y / SUBCHUNK_HEIGHT;
            if (subIdx >= NUM_SUBCHUNKS) subIdx = NUM_SUBCHUNKS - 1;
            
            uint8_t light = 0;
            if (z + 1 < (int)CHUNK_WIDTH) {
                 light = lightMap[x * CHUNK_WIDTH * CHUNK_HEIGHT + (z + 1) * CHUNK_HEIGHT + y];
            } else {
                 light = neighborLight(1, x, y);
            }

            GreedyQuad quad{x, y, z, width, height, id, SOUTH, light};
            emitQuad(quad, subIdx);
        }
    }
}

void Chunk::greedyMergeWestFaces(int x, uint16_t maskA[][CHUNK_HEIGHT],
                                 bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex) {
    for (int z = 0; z < CHUNK_WIDTH; z++) {
        for (int y = 0; y < meshTopY; y++) {
            if (maskA[z][y] == 0 || processedSide[z][y]) continue;
            uint16_t id = maskA[z][y];
            int width = 1, height = 1;
            while (z + width < CHUNK_WIDTH && maskA[z + width][y] == id && !processedSide[z + width][y]) width++;
            bool canExtend = true;
            while (y + height < meshTopY && canExtend) {
                for (int dz = 0; dz < width; dz++) {
                    if (maskA[z + dz][y + height] != id || processedSide[z + dz][y + height]) {
                        canExtend = false; break;
                    }
                }
                if (canExtend) height++;
            }
            for (int dz = 0; dz < width; dz++)
                for (int dy = 0; dy < height; dy++)
                    processedSide[z + dz][y + dy] = true;

            int subIdx = y / SUBCHUNK_HEIGHT;
            if (subIdx >= NUM_SUBCHUNKS) subIdx = NUM_SUBCHUNKS - 1;
            
            uint8_t light = 0;
            if (x - 1 >= 0) {
                 light = lightMap[(x - 1) * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT + y];
            } else {
                 light = neighborLight(2, z, y);
            }
            
            GreedyQuad quad{x, y, z, width, height, id, WEST, light};
            emitQuad(quad, subIdx);
        }
    }
}

void Chunk::greedyMergeEastFaces(int x, uint16_t maskB[][CHUNK_HEIGHT],
                                 bool processedSide[][CHUNK_HEIGHT], unsigned int* currentVertex) {
    for (int z = 0; z < CHUNK_WIDTH; z++) {
        for (int y = 0; y < meshTopY; y++) {
            if (maskB[z][y] == 0 || processedSide[z][y]) continue;
            uint16_t id = maskB[z][y];
            int width = 1, height = 1;
            while (z + width < CHUNK_WIDTH && maskB[z + width][y] == id && !processedSide[z + width][y]) width++;
            bool canExtend = true;
            while (y + height < meshTopY && canExtend) {
                for (int dz = 0; dz < width; dz++) {
                    if (maskB[z + dz][y + height] != id || processedSide[z + dz][y + height]) {
                        canExtend = false; break;
                    }
                }
                if (canExtend) height++;
            }
            for (int dz = 0; dz < width; dz++)
                for (int dy = 0; dy < height; dy++)
                    processedSide[z + dz][y + dy] = true;

            int subIdx = y / SUBCHUNK_HEIGHT;
            if (subIdx >= NUM_SUBCHUNKS) subIdx = NUM_SUBCHUNKS - 1;
            
            uint8_t light = 0;
            if (x + 1 < (int)CHUNK_WIDTH) {
                 light = lightMap[(x + 1) * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT + y];
            } else {
                 light = neighborLight(3, z, y);
            }
            
            GreedyQuad quad{x, y, z, width, height, id, EAST, light};
            emitQuad(quad, subIdx);
        }
    }
}

void Chunk::generateSpecialBlocks(unsigned int* currentVertex, unsigned int* currentLiquidVertex,
                                  unsigned int* currentBillboardVertex) {
    NeighborData neighbor{};
    
    auto localData = chunkData;
    if (!localData) return;

    const uint8_t* rawData = localData->data;
    const int strideZ = CHUNK_HEIGHT;
    const int strideX = CHUNK_WIDTH * CHUNK_HEIGHT;

    const MeshBlockTable &table = meshBlockTable();

    auto processSpecialBlock = [&](int x, int y, int z, int idx) {
        uint8_t blockId = rawData[idx];
        if (blockId == 0 || table.greedy[blockId]) return;

        int subChunkIndex = y / SUBCHUNK_HEIGHT;
        const Block *block = &BlockRegistry::getInstance().getBlock(blockId);

        // Cache top block data
        // Determine top block ID efficiently
        uint8_t topBlockId;
        if (y < CHUNK_HEIGHT - 1) {
            topBlockId = rawData[idx + 1];
        } else {
            topBlockId = upData ? upData->getBlock(x, 0, z) : Blocks::AIR;
        }
        
        const Block *topBlockType = &BlockRegistry::getInstance().getBlock(topBlockId);
        char waterTopValue = (topBlockType->blockType == Block::TRANSPARENT ||
                              topBlockType->blockType == Block::SOLID)
                                 ? 1
                                 : 0;

        // Handle billboards
        if (block->blockType == Block::BILLBOARD) {
            generateBillboardFaces(x, y, z, block, currentBillboardVertex[subChunkIndex], subChunkIndex);
            return;
        }

        // Skip Solids/Leaves (handled by greedy meshing)
        if (block->blockType == Block::SOLID || block->blockType == Block::LEAVES) {
            return;
        }

        bool isBlockLiquid = (block->blockType == Block::LIQUID);

        // --- Neighbor Checks ---
        
        // Helper inline for neighbor
        auto checkNeighbor = [&](uint8_t nBlockId, uint8_t nTopBlockId) {
             neighbor.init(nBlockId, nTopBlockId);
        };
        
        // 1. NORTH (Z-1)
        if (z > 0) { // Internal Z
             int nIdx = idx - strideZ;
             uint8_t nBlock = rawData[nIdx];
             uint8_t nTop = (y < CHUNK_HEIGHT - 1) ? rawData[nIdx + 1] : (upData ? upData->getBlock(x, 0, z-1) : Blocks::AIR);
             checkNeighbor(nBlock, nTop);
        } else { // Boundary Z
             fetchNorthNeighbor(neighbor, x, y, z, localData, northData, upData);
        }
        
        // Helper to get light at neighbor
        auto getNeighborLight = [&](int nx, int ny, int nz, int nDir) -> uint8_t {
             if (nx < 0) {
                 return neighborLight(2, nz, ny);
             }
             if (nx >= CHUNK_WIDTH) {
                 return neighborLight(3, nz, ny);
             }
             if (nz < 0) {
                 return neighborLight(0, nx, ny);
             }
             if (nz >= CHUNK_WIDTH) {
                 return neighborLight(1, nx, ny);
             }
             if (ny < 0 || ny >= CHUNK_HEIGHT) return 0; // Or Skylight 15 if > max?
             return lightMap[nx * CHUNK_WIDTH * CHUNK_HEIGHT + nz * CHUNK_HEIGHT + ny];
        };

        if (shouldRenderFace(neighbor, isBlockLiquid)) {
            if (isBlockLiquid) {
                uint8_t l = getNeighborLight(x, y, z - 1, 0); // North
                generateLiquidFaces(x, y, z, NORTH, block, currentLiquidVertex[subChunkIndex], waterTopValue, l, subChunkIndex);
            } else {
                generateWorldFaces(x, y, z, NORTH, block, currentVertex[subChunkIndex], subChunkIndex);
            }
        }

        // 2. SOUTH (Z+1)
        if (z < CHUNK_WIDTH - 1) {
             int sIdx = idx + strideZ;
             uint8_t sBlock = rawData[sIdx];
             uint8_t sTop = (y < CHUNK_HEIGHT - 1) ? rawData[sIdx + 1] : (upData ? upData->getBlock(x, 0, z+1) : Blocks::AIR);
             checkNeighbor(sBlock, sTop);
        } else {
             fetchSouthNeighbor(neighbor, x, y, z, localData, southData, upData);
        }

        if (shouldRenderFace(neighbor, isBlockLiquid)) {
            if (isBlockLiquid) {
                uint8_t l = getNeighborLight(x, y, z + 1, 1); // South
                generateLiquidFaces(x, y, z, SOUTH, block, currentLiquidVertex[subChunkIndex], waterTopValue, l, subChunkIndex);
            } else {
                generateWorldFaces(x, y, z, SOUTH, block, currentVertex[subChunkIndex], subChunkIndex);
            }
        }

        // 3. WEST (X-1)
        if (x > 0) {
             int wIdx = idx - strideX;
             uint8_t wBlock = rawData[wIdx];
             uint8_t wTop = (y < CHUNK_HEIGHT - 1) ? rawData[wIdx + 1] : (upData ? upData->getBlock(x-1, 0, z) : Blocks::AIR);
             checkNeighbor(wBlock, wTop);
        } else {
             fetchWestNeighbor(neighbor, x, y, z, localData, westData, upData);
        }

        if (shouldRenderFace(neighbor, isBlockLiquid)) {
            if (isBlockLiquid) {
                uint8_t l = getNeighborLight(x - 1, y, z, 2); // West
                generateLiquidFaces(x, y, z, WEST, block, currentLiquidVertex[subChunkIndex], waterTopValue, l, subChunkIndex);
            } else {
                generateWorldFaces(x, y, z, WEST, block, currentVertex[subChunkIndex], subChunkIndex);
            }
        }

        // 4. EAST (X+1)
        if (x < CHUNK_WIDTH - 1) {
             int eIdx = idx + strideX;
             uint8_t eBlock = rawData[eIdx];
             uint8_t eTop = (y < CHUNK_HEIGHT - 1) ? rawData[eIdx + 1] : (upData ? upData->getBlock(x+1, 0, z) : Blocks::AIR);
             checkNeighbor(eBlock, eTop);
        } else {
             fetchEastNeighbor(neighbor, x, y, z, localData, eastData, upData);
        }

        if (shouldRenderFace(neighbor, isBlockLiquid)) {
            if (isBlockLiquid) {
                uint8_t l = getNeighborLight(x + 1, y, z, 3); // East
                generateLiquidFaces(x, y, z, EAST, block, currentLiquidVertex[subChunkIndex], waterTopValue, l, subChunkIndex);
            } else {
                generateWorldFaces(x, y, z, EAST, block, currentVertex[subChunkIndex], subChunkIndex);
            }
        }

        // Liquid TOP faces (not greedy meshed)
        if (isBlockLiquid && topBlockType->blockType != Block::LIQUID) {
            uint8_t l = getNeighborLight(x, y + 1, z, 4); // Top (Up)
            generateLiquidFaces(x, y, z, TOP, block, currentLiquidVertex[subChunkIndex], waterTopValue, l, subChunkIndex);
        }

        // Liquid BOTTOM faces
        if (isBlockLiquid) {
            uint8_t bottomBlockId;
            if (y > 0) {
                bottomBlockId = rawData[idx - 1];
            } else {
                bottomBlockId = downData ? downData->getBlock(x, CHUNK_HEIGHT - 1, z) : Blocks::AIR;
            }
            
            const Block *bottomBlockType = &BlockRegistry::getInstance().getBlock(bottomBlockId);
            if (bottomBlockType->blockType != Block::LIQUID && bottomBlockType->blockType != Block::SOLID) {
                uint8_t l = getNeighborLight(x, y - 1, z, 5); // Bottom (Down)
                generateLiquidFaces(x, y, z, BOTTOM, block, currentLiquidVertex[subChunkIndex], waterTopValue, l, subChunkIndex);
            }
        }
    };

    for (int x = 0; x < CHUNK_WIDTH; x++) {
        int xOffset = x * strideX;
        for (int z = 0; z < CHUNK_WIDTH; z++) {
            int zOffset = z * strideZ;
            int baseIdx = xOffset + zOffset;
            
            for (int y = 0; y < meshTopY; y++) {
                processSpecialBlock(x, y, z, baseIdx + y);
            }
        }
    }
}