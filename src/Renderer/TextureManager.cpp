#include "headers/TextureManager.h"
#include "Renderer.h"
#include <cstring>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <queue>
#include <string_view>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace fs = std::filesystem;

/**
 * @file TextureManager.cpp
 * @brief This class manages loading all the textures for our voxel blocks.
 */

TextureManager& TextureManager::getInstance() {
    static TextureManager instance;
    return instance;
}

TextureManager::~TextureManager() = default;

uint16_t TextureManager::getLayerIndex(const std::string& textureName) const {
    const auto it = textureMap.find(textureName);
    if (it != textureMap.end()) [[likely]] {
        return it->second;
    }
    std::cerr << "Texture not found: " << textureName << ". Defaulting to 0." << std::endl;
    return 0;
}

TextureManager::TextureData TextureManager::loadPixels(const std::string& path, const std::string& name) {
    TextureData data;
    data.name = name;
    unsigned char* pixels = stbi_load(path.c_str(), &data.width, &data.height, &data.channels, 4);
    if (pixels) [[likely]] {
        const size_t size = static_cast<size_t>(data.width) * data.height * 4;
        data.pixels.resize(size);
        std::memcpy(data.pixels.data(), pixels, size);
        stbi_image_free(pixels);
    } else {
        std::cerr << "Failed to load texture: " << path << std::endl;
        data.width = 0; data.height = 0;
    }
    return data;
}

/**
 * @brief This function fixes a problem where dark borders appear around textures (texture bleeding).
 * 
 * Linear filtering can cause weird borders, so edge colors are spread
 * to adjacent transparent pixels to hide it.
 */
void fixTextureBleeding(std::vector<unsigned char>& pixels, const int width, const int height) {
    if (width <= 0 || height <= 0) [[unlikely]] return;

    const int totalPixels = width * height;
    std::vector sourceIdx(totalPixels, -1);
    std::queue<int> bfsQueue;

    // Initialize: seed BFS from all opaque pixels
    const unsigned char* alphaPtr = pixels.data() + 3;
    for (int i = 0; i < totalPixels; ++i, alphaPtr += 4) {
        if (*alphaPtr > 0) {
            sourceIdx[i] = i;
            bfsQueue.push(i);
        }
    }

    if (bfsQueue.empty()) [[unlikely]] return;

    // Checking the neighbors in 4 directions.
    const int offsets[] = {-1, 1, -width, width};

    while (!bfsQueue.empty()) {
        const int current = bfsQueue.front();
        bfsQueue.pop();

        const int cx = current % width;
        const int cy = current / width;
        const int srcIdx = sourceIdx[current];

        // Unrolled neighbor checking with bounds optimization
        for (int d = 0; d < 4; ++d) {
            int neighbor;
            bool inBounds;

            if (d < 2) {  // Horizontal neighbors
                inBounds = (d == 0) ? (cx >= 1) : (cx < width - 1);
                neighbor = current + offsets[d];
            } else {  // Vertical neighbors
                inBounds = (d == 2) ? (cy >= 1) : (cy < height - 1);
                neighbor = current + offsets[d];
            }

            if (inBounds && sourceIdx[neighbor] == -1) [[likely]] {
                sourceIdx[neighbor] = srcIdx;
                bfsQueue.push(neighbor);
            }
        }
    }

    // Apply colors with reduced branching
    unsigned char* pixelPtr = pixels.data();
    for (int i = 0; i < totalPixels; ++i, pixelPtr += 4) {
        if (pixelPtr[3] == 0) [[unlikely]] {
            const int srcI = sourceIdx[i];
            if (srcI != -1 && srcI != i) [[likely]] {
                const unsigned char* src = pixels.data() + srcI * 4;
                std::memcpy(pixelPtr, src, 3);  // Copy RGB only
            }
        }
    }
}

// This function resizes images if they are not the correct size for the array.
std::vector<unsigned char> TextureManager::upscaleImage(const std::vector<unsigned char>& src,
                                                         int srcW, int srcH, int dstW, int dstH) {
    if (srcW == dstW && srcH == dstH) [[unlikely]] return src;

    std::vector<unsigned char> dst(static_cast<size_t>(dstW) * dstH * 4);

    // Precompute both X and Y lookups
    std::vector<int> srcXLookup(dstW);
    std::vector<int> srcYLookup(dstH);

    const float xRatio = static_cast<float>(srcW) / dstW;
    const float yRatio = static_cast<float>(srcH) / dstH;

    for (int x = 0; x < dstW; ++x) {
        srcXLookup[x] = static_cast<int>(x * xRatio);
    }
    for (int y = 0; y < dstH; ++y) {
        srcYLookup[y] = static_cast<int>(y * yRatio);
    }

    const int srcStride = srcW * 4;
    const int dstStride = dstW * 4;
    const unsigned char* srcData = src.data();
    unsigned char* dstData = dst.data();

    for (int y = 0; y < dstH; ++y) {
        const unsigned char* srcRow = srcData + srcYLookup[y] * srcStride;
        unsigned char* dstRow = dstData + y * dstStride;

        for (int x = 0; x < dstW; ++x) {
            const unsigned char* srcPixel = srcRow + srcXLookup[x] * 4;
            unsigned char* dstPixel = dstRow + x * 4;
            // Copying 4 bytes of pixel data at once.
            *reinterpret_cast<uint32_t*>(dstPixel) = *reinterpret_cast<const uint32_t*>(srcPixel);
        }
    }
    return dst;
}

