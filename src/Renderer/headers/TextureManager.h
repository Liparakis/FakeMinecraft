#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <cstdint>

/**
 * @brief Manages the game's textures as one texture array.
 * 
 * TextureManager loads image files from the disk, processes them (like adding
 * tints or resizing them), and sends them to the GPU. 
 */
class TextureManager {
public:
    /**
     * @brief Gets the singleton instance of the TextureManager.
     * @return Reference to the TextureManager instance.
     */
    static TextureManager& getInstance();

    /**
     * @brief Scans a directory for textures, processes them, and builds the 2D Texture Array.
     * 
     * This method:
     * - Finds valid texture files (.png, .jpg, .tga).
     * - Adds tints to certain blocks (like leaves or water).
     * - Fixes issues where the edges of textures look weird.
     * - Resizes all textures so they are the same size.
     * - Handles animations for moving textures like water.
     * - Sends everything to the GPU.
     * 
     * @param directoryPath Path to the directory containing texture assets.
     */
    void loadTextures(const std::string& directoryPath);


    /**
     * @brief Returns the layer index for a given texture name.
     * 
     * @param textureName The name of the texture (filename without extension).
     * @return uint16_t The index of the first frame of the texture in the array. 
     *         Returns 0 if the texture is not found.
     */
    uint16_t getLayerIndex(const std::string& textureName) const;

    /// Average colour of a texture layer (after tinting), ignoring transparent texels.
    /// Distant terrain uses it in place of the full texture.
    [[nodiscard]] std::array<uint8_t, 3> averageColor(uint16_t layer) const {
        return layer < layerColors.size() ? layerColors[layer] : std::array<uint8_t, 3>{255, 0, 255};
    }

private:
    /// Average RGB per texture layer, filled by loadTextures.
    std::vector<std::array<uint8_t, 3>> layerColors;
    TextureManager() = default;
    ~TextureManager();

    /// Map from texture name to its starting layer index in the block texture array.
    std::unordered_map<std::string, uint16_t> textureMap;
    
    /**
     * @brief Internal struct to hold raw source image data before processing.
     */
    struct TextureData {
        std::string name;
        std::vector<unsigned char> pixels;
        int width, height, channels;
    };

    /**
     * @brief Helper to load raw pixel data from disk using stb_image.
     * @param path Full file path.
     * @param name Name identifier for the texture.
     * @return TextureData structure containing the loaded pixels and metadata.
     */
    static TextureData loadPixels(const std::string& path, const std::string& name);
    
    /**
     * @brief Helper to upscale image data using Nearest Neighbor filtering.
     * 
     * This is used to ensure all textures in the array have identical dimensions.
     * 
     * @param src Source pixel vector.
     * @param srcW Source width.
     * @param srcH Source height.
     * @param dstW Target width.
     * @param dstH Target height.
     * @return std::vector<unsigned char> The rescaled pixel data.
     */
    static std::vector<unsigned char> upscaleImage(const std::vector<unsigned char>& src, int srcW, int srcH, int dstW, int dstH);
};
