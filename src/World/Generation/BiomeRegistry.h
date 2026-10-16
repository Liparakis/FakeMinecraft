#pragma once

#include <array>
#include <cstdio>
#include <memory>
#include <vector>
#include "Biome.h"
#include "TreeFeature.h"
#include "BigTreeFeature.h"
#include "ConiferTreeFeature.h"
#include "CactusFeature.h"
#include "OreFeature.h"
#include "VegetationFeature.h"
#include "LakeFeature.h"
#include "../headers/Blocks.h"

/**
 * @class BiomeRegistry
 * @brief Owns every biome definition and the features they place.
 *
 * Which biome a column gets is decided by TerrainSampler::classify (Terrain.h).
 */
class BiomeRegistry {
public:
    static BiomeRegistry& getInstance() noexcept {
        static BiomeRegistry instance;
        return instance;
    }

    [[nodiscard]] const Biome& getBiome(const BiomeID id) const noexcept {
        return biomes[static_cast<size_t>(id)];
    }

    BiomeRegistry(const BiomeRegistry&) = delete;
    BiomeRegistry& operator=(const BiomeRegistry&) = delete;

    [[nodiscard]] OreFeature* getCoalOreFeature() const noexcept { return coalOre; }
    [[nodiscard]] OreFeature* getIronOreFeature() const noexcept { return ironOre; }
    [[nodiscard]] OreFeature* getGoldOreFeature() const noexcept { return goldOre; }
    [[nodiscard]] OreFeature* getDiamondOreFeature() const noexcept { return diamondOre; }
    [[nodiscard]] OreFeature* getEmeraldOreFeature() const noexcept { return emeraldOre; }
    [[nodiscard]] LakeFeature* getWaterPocketFeature() const noexcept { return waterPocket; }
    [[nodiscard]] LakeFeature* getLavaPocketFeature() const noexcept { return lavaPocket; }
    [[nodiscard]] LakeFeature* getLavaLakeFeature() const noexcept { return lavaLake; }

