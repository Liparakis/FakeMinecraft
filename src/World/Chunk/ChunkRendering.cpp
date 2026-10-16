#include "Chunk.h"

namespace {
    /// Concatenates the per-sub-chunk vertex/index lists into one mesh, rebasing indices.
    template<typename V>
    void mergeSubChunks(std::vector<V> (&vertices)[NUM_SUBCHUNKS], std::vector<unsigned int> (&indices)[NUM_SUBCHUNKS],
                        SubChunk (&subChunks)[NUM_SUBCHUNKS], unsigned int SubChunk::*triangleCount,
                        std::vector<V> &mergedVertices, std::vector<uint32_t> &mergedIndices) {
        mergedVertices.clear();
        mergedIndices.clear();
        uint32_t vertexOffset = 0;
        for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
            mergedVertices.insert(mergedVertices.end(), vertices[i].begin(), vertices[i].end());
            for (const unsigned int idx : indices[i]) mergedIndices.push_back(idx + vertexOffset);
            vertexOffset += static_cast<uint32_t>(vertices[i].size());
            subChunks[i].*triangleCount = static_cast<unsigned int>(indices[i].size());
        }
    }

    template<typename V>
    void upload(GpuMesh &mesh, const std::vector<V> &vertices, const std::vector<uint32_t> &indices) {
        Renderer::get().uploadMesh(mesh, vertices.data(), vertices.size() * sizeof(V), indices.data(),
                                   static_cast<uint32_t>(indices.size()));
    }
}

void Chunk::uploadMesh() {
    if (ready || !generated) return;

    // Track the highest and lowest points so culling uses a tight box.
    float minY = static_cast<float>(INT16_MAX), maxY = static_cast<float>(INT16_MIN);
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        for (const auto &v : worldVertices[i]) { minY = std::min<float>(minY, v.posY); maxY = std::max<float>(maxY, v.posY); }
        for (const auto &v : billboardVertices[i]) { minY = std::min(minY, v.posY); maxY = std::max(maxY, v.posY); }
        for (const auto &v : liquidVertices[i]) { minY = std::min<float>(minY, v.posY); maxY = std::max<float>(maxY, v.posY); }
    }

    if (minY <= maxY) {
        cullingCenter = glm::vec3(worldPos.x + CHUNK_WIDTH * 0.5f, (minY + maxY) * 0.5f, worldPos.z + CHUNK_WIDTH * 0.5f);
        cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, (maxY - minY) * 0.5f + 1.0f, CHUNK_WIDTH * 0.5f); // +1 safety margin
    } else {
        cullingCenter = worldPos + glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
        cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
    }

    // Staging vectors keep their capacity between uploads.
    static thread_local std::vector<WorldVertex> worldStaging;
    static thread_local std::vector<BillboardVertex> billboardStaging;
    static thread_local std::vector<FluidVertex> waterStaging;
    static thread_local std::vector<uint32_t> indexStaging;

    mergeSubChunks(worldVertices, worldIndices, subChunks, &SubChunk::numTrianglesWorld, worldStaging, indexStaging);
    upload(worldMesh, worldStaging, indexStaging);
    mergeSubChunks(billboardVertices, billboardIndices, subChunks, &SubChunk::numTrianglesBillboard, billboardStaging, indexStaging);
    upload(billboardMesh, billboardStaging, indexStaging);
    mergeSubChunks(liquidVertices, liquidIndices, subChunks, &SubChunk::numTrianglesLiquid, waterStaging, indexStaging);
    upload(waterMesh, waterStaging, indexStaging);

    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        worldVertices[i].clear();
        worldIndices[i].clear();
        billboardVertices[i].clear();
        billboardIndices[i].clear();
        liquidVertices[i].clear();
        liquidIndices[i].clear();
        subChunks[i].ready = true;
    }

    ready = true;
}

void Chunk::releaseMeshes() {
    Renderer &renderer = Renderer::get();
    renderer.releaseMesh(worldMesh);
    renderer.releaseMesh(billboardMesh);
    renderer.releaseMesh(waterMesh);
}

// The last uploaded mesh keeps drawing while a newer one waits for upload, so edits never flicker.
void Chunk::renderAllSolid() {
    Renderer::get().draw(worldMesh);
}

void Chunk::renderAllBillboard() const {
    Renderer::get().draw(billboardMesh);
}

void Chunk::renderAllWater() const {
    Renderer::get().draw(waterMesh);
}
