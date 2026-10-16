/**
 * @file LodTerrain.h
 * @brief Distant terrain: coarse, blocky heightfield tiles out to the horizon.
 *
 * Real chunks cover the area around the player. Beyond them, a quadtree of tiles
 * (finer near, coarser far) is built straight from the terrain function: height,
 * biome surface colour, water and a canopy bump for forests. No block data exists
 * for these tiles, so they are cheap enough to reach ~16 km. Where a real chunk is
 * drawn, the LOD fragment shader steps aside (see the chunk mask in Renderer).
 */
#pragma once

#include "Frustum.h"
#include "Renderer.h"
#include "ThreadPool.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

/// Matches VertexLayout::Lod in Renderer.cpp and lod.vert.
struct LodVertex {
    float x, y, z;
    uint8_t r, g, b;
    uint8_t direction; ///< FACE_DIRECTION, for lighting.
};
static_assert(sizeof(LodVertex) == 16);

class LodTerrain {
public:
    static constexpr int CELLS = 32;          ///< Samples along a tile edge.
    static constexpr int BASE_SPACING = 4;    ///< Blocks per sample at level 0.
    static constexpr int MAX_LEVEL = 8;       ///< Level 8 samples every 1024 blocks.
    static constexpr float VIEW_DISTANCE = 16448.0f; ///< Minecraft render distance 1028 (x16 blocks).
    static constexpr uint8_t WATER_TOP = 7; ///< LodVertex::direction of water surfaces (LOD_WATER_TOP in common.glsl).

    LodTerrain() = default;
    ~LodTerrain();

    /// Reads biome colours from the loaded textures. Call once after Blocks/BiomeRegistry init.
    void init();
    /// Frees every tile mesh; call before the renderer shuts down.
    void shutdown();

    void setEnabled(bool enabled) { active = enabled; }
    [[nodiscard]] bool enabled() const { return active; }

    /// Chooses the tiles for this camera, starts building missing ones and uploads finished ones.
    /// Records uploads, so it runs after Renderer::beginFrame and before the first pass.
    /// @param nearCenter, nearRadius Real chunks are drawn within this horizontal circle; tiles fully inside it are skipped.
    /// @param reflectionRadius Real chunks are reflected within this distance of the camera.
    /// @param chunkMask Which real chunks are drawn (Planet::chunkMaskData); tiles are skipped only over drawn chunks.
    void update(const glm::vec3 &cameraPos, const glm::vec2 &nearCenter, float nearRadius, float reflectionRadius,
                const uint8_t *chunkMask, long seed);
    /// Draws the selected tiles that pass the frustum, inside the main pass or (coarser) the reflection pass.
    void render(const Frustum &frustum, bool reflection = false) const;

private:
    struct TileKey {
        int level, x, z;
        bool operator==(const TileKey &o) const { return level == o.level && x == o.x && z == o.z; }
    };
    struct TileKeyHash {
        size_t operator()(const TileKey &k) const {
            return (static_cast<size_t>(k.level) * 73856093u) ^ (static_cast<size_t>(k.x) * 19349663u) ^
                   (static_cast<size_t>(k.z) * 83492791u);
        }
    };
    struct Tile {
        GpuMesh mesh;
        glm::vec3 center, extents;
        uint64_t lastUsed = 0;
    };
    struct BuiltTile {
        TileKey key;
        long seed;
        std::vector<LodVertex> vertices;
        std::vector<uint32_t> indices;
        float minY, maxY;
    };
    /// Per-biome colours and canopy, indexed by BiomeID.
    struct BiomeStyle {
        uint8_t top[3], wall[3], steep[3];
        uint8_t floor[3]; ///< Sea floor, seen through the water.
        uint8_t canopy[3];
        float canopyHeight = 0.0f; ///< Forests are drawn as a raised, leaf-coloured surface.
        bool frozen = false;
    };

    /// A set of tiles that covers the view once, without overlaps.
    struct Cut {
        std::vector<TileKey> list;
        std::unordered_set<TileKey, TileKeyHash> set;
    };

    static int tileSize(const int level) { return CELLS * BASE_SPACING << level; }
    void select(const TileKey &key, const glm::vec3 &cameraPos, const glm::vec2 &nearCenter, float nearRadius,
                float splitFactor, Cut &cut);
    void addToCut(const TileKey &key, Cut &cut);
    [[nodiscard]] bool realChunksCover(const glm::vec2 &min, float size) const;
    BuiltTile build(const TileKey &key, long seed) const;

    bool active = true;
    bool initialized = false;
    long currentSeed = -1;
    uint64_t frame = 0;

    std::vector<BiomeStyle> styles;
    uint8_t waterColor[3] = {}, iceColor[3] = {}, snowColor[3] = {};

    std::unordered_map<TileKey, Tile, TileKeyHash> tiles; ///< Uploaded tiles (render thread).
    std::unordered_set<TileKey, TileKeyHash> pending;      ///< Being built (render thread).
    std::vector<TileKey> wanted;                           ///< This frame's missing tiles, nearest first.
    Cut view;       ///< Drawn in the main pass.
    Cut reflection; ///< Coarser: the reflection is low resolution and rippled.
    const uint8_t *chunkMask = nullptr; ///< During update().

    std::mutex doneMutex;
    std::vector<BuiltTile> done; ///< Finished by workers, waiting for upload.
    std::atomic<long> workerSeed{-1};
    ThreadPool pool{2};
};
