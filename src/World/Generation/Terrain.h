/**
 * @file Terrain.h
 * @brief Continuous terrain shaping and biome classification.
 *
 * Height is a continuous function of a few climate-independent noise fields
 * (continentalness, erosion, ridges, rivers), so neighbouring columns can never
 * jump by more than the noise allows. Biomes are picked *from* the terrain and
 * climate afterwards and only decide surface blocks and decorations.
 * This mirrors the approach Minecraft 1.18+ uses:
 * https://minecraft.wiki/w/World_generation#Overworld
 *
 * Pure header: no OpenGL or registry dependencies, so tools can sample it directly.
 */
#pragma once

#include <OpenSimplexNoise.hh>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "../headers/WorldConstants.h"
#include "BiomeID.h"


struct ColumnSample {
    float height;      ///< Surface height in blocks.
    float temperature; ///< 0 (frozen) .. 1 (hot), already cooled by altitude.
    float humidity;    ///< 0 (arid) .. 1 (wet).
    float continental; ///< -1 (deep ocean) .. 1 (far inland).
    float river;       ///< 0 .. 1, 1 in the river channel.
};

namespace terrain {
    inline float smoothstep(const float e0, const float e1, const float x) {
        const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float lerp(const float a, const float b, const float t) { return a + (b - a) * t; }

    /// Piecewise-linear curve through (xs[i], ys[i]); xs must be ascending.
    template<size_t N>
    float spline(const float (&xs)[N], const float (&ys)[N], const float x) {
        if (x <= xs[0]) return ys[0];
        for (size_t i = 1; i < N; ++i) {
            if (x <= xs[i]) return lerp(ys[i - 1], ys[i], (x - xs[i - 1]) / (xs[i] - xs[i - 1]));
        }
        return ys[N - 1];
    }
}

class TerrainSampler {
public:
    explicit TerrainSampler(const long seed = 0)
        : continentalNoise(seed + 10), erosionNoise(seed + 11), ridgeNoise(seed + 12),
          temperatureNoise(seed + 13), humidityNoise(seed + 14), detailNoise(seed + 15),
          riverNoise(seed + 16), mesaNoise(seed + 17), jitterNoise(seed + 18) {}

    [[nodiscard]] ColumnSample sample(const float x, const float z) const {
        using namespace terrain;
        const float water = static_cast<float>(WATER_LEVEL);

        const float c = fbm(continentalNoise, x, z, 1.0f / 1400.0f, 3);
        const float e = fbm(erosionNoise, x, z, 1.0f / 800.0f, 2);
        const float ridge = 1.0f - std::abs(fbm(ridgeNoise, x, z, 1.0f / 380.0f, 2));
        const float detail = fbm(detailNoise, x, z, 1.0f / 90.0f, 2);
        const float t = fbm(temperatureNoise, x, z, 1.0f / 1100.0f, 2) * 0.5f + 0.5f;
        const float hu = fbm(humidityNoise, x, z, 1.0f / 950.0f, 2) * 0.5f + 0.5f;

        // Coastline and ocean depth come only from continentalness.
        static constexpr float CX[] = {-1.0f, -0.65f, -0.42f, -0.28f, -0.17f, -0.09f, 0.22f, 1.0f};
        static constexpr float CY[] = {30.0f, 36.0f, 48.0f, 57.0f, 63.0f, 66.0f, 74.0f, 92.0f};
        float h = spline(CX, CY, c);

        // Mountains need low erosion and must be inland, so they rise gradually from the coast.
        const float mountain = smoothstep(-0.1f, 0.3f, c) * smoothstep(0.15f, -0.45f, e);
        h += (std::pow(ridge, 2.2f) * 125.0f + ridge * 15.0f) * mountain;

        // Rolling hills everywhere on land; rougher where erosion is low.
        h += detail * (3.0f + 12.0f * smoothstep(0.5f, -0.3f, e)) * smoothstep(-0.23f, -0.09f, c);

        // Wet, warm lowlands flatten towards the water table (swamps).
        const float wet = smoothstep(0.62f, 0.8f, hu) * smoothstep(0.5f, 0.6f, t) * smoothstep(0.8f, 0.6f, t);
        h = lerp(h, water + 1.0f + (h - water) * 0.15f, wet * (1.0f - mountain));

        // Hot, dry badlands get flat-topped mesas.
        const float mesa = smoothstep(0.72f, 0.8f, t) * smoothstep(0.42f, 0.32f, hu) * smoothstep(-0.05f, 0.1f, c);
        h += smoothstep(0.15f, 0.3f, fbm(mesaNoise, x, z, 1.0f / 160.0f, 1)) * 26.0f * mesa;

        // Rivers carve a valley and a channel, fading out at the coast and in the mountains.
        const float rv = std::abs(fbm(riverNoise, x, z, 1.0f / 700.0f, 2));
        const float riverAllowed = smoothstep(-0.17f, -0.07f, c) * (1.0f - smoothstep(0.25f, 0.55f, mountain));
        const float valley = (1.0f - smoothstep(0.0f, 0.07f, rv)) * riverAllowed;
        const float channel = (1.0f - smoothstep(0.012f, 0.03f, rv)) * riverAllowed;
        if (h > water - 4.0f) {
            h = lerp(h, std::min(h, water + 1.0f + (h - water) * 0.3f), valley);
            h = lerp(h, water - 4.0f, channel);
        }

        h = std::clamp(h, 4.0f, static_cast<float>(MAX_HEIGHT - 24));

        // Air gets colder with altitude, so peaks snow over regardless of the climate below.
        // Small high-frequency jitter keeps biome borders from tracing smooth contour lines.
        const float jitter = fbm(jitterNoise, x, z, 1.0f / 24.0f, 2) * 0.03f;
        const float temperature = std::clamp(t + jitter - std::max(0.0f, h - 95.0f) / 220.0f, 0.0f, 1.0f);
        return {h, temperature, std::clamp(hu - jitter, 0.0f, 1.0f), c, channel};
    }

