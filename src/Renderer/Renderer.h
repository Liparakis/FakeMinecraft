/**
 * @file Renderer.h
 * @brief The game's passes on top of VkContext: render targets, descriptors and pipelines.
 *
 * A frame is: beginFrame, shadow pass, (reflection pass), main pass, UI pass, endFrame.
 * Between begin/end of a pass, callers bind a pipeline and draw meshes.
 */
#pragma once

#include "Vulkan/VkContext.h"
#include <glm/glm.hpp>
#include <map>
#include <tuple>
#include <string>
#include <vector>

/// Vertex and index buffers of one uploaded mesh.
struct GpuMesh {
    GpuBuffer vertices;
    GpuBuffer indices;
    uint32_t indexCount = 0;
    explicit operator bool() const { return indexCount > 0; }
};

/// Maps a GL-style projection (clip z in [-w, w]) to Vulkan's [0, w]: z' = z/2 + w/2.
/// Game code keeps GL conventions (the bundled glm 0.9.6 has no zero-to-one mode);
/// every matrix handed to a shader goes through this once.
inline glm::mat4 toVulkanClip(const glm::mat4 &projection) {
    glm::mat4 correction(1.0f);
    correction[2][2] = 0.5f;
    correction[3][2] = 0.5f;
    return correction * projection;
}

/// Matches `FrameUniforms` in assets/shaders/common.glsl.
struct alignas(16) FrameUniforms {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 lightSpace{1.0f};
    glm::mat4 reflectionViewProjection{1.0f};
    glm::vec4 sunDirection{0.0f};
    glm::vec4 sunColor{0.0f};
    glm::vec4 cameraPos{0.0f};
    glm::vec4 clipPlane{0.0f, 0.0f, 0.0f, 1.0f}; ///< Fragments with dot(pos, plane) < 0 are discarded.
    glm::vec4 fogColor{0.0f};  ///< rgb; matches the sky so the horizon blends into it.
    glm::vec4 fogRange{1e9f, 2e9f, 0.0f, 0.0f}; ///< x = start, y = end (horizontal distance in blocks).
    float ambientStrength = 0.0f;
    float time = 0.0f;
    int32_t simpleLighting = 0;
    int32_t pad = 0;
};

enum class PipelineId : uint8_t {
    World, WorldOpaque, Billboard, Fluid, Sky, Outline, Model, ShadowWorld, ShadowBillboard, UI, Lod
};

using TextureId = uint32_t;

class Renderer {
public:
    static constexpr uint32_t SHADOW_SIZE = 4096;
    static constexpr uint32_t REFLECTION_SIZE = 1024;
    static constexpr uint32_t PUSH_CONSTANT_SIZE = 128;
    /// The chunk mask covers 128x128 chunks around the camera, indexed by chunk coordinate mod 128.
    static constexpr uint32_t CHUNK_MASK_SIZE = 128;

    static Renderer &get();

    void init(SDL_Window *window, bool vsync);
    void shutdown();

    /// Replaces the mesh's buffers with new device-local ones; the old buffers are freed once the GPU is done with them.
    /// Inside a frame the copy is recorded into the frame (call before the first pass, then finishUploads());
    /// outside a frame it completes immediately.
    void uploadMesh(GpuMesh &mesh, const void *vertices, size_t vertexBytes, const uint32_t *indices, uint32_t indexCount);
    /// Makes this frame's recorded uploads visible to vertex input. Call after the last upload, before the passes.
    void finishUploads();
    /// Replaces the chunk mask (CHUNK_MASK_SIZE^2 bytes, non-zero where a real chunk is drawn), which distant
    /// terrain uses to stay out of the way. Same timing rules as uploadMesh.
    void uploadChunkMask(const uint8_t *mask);
    void releaseMesh(GpuMesh &mesh);
    /// Host-visible vertex buffer, used for small static meshes (sky quad, outline, player model).
    GpuBuffer createVertexBuffer(const void *data, size_t bytes);

    /// Uploads the block texture array (RGBA8, `layers` layers of width x height) and builds its mipmaps.
    void setTextureArray(uint32_t width, uint32_t height, uint32_t layers, const uint8_t *rgba);
    /// Creates a sampled 2D texture usable with bindTexture. `format` is RGBA8 or R8.
    TextureId createTexture(uint32_t width, uint32_t height, VkFormat format, const uint8_t *pixels, bool linearFilter);

    /// Saves the next presented frame as a 24-bit BMP (used for automated visual checks).
    void requestScreenshot(const std::string &path) { screenshotPath = path; }