    /// Must run after Blocks::init(), since features capture block IDs.
    void init() {
        if (initialized) return;
        initialized = true;
        using namespace Blocks;

        coalOre = make<OreFeature>(COAL_ORE, STONE, 5, 128, 12, 0.15f);
        ironOre = make<OreFeature>(IRON_ORE, STONE, 5, 64, 8, 0.08f);
        goldOre = make<OreFeature>(GOLD_ORE, STONE, 1, 32, 6, 0.02f);
        diamondOre = make<OreFeature>(DIAMOND_ORE, STONE, 1, 15, 4, 0.005f);
        emeraldOre = make<OreFeature>(EMERALD_ORE, STONE, 1, 48, 2, 0.002f);
        waterPocket = make<LakeFeature>(WATER, STONE, 3, 0.002f);
        lavaPocket = make<LakeFeature>(LAVA, STONE, 4, 0.0002f);
        lavaLake = make<LakeFeature>(LAVA, STONE, 7, 0.00005f);

        auto plant = [&](uint8_t block, uint8_t soil, float chance, uint8_t top = 0) {
            return make<VegetationFeature>(block, soil, chance, top);
        };
        auto meadow = [&](Biome& b, uint8_t soil, float flowerChance) {
            for (uint8_t flower : {POPPY, DANDELION, CORNFLOWER, AZURE_BLUET, WHITE_TULIP, PINK_TULIP, ORANGE_TULIP})
                b.features.push_back(plant(flower, soil, flowerChance));
        };

        define(BiomeID::OCEAN, "Ocean", GRAVEL, GRAVEL, GRAVEL);
        define(BiomeID::DEEP_OCEAN, "Deep Ocean", GRAVEL, GRAVEL, GRAVEL);
        define(BiomeID::FROZEN_OCEAN, "Frozen Ocean", GRAVEL, GRAVEL, GRAVEL).frozenWater = true;
        define(BiomeID::WARM_OCEAN, "Warm Ocean", SAND, SAND, SAND);
        define(BiomeID::RIVER, "River", SAND, SAND, SAND);
        define(BiomeID::FROZEN_RIVER, "Frozen River", SAND, SAND, GRAVEL).frozenWater = true;

        define(BiomeID::BEACH, "Beach", SAND, SAND, SAND, SANDSTONE).underDepth = 4;
        define(BiomeID::SNOWY_BEACH, "Snowy Beach", SNOW, SAND, SAND, SANDSTONE).frozenWater = true;
        define(BiomeID::STONY_SHORE, "Stony Shore", STONE, STONE, GRAVEL);

        Biome& plains = define(BiomeID::PLAINS, "Plains", GRASS_BLOCK, DIRT, DIRT);
        plains.features = {make<TreeFeature>(OAK_LOG, OAK_LEAVES, 4, 6, 2, 0.002f),
                           plant(TALL_GRASS_BOTTOM, GRASS_BLOCK, 0.04f, TALL_GRASS_TOP)};
        meadow(plains, GRASS_BLOCK, 0.012f);
        plains.features.push_back(plant(GRASS, GRASS_BLOCK, 0.4f));

        Biome& flowers = define(BiomeID::FLOWER_PLAINS, "Flower Plains", GRASS_BLOCK, DIRT, DIRT);
        flowers.features = {make<BigTreeFeature>(OAK_LOG, OAK_LEAVES, 0.0015f)};
        meadow(flowers, GRASS_BLOCK, 0.06f);
        flowers.features.push_back(plant(GRASS, GRASS_BLOCK, 0.3f));

        Biome& snowyPlains = define(BiomeID::SNOWY_PLAINS, "Snowy Plains", SNOWY_GRASS_BLOCK, DIRT, DIRT);
        snowyPlains.frozenWater = true;
        snowyPlains.features = {make<ConiferTreeFeature>(SPRUCE_LOG, SPRUCE_LEAVES, 6, 9, 0.003f)};

        Biome& forest = define(BiomeID::FOREST, "Forest", GRASS_BLOCK, DIRT, DIRT);
        forest.features = {make<LakeFeature>(WATER, GRASS_BLOCK, 4, 0.001f),
                           make<BigTreeFeature>(OAK_LOG, OAK_LEAVES, 0.03f),
                           make<TreeFeature>(BIRCH_LOG, BIRCH_LEAVES, 5, 7, 2, 0.02f),
                           make<TreeFeature>(OAK_LOG, OAK_LEAVES, 5, 8, 3, 0.12f),
                           plant(WHITE_TULIP, GRASS_BLOCK, 0.03f),
                           plant(POPPY, GRASS_BLOCK, 0.03f),
                           plant(GRASS, GRASS_BLOCK, 0.25f)};

        Biome& birch = define(BiomeID::BIRCH_FOREST, "Birch Forest", GRASS_BLOCK, DIRT, DIRT);
        birch.features = {make<TreeFeature>(BIRCH_LOG, BIRCH_LEAVES, 5, 8, 2, 0.1f)};
        meadow(birch, GRASS_BLOCK, 0.01f);
        birch.features.push_back(plant(GRASS, GRASS_BLOCK, 0.3f));

        Biome& dark = define(BiomeID::DARK_FOREST, "Dark Forest", GRASS_BLOCK, DIRT, DIRT);
        dark.features = {make<BigTreeFeature>(DARK_OAK_LOG, DARK_OAK_LEAVES, 0.06f),
                         make<TreeFeature>(DARK_OAK_LOG, DARK_OAK_LEAVES, 4, 6, 3, 0.25f),
                         plant(GRASS, GRASS_BLOCK, 0.2f)};

        Biome& taiga = define(BiomeID::TAIGA, "Taiga", COLD_GRASS_BLOCK, DIRT, DIRT);
        taiga.features = {make<ConiferTreeFeature>(SPRUCE_LOG, SPRUCE_LEAVES, 7, 12, 0.07f),
                          plant(FERN, COLD_GRASS_BLOCK, 0.12f),
                          plant(GRASS, COLD_GRASS_BLOCK, 0.15f)};

        Biome& snowyTaiga = define(BiomeID::SNOWY_TAIGA, "Snowy Taiga", SNOWY_GRASS_BLOCK, DIRT, DIRT);
        snowyTaiga.frozenWater = true;
        snowyTaiga.features = {make<ConiferTreeFeature>(SPRUCE_LOG, SPRUCE_LEAVES, 7, 11, 0.05f)};

        Biome& desert = define(BiomeID::DESERT, "Desert", SAND, SAND, SAND, SANDSTONE);
        desert.underDepth = 5;
        desert.features = {make<LakeFeature>(LAVA, SAND, 5, 0.0005f),
                           make<CactusFeature>(0.006f),
                           plant(DEAD_BUSH, SAND, 0.005f),
                           plant(DRY_SHRUB, SAND, 0.004f)};

        Biome& savanna = define(BiomeID::SAVANNA, "Savanna", DRY_GRASS_BLOCK, DIRT, DIRT);
        savanna.features = {make<TreeFeature>(ACACIA_LOG, ACACIA_LEAVES, 4, 6, 3, 0.006f),
                            plant(DRY_SHRUB, DRY_GRASS_BLOCK, 0.12f),
                            plant(GRASS, DRY_GRASS_BLOCK, 0.25f)};

        Biome& badlands = define(BiomeID::BADLANDS, "Badlands", RED_SAND, RED_SAND, RED_SAND, TERRACOTTA);
        badlands.terracottaBands = true;
        badlands.underDepth = 2;
        badlands.features = {make<CactusFeature>(0.002f), plant(DEAD_BUSH, RED_SAND, 0.01f)};

        Biome& swamp = define(BiomeID::SWAMP, "Swamp", SWAMP_GRASS_BLOCK, DIRT, DIRT);
        swamp.features = {make<TreeFeature>(OAK_LOG, OAK_LEAVES, 5, 7, 3, 0.02f),
                          plant(BLUE_ORCHID, SWAMP_GRASS_BLOCK, 0.03f),
                          plant(GRASS, SWAMP_GRASS_BLOCK, 0.3f)};

        Biome& jungle = define(BiomeID::JUNGLE, "Jungle", LUSH_GRASS_BLOCK, DIRT, DIRT);
        jungle.features = {make<BigTreeFeature>(JUNGLE_LOG, JUNGLE_LEAVES, 0.04f),
                           make<TreeFeature>(JUNGLE_LOG, JUNGLE_LEAVES, 4, 9, 3, 0.12f),
                           plant(FERN, LUSH_GRASS_BLOCK, 0.15f),
                           plant(TALL_GRASS_BOTTOM, LUSH_GRASS_BLOCK, 0.08f, TALL_GRASS_TOP),
                           plant(GRASS, LUSH_GRASS_BLOCK, 0.35f)};

        Biome& hills = define(BiomeID::WINDSWEPT_HILLS, "Windswept Hills", GRASS_BLOCK, DIRT, GRAVEL);
        hills.features = {make<ConiferTreeFeature>(SPRUCE_LOG, SPRUCE_LEAVES, 6, 10, 0.01f),
                          make<LakeFeature>(LAVA, STONE, 6, 0.001f),
                          plant(GRASS, GRASS_BLOCK, 0.1f)};

        define(BiomeID::SNOWY_PEAKS, "Snowy Peaks", SNOW, SNOW, PACKED_ICE).frozenWater = true;
        define(BiomeID::STONY_PEAKS, "Stony Peaks", STONE, STONE, GRAVEL);
    }

private:
    BiomeRegistry() = default;

    bool initialized = false;
    std::array<Biome, static_cast<size_t>(BiomeID::COUNT)> biomes{};
    std::vector<std::unique_ptr<Feature>> ownedFeatures;

    OreFeature *coalOre = nullptr, *ironOre = nullptr, *goldOre = nullptr, *diamondOre = nullptr, *emeraldOre = nullptr;
    LakeFeature *waterPocket = nullptr, *lavaPocket = nullptr, *lavaLake = nullptr;

    template<class T, class... Args>
    T* make(Args&&... args) {
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T* raw = owned.get();
        ownedFeatures.push_back(std::move(owned));
        return raw;
    }

    Biome& define(const BiomeID id, const char* name, const uint8_t surface, const uint8_t under,
                  const uint8_t underwater, const uint8_t steep = Blocks::STONE) {
        Biome& b = biomes[static_cast<size_t>(id)];
        b.id = id;
        snprintf(b.name, sizeof(b.name), "%s", name);
        b.surfaceBlock = surface;
        b.underBlock = under;
        b.underwaterBlock = underwater;
        b.steepBlock = steep;
        return b;
    }
};
