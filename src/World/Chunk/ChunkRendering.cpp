#include "Chunk.h"
#include <GL/glew.h>

// Helper function to setup VAO attributes
inline void setupWorldVAO() {
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, posX)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_BYTE, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<void *>(offsetof(Vertex, texU)));
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(2, 1, GL_BYTE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, direction)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, layerIndex)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_BYTE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, lightLevel)));
    glEnableVertexAttribArray(4);
}

inline void setupWaterVAO() {
    glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, sizeof(FluidVertex),
                          reinterpret_cast<void *>(offsetof(FluidVertex, posX)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_BYTE, GL_FALSE, sizeof(FluidVertex),
                          reinterpret_cast<void *>(offsetof(FluidVertex, texU)));
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(2, 1, GL_BYTE, sizeof(FluidVertex),
                           reinterpret_cast<void *>(offsetof(FluidVertex, direction)));
    glEnableVertexAttribArray(2);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(FluidVertex), reinterpret_cast<void *>(offsetof(FluidVertex, layerIndex)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_BYTE, sizeof(FluidVertex), reinterpret_cast<void *>(offsetof(FluidVertex, lightLevel)));
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(5, 1, GL_BYTE, sizeof(FluidVertex), reinterpret_cast<void *>(offsetof(FluidVertex, top)));
    glEnableVertexAttribArray(5);
}

inline void setupBillboardVAO() {
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardVertex),
                          reinterpret_cast<void *>(offsetof(BillboardVertex, posX)));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_BYTE, GL_FALSE, sizeof(BillboardVertex),
                              reinterpret_cast<void *>(offsetof(BillboardVertex, texU)));
    glEnableVertexAttribArray(1);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_SHORT, sizeof(BillboardVertex), 
                          reinterpret_cast<void *>(offsetof(BillboardVertex, layerIndex)));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(4, 1, GL_UNSIGNED_BYTE, sizeof(BillboardVertex), reinterpret_cast<void *>(offsetof(Vertex, lightLevel)));
    glEnableVertexAttribArray(4);
}

