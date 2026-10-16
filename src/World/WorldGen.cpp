#include "WorldGen.h"

#include <OpenSimplexNoise.hh>
#include <random>
#include <cstring>
#include <algorithm>
#include "Blocks.h"
#include "Planet.h"
#include "ChunkPos.h"
#include "Generation/BiomeRegistry.h"
#include "Generation/Feature.h"
#include "Generation/Terrain.h"

/**
 * @file WorldGen.cpp
 * @brief World generation implementation using a 4-pass system.
 * This project uses OpenSimplex Noise for terrain generation
 */

/** @name Prefetching Macros
 * Macros for software prefetching to improve memory throughput.
 * @{
 */
#if defined(__GNUC__) || defined(__clang__)
#define PREFETCH(addr, rw, locality) __builtin_prefetch(addr, rw, locality) ///< GCC/Clang software prefetch hint.
#elif defined(_MSC_VER)
#include <xmmintrin.h>
#define PREFETCH(addr, rw, locality) _mm_prefetch((const char*)(addr), locality == 3 ? _MM_HINT_T0 : (locality == 2 ? _MM_HINT_T1 : _MM_HINT_T2)) ///< MSVC SIMD prefetch hint.

#else
#define PREFETCH(addr, rw, locality) ((void)0)
#endif
/** @} */

namespace {
    /**
     * @struct NoiseCache
     * @brief A small cache for noise values so we don't have to keep recalculating them.
     */
    struct alignas(64) NoiseCache {
        int16_t surfaceHeight[CHUNK_WIDTH][CHUNK_WIDTH]; ///< Terrain height of each column.
        uint8_t slope[CHUNK_WIDTH][CHUNK_WIDTH]; ///< Largest height step to a neighbouring column.
        const Biome *biomePtr[CHUNK_WIDTH][CHUNK_WIDTH]; ///< Cached pointer for fast property access.
    };

    struct NoiseSettings {
        float frequency{};
        float amplitude{};
        float offset{};
        float chance = 0.0f;
        int minHeight = 0;
        int maxHeight = MAX_HEIGHT;
        uint8_t block = 0;
    };

    /**
     * @brief A fast hash function to help with randomizing where things like trees and ores go.
     */
    [[gnu::always_inline, msvc::forceinline]]
    uint64_t fastHash(int64_t x, int64_t z, uint64_t seed, uint64_t salt = 0) {
        uint64_t h = seed;
        h ^= static_cast<uint64_t>(x) * 0x517cc1b727220a95ULL;
        h ^= static_cast<uint64_t>(z) * 0x85ebca6b5f0e7d9bULL;
        h ^= salt;
        h ^= h >> 33;
        h *= 0xff51afd7ed558ccdULL;
        h ^= h >> 33;
        h *= 0xc4ceb9fe1a85ec53ULL;
        h ^= h >> 33;
        return h;
    }

    /**
     * @class TrilinearNoise
     * @brief Class for handling 3D noise by interpolating between grid points.
     * 
     * Rare samples are taken at grid corners, and trilinear interpolation 
     * is used to fill the gaps for efficiency.
     */
    template<int STEP>
    class TrilinearNoise {
        static constexpr int POINTS_X = CHUNK_WIDTH / STEP + 1;
        static constexpr int POINTS_Y = CHUNK_HEIGHT / STEP + 1;
        static constexpr int POINTS_Z = CHUNK_WIDTH / STEP + 1;

        /// @brief 3D grid of noise samples, oriented Y-inner for cache-coherent sampling.
        alignas(64) float storage[POINTS_X][POINTS_Z][POINTS_Y] = {};

