#include "LodTerrain.h"

#include "Blocks.h"
#include "_Vertex.h"
#include "TextureManager.h"
#include "../Generation/BiomeRegistry.h"
#include "../Generation/Terrain.h"
#include <algorithm>
#include <cmath>

namespace {
    constexpr int SNOW_LINE = 172;       ///< Matches WorldGen: peaks above this are snow-capped.
    /// A tile splits while the camera is closer than this many tile sizes. A cell then spans about
    /// 640 / (CELLS * SPLIT_FACTOR) pixels at 1280 px wide and 90 degrees FOV (~5 px), at any distance.
    constexpr float SPLIT_FACTOR = 4.0f;
    constexpr size_t MAX_PENDING = 24;   ///< Tiles being built at once.
    constexpr size_t MAX_UPLOADS_PER_FRAME = 8;

    void setColor(uint8_t (&dst)[3], const std::array<uint8_t, 3> &src) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }

    std::array<uint8_t, 3> topColor(const uint8_t block) {
        return TextureManager::getInstance().averageColor(BlockRegistry::getInstance().getBlock(block).topLayer);
    }

    /// Distance in XZ from a point to a square [min, min + size).
    float distanceToSquare(const glm::vec2 &p, const glm::vec2 &min, const float size) {
        const float dx = std::max({min.x - p.x, 0.0f, p.x - (min.x + size)});
        const float dz = std::max({min.y - p.y, 0.0f, p.y - (min.y + size)});
        return std::max(dx, dz);
    }
}

LodTerrain::~LodTerrain() {
    pool.clearPending();
    pool.waitAll();
}

void LodTerrain::init() {
    using namespace Blocks;
    // Real water looks lighter than its texture (sky reflection, fresnel), so the distant version is brightened to match.
    const auto water = TextureManager::getInstance().averageColor(TextureManager::getInstance().getLayerIndex("water_still"));
    for (int c = 0; c < 3; ++c) waterColor[c] = static_cast<uint8_t>(std::min(255.0f, water[c] * 1.8f + 20.0f));
    setColor(iceColor, topColor(ICE));
    setColor(snowColor, topColor(SNOW));

    const BiomeRegistry &registry = BiomeRegistry::getInstance();
    styles.resize(static_cast<size_t>(BiomeID::COUNT));
    for (size_t i = 0; i < styles.size(); ++i) {
        const Biome &biome = registry.getBiome(static_cast<BiomeID>(i));
        BiomeStyle &style = styles[i];
        setColor(style.top, topColor(biome.surfaceBlock));
        setColor(style.wall, topColor(biome.underBlock));
        setColor(style.steep, topColor(biome.terracottaBands ? ORANGE_TERRACOTTA : biome.steepBlock));
        style.frozen = biome.frozenWater;
    }

    // Forests are seen from afar as their canopy: leaf colour, raised by roughly a tree's height.
    auto canopy = [&](const BiomeID id, const uint8_t leaves, const float height) {
        BiomeStyle &style = styles[static_cast<size_t>(id)];
        setColor(style.canopy, topColor(leaves));
        style.canopyHeight = height;
    };
    canopy(BiomeID::FOREST, OAK_LEAVES, 6.0f);
    canopy(BiomeID::BIRCH_FOREST, BIRCH_LEAVES, 6.0f);
    canopy(BiomeID::DARK_FOREST, DARK_OAK_LEAVES, 7.0f);
    canopy(BiomeID::TAIGA, SPRUCE_LEAVES, 8.0f);
    canopy(BiomeID::SNOWY_TAIGA, SPRUCE_LEAVES, 7.0f);
    canopy(BiomeID::JUNGLE, JUNGLE_LEAVES, 9.0f);
    canopy(BiomeID::SWAMP, OAK_LEAVES, 4.0f);
    // Real canopies are full of shadowed gaps between leaves, so they read darker than the leaf texture.
    for (BiomeStyle &style : styles) {
        for (uint8_t &c : style.canopy) c = static_cast<uint8_t>(c * 0.75f);
    }
    initialized = true;
}

void LodTerrain::shutdown() {
    pool.clearPending();
    pool.waitAll();
    for (auto &[key, tile] : tiles) Renderer::get().releaseMesh(tile.mesh);
    tiles.clear();
    done.clear();
}

