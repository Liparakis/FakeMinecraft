#pragma once

#include <atomic>
#include <memory>
#include <iostream>
#include "Block.h"
#include "ChunkData.h"
#include "Blocks.h"

// Group neighbors to avoid repeating the same code.
/// Meshing properties per block id, built once from the registry (after Blocks::init).
/// Replaces two registry lookups per neighbour test in the mesher's hot loops.
struct MeshBlockTable {
    bool greedy[256] = {};    ///< SOLID or LEAVES: meshed by the greedy face passes.
    bool showsFace[256] = {}; ///< LEAVES, TRANSPARENT, BILLBOARD or LIQUID: a solid face next to it is visible.
    bool cutout[256] = {};    ///< Has see-through texels, so it goes in the alpha-tested cutout mesh.
    bool leaves[256] = {};
    /// Fancy leaves draw the faces between neighbouring leaf blocks too; fast leaves skip those
    /// internal canopy layers, which are most of the leaf overdraw. Changed via setFancyLeaves.
    std::atomic<bool> fancyLeaves{false};

    /// Whether the face of `block` that touches `neighbor` needs to be drawn.
    [[nodiscard]] bool faceVisible(const uint8_t block, const uint8_t neighbor) const {
        return showsFace[neighbor] && (fancyLeaves.load(std::memory_order_relaxed) || !(leaves[block] && leaves[neighbor]));
    }
};

inline MeshBlockTable &mutableMeshBlockTable() {
    static MeshBlockTable table;
    static const bool filled = [] {
        MeshBlockTable &t = table;
        const auto &blocks = BlockRegistry::getInstance().getAllBlocks();
        for (const Block &b : blocks) {
            t.greedy[b.id] = b.blockType == Block::SOLID || b.blockType == Block::LEAVES;
            t.showsFace[b.id] = b.blockType == Block::LEAVES || b.blockType == Block::TRANSPARENT ||
                                b.blockType == Block::BILLBOARD || b.blockType == Block::LIQUID;
            t.cutout[b.id] = b.blockType == Block::LEAVES || b.blockType == Block::TRANSPARENT;
            t.leaves[b.id] = b.blockType == Block::LEAVES;
        }
        return true;
    }();
    (void) filled;
    return table;
}

inline const MeshBlockTable &meshBlockTable() { return mutableMeshBlockTable(); }

/// Switches leaf rendering; existing chunks keep their meshes until remeshed (Planet::remeshAll).
inline void setFancyLeaves(const bool fancy) { mutableMeshBlockTable().fancyLeaves = fancy; }

struct NeighborData {
    int block;
    int topBlock;
    const Block *blockType;
    const Block *topBlockType;
    bool isLiquid;

    void init(int const blk, int const topBlk) {
        block = blk;
        topBlock = topBlk;
        blockType = &BlockRegistry::getInstance().getBlock(block);
        topBlockType = &BlockRegistry::getInstance().getBlock(topBlock);
        isLiquid = (blockType->blockType == Block::LIQUID);
    }
};

// Inline helper to check if face should render
inline bool shouldRenderFace(const NeighborData &neighbor, bool const isCurrentLiquid) {
    const auto shouldRender = (neighbor.blockType->blockType == Block::LEAVES) ||
                              (neighbor.blockType->blockType == Block::TRANSPARENT) ||
                              (neighbor.blockType->blockType == Block::BILLBOARD) ||
                              (neighbor.isLiquid && !isCurrentLiquid);

    return shouldRender;
}