    /// @param slope Largest height difference to a direct neighbour column.
    [[nodiscard]] static BiomeID classify(const ColumnSample &s, const int slope) {
        const float water = static_cast<float>(WATER_LEVEL);
        const float t = s.temperature;
        const float hu = s.humidity;
        const bool frozen = t < 0.24f;

        if (s.river > 0.5f && s.height < water) return frozen ? BiomeID::FROZEN_RIVER : BiomeID::RIVER;
        if (s.height < water - 1.0f) {
            if (frozen) return BiomeID::FROZEN_OCEAN;
            if (s.height < water - 18.0f) return BiomeID::DEEP_OCEAN;
            return t > 0.68f ? BiomeID::WARM_OCEAN : BiomeID::OCEAN;
        }
        if (s.height <= water + 2.0f && s.continental < -0.05f) {
            if (slope >= 3) return BiomeID::STONY_SHORE;
            return frozen ? BiomeID::SNOWY_BEACH : BiomeID::BEACH;
        }
        if (s.height > 160.0f) return t < 0.4f ? BiomeID::SNOWY_PEAKS : BiomeID::STONY_PEAKS;
        if (s.height > 118.0f) return frozen ? BiomeID::SNOWY_PEAKS : BiomeID::WINDSWEPT_HILLS;

        if (frozen) return hu < 0.45f ? BiomeID::SNOWY_PLAINS : BiomeID::SNOWY_TAIGA;
        if (t < 0.38f) return hu < 0.35f ? BiomeID::PLAINS : BiomeID::TAIGA;
        if (t < 0.58f) {
            if (hu < 0.32f) return BiomeID::PLAINS;
            if (hu < 0.45f) return BiomeID::FLOWER_PLAINS;
            if (hu < 0.58f) return BiomeID::FOREST;
            if (hu < 0.68f) return BiomeID::BIRCH_FOREST;
            return BiomeID::DARK_FOREST;
        }
        if (t < 0.72f) {
            if (hu < 0.35f) return BiomeID::SAVANNA;
            if (hu < 0.5f) return BiomeID::PLAINS;
            if (hu < 0.64f) return BiomeID::FOREST;
            return s.height < water + 6.0f ? BiomeID::SWAMP : BiomeID::JUNGLE;
        }
        if (hu < 0.32f) return BiomeID::BADLANDS;
        if (hu < 0.45f) return BiomeID::DESERT;
        if (hu < 0.6f) return BiomeID::SAVANNA;
        return BiomeID::JUNGLE;
    }

private:
    OSN::Noise<2> continentalNoise, erosionNoise, ridgeNoise, temperatureNoise,
                  humidityNoise, detailNoise, riverNoise, mesaNoise, jitterNoise;

    /// Fractal noise normalised to roughly [-1, 1]; raw simplex rarely leaves [-0.6, 0.6].
    static float fbm(const OSN::Noise<2> &n, const float x, const float z, const float freq, const int octaves) {
        float sum = 0.0f, amp = 1.0f, norm = 0.0f, f = freq;
        for (int i = 0; i < octaves; ++i) {
            sum += n.eval(x * f, z * f) * amp;
            norm += amp;
            amp *= 0.5f;
            f *= 2.0f;
        }
        return std::clamp(sum / norm * NOISE_STRETCH, -1.0f, 1.0f);
    }

    static constexpr float NOISE_STRETCH = 1.8f;
};