    /// FAKEMC_PROFILE=1: GPU time per pass (timestamp queries) and CPU sections, printed every 2 s.
    /// mark() closes a GPU section ending at the current point of the command buffer.
    void mark(const char *section);
    /// Adds CPU time to a named section of the profile.
    void addCpuTime(const char *section, double ms);

    void setVsync(bool vsync);
    void setMsaa(bool enabled) { msaa = enabled; }
    [[nodiscard]] bool msaaEnabled() const { return msaa; }
    [[nodiscard]] VkExtent2D extent() const { return ctx().swapchainExtent; }

    /// Marks the swapchain for rebuilding at the next beginFrame (window resized).
    void onResize() { swapchainStale = true; }

    /// Returns false when no frame could be started (window minimised or swapchain rebuilt).
    bool beginFrame();
    void endFrame();

    void setUniforms(const FrameUniforms &main, const FrameUniforms &reflection);

    void beginShadowPass();
    void beginReflectionPass(const glm::vec3 &clearColor);
    void beginMainPass(const glm::vec3 &clearColor);
    void beginUIPass();
    void endPass();

    /// Binds the pipeline for the current pass and the matching frame descriptor set.
    void bindPipeline(PipelineId id);
    void bindTexture(TextureId texture);
    void pushConstants(const void *data, uint32_t size);
    void setLineWidth(float width);

    void draw(const GpuMesh &mesh);
    void draw(const GpuBuffer &vertices, uint32_t vertexCount, uint32_t firstVertex = 0);

    /// Per-frame scratch vertex memory (host visible), valid until the frame is submitted.
    /// Returns nullptr if the frame's budget is exhausted.
    void *allocateTransient(size_t bytes, GpuBuffer &buffer, uint32_t &firstVertex, uint32_t stride);

private:
    enum class Target : uint8_t { None, Shadow, Reflection, Main, UI };

    struct PipelineKey {
        PipelineId id;
        Target target;
        VkSampleCountFlagBits samples;
        bool operator<(const PipelineKey &o) const {
            return std::tie(id, target, samples) < std::tie(o.id, o.target, o.samples);
        }
    };

    static VkContext &ctx() { return VkContext::get(); }

    void createTargets();
    void destroyTargets();
    void createDescriptors();
    void writeFrameSets();
    VkPipeline pipelineFor(PipelineId id);
    VkPipeline buildPipeline(const PipelineKey &key);
    VkShaderModule loadShader(const std::string &name);
    void beginRendering(const VkRenderingInfo &info, VkExtent2D extent, bool flipY);
    void copyToBuffer(const GpuBuffer &dst, const void *data, size_t bytes);

    SDL_Window *window = nullptr;
    bool msaa = true;
    bool vsync = true;
    bool swapchainStale = false;
    bool recording = false; ///< Between beginFrame and endFrame.
    Target target = Target::None;
    VkSampleCountFlagBits targetSamples = VK_SAMPLE_COUNT_1_BIT;

    GpuImage textureArray, shadowMap, reflectionColor, reflectionDepth;
    GpuImage msaaColor, msaaDepth, depth;
    GpuImage chunkMask;
    VkSampler blockSampler = VK_NULL_HANDLE, nearestClamp = VK_NULL_HANDLE, linearClamp = VK_NULL_HANDLE;
    VkSampler shadowSampler = VK_NULL_HANDLE; ///< Linear depth-compare sampler (hardware PCF).

    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE, textureSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    struct FrameResources {
        GpuBuffer mainUniforms, reflectionUniforms, transient, staging;
        VkDescriptorSet mainSet = VK_NULL_HANDLE, reflectionSet = VK_NULL_HANDLE;
        size_t transientUsed = 0;
        size_t stagingUsed = 0;
        bool uploadsPending = false;
    };
    std::array<FrameResources, VkContext::FRAMES_IN_FLIGHT> frames{};

    std::vector<GpuImage> textures;
    std::vector<VkDescriptorSet> textureSets;
    TextureId whiteTexture = 0;

    std::string screenshotPath;

    struct Profiler {
        static constexpr uint32_t MAX_MARKS = 16;
        bool enabled = false;
        VkQueryPool pool = VK_NULL_HANDLE;
        double nsPerTick = 1.0;
        const char *labels[VkContext::FRAMES_IN_FLIGHT][MAX_MARKS] = {};
        uint32_t count[VkContext::FRAMES_IN_FLIGHT] = {};
        std::vector<std::pair<const char *, double>> gpu, cpu; ///< Accumulated ms per section.
        uint32_t frames = 0;
        double lastReport = 0.0;
        void accumulate(std::vector<std::pair<const char *, double>> &into, const char *section, double ms);
    } profiler;
    void collectProfile();

    std::map<PipelineKey, VkPipeline> pipelines;
    std::map<std::string, VkShaderModule> shaders;
};