// Functions to find blocks in neighboring chunks.
inline void fetchNorthNeighbor(NeighborData &neighbor, const int x, const int y, const int z,
                               const std::shared_ptr<ChunkData> &chunkData,
                               const std::shared_ptr<ChunkData> &northData,
                               const std::shared_ptr<ChunkData> &upData) {
    if (z > 0) {
        const auto blk = chunkData->getBlock(x, y, z - 1);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? chunkData->getBlock(x, y + 1, z - 1)
                                : upData->getBlock(x, 0, z - 1);
        neighbor.init(blk, topBlk);
    } else if (northData) {
        const auto blk = northData->getBlock(x, y, CHUNK_WIDTH - 1);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? northData->getBlock(x, y + 1, CHUNK_WIDTH - 1)
                                : Blocks::AIR;
        neighbor.init(blk, topBlk);

        static auto logN = false;
        if (blk == 0 && topBlk == 0) {
            int sum = 0;
            for (int i = 0; i < 64; i++) sum += northData->getBlock(x, i, CHUNK_WIDTH - 1);

            if (sum == 0 && !logN) {
                std::cout << "[DEBUG] Note: The chunk to the North seems to be empty for some reason." << std::endl;
                logN = true;
            }
        }
    } else {
        static auto logN = false;
        if (!logN) {
            std::cout << "[DEBUG] North Neighbor PTR is NULL!" << std::endl;
            logN = true;
        }
        neighbor.init(Blocks::AIR, Blocks::AIR);
    }
}

inline void fetchSouthNeighbor(NeighborData &neighbor, const int x, const int y, const int z,
                               const std::shared_ptr<ChunkData> &chunkData,
                               const std::shared_ptr<ChunkData> &southData,
                               const std::shared_ptr<ChunkData> &upData) {
    if (z < CHUNK_WIDTH - 1) {
        const auto blk = chunkData->getBlock(x, y, z + 1);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? chunkData->getBlock(x, y + 1, z + 1)
                                : upData->getBlock(x, 0, z + 1);
        neighbor.init(blk, topBlk);
    } else if (southData) {
        const auto blk = southData->getBlock(x, y, 0);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? southData->getBlock(x, y + 1, 0)
                                : Blocks::AIR;
        neighbor.init(blk, topBlk);
    } else {
        static auto logS = false;
        if (!logS) {
            std::cout << "[DEBUG] South Neighbor PTR is NULL!" << std::endl;
            logS = true;
        }
        neighbor.init(Blocks::AIR, Blocks::AIR);
    }
}

inline void fetchWestNeighbor(NeighborData &neighbor, const int x, const int y, const int z,
                              const std::shared_ptr<ChunkData> &chunkData,
                              const std::shared_ptr<ChunkData> &westData,
                              const std::shared_ptr<ChunkData> &upData) {
    if (x > 0) {
        const auto blk = chunkData->getBlock(x - 1, y, z);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? chunkData->getBlock(x - 1, y + 1, z)
                                : upData->getBlock(x - 1, 0, z);
        neighbor.init(blk, topBlk);
    } else if (westData) {
        const auto blk = westData->getBlock(CHUNK_WIDTH - 1, y, z);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? westData->getBlock(CHUNK_WIDTH - 1, y + 1, z)
                                : Blocks::AIR;
        neighbor.init(blk, topBlk);
    } else {
        static auto logW = false;
        if (!logW) {
            std::cout << "[DEBUG] West Neighbor PTR is NULL!" << std::endl;
            logW = true;
        }
        neighbor.init(Blocks::AIR, Blocks::AIR);
    }
}

inline void fetchEastNeighbor(NeighborData &neighbor, int x, int y, int z,
                              const std::shared_ptr<ChunkData> &chunkData,
                              const std::shared_ptr<ChunkData> &eastData,
                              const std::shared_ptr<ChunkData> &upData) {
    if (x < CHUNK_WIDTH - 1) {
        const auto blk = chunkData->getBlock(x + 1, y, z);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? chunkData->getBlock(x + 1, y + 1, z)
                                : upData->getBlock(x + 1, 0, z);
        neighbor.init(blk, topBlk);
    } else if (eastData) {
        const auto blk = eastData->getBlock(0, y, z);
        const auto topBlk = (y + 1 < CHUNK_HEIGHT)
                                ? eastData->getBlock(0, y + 1, z)
                                : Blocks::AIR;
        neighbor.init(blk, topBlk);
    } else {
        static auto logE = false;
        if (!logE) {
            std::cout << "[DEBUG] East Neighbor PTR is NULL!" << std::endl;
            logE = true;
        }
        neighbor.init(Blocks::AIR, Blocks::AIR);
    }
}
