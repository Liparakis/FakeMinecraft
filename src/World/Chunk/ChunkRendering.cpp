#include "Chunk.h"

namespace {
    /// Concatenates the per-sub-chunk vertex/index lists into one exactly sized mesh, rebasing indices.
    template<typename V>
    void mergeSubChunks(std::vector<V> *vertices, std::vector<unsigned int> *indices,
                        SubChunk (&subChunks)[NUM_SUBCHUNKS], unsigned int SubChunk::*triangleCount,
                        std::vector<V> &mergedVertices, std::vector<uint32_t> &mergedIndices) {
        size_t vertexTotal = 0, indexTotal = 0;
        for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
            vertexTotal += vertices[i].size();
            indexTotal += indices[i].size();
        }
        mergedVertices.clear();
        mergedIndices.clear();
        mergedVertices.reserve(vertexTotal);
        mergedIndices.reserve(indexTotal);

        uint32_t vertexOffset = 0;
        for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
            mergedVertices.insert(mergedVertices.end(), vertices[i].begin(), vertices[i].end());
            for (const unsigned int idx : indices[i]) mergedIndices.push_back(idx + vertexOffset);
            vertexOffset += static_cast<uint32_t>(vertices[i].size());
            subChunks[i].*triangleCount = static_cast<unsigned int>(indices[i].size());
            vertices[i].clear();
            indices[i].clear();
        }
    }

    template<typename V>
    void upload(GpuMesh &mesh, std::vector<V> &vertices, std::vector<uint32_t> &indices) {
        Renderer::get().uploadMesh(mesh, vertices.data(), vertices.size() * sizeof(V), indices.data(),
                                   static_cast<uint32_t>(indices.size()));
        std::vector<V>().swap(vertices);
        std::vector<uint32_t>().swap(indices);
    }
}

void Chunk::mergeMeshes() {
    // Track the highest and lowest points so culling uses a tight box.
    float minY = static_cast<float>(INT16_MAX), maxY = static_cast<float>(INT16_MIN);
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        for (const auto &v : worldVertices[i]) { minY = std::min<float>(minY, v.posY); maxY = std::max<float>(maxY, v.posY); }
        for (const auto &v : cutoutVertices[i]) { minY = std::min<float>(minY, v.posY); maxY = std::max<float>(maxY, v.posY); }
        for (const auto &v : billboardVertices[i]) { minY = std::min(minY, v.posY); maxY = std::max(maxY, v.posY); }
        for (const auto &v : liquidVertices[i]) { minY = std::min<float>(minY, v.posY); maxY = std::max<float>(maxY, v.posY); }
    }
    meshMinY = minY;
    meshMaxY = maxY;

    mergeSubChunks(worldVertices, worldIndices, subChunks, &SubChunk::numTrianglesWorld, mergedWorldVertices, mergedWorldIndices);
    mergeSubChunks(cutoutVertices, cutoutIndices, subChunks, &SubChunk::numTrianglesCutout, mergedCutoutVertices, mergedCutoutIndices);
    mergeSubChunks(billboardVertices, billboardIndices, subChunks, &SubChunk::numTrianglesBillboard, mergedBillboardVertices, mergedBillboardIndices);
    mergeSubChunks(liquidVertices, liquidIndices, subChunks, &SubChunk::numTrianglesLiquid, mergedWaterVertices, mergedWaterIndices);
}

bool Chunk::uploadMesh() {
    if (ready || !generated) return false;
    std::unique_lock lock(meshMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;

    if (meshMinY <= meshMaxY) {
        cullingCenter = glm::vec3(worldPos.x + CHUNK_WIDTH * 0.5f, (meshMinY + meshMaxY) * 0.5f, worldPos.z + CHUNK_WIDTH * 0.5f);
        cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, (meshMaxY - meshMinY) * 0.5f + 1.0f, CHUNK_WIDTH * 0.5f); // +1 safety margin
    } else {
        cullingCenter = worldPos + glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
        cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
    }

    upload(worldMesh, mergedWorldVertices, mergedWorldIndices);
    upload(cutoutMesh, mergedCutoutVertices, mergedCutoutIndices);
    upload(billboardMesh, mergedBillboardVertices, mergedBillboardIndices);
    upload(waterMesh, mergedWaterVertices, mergedWaterIndices);
    for (SubChunk &sub : subChunks) sub.ready = true;

    ready = true;
    return true;
}

void Chunk::releaseMeshes() {
    Renderer &renderer = Renderer::get();
    renderer.releaseMesh(worldMesh);
    renderer.releaseMesh(cutoutMesh);
    renderer.releaseMesh(billboardMesh);
    renderer.releaseMesh(waterMesh);
}

// The last uploaded mesh keeps drawing while a newer one waits for upload, so edits never flicker.
void Chunk::renderAllSolid() {
    if (!worldMesh) return;
    Renderer::get().pushChunkOrigin(origin());
    Renderer::get().draw(worldMesh);
}

void Chunk::renderAllCutout() const {
    if (!cutoutMesh) return;
    Renderer::get().pushChunkOrigin(origin());
    Renderer::get().draw(cutoutMesh);
}

void Chunk::renderAllBillboard() const {
    Renderer::get().draw(billboardMesh);
}

void Chunk::renderAllWater() const {
    if (!waterMesh) return;
    Renderer::get().pushChunkOrigin(origin());
    Renderer::get().draw(waterMesh);
}