void Chunk::uploadMesh() {
    if (ready || !generated) return;
    
    // Generate occlusion query ID on main thread (OpenGL context required)
    if (queryID == 0) {
        glGenQueries(1, &queryID);
    }

    // Calculate total sizes across all sub-chunks
    size_t totalWorldVerts = 0, totalWorldInds = 0;
    size_t totalBillboardVerts = 0, totalBillboardInds = 0;
    size_t totalWaterVerts = 0, totalWaterInds = 0;
    
    // Track the highest and lowest points to help hide the chunk if it's not on screen.
    int16_t minY = INT16_MAX, maxY = INT16_MIN;
    
    for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
        totalWorldVerts += worldVertices[i].size();
        totalWorldInds += worldIndices[i].size();
        totalBillboardVerts += billboardVertices[i].size();
        totalBillboardInds += billboardIndices[i].size();
        totalWaterVerts += liquidVertices[i].size();
        totalWaterInds += liquidIndices[i].size();
        
        // Find actual Y bounds from world vertices
        for (const auto& v : worldVertices[i]) {
            if (v.posY < minY) minY = v.posY;
            if (v.posY > maxY) maxY = v.posY;
        }
        for (const auto& v : billboardVertices[i]) {
            if (v.posY < minY) minY = v.posY;
            if (v.posY > maxY) maxY = v.posY;
        }
        for (const auto& v : liquidVertices[i]) {
            if (v.posY < minY) minY = v.posY;
            if (v.posY > maxY) maxY = v.posY;
        }
    }
    
    // Update culling bounds to match actual geometry (tight bounding box)
    if (minY != INT16_MAX && maxY != INT16_MIN) {
        float geometryMinY = (minY);
        float geometryMaxY = (maxY);
        float geometryHeight = geometryMaxY - geometryMinY;
        
        cullingCenter = glm::vec3(
            worldPos.x + CHUNK_WIDTH * 0.5f,
            (geometryMinY + geometryMaxY) * 0.5f,
            worldPos.z + CHUNK_WIDTH * 0.5f
        );
        cullingExtents = glm::vec3(
            CHUNK_WIDTH * 0.5f,
            geometryHeight * 0.5f + 1.0f, // +1 for safety margin
            CHUNK_WIDTH * 0.5f
        );
    } else {
        // Fallback for empty/weird chunks
        cullingCenter = worldPos + glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
        cullingExtents = glm::vec3(CHUNK_WIDTH * 0.5f, CHUNK_HEIGHT * 0.5f, CHUNK_WIDTH * 0.5f);
    }

    // --- Persistent Staging Buffers ---
    static thread_local std::vector<WorldVertex> stagingWorldVerts;
    static thread_local std::vector<unsigned int> stagingWorldInds;
    
    static thread_local std::vector<BillboardVertex> stagingBillboardVerts;
    static thread_local std::vector<unsigned int> stagingBillboardInds;
    
    static thread_local std::vector<FluidVertex> stagingWaterVerts;
    static thread_local std::vector<unsigned int> stagingWaterInds;

    // Merge and upload WORLD geometry
    mergedWorldTriangles = totalWorldInds;
    if (totalWorldVerts > 0) {
        stagingWorldVerts.clear();
        stagingWorldInds.clear();
        stagingWorldVerts.reserve(totalWorldVerts);
        stagingWorldInds.reserve(totalWorldInds);
        
        unsigned int vertexOffset = 0;
        for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
            stagingWorldVerts.insert(stagingWorldVerts.end(), worldVertices[i].begin(), worldVertices[i].end());
            for (unsigned int idx : worldIndices[i]) {
                stagingWorldInds.push_back(idx + vertexOffset);
            }
            vertexOffset += worldVertices[i].size();
            subChunks[i].numTrianglesWorld = worldIndices[i].size();
        }
        
        glGenVertexArrays(1, &mergedWorldVAO);
        glGenBuffers(1, &mergedWorldVBO);
        glGenBuffers(1, &mergedWorldEBO);
        glBindVertexArray(mergedWorldVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mergedWorldVBO);
        glBufferData(GL_ARRAY_BUFFER, stagingWorldVerts.size() * sizeof(WorldVertex), stagingWorldVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mergedWorldEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, stagingWorldInds.size() * sizeof(unsigned int), stagingWorldInds.data(), GL_STATIC_DRAW);
        setupWorldVAO();
    }

    // Merge and upload BILLBOARD geometry
    mergedBillboardTriangles = totalBillboardInds;
    if (totalBillboardVerts > 0) {
        stagingBillboardVerts.clear();
        stagingBillboardInds.clear();
        stagingBillboardVerts.reserve(totalBillboardVerts);
        stagingBillboardInds.reserve(totalBillboardInds);
        
        unsigned int vertexOffset = 0;
        for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
            stagingBillboardVerts.insert(stagingBillboardVerts.end(), billboardVertices[i].begin(), billboardVertices[i].end());
            for (unsigned int idx : billboardIndices[i]) {
                stagingBillboardInds.push_back(idx + vertexOffset);
            }
            vertexOffset += billboardVertices[i].size();
            subChunks[i].numTrianglesBillboard = billboardIndices[i].size();
        }
        
        glGenVertexArrays(1, &mergedBillboardVAO);
        glGenBuffers(1, &mergedBillboardVBO);
        glGenBuffers(1, &mergedBillboardEBO);
        glBindVertexArray(mergedBillboardVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mergedBillboardVBO);
        glBufferData(GL_ARRAY_BUFFER, stagingBillboardVerts.size() * sizeof(BillboardVertex), stagingBillboardVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mergedBillboardEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, stagingBillboardInds.size() * sizeof(unsigned int), stagingBillboardInds.data(), GL_STATIC_DRAW);
        setupBillboardVAO();
    }

    // Merge and upload WATER geometry
    mergedWaterTriangles = totalWaterInds;
    if (totalWaterVerts > 0) {
        stagingWaterVerts.clear();
        stagingWaterInds.clear();
        stagingWaterVerts.reserve(totalWaterVerts);
        stagingWaterInds.reserve(totalWaterInds);
        
        unsigned int vertexOffset = 0;
        for (int i = 0; i < NUM_SUBCHUNKS; ++i) {
            stagingWaterVerts.insert(stagingWaterVerts.end(), liquidVertices[i].begin(), liquidVertices[i].end());
            for (unsigned int idx : liquidIndices[i]) {
                stagingWaterInds.push_back(idx + vertexOffset);
            }
            vertexOffset += liquidVertices[i].size();
            subChunks[i].numTrianglesLiquid = liquidIndices[i].size();
        }
        
        glGenVertexArrays(1, &mergedWaterVAO);
        glGenBuffers(1, &mergedWaterVBO);
        glGenBuffers(1, &mergedWaterEBO);
        glBindVertexArray(mergedWaterVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mergedWaterVBO);
        glBufferData(GL_ARRAY_BUFFER, stagingWaterVerts.size() * sizeof(FluidVertex), stagingWaterVerts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mergedWaterEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, stagingWaterInds.size() * sizeof(unsigned int), stagingWaterInds.data(), GL_STATIC_DRAW);
        setupWaterVAO();
    }

    // Clear ALL CPU-side mesh data
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

// Render all solid geometry in ONE draw call using merged VAO
void Chunk::renderAllSolid() {
    if (!ready) {
        if (generated) {
            uploadMesh();
        } else {
            return;
        }
    }

    if (mergedWorldTriangles > 0 && mergedWorldVAO != 0) {
        glBindVertexArray(mergedWorldVAO);
        glDrawElements(GL_TRIANGLES, mergedWorldTriangles, GL_UNSIGNED_INT, 0);
    }
}

// Render all billboard geometry in ONE draw call using merged VAO
void Chunk::renderAllBillboard() const {
    if (!ready) return;

    if (mergedBillboardTriangles > 0 && mergedBillboardVAO != 0) {
        glBindVertexArray(mergedBillboardVAO);
        glDrawElements(GL_TRIANGLES, mergedBillboardTriangles, GL_UNSIGNED_INT, 0);
    }
}

// Render all water geometry in ONE draw call using merged VAO
void Chunk::renderAllWater() const {
    if (!ready) return;

    if (mergedWaterTriangles > 0 && mergedWaterVAO != 0) {
        glBindVertexArray(mergedWaterVAO);
        glDrawElements(GL_TRIANGLES, mergedWaterTriangles, GL_UNSIGNED_INT, 0);
    }
}