void LodTerrain::update(const glm::vec3 &cameraPos, const glm::vec2 &nearCenter, const float nearRadius, const long seed) {
    drawList.clear();
    drawSet.clear();
    if (!active || !initialized) return;
    ++frame;

    if (seed != currentSeed) {
        // New world: drop everything; workers discard results built for the old seed.
        currentSeed = seed;
        workerSeed = seed;
        pool.clearPending();
        for (auto &[key, tile] : tiles) Renderer::get().releaseMesh(tile.mesh);
        tiles.clear();
        pending.clear();
        std::lock_guard lock(doneMutex);
        done.clear();
    }

    // Upload finished tiles.
    {
        std::lock_guard lock(doneMutex);
        size_t uploaded = 0;
        for (auto it = done.begin(); it != done.end() && uploaded < MAX_UPLOADS_PER_FRAME;) {
            pending.erase(it->key);
            if (it->seed == currentSeed) {
                Tile &tile = tiles[it->key];
                Renderer::get().uploadMesh(tile.mesh, it->vertices.data(), it->vertices.size() * sizeof(LodVertex),
                                           it->indices.data(), static_cast<uint32_t>(it->indices.size()));
                const float size = static_cast<float>(tileSize(it->key.level));
                tile.center = glm::vec3((it->key.x + 0.5f) * size, (it->minY + it->maxY) * 0.5f, (it->key.z + 0.5f) * size);
                tile.extents = glm::vec3(size * 0.5f, (it->maxY - it->minY) * 0.5f + 1.0f, size * 0.5f);
                tile.lastUsed = frame;
                ++uploaded;
            }
            it = done.erase(it);
        }
    }

    // Walk the quadtree from the roots that cover the view square.
    wanted.clear();
    const int rootSize = tileSize(MAX_LEVEL);
    const int x0 = static_cast<int>(std::floor((cameraPos.x - VIEW_DISTANCE) / rootSize));
    const int x1 = static_cast<int>(std::floor((cameraPos.x + VIEW_DISTANCE) / rootSize));
    const int z0 = static_cast<int>(std::floor((cameraPos.z - VIEW_DISTANCE) / rootSize));
    const int z1 = static_cast<int>(std::floor((cameraPos.z + VIEW_DISTANCE) / rootSize));
    for (int x = x0; x <= x1; ++x) {
        for (int z = z0; z <= z1; ++z) select({MAX_LEVEL, x, z}, cameraPos, nearCenter, nearRadius);
    }

    // Start building the nearest missing tiles.
    std::sort(wanted.begin(), wanted.end(), [&](const TileKey &a, const TileKey &b) {
        if (a.level != b.level) return a.level < b.level;
        const glm::vec2 cam(cameraPos.x, cameraPos.z);
        const float sa = static_cast<float>(tileSize(a.level)), sb = static_cast<float>(tileSize(b.level));
        return distanceToSquare(cam, glm::vec2(a.x, a.z) * sa, sa) < distanceToSquare(cam, glm::vec2(b.x, b.z) * sb, sb);
    });
    for (const TileKey &key : wanted) {
        if (pending.size() >= MAX_PENDING) break;
        if (!pending.insert(key).second) continue;
        pool.submit([this, key, seed] {
            if (workerSeed.load() != seed) return;
            BuiltTile built = build(key, seed);
            std::lock_guard lock(doneMutex);
            done.push_back(std::move(built));
        });
    }

    // Forget tiles that have not been drawn for a while.
    if (tiles.size() > 2 * drawList.size() + 64) {
        for (auto it = tiles.begin(); it != tiles.end();) {
            if (frame - it->second.lastUsed > 120) {
                Renderer::get().releaseMesh(it->second.mesh);
                it = tiles.erase(it);
            } else {
                ++it;
            }
        }
    }
}