namespace {
    /**
     * Fixed-point math is used here to allow for integer bit shifts, which improves performance.
     */
    struct TintParams {
        uint32_t rMul, gMul, bMul;
    };

    /**
     * @brief Applies a color tint to a pixel buffer.
     * 
     * @param pixels The pixel buffer.
     * @param size Size of the buffer in bytes.
     * @param tint The tint parameters.
     * @param modifyAlpha Making sure the water isn't too transparent.
     */
    [[gnu::always_inline]]
    void applyTintImpl(unsigned char* pixels, const size_t size, const TintParams& tint, const bool modifyAlpha = false) {
        const unsigned char* end = pixels + size;
        for (unsigned char* p = pixels; p < end; p += 4) {
            if (p[3] > 0) [[likely]] {
                p[0] = static_cast<unsigned char>((p[0] * tint.rMul) >> 8);
                p[1] = static_cast<unsigned char>((p[1] * tint.gMul) >> 8);
                p[2] = static_cast<unsigned char>((p[2] * tint.bMul) >> 8);
                if (modifyAlpha && p[3] < 180) {
                    p[3] = 180;
                }
            }
        }
    }

    inline void applyTint(std::vector<unsigned char>& pixels, const TintParams& tint) {
        applyTintImpl(pixels.data(), pixels.size(), tint, false);
    }

    inline void applyWaterTint(std::vector<unsigned char>& pixels, const TintParams& tint) {
        applyTintImpl(pixels.data(), pixels.size(), tint, true);
    }

    /// Predefined green tint for grass and foliage.
    constexpr TintParams GREEN_TINT = {
        (145 * 256) / 255,
        (189 * 256) / 255,
        (89 * 256) / 255
    };

    /// Predefined blue tint for water.
    constexpr TintParams WATER_TINT = {
        (63 * 256) / 255,
        (118 * 256) / 255,
        (228 * 256) / 255
    };

    /**
     * @brief A hash function is used to help find textures quickly by their names.
     */
    constexpr uint32_t hashString(std::string_view sv) {
        uint32_t hash = 5381;
        for (char c : sv) {
            hash = ((hash << 5) + hash) + static_cast<unsigned char>(c);
        }
        return hash;
    }

    /**
     * @brief Determines if a texture name refers to a "foliage" block that needs green tinting.
     */
    inline bool needsGreenTint(std::string_view name) {
        // Fast path: check first character
        if (name.empty()) [[unlikely]] return false;

        const char first = name[0];
        if (first != 'g' && first != 's' && first != 't' && first != 'o') {
            return false;
        }

        // Use constexpr hashes for compile-time optimization
        constexpr uint32_t GRASS_BLOCK_TOP = hashString("grass_block_top");
        constexpr uint32_t SHORT_GRASS = hashString("short_grass");
        constexpr uint32_t TALL_GRASS_BOTTOM = hashString("tall_grass_bottom");
        constexpr uint32_t TALL_GRASS_TOP = hashString("tall_grass_top");
        constexpr uint32_t OAK_LEAVES = hashString("oak_leaves");

        const uint32_t hash = hashString(name);
        return hash == GRASS_BLOCK_TOP || hash == SHORT_GRASS ||
               hash == TALL_GRASS_BOTTOM || hash == TALL_GRASS_TOP ||
               hash == OAK_LEAVES;
    }

    /**
     * @brief Checks if a texture is a fluid (water or lava).
     * Fluids are special because they are often animated and need specific frame counts.
     */
    inline bool isFluidTexture(std::string_view name) {
        return name == "water_still" || name == "lava_still";
    }

    /**
     * @brief Checks if a file extension is supported by the manager.
     */
    inline bool isValidTextureExtension(std::string_view ext) {
        return ext == ".png" || ext == ".jpg" || ext == ".tga";
    }
}

