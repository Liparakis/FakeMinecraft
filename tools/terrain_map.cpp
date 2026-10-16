// Renders a top-down biome/height map of the terrain generator and prints coverage stats.
// Build (MSVC): cl /std:c++20 /O2 /EHsc /I dependencies/include /I src tools/terrain_map.cpp
// Usage: terrain_map [seed] [size] [out.ppm]
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "World/Generation/Terrain.h"

static constexpr const char *NAMES[] = {
    "ocean", "deep_ocean", "frozen_ocean", "warm_ocean", "river", "frozen_river",
    "beach", "snowy_beach", "stony_shore", "plains", "flower_plains", "snowy_plains",
    "forest", "birch_forest", "dark_forest", "taiga", "snowy_taiga",
    "desert", "savanna", "badlands", "swamp", "jungle",
    "windswept_hills", "snowy_peaks", "stony_peaks",
};
static constexpr unsigned char COLORS[][3] = {
    {30, 60, 170}, {15, 30, 110}, {140, 160, 220}, {30, 120, 190}, {60, 110, 240}, {160, 180, 240},
    {230, 220, 150}, {240, 240, 220}, {130, 130, 130}, {120, 190, 80}, {200, 170, 210}, {235, 245, 250},
    {40, 120, 40}, {120, 170, 90}, {30, 70, 25}, {50, 100, 80}, {200, 225, 220},
    {240, 200, 110}, {190, 180, 80}, {200, 90, 40},
    {70, 90, 50}, {30, 170, 20},
    {110, 130, 110}, {255, 255, 255}, {150, 150, 150},
};
static_assert(std::size(NAMES) == static_cast<size_t>(BiomeID::COUNT));
static_assert(std::size(COLORS) == static_cast<size_t>(BiomeID::COUNT));

int main(int argc, char **argv) {
    const long seed = argc > 1 ? std::atol(argv[1]) : 0;
    const int size = argc > 2 ? std::atoi(argv[2]) : 4096;
    const char *out = argc > 3 ? argv[3] : "terrain_map.ppm";
    const int px = std::min(1024, size), step = size / px;

    const TerrainSampler sampler(seed);
    std::vector<float> h(static_cast<size_t>(size + 1) * (size + 1));
    for (int z = 0; z <= size; ++z)
        for (int x = 0; x <= size; ++x)
            h[static_cast<size_t>(z) * (size + 1) + x] = sampler.sample(static_cast<float>(x - size / 2), static_cast<float>(z - size / 2)).height;
    auto H = [&](int x, int z) { return static_cast<int>(h[static_cast<size_t>(z) * (size + 1) + x]); };

    long counts[static_cast<size_t>(BiomeID::COUNT)] = {};
    int maxSlope = 0;
    long steep = 0, landColumns = 0;
    std::vector<unsigned char> img(static_cast<size_t>(px) * px * 3);
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            const int slope = std::max(std::abs(H(x + 1, z) - H(x, z)), std::abs(H(x, z + 1) - H(x, z)));
            const ColumnSample s = sampler.sample(static_cast<float>(x - size / 2), static_cast<float>(z - size / 2));
            const auto b = static_cast<size_t>(TerrainSampler::classify(s, slope));
            ++counts[b];
            if (s.height >= WATER_LEVEL) {
                ++landColumns;
                maxSlope = std::max(maxSlope, slope);
                steep += slope >= 6;
            }
            if (x % step == 0 && z % step == 0 && x / step < px && z / step < px) {
                const float shade = std::clamp(0.55f + (s.height - WATER_LEVEL) / 160.0f, 0.35f, 1.25f);
                unsigned char *p = &img[(static_cast<size_t>(z / step) * px + x / step) * 3];
                for (int i = 0; i < 3; ++i) p[i] = static_cast<unsigned char>(std::min(255.0f, COLORS[b][i] * shade));
            }
        }
    }

    const long total = static_cast<long>(size) * size;
    for (size_t i = 0; i < std::size(NAMES); ++i)
        std::printf("%-16s %6.2f%%\n", NAMES[i], 100.0 * counts[i] / total);
    std::printf("max land slope %d, columns with slope>=6: %.3f%% of land\n", maxSlope, 100.0 * steep / std::max(1L, landColumns));

    FILE *f = std::fopen(out, "wb");
    std::fprintf(f, "P6 %d %d 255\n", px, px);
    std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
    return 0;
}