void LodTerrain::select(const TileKey &key, const glm::vec3 &cameraPos, const glm::vec2 &nearCenter, const float nearRadius) {
    const float size = static_cast<float>(tileSize(key.level));
    const glm::vec2 min(key.x * size, key.z * size);
    const glm::vec2 cam(cameraPos.x, cameraPos.z);
    const float distance = distanceToSquare(cam, min, size);

    if (distance > VIEW_DISTANCE) return;
    // Entirely covered by real chunks: every corner well inside their circle (a chunk is drawn when its
    // centre is inside, so a chunk-sized margin keeps the two from ever leaving a gap between them).
    const glm::vec2 farCorner(std::max(std::abs(min.x - nearCenter.x), std::abs(min.x + size - nearCenter.x)),
                              std::max(std::abs(min.y - nearCenter.y), std::abs(min.y + size - nearCenter.y)));
    if (glm::length(farCorner) < nearRadius - static_cast<float>(CHUNK_WIDTH)) return;

    if (key.level > 0 && distance < SPLIT_FACTOR * size) {
        for (int dx = 0; dx < 2; ++dx) {
            for (int dz = 0; dz < 2; ++dz) select({key.level - 1, key.x * 2 + dx, key.z * 2 + dz}, cameraPos, nearCenter, nearRadius);
        }
        return;
    }

    if (tiles.contains(key)) {
        addToDrawList(key);
        return;
    }
    wanted.push_back(key);

    // Until it is built, show the nearest ready ancestor, or else whichever children are ready.
    for (TileKey parent = key; parent.level < MAX_LEVEL;) {
        parent = {parent.level + 1, parent.x >> 1, parent.z >> 1};
        if (tiles.contains(parent)) {
            addToDrawList(parent);
            return;
        }
    }
    if (key.level > 0) {
        for (int dx = 0; dx < 2; ++dx) {
            for (int dz = 0; dz < 2; ++dz) {
                const TileKey child{key.level - 1, key.x * 2 + dx, key.z * 2 + dz};
                if (tiles.contains(child)) addToDrawList(child);
            }
        }
    }
}

void LodTerrain::addToDrawList(const TileKey &key) {
    if (!drawSet.insert(key).second) return;
    drawList.push_back(key);
    tiles[key].lastUsed = frame;
}

void LodTerrain::render(const Frustum &frustum) const {
    if (!active || drawList.empty()) return;
    Renderer &renderer = Renderer::get();
    renderer.bindPipeline(PipelineId::Lod);
    for (const TileKey &key : drawList) {
        const Tile &tile = tiles.at(key);
        if (frustum.isBoxVisible(tile.center, tile.extents)) renderer.draw(tile.mesh);
    }
}

