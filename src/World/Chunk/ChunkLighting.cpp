#include "Chunk.h"
#include "Blocks.h"
#include "BlockRegistry.h"
#include "Planet.h"
#include <array>
#include <queue>
#include <cstring>

// Spreads light from glowing blocks and from the sky.
// This uses a Flood Fill / BFS approach common in voxel games.
// Reference: https://gist.github.com/JuanDiegoMontoya/383bf2cc2ad3d1b8717a42b1f0bc6a7b
void Chunk::computeLightMap() {
    // Neighbours read this chunk's light map from other threads, so the new map is built in
    // scratch space and published with one copy at the end. The local name shadows the member.
    thread_local uint8_t scratch[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH];
    uint8_t (&lightMap)[CHUNK_WIDTH * CHUNK_HEIGHT * CHUNK_WIDTH] = scratch;
    memset(lightMap, 0, sizeof(lightMap));

    if (!chunkData) {
        memcpy(this->lightMap, lightMap, sizeof(lightMap));
        for (auto &flag : borderBlockLight) flag = false;
        return;
    }

    const BlockRegistry& registry = BlockRegistry::getInstance();
    // Solids, leaves and liquids stop sunlight; built once since block types never change.
    static const auto blocksSky = [] {
        std::array<bool, 256> table{};
        for (const Block &b : BlockRegistry::getInstance().getAllBlocks()) {
            table[b.id] = b.blockType == Block::SOLID || b.blockType == Block::LEAVES || b.blockType == Block::LIQUID;
        }
        return table;
    }();
    const int CW = CHUNK_WIDTH;
    const int CH = CHUNK_HEIGHT;
    const int strideX = CW * CH;
    const int strideZ = CH;

    // --- PASS 1: BLOCK LIGHT (Lower 4 bits) ---
    struct LightNode {
        int16_t x, y, z;
        uint8_t level;
    };
    std::queue<LightNode> lightQueue;

    // Seed: find all lava blocks
    // Seed: find all lava blocks
    for (int x = 0; x < CW; x++) {
        for (int z = 0; z < CW; z++) {
            for (int y = 0; y < CH; y++) {
                int idx = x * strideX + z * strideZ + y;
                uint8_t blockId = chunkData->data[idx];
                
                if (blockId == Blocks::LAVA) {
                    // Set Block Light to 15 (preserve existing upper bits, though here it's 0)
                    lightMap[idx] = (lightMap[idx] & 0xF0) | 15;
                    lightQueue.push({(int16_t)x, (int16_t)y, (int16_t)z, 15});
                }
            }
        }
    }

    // Seed from Neighbor Chunks (Propagate incoming light)
    Chunk* nChunk = Planet::planet->getChunk({chunkPos.x, chunkPos.y, chunkPos.z - 1});
    Chunk* sChunk = Planet::planet->getChunk({chunkPos.x, chunkPos.y, chunkPos.z + 1});
    Chunk* wChunk = Planet::planet->getChunk({chunkPos.x - 1, chunkPos.y, chunkPos.z});
    Chunk* eChunk = Planet::planet->getChunk({chunkPos.x + 1, chunkPos.y, chunkPos.z});

    // North (My Z=0 touches Neighbor Z=CW-1)
    if (nChunk && nChunk->generated) {
        for (int x = 0; x < CW; x++) {
            for (int y = 0; y < CH; y++) {
                uint8_t nLight = nChunk->getLightLevel(x, y, CW - 1) & 0x0F;
                if (nLight > 1) {
                    uint8_t localLight = lightMap[x * strideX + 0 * strideZ + y] & 0x0F;
                    if (nLight - 1 > localLight) {
                        lightMap[x * strideX + 0 * strideZ + y] = (lightMap[x * strideX + 0 * strideZ + y] & 0xF0) | (nLight - 1);
                        lightQueue.push({(int16_t)x, (int16_t)y, (int16_t)0, (uint8_t)(nLight - 1)});
                    }
                }
            }
        }
    }

    // South (My Z=CW-1 touches Neighbor Z=0)
    if (sChunk && sChunk->generated) {
        for (int x = 0; x < CW; x++) {
            for (int y = 0; y < CH; y++) {
                uint8_t nLight = sChunk->getLightLevel(x, y, 0) & 0x0F;
                if (nLight > 1) {
                    int z = CW - 1;
                    uint8_t localLight = lightMap[x * strideX + z * strideZ + y] & 0x0F;
                    if (nLight - 1 > localLight) {
                        lightMap[x * strideX + z * strideZ + y] = (lightMap[x * strideX + z * strideZ + y] & 0xF0) | (nLight - 1);
                        lightQueue.push({(int16_t)x, (int16_t)y, (int16_t)z, (uint8_t)(nLight - 1)});
                    }
                }
            }
        }
    }

    // West (My X=0 touches Neighbor X=CW-1)
    if (wChunk && wChunk->generated) {
        for (int z = 0; z < CW; z++) {
            for (int y = 0; y < CH; y++) {
                uint8_t nLight = wChunk->getLightLevel(CW - 1, y, z) & 0x0F;
                if (nLight > 1) {
                    uint8_t localLight = lightMap[0 * strideX + z * strideZ + y] & 0x0F;
                    if (nLight - 1 > localLight) {
                        lightMap[0 * strideX + z * strideZ + y] = (lightMap[0 * strideX + z * strideZ + y] & 0xF0) | (nLight - 1);
                        lightQueue.push({(int16_t)0, (int16_t)y, (int16_t)z, (uint8_t)(nLight - 1)});
                    }
                }
            }
        }
    }

    // East (My X=CW-1 touches Neighbor X=0)
    if (eChunk && eChunk->generated) {
        for (int z = 0; z < CW; z++) {
            for (int y = 0; y < CH; y++) {
                uint8_t nLight = eChunk->getLightLevel(0, y, z) & 0x0F;
                if (nLight > 1) {
                    int x = CW - 1;
                    uint8_t localLight = lightMap[x * strideX + z * strideZ + y] & 0x0F;
                    if (nLight - 1 > localLight) {
                        lightMap[x * strideX + z * strideZ + y] = (lightMap[x * strideX + z * strideZ + y] & 0xF0) | (nLight - 1);
                        lightQueue.push({(int16_t)x, (int16_t)y, (int16_t)z, (uint8_t)(nLight - 1)});
                    }
                }
            }
        }
    }

    const int dx[] = {1, -1, 0, 0, 0, 0};
    const int dy[] = {0, 0, 1, -1, 0, 0};
    const int dz[] = {0, 0, 0, 0, 1, -1};

    while (!lightQueue.empty()) {
        LightNode node = lightQueue.front();
        lightQueue.pop();

        if (node.level <= 1) continue;
        uint8_t newLevel = node.level - 1;

        for (int d = 0; d < 6; d++) {
            int nx = node.x + dx[d];
            int ny = node.y + dy[d];
            int nz = node.z + dz[d];

            if (nx < 0 || nx >= CW || ny < 0 || ny >= CH || nz < 0 || nz >= CW) continue;

            int nIdx = nx * strideX + nz * strideZ + ny;
            
            // Check existing Block Light level (lower 4 bits)
            uint8_t currentBlockLight = lightMap[nIdx] & 0x0F;
            if (currentBlockLight >= newLevel) continue;

            uint8_t neighborBlock = chunkData->data[nIdx];
            const Block& block = registry.getBlock(neighborBlock);

            if (block.blockType == Block::SOLID) continue; // Leaves/Water pass light? Treating leaves as solid for BFS? 
            // Original code treated SOLID as blocking.

            // Update Block Light (keep Sky Light)
            lightMap[nIdx] = (lightMap[nIdx] & 0xF0) | newLevel;
            lightQueue.push({(int16_t)nx, (int16_t)ny, (int16_t)nz, newLevel});
        }
    }

    // Neighbours only need to be remeshed for this chunk's lava light if it reaches their border.
    bool reaches[4] = {};
    for (int a = 0; a < CW; a++) {
        for (int y = 0; y < CH; y++) {
            reaches[0] |= (lightMap[a * strideX + 0 * strideZ + y] & 0x0F) > 1;
            reaches[1] |= (lightMap[a * strideX + (CW - 1) * strideZ + y] & 0x0F) > 1;
            reaches[2] |= (lightMap[0 * strideX + a * strideZ + y] & 0x0F) > 1;
            reaches[3] |= (lightMap[(CW - 1) * strideX + a * strideZ + y] & 0x0F) > 1;
        }
    }
    for (int side = 0; side < 4; side++) borderBlockLight[side] = reaches[side];

    // --- PASS 2: SKY LIGHT ---
    // Each column is lit (15) from the top down to and including the first block that blocks
    // the sky; everything below keeps sky light 0, which the memset above already set.
    for (int x = 0; x < CW; x++) {
        for (int z = 0; z < CW; z++) {
            const int column = x * strideX + z * strideZ;
            for (int y = CH - 1; y >= 0; y--) {
                const int idx = column + y;
                lightMap[idx] = (lightMap[idx] & 0x0F) | (15 << 4);
                if (blocksSky[chunkData->data[idx]]) break;
            }
        }
    }

    memcpy(this->lightMap, lightMap, sizeof(lightMap));
}
