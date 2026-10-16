#include "Chunk.h"
#include "ChunkUtils.h"
#include <glm/gtc/type_ptr.hpp>
#include "Planet.h"
#include "WorldGen.h"

Chunk::Chunk(const ChunkPos chunkPos)
    : chunkPos(chunkPos) {
    worldPos = glm::vec3(static_cast<float>(chunkPos.x) * CHUNK_WIDTH,
                         static_cast<float>(chunkPos.y) * CHUNK_HEIGHT,
                         static_cast<float>(chunkPos.z) * CHUNK_WIDTH);

    // Initialize data to help decide if the chunk should be drawn.
    cullingCenter = worldPos + glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
    cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);

    // Set up area checks for smaller parts of the chunk.
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        const float subChunkY = worldPos.y + i * SUBCHUNK_HEIGHT;
        subChunks[i].cullingCenter = glm::vec3(
            worldPos.x + CHUNK_WIDTH * 0.5f,
            subChunkY + SUBCHUNK_HEIGHT * 0.5f,
            worldPos.z + CHUNK_WIDTH * 0.5f
        );
        subChunks[i].cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, SUBCHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
    }

    ready = false;
    generated = false;
}

Chunk::~Chunk() {
    if (chunkThread.joinable())
        chunkThread.join();

    releaseMeshes();
}

void Chunk::reset(ChunkPos newPos) {
    // Join any existing thread
    if (chunkThread.joinable()) {
        chunkThread.join();
    }

    releaseMeshes();

    // Reset position and state
    chunkPos = newPos;
    worldPos = glm::vec3(
        static_cast<float>(newPos.x) * CHUNK_WIDTH,
        static_cast<float>(newPos.y) * CHUNK_HEIGHT,
        static_cast<float>(newPos.z) * CHUNK_WIDTH
    );

    cullingCenter = worldPos + glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
    cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);

    // Reset sub-chunk culling
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        float subChunkY = worldPos.y + i * SUBCHUNK_HEIGHT;
        subChunks[i].cullingCenter = glm::vec3(
            worldPos.x + CHUNK_WIDTH * 0.5f,
            subChunkY + SUBCHUNK_HEIGHT * 0.5f,
            worldPos.z + CHUNK_WIDTH * 0.5f
        );
        subChunks[i].cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, SUBCHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
        subChunks[i].ready = false;
    }

    // Reset data pointers
    chunkData = nullptr;
    northData = nullptr;
    southData = nullptr;
    upData = nullptr;
    downData = nullptr;
    eastData = nullptr;
    westData = nullptr;

    // Reset flags
    ready = false;
    generated = false;
    cachedDistSq = 0.0f;
    listIndex = 0;

    // Clear vertex buffers
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        worldVertices[i].clear();
        worldIndices[i].clear();
        billboardVertices[i].clear();
        billboardIndices[i].clear();
        liquidVertices[i].clear();
        liquidIndices[i].clear();
    }
}

uint16_t Chunk::getBlockAtPos(int x, int y, int z) const {
    return ready ? chunkData->getBlock(x, y, z) : 0;
}

void Chunk::updateBlock(int x, int y, int z, uint8_t newBlock) {
    chunkData->setBlock(x, y, z, newBlock);

    // Regenerate mesh data
    generateChunkMesh();

    // Planet::prepare uploads it next frame; the old mesh draws until then.
    ready = false;
    generated = true;

    // Update neighboring chunks if at boundaries
    if (x == 0) {
        Chunk *westChunk = Planet::planet->getChunk({chunkPos.x - 1, chunkPos.y, chunkPos.z});
        if (westChunk) westChunk->updateChunk();
    } else if (x == CHUNK_WIDTH - 1) {
        Chunk *eastChunk = Planet::planet->getChunk({chunkPos.x + 1, chunkPos.y, chunkPos.z});
        if (eastChunk) eastChunk->updateChunk();
    }

    if (y == 0) {
        Chunk *downChunk = Planet::planet->getChunk({chunkPos.x, chunkPos.y - 1, chunkPos.z});
        if (downChunk) downChunk->updateChunk();
    } else if (y == CHUNK_HEIGHT - 1) {
        Chunk *upChunk = Planet::planet->getChunk({chunkPos.x, chunkPos.y + 1, chunkPos.z});
        if (upChunk) upChunk->updateChunk();
    }

    if (z == 0) {
        Chunk *northChunk = Planet::planet->getChunk({chunkPos.x, chunkPos.y, chunkPos.z - 1});
        if (northChunk) northChunk->updateChunk();
    } else if (z == CHUNK_WIDTH - 1) {
        Chunk *southChunk = Planet::planet->getChunk({chunkPos.x, chunkPos.y, chunkPos.z + 1});
        if (southChunk) southChunk->updateChunk();
    }
}

void Chunk::updateChunk() {
    generateChunkMesh();
    ready = false;
    generated = true;
}