LodTerrain::BuiltTile LodTerrain::build(const TileKey &key, const long seed) const {
    // One sampler per worker thread, rebuilt when the seed changes.
    thread_local TerrainSampler sampler;
    thread_local long samplerSeed = -1;
    if (samplerSeed != seed) {
        sampler = TerrainSampler(seed);
        samplerSeed = seed;
    }

    const int spacing = BASE_SPACING << key.level;
    const float originX = static_cast<float>(key.x * tileSize(key.level));
    const float originZ = static_cast<float>(key.z * tileSize(key.level));
    constexpr int G = CELLS + 2; // One sample of border on each side, for walls and slopes.

    struct Cell {
        ColumnSample sample;
        int ground;   ///< Terrain height (top block).
        int surface;  ///< Drawn top: ground, water level or forest canopy.
        uint8_t top[3], wall[3];
    };
    thread_local Cell grid[G][G];

    for (int i = 0; i < G; ++i) {
        for (int j = 0; j < G; ++j) {
            const float x = originX + (static_cast<float>(i - 1) + 0.5f) * spacing;
            const float z = originZ + (static_cast<float>(j - 1) + 0.5f) * spacing;
            grid[i][j].sample = sampler.sample(x, z);
            grid[i][j].ground = static_cast<int>(grid[i][j].sample.height);
        }
    }

    float minY = 1e9f, maxY = -1e9f;
    for (int i = 0; i < G; ++i) {
        for (int j = 0; j < G; ++j) {
            Cell &cell = grid[i][j];
            int slope = 0;
            if (i > 0 && i < G - 1 && j > 0 && j < G - 1) {
                const int h = cell.ground;
                slope = std::max({std::abs(grid[i - 1][j].ground - h), std::abs(grid[i + 1][j].ground - h),
                                  std::abs(grid[i][j - 1].ground - h), std::abs(grid[i][j + 1].ground - h)}) / spacing;
            }
            const BiomeStyle &style = styles[static_cast<size_t>(TerrainSampler::classify(cell.sample, slope))];

            const auto copy = [](uint8_t (&dst)[3], const uint8_t (&src)[3]) { std::copy(src, src + 3, dst); };
            cell.surface = cell.ground;
            // Small steps read as part of the slope, so they keep the top colour; only cliffs show their rock.
            copy(cell.wall, slope >= 4 ? style.steep : style.top);
            if (cell.ground < WATER_LEVEL) {
                cell.surface = WATER_LEVEL;
                copy(cell.top, style.frozen ? iceColor : waterColor);
            } else if (cell.ground >= SNOW_LINE) {
                copy(cell.top, snowColor);
                copy(cell.wall, snowColor);
            } else if (slope >= 4) {
                copy(cell.top, style.steep);
            } else if (style.canopyHeight > 0.0f) {
                cell.surface = cell.ground + static_cast<int>(style.canopyHeight);
                copy(cell.top, style.canopy);
                copy(cell.wall, style.canopy);
            } else {
                copy(cell.top, style.top);
            }

            // A little per-cell brightness noise stands in for the block textures' detail.
            const int wx = static_cast<int>(std::floor(originX / spacing)) + i, wz = static_cast<int>(std::floor(originZ / spacing)) + j;
            const uint32_t hash = (static_cast<uint32_t>(wx) * 73856093u) ^ (static_cast<uint32_t>(wz) * 19349663u) ^ (static_cast<uint32_t>(key.level) * 83492791u);
            const float shade = 0.93f + 0.14f * static_cast<float>((hash * 2654435761u) >> 24) / 255.0f;
            if (cell.ground >= WATER_LEVEL) {
                for (int c = 0; c < 3; ++c) {
                    cell.top[c] = static_cast<uint8_t>(std::min(255.0f, cell.top[c] * shade));
                    cell.wall[c] = static_cast<uint8_t>(std::min(255.0f, cell.wall[c] * shade));
                }
            }
        }
    }

    BuiltTile out{key, seed, {}, {}, 0.0f, 0.0f};
    out.vertices.reserve(CELLS * CELLS * 8);
    out.indices.reserve(CELLS * CELLS * 12);
    auto quad = [&](const glm::vec3 &a, const glm::vec3 &b, const glm::vec3 &c, const glm::vec3 &d,
                    const uint8_t (&color)[3], const uint8_t direction) {
        const auto base = static_cast<uint32_t>(out.vertices.size());
        for (const glm::vec3 &p : {a, b, c, d}) {
            out.vertices.push_back({p.x, p.y, p.z, color[0], color[1], color[2], direction});
            minY = std::min(minY, p.y);
            maxY = std::max(maxY, p.y);
        }
        for (const uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) out.indices.push_back(base + k);
    };

    const float s = static_cast<float>(spacing);
    const float skirt = 2.0f * s; // Tile edges hang down a little so neighbouring LODs never show cracks.
    for (int i = 1; i <= CELLS; ++i) {
        // Top faces, merged along z while the height and colour stay the same.
        for (int j = 1; j <= CELLS;) {
            const Cell &cell = grid[i][j];
            int run = 1;
            while (j + run <= CELLS && grid[i][j + run].surface == cell.surface &&
                   std::equal(cell.top, cell.top + 3, grid[i][j + run].top)) ++run;
            const float x0 = originX + (i - 1) * s, x1 = x0 + s;
            const float z0 = originZ + (j - 1) * s, z1 = z0 + run * s;
            const float y = static_cast<float>(cell.surface + 1);
            quad({x0, y, z0}, {x1, y, z0}, {x1, y, z1}, {x0, y, z1}, cell.top, TOP);
            j += run;
        }

        // Walls toward lower neighbours (and always, as skirts, on the tile edge).
        for (int j = 1; j <= CELLS; ++j) {
            const Cell &cell = grid[i][j];
            const float x0 = originX + (i - 1) * s, x1 = x0 + s;
            const float z0 = originZ + (j - 1) * s, z1 = z0 + s;
            const float top = static_cast<float>(cell.surface + 1);
            auto wall = [&](const int ni, const int nj, const bool edge, const uint8_t direction) {
                const float neighborTop = static_cast<float>(grid[ni][nj].surface + 1);
                const float bottom = edge ? std::min(top, neighborTop) - skirt : neighborTop;
                if (bottom >= top) return;
                switch (direction) {
                    case WEST: quad({x0, bottom, z0}, {x0, top, z0}, {x0, top, z1}, {x0, bottom, z1}, cell.wall, WEST); break;
                    case EAST: quad({x1, bottom, z1}, {x1, top, z1}, {x1, top, z0}, {x1, bottom, z0}, cell.wall, EAST); break;
                    case NORTH: quad({x1, bottom, z0}, {x1, top, z0}, {x0, top, z0}, {x0, bottom, z0}, cell.wall, NORTH); break;
                    default: quad({x0, bottom, z1}, {x0, top, z1}, {x1, top, z1}, {x1, bottom, z1}, cell.wall, SOUTH); break;
                }
            };
            wall(i - 1, j, i == 1, WEST);
            wall(i + 1, j, i == CELLS, EAST);
            wall(i, j - 1, j == 1, NORTH);
            wall(i, j + 1, j == CELLS, SOUTH);
        }
    }
    out.minY = minY;
    out.maxY = maxY;
    return out;
}