    public:
        /**
         * @brief Populates the sparse 3D grid with raw noise values.
         * 
         * @param noise The 3D OpenSimplexNoise generator.
         * @param startX World X coordinate of the chunk's origin.
         * @param startY World Y coordinate of the chunk's origin.
         * @param startZ World Z coordinate of the chunk's origin.
         * @param settings Noise generation parameters (frequency, amplitude, etc.).
         */
        void fill(const OSN::Noise<3> &noise, const int startX, const int startY, const int startZ,
                  const NoiseSettings &settings) {
            const float freq = settings.frequency;
            const float offset = settings.offset;
            const float amp = settings.amplitude;
            const int maxH = settings.maxHeight;
            const int minH = MIN_HEIGHT;

            // Precompute world coordinates
            float worldX[POINTS_X];
            float worldZ[POINTS_Z];

            for (int x = 0; x < POINTS_X; ++x) {
                worldX[x] = (startX + x * STEP) * freq + offset;
            }
            for (int z = 0; z < POINTS_Z; ++z) {
                worldZ[z] = (startZ + z * STEP) * freq + offset;
            }

            // Loops are reordered to optimize memory access patterns for the CPU.
            for (int x = 0; x < POINTS_X; ++x) {
                const float nx = worldX[x];

                for (int z = 0; z < POINTS_Z; ++z) {
                    const float nz = worldZ[z];

                    for (int y = 0; y < POINTS_Y; ++y) {
                        const int worldY = startY + y * STEP;

                        if (worldY > maxH + STEP || worldY < minH - STEP) [[unlikely]] {
                            storage[x][z][y] = -1.0f;
                            continue;
                        }

                        const float ny = worldY * freq + offset;
                        storage[x][z][y] = noise.eval(nx, ny, nz) * amp;
                    }
                }
            }
        }

        /**
         * @brief Interpolates a specific voxel value from the sparse grid.
         * 
         * Fused Multiply-Add (FMA) is used for better accuracy and performance.
         * 
         * @param localX Local X coordinate within the chunk.
         * @param localY Local Y coordinate within the chunk.
         * @param localZ Local Z coordinate within the chunk.
         * @return The interpolated noise value at the given local coordinates.
         */
        // Force inline for hot path
        [[gnu::always_inline]]
        [[nodiscard]] float get(const int localX, const int localY, const int localZ) const {
            const int x0 = localX / STEP;
            const int y0 = localY / STEP;
            const int z0 = localZ / STEP;

            // Use constexpr reciprocal for division
            constexpr float INV_STEP = 1.0f / STEP;
            const float tx = static_cast<float>(localX % STEP) * INV_STEP;
            const float ty = static_cast<float>(localY % STEP) * INV_STEP;
            const float tz = static_cast<float>(localZ % STEP) * INV_STEP;

            // Prefetch next cache line
            PREFETCH(&storage[x0 + 1][z0][y0], 0, 1);

            // Load corners
            const float c000 = storage[x0][z0][y0];
            const float c100 = storage[x0 + 1][z0][y0];
            const float c010 = storage[x0][z0 + 1][y0];
            const float c110 = storage[x0 + 1][z0 + 1][y0];
            const float c001 = storage[x0][z0][y0 + 1];
            const float c101 = storage[x0 + 1][z0][y0 + 1];
            const float c011 = storage[x0][z0 + 1][y0 + 1];
            const float c111 = storage[x0 + 1][z0 + 1][y0 + 1];

            // FMA operations if available
#if defined(__FMA__) || defined(__AVX2__)
            const float c00 = std::fma(c100 - c000, tx, c000);
            const float c01 = std::fma(c101 - c001, tx, c001);
            const float c10 = std::fma(c110 - c010, tx, c010);
            const float c11 = std::fma(c111 - c011, tx, c011);

            const float c0 = std::fma(c10 - c00, tz, c00);
            const float c1 = std::fma(c11 - c01, tz, c01);

            return std::fma(c1 - c0, ty, c0);
#else
            const float c00 = c000 + (c100 - c000) * tx;
            const float c01 = c001 + (c101 - c001) * tx;
            const float c10 = c010 + (c110 - c010) * tx;
            const float c11 = c011 + (c111 - c011) * tx;

            const float c0 = c00 + (c10 - c00) * tz;
            const float c1 = c01 + (c11 - c01) * tz;

            return c0 + (c1 - c0) * ty;
#endif
        }
    };

    /// Repeating terracotta layers for badlands, indexed by world Y.
    uint8_t terracottaBand(const int worldY) {
        const uint8_t bands[] = {
            Blocks::TERRACOTTA, Blocks::ORANGE_TERRACOTTA, Blocks::ORANGE_TERRACOTTA, Blocks::YELLOW_TERRACOTTA,
            Blocks::TERRACOTTA, Blocks::WHITE_TERRACOTTA, Blocks::RED_TERRACOTTA, Blocks::RED_TERRACOTTA,
            Blocks::TERRACOTTA, Blocks::BROWN_TERRACOTTA, Blocks::ORANGE_TERRACOTTA, Blocks::LIGHT_GRAY_TERRACOTTA,
            Blocks::TERRACOTTA, Blocks::YELLOW_TERRACOTTA, Blocks::RED_TERRACOTTA, Blocks::TERRACOTTA,
            Blocks::WHITE_TERRACOTTA, Blocks::BROWN_TERRACOTTA,
        };
        return bands[worldY % std::size(bands)];
    }
}