void TextureManager::loadTextures(const std::string& directoryPath) {
    std::vector<TextureData> rawTextures;
    int maxWidth = 0;
    int maxHeight = 0;

    stbi_set_flip_vertically_on_load(true);

    if (!fs::exists(directoryPath)) [[unlikely]] {
        std::cerr << "Texture directory does not exist: " << directoryPath << std::endl;
        return;
    }

    rawTextures.reserve(64);

    // 1. Look through the folder and load all the images.
    for (const auto& entry : fs::directory_iterator(directoryPath)) {
        if (!entry.is_regular_file()) continue;

        const std::string ext = entry.path().extension().string();
        if (!isValidTextureExtension(ext)) continue;

        std::string name = entry.path().stem().string();
        TextureData data = loadPixels(entry.path().string(), name);

        if (data.width > 0) [[likely]] {
            maxWidth = std::max(maxWidth, data.width);
            int frameHeight = (data.height > data.width) ? data.width : data.height;
            maxHeight = std::max(maxHeight, frameHeight);
            rawTextures.emplace_back(std::move(data));
        }
    }

    if (rawTextures.empty()) [[unlikely]] {
        std::cerr << "No textures found in " << directoryPath << std::endl;
        return;
    }

    const int arrayWidth = std::max(maxWidth, maxHeight);
    const int arrayHeight = arrayWidth;

    // 2. Figure out how many layers we need (including animations).
    size_t layerCount = 0;
    for (const auto& tex : rawTextures) {
        int frames = (tex.height > tex.width && tex.height % tex.width == 0)
                     ? (tex.height / tex.width) : 1;

        // Fluid textures are padded/looped to 32 frames to match shader expectations
        if (isFluidTexture(tex.name)) {
            frames = 32;
        }
        layerCount += frames;
    }

    const size_t layerSize = static_cast<size_t>(arrayWidth) * arrayHeight * 4;

    // 3. Pre-allocate intermediate buffer
    std::vector<unsigned char> textureArrayBuffer(layerSize * layerCount);
    unsigned char* bufferPtr = textureArrayBuffer.data();

    // Sorting the textures so the order is always the same.
    std::sort(rawTextures.begin(), rawTextures.end(),
              [](const TextureData& a, const TextureData& b) { return a.name < b.name; });

    uint16_t currentLayer = 0;
    for (auto& tex : rawTextures) {
        std::vector<unsigned char>& currentPixels = tex.pixels;

        // Apply Tints
        const std::string_view nameView(tex.name);
        if (needsGreenTint(nameView)) {
            applyTint(currentPixels, GREEN_TINT);
            std::cout << "Applied Green Tint to: " << tex.name << std::endl;
        }
        else if (nameView == "water_still") {
            applyWaterTint(currentPixels, WATER_TINT);
            std::cout << "Applied Blue Tint to: " << tex.name << std::endl;
        }

        // Fix Bleeding
        fixTextureBleeding(currentPixels, tex.width, tex.height);

        // Calculate animated frames
        const int sourceFrames = (tex.height > tex.width && tex.height % tex.width == 0)
                                 ? (tex.height / tex.width) : 1;
        const int targetFrames = isFluidTexture(nameView) ? 32 : sourceFrames;
        const int srcFrameHeight = tex.width;
        const size_t srcFrameSize = static_cast<size_t>(tex.width) * srcFrameHeight * 4;

        // Process frames individually
        for (int f = 0; f < targetFrames; ++f) {
            const int sourceFrameIndex = f % sourceFrames; // Loop frames if targetCount > sourceCount

            std::vector<unsigned char> framePixels;

            if (sourceFrames > 1) {
                // Extract single frame from strip
                const size_t offset = sourceFrameIndex * srcFrameSize;
                framePixels.assign(currentPixels.begin() + offset,
                                  currentPixels.begin() + offset + srcFrameSize);
            } else {
                // Single frame source
                framePixels = (targetFrames == 1) ? std::move(currentPixels) : currentPixels;
            }

            // Upscale to match array dimension and copy to CPU buffer
            if (tex.width != arrayWidth || srcFrameHeight != arrayHeight) {
                std::vector<unsigned char> scaled = upscaleImage(framePixels, tex.width,
                                                                 srcFrameHeight, arrayWidth, arrayHeight);
                std::memcpy(bufferPtr, scaled.data(), layerSize);
            } else {
                std::memcpy(bufferPtr, framePixels.data(), layerSize);
            }

            bufferPtr += layerSize;
        }

        textureMap[tex.name] = currentLayer;
        std::cout << "Loaded Texture: " << tex.name << " -> Layer " << currentLayer
                  << " (Source: " << sourceFrames << ", Target: " << targetFrames << " frames)" << std::endl;
        currentLayer += targetFrames;
    }

    // 4. Upload to the GPU (mipmaps are generated there).
    layerColors.assign(layerCount, {0, 0, 0});
    for (size_t layer = 0; layer < layerCount; ++layer) {
        const unsigned char *texels = textureArrayBuffer.data() + layer * layerSize;
        uint64_t sum[3] = {}, weight = 0;
        for (size_t i = 0; i < layerSize; i += 4) {
            const uint32_t a = texels[i + 3];
            for (int c = 0; c < 3; ++c) sum[c] += static_cast<uint64_t>(texels[i + c]) * a;
            weight += a;
        }
        if (weight > 0) {
            for (int c = 0; c < 3; ++c) layerColors[layer][c] = static_cast<uint8_t>(sum[c] / weight);
        }
    }

    Renderer::get().setTextureArray(static_cast<uint32_t>(arrayWidth), static_cast<uint32_t>(arrayHeight),
                                    static_cast<uint32_t>(layerCount), textureArrayBuffer.data());

    std::cout << "Texture Array Generated. Total Size: " << arrayWidth << "x" << arrayHeight
              << " | Total Layers: " << layerCount << std::endl;
}