// ============================================================================
// MAIN GENERATION FUNCTION - 4-Pass Pipeline
// ============================================================================
/**
 * @brief The main function that generates a chunk in 4 different steps.
 */
void WorldGen::generateChunkData(const ChunkPos chunkPos, uint8_t *chunkData, const long seed) {
    /**
     * @struct NoiseContext
     * @brief thread_local storage ensures each thread has its own noise generators.
     * Generators are kept alive across calls to avoid initialization overhead.
     */
    thread_local struct NoiseContext {
        TerrainSampler terrain;
        OSN::Noise<3> noise3D;
        long cachedSeed = -1;

        /**
         * @brief Initializes noise generators if the seed has changed.
         * @param newSeed The current world seed.
         */
        void initIfNeeded(long newSeed) {
            if (cachedSeed != newSeed) [[unlikely]] {
                terrain = TerrainSampler(newSeed);
                noise3D = OSN::Noise<3>(newSeed + 3);
                cachedSeed = newSeed;
            }
        }
    } noise;

    noise.initIfNeeded(seed);

    // Precompute world coordinates
    // Precompute boundaries
    const int startX = chunkPos.x * CHUNK_WIDTH;
    const int startY = chunkPos.y * CHUNK_HEIGHT;
    const int startZ = chunkPos.z * CHUNK_WIDTH;
    const int endY = startY + CHUNK_HEIGHT - 1;

    // Early exit for out-of-bounds chunks
    // Culling: Chunks above space or below core are skipped
    if (startY >= MAX_HEIGHT || endY < 0) [[unlikely]] {
        std::memset(chunkData, 0, CHUNK_WIDTH * CHUNK_WIDTH * CHUNK_HEIGHT);
        return;
    }

    // -------------------------------------------------------------------------
    // PASS 1: Terrain height & biome per column
    // -------------------------------------------------------------------------
    // Heights are sampled with a 1-column border so slopes are correct at chunk edges.
    thread_local NoiseCache cache;
    thread_local ColumnSample samples[CHUNK_WIDTH][CHUNK_WIDTH];
    thread_local int16_t bordered[CHUNK_WIDTH + 2][CHUNK_WIDTH + 2];
    auto &registry = BiomeRegistry::getInstance();

    for (int x = -1; x <= static_cast<int>(CHUNK_WIDTH); ++x) {
        for (int z = -1; z <= static_cast<int>(CHUNK_WIDTH); ++z) {
            const ColumnSample s = noise.terrain.sample(static_cast<float>(startX + x), static_cast<float>(startZ + z));
            bordered[x + 1][z + 1] = static_cast<int16_t>(s.height);
            if (x >= 0 && z >= 0 && x < static_cast<int>(CHUNK_WIDTH) && z < static_cast<int>(CHUNK_WIDTH)) samples[x][z] = s;
        }
    }

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int h = bordered[x + 1][z + 1];
            const int slope = std::max({std::abs(bordered[x][z + 1] - h), std::abs(bordered[x + 2][z + 1] - h),
                                        std::abs(bordered[x + 1][z] - h), std::abs(bordered[x + 1][z + 2] - h)});
            cache.surfaceHeight[x][z] = static_cast<int16_t>(h);
            cache.slope[x][z] = static_cast<uint8_t>(std::min(slope, 255));
            cache.biomePtr[x][z] = &registry.getBiome(TerrainSampler::classify(samples[x][z], slope));
        }
    }

    // -------------------------------------------------------------------------
    // PASS 2: 3D Noise Preparation (Caves) - Optimized
    // -------------------------------------------------------------------------
    constexpr NoiseSettings caveSettings = {0.05f, 1.0f, 0, 0.5f, 0, MAX_HEIGHT - 10};
    constexpr int STEP = 4;
    thread_local TrilinearNoise<STEP> caveNoise;
    caveNoise.fill(noise.noise3D, startX, startY, startZ, caveSettings);

    // -------------------------------------------------------------------------
    // PASS 3: Volume Fill
    // -------------------------------------------------------------------------
    constexpr int STEEP_SLOPE = 4;
    constexpr int SNOW_LINE = 172;

    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int surfaceY = cache.surfaceHeight[x][z];
            const int slope = cache.slope[x][z];
            const Biome *biome = cache.biomePtr[x][z];
            const int baseIndex = x * CHUNK_WIDTH * CHUNK_HEIGHT + z * CHUNK_HEIGHT;
            const bool underwater = surfaceY < WATER_LEVEL;
            const bool steep = slope >= STEEP_SLOPE;
            const bool snowCapped = !underwater && surfaceY >= SNOW_LINE + static_cast<int>(fastHash(startX + x, startZ + z, seed, 99) % 6);

            uint8_t top = biome->surfaceBlock;
            uint8_t under = biome->underBlock;
            if (underwater) {
                top = biome->underwaterBlock;
            } else if (snowCapped) {
                top = Blocks::SNOW;
            } else if (steep) {
                top = under = biome->steepBlock;
            }

            if (z < CHUNK_WIDTH - 1) {
                PREFETCH(&chunkData[x * CHUNK_WIDTH * CHUNK_HEIGHT + (z + 1) * CHUNK_HEIGHT], 1, 1);
            }

            for (int y = 0; y < CHUNK_HEIGHT; ++y) {
                const int worldY = y + startY;
                const int index = baseIndex + y;

                if (worldY <= 0) [[unlikely]] {
                    chunkData[index] = Blocks::BEDROCK;
                    continue;
                }

                if (worldY > surfaceY) [[likely]] {
                    if (worldY > WATER_LEVEL) chunkData[index] = Blocks::AIR;
                    else chunkData[index] = (worldY == WATER_LEVEL && biome->frozenWater) ? Blocks::ICE : Blocks::WATER;
                    continue;
                }

                // Caves rarely breach the surface and never open into the sea floor.
                const int depth = surfaceY - worldY;
                const float surfaceSeal = underwater ? (depth < 6 ? 1.0f : 0.0f)
                                                     : 0.45f * (1.0f - terrain::smoothstep(0.0f, 10.0f, static_cast<float>(depth)));
                if (caveNoise.get(x, y, z) > caveSettings.chance + surfaceSeal) [[unlikely]] {
                    chunkData[index] = Blocks::AIR;
                    continue;
                }

                if (biome->terracottaBands && !underwater && depth < 24 && (depth > 0 || steep)) {
                    chunkData[index] = terracottaBand(worldY);
                } else if (depth == 0) {
                    chunkData[index] = top;
                } else {
                    chunkData[index] = (depth < biome->underDepth) ? under : Blocks::STONE;
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // PASS 4: Feature Pass (Vegetation & Ores)
    // -------------------------------------------------------------------------
    // Use faster RNG (xorshift64*)
    /**
     * @struct FastRNG
     * @brief State-minimal Xorshift64* generator.
     * 
     * Used for randomized feature placement within a chunk. Faster than mt19937 
     * and maintains better statistical qualities than simple LCGs.
     * Reference: https://en.wikipedia.org/wiki/Xorshift#xorshift*
     */
    struct FastRNG {
        uint64_t state;

        explicit FastRNG(uint64_t seed) : state(seed) {
        }

        [[gnu::always_inline]]
        uint64_t next() {
            state ^= state >> 12;
            state ^= state << 25;
            state ^= state >> 27;
            return state * 0x2545F4914F6CDD1DULL;
        }

        [[gnu::always_inline]]
        uint32_t nextInt(uint32_t bound) {
            return static_cast<uint32_t>(next()) % bound;
        }

        [[gnu::always_inline]]
        float nextFloat() {
            return static_cast<float>(next() & 0xFFFFFF) / 16777216.0f;
        }
    };

    FastRNG rng(seed ^ (static_cast<uint64_t>(chunkPos.x) * 341873128712ULL) ^
                (static_cast<uint64_t>(chunkPos.z) * 1328979838ULL));

    // 4.1 Surface Decorations (Optimized)
    for (int x = 0; x < CHUNK_WIDTH; ++x) {
        const int worldX = startX + x;

        for (int z = 0; z < CHUNK_WIDTH; ++z) {
            const int worldZ = startZ + z;
            const int surfaceY = cache.surfaceHeight[x][z];

            if (surfaceY < startY || surfaceY >= endY || surfaceY < WATER_LEVEL) [[unlikely]] {
                continue;
            }

            const Biome *biome = cache.biomePtr[x][z];
            const int localY = surfaceY - startY + 1;

            // Fast position-based hash for features
            for (size_t i = 0; i < biome->features.size(); ++i) {
                Feature *feature = biome->features[i];

                const uint64_t hash = fastHash(worldX, worldZ, seed, i);
                const float prob = static_cast<float>(hash & 0xFFFF) / 65536.0f;

                if (prob < feature->getSpawnChance()) [[unlikely]] {
                    if (feature->canPlace(chunkData, x, localY, z)) {
                        feature->place(chunkData, x, localY, z, worldX, worldZ, seed);
                    }
                }
            }
        }
    }

    // 4.2 Underground Features (Ores & Pockets)
    auto tryPlaceOre = [&](OreFeature *ore) {
        if (!ore) [[unlikely]] return;

        const int attempts = static_cast<int>(ore->getSpawnChance() * 1000);
        const int heightRange = ore->maxHeight - ore->minHeight + 1;

        for (int i = 0; i < attempts; ++i) {
            const int rx = rng.nextInt(CHUNK_WIDTH);
            const int rz = rng.nextInt(CHUNK_WIDTH);
            const int ry_world = ore->minHeight + rng.nextInt(heightRange);

            if (ry_world >= startY && ry_world < endY) [[likely]] {
                const int ry = ry_world - startY;
                if (ore->canPlace(chunkData, rx, ry, rz)) [[likely]] {
                    ore->place(chunkData, rx, ry, rz, startX + rx, startZ + rz, seed);
                }
            }
        }
    };

    auto tryPlacePocket = [&](LakeFeature *pocket) {
        if (!pocket) [[unlikely]] return;

        const int attempts = static_cast<int>(pocket->getSpawnChance() * 1000);
        constexpr int POCKET_HEIGHT_RANGE = 55; // 5 to 60

        for (int i = 0; i < attempts; ++i) {
            const int rx = rng.nextInt(CHUNK_WIDTH);
            const int rz = rng.nextInt(CHUNK_WIDTH);
            const int ry_world = 5 + rng.nextInt(POCKET_HEIGHT_RANGE);

            if (ry_world >= startY && ry_world < endY) [[likely]] {
                const int ry = ry_world - startY;
                if (pocket->canPlace(chunkData, rx, ry, rz)) [[likely]] {
                    pocket->place(chunkData, rx, ry, rz, startX + rx, startZ + rz, seed);
                }
            }
        }
    };

    // Batch ore placement calls
    tryPlaceOre(registry.getCoalOreFeature());
    tryPlaceOre(registry.getIronOreFeature());
    tryPlaceOre(registry.getGoldOreFeature());
    tryPlaceOre(registry.getDiamondOreFeature());
    tryPlaceOre(registry.getEmeraldOreFeature());

    tryPlacePocket(registry.getWaterPocketFeature());
    tryPlacePocket(registry.getLavaPocketFeature());

    // Large lava lakes (rare and deep)
    auto tryPlaceLavaLake = [&](LakeFeature *lake) {
        if (!lake) [[unlikely]] return;

        constexpr int LAKE_ATTEMPTS = 2;
        constexpr int LAKE_HEIGHT_RANGE = 10; // 5 to 15

        for (int i = 0; i < LAKE_ATTEMPTS; ++i) {
            const int rx = rng.nextInt(CHUNK_WIDTH);
            const int rz = rng.nextInt(CHUNK_WIDTH);
            const int ry_world = 5 + rng.nextInt(LAKE_HEIGHT_RANGE);

            if (ry_world >= startY && ry_world < endY) [[likely]] {
                const int ry = ry_world - startY;
                if (lake->canPlace(chunkData, rx, ry, rz)) [[likely]] {
                    lake->place(chunkData, rx, ry, rz, startX + rx, startZ + rz, seed);
                }
            }
        }
    };
    tryPlaceLavaLake(registry.getLavaLakeFeature());
}
