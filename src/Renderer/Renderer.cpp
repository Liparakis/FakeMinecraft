#include "Renderer.h"

#include <SDL2/SDL_vulkan.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
    constexpr VkFormat DEPTH_FORMAT = VK_FORMAT_D32_SFLOAT;
    constexpr VkFormat REFLECTION_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
    constexpr VkDeviceSize TRANSIENT_BYTES = 4 * 1024 * 1024;
    constexpr VkDeviceSize STAGING_BYTES = 32 * 1024 * 1024;
    constexpr const char *SHADER_DIR = "../assets/shaders/";

    void check(const VkResult result, const char *what) {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::string("[Renderer] ") + what + " failed: " + std::to_string(result));
        }
    }

    enum class VertexLayout : uint8_t { None, Chunk, Fluid, ChunkPosition, Billboard, BillboardPosition, PosUV, Pos, Model, UI };
    enum class Blend : uint8_t { None, Alpha, Additive };

    struct PipelineSpec {
        const char *vertexShader;
        const char *fragmentShader; ///< nullptr for depth-only pipelines.
        VertexLayout layout;
        VkPrimitiveTopology topology;
        VkCullModeFlags cull;
        bool depthTest, depthWrite;
        VkCompareOp compare;
        Blend blend;
        bool depthBias;
    };

    // Depth uses reverse-Z (clear 0, GREATER_OR_EQUAL) everywhere except the shadow map.
    PipelineSpec specFor(const PipelineId id) {
        constexpr auto TRI = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        constexpr auto GE = VK_COMPARE_OP_GREATER_OR_EQUAL;
        switch (id) {
            case PipelineId::World: return {"world.vert", "world.frag", VertexLayout::Chunk, TRI, VK_CULL_MODE_BACK_BIT, true, true, GE, Blend::None, false};
            case PipelineId::WorldOpaque: return {"world.vert", "world_opaque.frag", VertexLayout::Chunk, TRI, VK_CULL_MODE_BACK_BIT, true, true, GE, Blend::None, false};
            case PipelineId::Billboard: return {"billboard.vert", "billboard.frag", VertexLayout::Billboard, TRI, VK_CULL_MODE_NONE, true, true, GE, Blend::None, false};
            case PipelineId::Fluid: return {"fluid.vert", "fluid.frag", VertexLayout::Fluid, TRI, VK_CULL_MODE_NONE, true, true, GE, Blend::Alpha, false};
            case PipelineId::Sky: return {"sky.vert", "sky.frag", VertexLayout::PosUV, TRI, VK_CULL_MODE_NONE, false, false, GE, Blend::Additive, false};
            case PipelineId::Outline: return {"outline.vert", "outline.frag", VertexLayout::Pos, VK_PRIMITIVE_TOPOLOGY_LINE_LIST, VK_CULL_MODE_NONE, true, true, GE, Blend::None, false};
            case PipelineId::Model: return {"model.vert", "model.frag", VertexLayout::Model, TRI, VK_CULL_MODE_NONE, false, false, GE, Blend::None, false};
            case PipelineId::ShadowWorld: return {"shadow_world.vert", nullptr, VertexLayout::ChunkPosition, TRI, VK_CULL_MODE_NONE, true, true, VK_COMPARE_OP_LESS, Blend::None, true};
            case PipelineId::ShadowBillboard: return {"shadow_billboard.vert", nullptr, VertexLayout::BillboardPosition, TRI, VK_CULL_MODE_NONE, true, true, VK_COMPARE_OP_LESS, Blend::None, true};
            case PipelineId::UI: return {"ui.vert", "ui.frag", VertexLayout::UI, TRI, VK_CULL_MODE_NONE, false, false, GE, Blend::Alpha, false};
        }
        throw std::runtime_error("[Renderer] unknown pipeline");
    }

    struct VertexInput {
        VkVertexInputBindingDescription binding{};
        std::vector<VkVertexInputAttributeDescription> attributes;
    };

    // Layouts match Renderer/Vertices/*.h and the inputs of the shaders that use them.
    VertexInput vertexInputFor(const VertexLayout layout) {
        auto attr = [](uint32_t location, VkFormat format, uint32_t offset) {
            return VkVertexInputAttributeDescription{location, 0, format, offset};
        };
        switch (layout) {
            case VertexLayout::Chunk:
                return {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX},
                        {attr(0, VK_FORMAT_R16G16B16A16_SINT, 0), attr(1, VK_FORMAT_R8G8B8A8_UINT, 8),
                         attr(2, VK_FORMAT_R16_UINT, 12)}};
            case VertexLayout::Fluid:
                return {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX},
                        {attr(0, VK_FORMAT_R16G16B16A16_SINT, 0), attr(1, VK_FORMAT_R8G8B8A8_UINT, 8),
                         attr(2, VK_FORMAT_R16_UINT, 12), attr(3, VK_FORMAT_R8_UINT, 14)}};
            case VertexLayout::ChunkPosition:
                return {{0, 16, VK_VERTEX_INPUT_RATE_VERTEX}, {attr(0, VK_FORMAT_R16G16B16A16_SINT, 0)}};
            case VertexLayout::Billboard:
                return {{0, 20, VK_VERTEX_INPUT_RATE_VERTEX},
                        {attr(0, VK_FORMAT_R32G32B32_SFLOAT, 0), attr(1, VK_FORMAT_R8G8B8A8_UINT, 12),
                         attr(2, VK_FORMAT_R16_UINT, 16)}};
            case VertexLayout::BillboardPosition:
                return {{0, 20, VK_VERTEX_INPUT_RATE_VERTEX}, {attr(0, VK_FORMAT_R32G32B32_SFLOAT, 0)}};
            case VertexLayout::PosUV:
                return {{0, 20, VK_VERTEX_INPUT_RATE_VERTEX},
                        {attr(0, VK_FORMAT_R32G32B32_SFLOAT, 0), attr(1, VK_FORMAT_R32G32_SFLOAT, 12)}};
            case VertexLayout::Pos:
                return {{0, 12, VK_VERTEX_INPUT_RATE_VERTEX}, {attr(0, VK_FORMAT_R32G32B32_SFLOAT, 0)}};
            case VertexLayout::Model:
                return {{0, 24, VK_VERTEX_INPUT_RATE_VERTEX}, {attr(0, VK_FORMAT_R32G32B32_SFLOAT, 0)}};
            case VertexLayout::UI:
                return {{0, 20, VK_VERTEX_INPUT_RATE_VERTEX},
                        {attr(0, VK_FORMAT_R32G32_SFLOAT, 0), attr(1, VK_FORMAT_R32G32_SFLOAT, 8),
                         attr(2, VK_FORMAT_R8G8B8A8_UNORM, 16)}};
            case VertexLayout::None:
                break;
        }
        return {};
    }

    VkSampler makeSampler(const VkFilter mag, const VkFilter min, const VkSamplerMipmapMode mip,
                          const VkSamplerAddressMode address, const float maxLod) {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = mag;
        info.minFilter = min;
        info.mipmapMode = mip;
        info.addressModeU = info.addressModeV = info.addressModeW = address;
        info.maxLod = maxLod;
        VkSampler sampler;
        check(vkCreateSampler(VkContext::get().device, &info, nullptr, &sampler), "vkCreateSampler");
        return sampler;
    }

    /// Writes 32-bit pixels (top row first) as a bottom-up 24-bit BMP.
    void writeBmp(const std::string &path, const uint8_t *pixels, const uint32_t width, const uint32_t height, const bool rgba) {
        const uint32_t rowBytes = (width * 3 + 3) & ~3u;
        const uint32_t dataBytes = rowBytes * height;
        uint8_t header[54] = {'B', 'M'};
        auto put32 = [&](const int offset, const uint32_t v) { std::memcpy(header + offset, &v, 4); };
        put32(2, 54 + dataBytes);
        put32(10, 54);
        put32(14, 40);
        put32(18, width);
        put32(22, height);
        header[26] = 1;
        header[28] = 24;
        put32(34, dataBytes);

        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(header), sizeof(header));
        std::vector<uint8_t> row(rowBytes, 0);
        for (uint32_t y = 0; y < height; ++y) {
            const uint8_t *src = pixels + static_cast<size_t>(height - 1 - y) * width * 4;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 3 + 0] = src[x * 4 + (rgba ? 2 : 0)];
                row[x * 3 + 1] = src[x * 4 + 1];
                row[x * 3 + 2] = src[x * 4 + (rgba ? 0 : 2)];
            }
            out.write(reinterpret_cast<const char *>(row.data()), rowBytes);
        }
        std::cout << "[Renderer] Saved screenshot " << path << std::endl;
    }

    void transitionNow(const GpuImage &image, const VkImageAspectFlags aspect, const VkImageLayout layout) {
        VkContext::get().immediateSubmit([&](VkCommandBuffer cmd) {
            imageBarrier(cmd, image.image, aspect, VK_IMAGE_LAYOUT_UNDEFINED, layout,
                         VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });
    }
}

Renderer &Renderer::get() {
    static Renderer renderer;
    return renderer;
}

void Renderer::init(SDL_Window *sdlWindow, const bool useVsync) {
    window = sdlWindow;
    vsync = useVsync;
    ctx().init(window, vsync);

    blockSampler = makeSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_LINEAR,
                               VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_LOD_CLAMP_NONE);
    nearestClamp = makeSampler(VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                               VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f);
    linearClamp = makeSampler(VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_NEAREST,
                              VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f);
    {
        // Lit where the reference depth (fragment depth minus bias) is <= the stored depth.
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        info.magFilter = info.minFilter = VK_FILTER_LINEAR;
        info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        info.addressModeU = info.addressModeV = info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.compareEnable = VK_TRUE;
        info.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        check(vkCreateSampler(ctx().device, &info, nullptr, &shadowSampler), "vkCreateSampler");
    }

    shadowMap = ctx().createImage({SHADOW_SIZE, SHADOW_SIZE}, DEPTH_FORMAT,
                                  VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                  VK_IMAGE_ASPECT_DEPTH_BIT);
    reflectionColor = ctx().createImage({REFLECTION_SIZE, REFLECTION_SIZE}, REFLECTION_FORMAT,
                                        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                        VK_IMAGE_ASPECT_COLOR_BIT);
    reflectionDepth = ctx().createImage({REFLECTION_SIZE, REFLECTION_SIZE}, DEPTH_FORMAT,
                                        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    // Both are sampled every frame even when their pass is skipped, so they start out readable.
    transitionNow(shadowMap, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    transitionNow(reflectionColor, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    createTargets();
    createDescriptors();

    constexpr uint8_t white[4] = {255, 255, 255, 255};
    whiteTexture = createTexture(1, 1, VK_FORMAT_R8G8B8A8_UNORM, white, false);

    if (const char *profile = std::getenv("FAKEMC_PROFILE"); profile && profile[0] == '1') {
        VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = Profiler::MAX_MARKS * VkContext::FRAMES_IN_FLIGHT;
        check(vkCreateQueryPool(ctx().device, &queryInfo, nullptr, &profiler.pool), "vkCreateQueryPool");
        profiler.nsPerTick = ctx().properties.limits.timestampPeriod;
        profiler.enabled = true;
    }

    for (FrameResources &frame : frames) {
        frame.mainUniforms = ctx().createBuffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        frame.reflectionUniforms = ctx().createBuffer(sizeof(FrameUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        frame.transient = ctx().createBuffer(TRANSIENT_BYTES, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
        frame.staging = ctx().createBuffer(STAGING_BYTES, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    }
}

void Renderer::createTargets() {
    const VkExtent2D size = ctx().swapchainExtent;
    msaaColor = ctx().createImage(size, ctx().swapchainFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1,
                                  VK_SAMPLE_COUNT_4_BIT);
    msaaDepth = ctx().createImage(size, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                  VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT, 1, 1,
                                  VK_SAMPLE_COUNT_4_BIT);
    depth = ctx().createImage(size, DEPTH_FORMAT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                              VK_IMAGE_ASPECT_DEPTH_BIT);
}

void Renderer::destroyTargets() {
    ctx().destroyImage(msaaColor);
    ctx().destroyImage(msaaDepth);
    ctx().destroyImage(depth);
}

void Renderer::createDescriptors() {
    const VkDevice device = ctx().device;

    VkDescriptorSetLayoutBinding frameBindings[4] = {
        {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
    };
    VkDescriptorSetLayoutCreateInfo frameInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    frameInfo.bindingCount = 4;
    frameInfo.pBindings = frameBindings;
    check(vkCreateDescriptorSetLayout(device, &frameInfo, nullptr, &frameSetLayout), "vkCreateDescriptorSetLayout");

    VkDescriptorSetLayoutBinding textureBinding{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo textureInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    textureInfo.bindingCount = 1;
    textureInfo.pBindings = &textureBinding;
    check(vkCreateDescriptorSetLayout(device, &textureInfo, nullptr, &textureSetLayout), "vkCreateDescriptorSetLayout");

    constexpr uint32_t MAX_TEXTURES = 32;
    VkDescriptorPoolSize sizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * VkContext::FRAMES_IN_FLIGHT},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * VkContext::FRAMES_IN_FLIGHT * 3 + MAX_TEXTURES},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 2 * VkContext::FRAMES_IN_FLIGHT + MAX_TEXTURES;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    for (FrameResources &frame : frames) {
        const VkDescriptorSetLayout layouts[2] = {frameSetLayout, frameSetLayout};
        VkDescriptorSet sets[2];
        VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        alloc.descriptorPool = descriptorPool;
        alloc.descriptorSetCount = 2;
        alloc.pSetLayouts = layouts;
        check(vkAllocateDescriptorSets(device, &alloc, sets), "vkAllocateDescriptorSets");
        frame.mainSet = sets[0];
        frame.reflectionSet = sets[1];
    }

    const VkDescriptorSetLayout setLayouts[2] = {frameSetLayout, textureSetLayout};
    VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, PUSH_CONSTANT_SIZE};
    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 2;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout");
}

void Renderer::writeFrameSets() {
    for (FrameResources &frame : frames) {
        for (const bool reflection : {false, true}) {
            const VkDescriptorBufferInfo uniforms{reflection ? frame.reflectionUniforms.buffer : frame.mainUniforms.buffer, 0, sizeof(FrameUniforms)};
            const VkDescriptorImageInfo blocks{blockSampler, textureArray.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            const VkDescriptorImageInfo shadow{shadowSampler, shadowMap.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
            // The reflection pass cannot sample the image it renders into, so it gets a white texture.
            const VkDescriptorImageInfo reflected{linearClamp, reflection ? textures[whiteTexture].view : reflectionColor.view,
                                                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

            const VkDescriptorSet set = reflection ? frame.reflectionSet : frame.mainSet;
            VkWriteDescriptorSet writes[4]{};
            for (uint32_t i = 0; i < 4; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            }
            writes[0].pBufferInfo = &uniforms;
            writes[1].pImageInfo = &blocks;
            writes[2].pImageInfo = &shadow;
            writes[3].pImageInfo = &reflected;
            vkUpdateDescriptorSets(ctx().device, 4, writes, 0, nullptr);
        }
    }
}

void Renderer::shutdown() {
    const VkDevice device = ctx().device;
    if (!device) return;
    vkDeviceWaitIdle(device);

    for (auto &[key, pipeline] : pipelines) vkDestroyPipeline(device, pipeline, nullptr);
    pipelines.clear();
    for (auto &[name, module] : shaders) vkDestroyShaderModule(device, module, nullptr);
    shaders.clear();

    for (FrameResources &frame : frames) {
        ctx().destroyBuffer(frame.mainUniforms);
        ctx().destroyBuffer(frame.reflectionUniforms);
        ctx().destroyBuffer(frame.transient);
        ctx().destroyBuffer(frame.staging);
    }
    for (GpuImage &image : textures) ctx().destroyImage(image);
    textures.clear();
    destroyTargets();
    ctx().destroyImage(textureArray);
    ctx().destroyImage(shadowMap);
    ctx().destroyImage(reflectionColor);
    ctx().destroyImage(reflectionDepth);

    if (profiler.pool) vkDestroyQueryPool(device, profiler.pool, nullptr);
    vkDestroySampler(device, blockSampler, nullptr);
    vkDestroySampler(device, nearestClamp, nullptr);
    vkDestroySampler(device, shadowSampler, nullptr);
    vkDestroySampler(device, linearClamp, nullptr);
    vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyDescriptorSetLayout(device, frameSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, textureSetLayout, nullptr);
    ctx().shutdown();
}

void Renderer::uploadMesh(GpuMesh &mesh, const void *vertices, const size_t vertexBytes,
                          const uint32_t *indices, const uint32_t indexCount) {
    releaseMesh(mesh);
    if (indexCount == 0 || vertexBytes == 0) return;

    // Device-local: the GPU reads chunk meshes every pass, and host-visible VRAM is scarce without Resizable BAR.
    mesh.vertices = ctx().createBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    mesh.indices = ctx().createBuffer(indexCount * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false);
    copyToBuffer(mesh.vertices, vertices, vertexBytes);
    copyToBuffer(mesh.indices, indices, indexCount * sizeof(uint32_t));
    mesh.indexCount = indexCount;
}

void Renderer::copyToBuffer(const GpuBuffer &dst, const void *data, const size_t bytes) {
    if (!recording) {
        GpuBuffer staging = ctx().createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        std::memcpy(staging.mapped, data, bytes);
        vmaFlushAllocation(ctx().allocator, staging.allocation, 0, VK_WHOLE_SIZE);
        ctx().immediateSubmit([&](VkCommandBuffer cmd) {
            const VkBufferCopy region{0, 0, bytes};
            vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
        });
        ctx().destroyBuffer(staging);
        return;
    }

    FrameResources &frame = frames[ctx().currentFrame()];
    const size_t offset = (frame.stagingUsed + 15) & ~static_cast<size_t>(15);
    GpuBuffer source = frame.staging;
    VkDeviceSize sourceOffset = offset;
    if (offset + bytes <= STAGING_BYTES) {
        frame.stagingUsed = offset + bytes;
    } else {
        // Ring is full this frame: use a one-off staging buffer, freed after the frame.
        source = ctx().createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
        sourceOffset = 0;
        vmaFlushAllocation(ctx().allocator, source.allocation, 0, VK_WHOLE_SIZE);
    }
    std::memcpy(static_cast<uint8_t *>(source.mapped) + sourceOffset, data, bytes);
    if (source.buffer != frame.staging.buffer) {
        vmaFlushAllocation(ctx().allocator, source.allocation, 0, VK_WHOLE_SIZE);
    }

    const VkBufferCopy region{sourceOffset, 0, bytes};
    vkCmdCopyBuffer(ctx().cmd(), source.buffer, dst.buffer, 1, &region);
    frame.uploadsPending = true;
    if (source.buffer != frame.staging.buffer) ctx().destroyLater(source);
}

void Renderer::finishUploads() {
    FrameResources &frame = frames[ctx().currentFrame()];
    if (!frame.uploadsPending) return;
    vmaFlushAllocation(ctx().allocator, frame.staging.allocation, 0, frame.stagingUsed);

    VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(ctx().cmd(), &dep);
    frame.uploadsPending = false;
}

void Renderer::releaseMesh(GpuMesh &mesh) {
    ctx().destroyLater(mesh.vertices);
    ctx().destroyLater(mesh.indices);
    mesh.indexCount = 0;
}

GpuBuffer Renderer::createVertexBuffer(const void *data, const size_t bytes) {
    GpuBuffer buffer = ctx().createBuffer(bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, true);
    std::memcpy(buffer.mapped, data, bytes);
    vmaFlushAllocation(ctx().allocator, buffer.allocation, 0, VK_WHOLE_SIZE);
    return buffer;
}

void Renderer::setTextureArray(const uint32_t width, const uint32_t height, const uint32_t layers, const uint8_t *rgba) {
    vkDeviceWaitIdle(ctx().device);
    ctx().destroyImage(textureArray);

    const uint32_t mips = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    textureArray = ctx().createImage({width, height}, VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                     VK_IMAGE_ASPECT_COLOR_BIT, layers, mips, VK_SAMPLE_COUNT_1_BIT,
                                     VK_IMAGE_VIEW_TYPE_2D_ARRAY);

    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * 4 * layers;
    GpuBuffer staging = ctx().createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, rgba, bytes);
    vmaFlushAllocation(ctx().allocator, staging.allocation, 0, VK_WHOLE_SIZE);

    const VkImage image = textureArray.image;
    ctx().immediateSubmit([&](VkCommandBuffer cmd) {
        imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, layers};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        // Each mip is blitted from the previous one, like glGenerateMipmap.
        for (uint32_t level = 1; level < mips; ++level) {
            VkImageMemoryBarrier2 toSrc{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
            toSrc.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toSrc.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            toSrc.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            toSrc.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
            toSrc.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            toSrc.srcQueueFamilyIndex = toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            toSrc.image = image;
            toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 1, 0, layers};
            VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &toSrc;
            vkCmdPipelineBarrier2(cmd, &dep);

            const int32_t srcW = static_cast<int32_t>(std::max(1u, width >> (level - 1)));
            const int32_t srcH = static_cast<int32_t>(std::max(1u, height >> (level - 1)));
            VkImageBlit blit{};
            blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level - 1, 0, layers};
            blit.srcOffsets[1] = {srcW, srcH, 1};
            blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, level, 0, layers};
            blit.dstOffsets[1] = {std::max(1, srcW / 2), std::max(1, srcH / 2), 1};
            vkCmdBlitImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);
        }

        // Every level but the last is now TRANSFER_SRC; the last is still TRANSFER_DST.
        VkImageMemoryBarrier2 toRead[2]{};
        for (VkImageMemoryBarrier2 &b : toRead) {
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
        }
        toRead[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toRead[0].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips - 1, 0, layers};
        toRead[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toRead[1].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mips - 1, 1, 0, layers};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = mips > 1 ? 2 : 1;
        dep.pImageMemoryBarriers = mips > 1 ? toRead : &toRead[1];
        vkCmdPipelineBarrier2(cmd, &dep);
    });
    ctx().destroyBuffer(staging);
    writeFrameSets();
}

TextureId Renderer::createTexture(const uint32_t width, const uint32_t height, const VkFormat format,
                                  const uint8_t *pixels, const bool linearFilter) {
    const uint32_t bytesPerPixel = format == VK_FORMAT_R8_UNORM ? 1 : 4;
    GpuImage image = ctx().createImage({width, height}, format,
                                       VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                       VK_IMAGE_ASPECT_COLOR_BIT);
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;
    GpuBuffer staging = ctx().createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true);
    std::memcpy(staging.mapped, pixels, bytes);
    vmaFlushAllocation(ctx().allocator, staging.allocation, 0, VK_WHOLE_SIZE);

    ctx().immediateSubmit([&](VkCommandBuffer cmd) {
        imageBarrier(cmd, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {width, height, 1};
        vkCmdCopyBufferToImage(cmd, staging.buffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        imageBarrier(cmd, image.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    });
    ctx().destroyBuffer(staging);

    VkDescriptorSet set;
    VkDescriptorSetAllocateInfo alloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    alloc.descriptorPool = descriptorPool;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &textureSetLayout;
    check(vkAllocateDescriptorSets(ctx().device, &alloc, &set), "vkAllocateDescriptorSets");

    const VkDescriptorImageInfo info{linearFilter ? linearClamp : nearestClamp, image.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    vkUpdateDescriptorSets(ctx().device, 1, &write, 0, nullptr);

    textures.push_back(image);
    textureSets.push_back(set);
    return static_cast<TextureId>(textures.size() - 1);
}

void Renderer::setVsync(const bool enabled) {
    if (enabled == vsync) return;
    vsync = enabled;
    swapchainStale = true;
}

bool Renderer::beginFrame() {
    if (swapchainStale) {
        // A minimized window has a 0x0 surface, which no swapchain can match; skip frames until it returns.
        const VkExtent2D surface = ctx().surfaceExtent();
        if (surface.width == 0 || surface.height == 0) return false;
        ctx().recreateSwapchain(vsync);
        destroyTargets();
        createTargets();
        swapchainStale = false;
    }
    if (!ctx().beginFrame()) {
        swapchainStale = true;
        return false;
    }
    FrameResources &frame = frames[ctx().currentFrame()];
    frame.transientUsed = 0;
    frame.stagingUsed = 0;
    recording = true;
    collectProfile();
    return true;
}

void Renderer::endFrame() {
    FrameResources &frame = frames[ctx().currentFrame()];
    if (frame.transientUsed > 0) vmaFlushAllocation(ctx().allocator, frame.transient.allocation, 0, frame.transientUsed);
    const VkCommandBuffer cmd = ctx().cmd();
    const VkImage image = ctx().swapchainImage();
    const VkExtent2D size = ctx().swapchainExtent;

    GpuBuffer readback;
    if (!screenshotPath.empty()) {
        readback = ctx().createBuffer(static_cast<VkDeviceSize>(size.width) * size.height * 4,
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {size.width, size.height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.buffer, 1, &copy);
        imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_NONE,
                     VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    } else {
        imageBarrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE);
    }

    recording = false;
    if (!ctx().endFrame()) swapchainStale = true;

    if (readback) {
        vkDeviceWaitIdle(ctx().device);
        vmaInvalidateAllocation(ctx().allocator, readback.allocation, 0, VK_WHOLE_SIZE);
        writeBmp(screenshotPath, static_cast<const uint8_t *>(readback.mapped), size.width, size.height,
                 ctx().swapchainFormat == VK_FORMAT_R8G8B8A8_UNORM);
        ctx().destroyBuffer(readback);
        screenshotPath.clear();
    }
}

void Renderer::setUniforms(const FrameUniforms &main, const FrameUniforms &reflection) {
    FrameResources &frame = frames[ctx().currentFrame()];
    std::memcpy(frame.mainUniforms.mapped, &main, sizeof(FrameUniforms));
    std::memcpy(frame.reflectionUniforms.mapped, &reflection, sizeof(FrameUniforms));
    vmaFlushAllocation(ctx().allocator, frame.mainUniforms.allocation, 0, VK_WHOLE_SIZE);
    vmaFlushAllocation(ctx().allocator, frame.reflectionUniforms.allocation, 0, VK_WHOLE_SIZE);
}

void Renderer::beginRendering(const VkRenderingInfo &info, const VkExtent2D size, const bool flipY) {
    const VkCommandBuffer cmd = ctx().cmd();
    vkCmdBeginRendering(cmd, &info);

    // A negative-height viewport keeps GL's y-up clip space and CW front faces for the 3D passes.
    VkViewport viewport{};
    viewport.width = static_cast<float>(size.width);
    viewport.height = flipY ? -static_cast<float>(size.height) : static_cast<float>(size.height);
    viewport.y = flipY ? static_cast<float>(size.height) : 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, size};
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdSetLineWidth(cmd, 1.0f);
}

void Renderer::beginShadowPass() {
    const VkCommandBuffer cmd = ctx().cmd();
    imageBarrier(cmd, shadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = shadowMap.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {SHADOW_SIZE, SHADOW_SIZE}};
    info.layerCount = 1;
    info.pDepthAttachment = &depthAttachment;
    target = Target::Shadow;
    targetSamples = VK_SAMPLE_COUNT_1_BIT;
    beginRendering(info, {SHADOW_SIZE, SHADOW_SIZE}, true);
}

void Renderer::beginReflectionPass(const glm::vec3 &clearColor) {
    const VkCommandBuffer cmd = ctx().cmd();
    imageBarrier(cmd, reflectionColor.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                 VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    imageBarrier(cmd, reflectionDepth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = reflectionColor.view;
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, 1.0f}};

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = reflectionDepth.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {0.0f, 0};

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, {REFLECTION_SIZE, REFLECTION_SIZE}};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = &depthAttachment;
    target = Target::Reflection;
    targetSamples = VK_SAMPLE_COUNT_1_BIT;
    beginRendering(info, {REFLECTION_SIZE, REFLECTION_SIZE}, true);
}

void Renderer::beginMainPass(const glm::vec3 &clearColor) {
    const VkCommandBuffer cmd = ctx().cmd();
    const GpuImage &depthImage = msaa ? msaaDepth : depth;

    imageBarrier(cmd, ctx().swapchainImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                 VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    if (msaa) {
        imageBarrier(cmd, msaaColor.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    imageBarrier(cmd, depthImage.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
                 VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                 VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                 VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = msaa ? msaaColor.view : ctx().swapchainView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = msaa ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{clearColor.r, clearColor.g, clearColor.b, 1.0f}};
    if (msaa) {
        color.resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
        color.resolveImageView = ctx().swapchainView();
        color.resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    VkRenderingAttachmentInfo depthAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    depthAttachment.imageView = depthImage.view;
    depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.clearValue.depthStencil = {0.0f, 0};

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, ctx().swapchainExtent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    info.pDepthAttachment = &depthAttachment;
    target = Target::Main;
    targetSamples = msaa ? VK_SAMPLE_COUNT_4_BIT : VK_SAMPLE_COUNT_1_BIT;
    beginRendering(info, ctx().swapchainExtent, true);
}

void Renderer::beginUIPass() {
    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = ctx().swapchainView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo info{VK_STRUCTURE_TYPE_RENDERING_INFO};
    info.renderArea = {{0, 0}, ctx().swapchainExtent};
    info.layerCount = 1;
    info.colorAttachmentCount = 1;
    info.pColorAttachments = &color;
    target = Target::UI;
    targetSamples = VK_SAMPLE_COUNT_1_BIT;
    beginRendering(info, ctx().swapchainExtent, false);
}

void Renderer::endPass() {
    const VkCommandBuffer cmd = ctx().cmd();
    vkCmdEndRendering(cmd);
    static constexpr const char *PASS_NAMES[] = {"none", "shadow", "reflection", "main", "ui"};
    mark(PASS_NAMES[static_cast<int>(target)]);

    if (target == Target::Shadow) {
        imageBarrier(cmd, shadowMap.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                     VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    } else if (target == Target::Reflection) {
        imageBarrier(cmd, reflectionColor.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                     VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    } else if (target == Target::Main) {
        // The UI pass loads what the main pass resolved into the swapchain image.
        imageBarrier(cmd, ctx().swapchainImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    }
    target = Target::None;
}

void Renderer::bindPipeline(const PipelineId id) {
    const VkCommandBuffer cmd = ctx().cmd();
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineFor(id));
    if (target == Target::Main || target == Target::Reflection) {
        const FrameResources &frame = frames[ctx().currentFrame()];
        const VkDescriptorSet set = target == Target::Reflection ? frame.reflectionSet : frame.mainSet;
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &set, 0, nullptr);
    }
}

void Renderer::bindTexture(const TextureId texture) {
    vkCmdBindDescriptorSets(ctx().cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 1, 1,
                            &textureSets[texture], 0, nullptr);
}

void Renderer::pushConstants(const void *data, const uint32_t size) {
    vkCmdPushConstants(ctx().cmd(), pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
}

void Renderer::setLineWidth(const float width) {
    vkCmdSetLineWidth(ctx().cmd(), ctx().wideLines ? width : 1.0f);
}

void Renderer::draw(const GpuMesh &mesh) {
    if (!mesh) return;
    const VkCommandBuffer cmd = ctx().cmd();
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
    vkCmdBindIndexBuffer(cmd, mesh.indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
}

void Renderer::draw(const GpuBuffer &vertices, const uint32_t vertexCount, const uint32_t firstVertex) {
    if (!vertices || vertexCount == 0) return;
    const VkCommandBuffer cmd = ctx().cmd();
    constexpr VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertices.buffer, &offset);
    vkCmdDraw(cmd, vertexCount, 1, firstVertex, 0);
}

void *Renderer::allocateTransient(const size_t bytes, GpuBuffer &buffer, uint32_t &firstVertex, const uint32_t stride) {
    FrameResources &frame = frames[ctx().currentFrame()];
    const size_t start = (frame.transientUsed + stride - 1) / stride * stride;
    if (start + bytes > TRANSIENT_BYTES) return nullptr;
    frame.transientUsed = start + bytes;
    buffer = frame.transient;
    firstVertex = static_cast<uint32_t>(start / stride);
    return static_cast<uint8_t *>(frame.transient.mapped) + start;
}

VkShaderModule Renderer::loadShader(const std::string &name) {
    if (const auto it = shaders.find(name); it != shaders.end()) return it->second;

    const std::string path = std::string(SHADER_DIR) + name + ".spv";
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("[Renderer] Missing shader " + path);
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<uint32_t> code((size + 3) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(code.data()), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code.data();
    VkShaderModule module;
    check(vkCreateShaderModule(ctx().device, &info, nullptr, &module), "vkCreateShaderModule");
    shaders[name] = module;
    return module;
}

VkPipeline Renderer::pipelineFor(const PipelineId id) {
    const PipelineKey key{id, target, targetSamples};
    if (const auto it = pipelines.find(key); it != pipelines.end()) return it->second;
    const VkPipeline pipeline = buildPipeline(key);
    pipelines[key] = pipeline;
    return pipeline;
}

VkPipeline Renderer::buildPipeline(const PipelineKey &key) {
    const PipelineSpec spec = specFor(key.id);

    VkPipelineShaderStageCreateInfo stages[2]{};
    uint32_t stageCount = 1;
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = loadShader(spec.vertexShader);
    stages[0].pName = "main";
    if (spec.fragmentShader) {
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = loadShader(spec.fragmentShader);
        stages[1].pName = "main";
        stageCount = 2;
    }

    const VertexInput input = vertexInputFor(spec.layout);
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    if (spec.layout != VertexLayout::None) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &input.binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(input.attributes.size());
        vertexInput.pVertexAttributeDescriptions = input.attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = spec.topology;

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = spec.cull;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    raster.depthBiasEnable = spec.depthBias;
    raster.depthBiasConstantFactor = 2.0f;
    raster.depthBiasSlopeFactor = 2.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = key.samples;

    VkPipelineDepthStencilStateCreateInfo depthState{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    const bool hasDepth = key.target != Target::UI;
    depthState.depthTestEnable = hasDepth && spec.depthTest;
    depthState.depthWriteEnable = hasDepth && spec.depthWrite;
    depthState.depthCompareOp = spec.compare;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    if (spec.blend != Blend::None) {
        blend.blendEnable = VK_TRUE;
        const bool additive = spec.blend == Blend::Additive;
        blend.srcColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
    }

    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    if (key.target == Target::Main || key.target == Target::UI) colorFormat = ctx().swapchainFormat;
    if (key.target == Target::Reflection) colorFormat = REFLECTION_FORMAT;
    const bool hasColor = colorFormat != VK_FORMAT_UNDEFINED;

    VkPipelineColorBlendStateCreateInfo blendState{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blendState.attachmentCount = hasColor ? 1 : 0;
    blendState.pAttachments = &blend;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 3;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = hasColor ? 1 : 0;
    rendering.pColorAttachmentFormats = &colorFormat;
    rendering.depthAttachmentFormat = hasDepth ? DEPTH_FORMAT : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = stageCount;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthState;
    info.pColorBlendState = &blendState;
    info.pDynamicState = &dynamic;
    info.layout = pipelineLayout;

    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(ctx().device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline), "vkCreateGraphicsPipelines");
    return pipeline;
}

void Renderer::Profiler::accumulate(std::vector<std::pair<const char *, double>> &into, const char *section, const double ms) {
    for (auto &[name, total] : into) {
        if (name == section) {
            total += ms;
            return;
        }
    }
    into.emplace_back(section, ms);
}

void Renderer::mark(const char *section) {
    if (!profiler.enabled || !recording) return;
    const uint32_t slot = ctx().currentFrame();
    uint32_t &count = profiler.count[slot];
    if (count >= Profiler::MAX_MARKS) return;
    vkCmdWriteTimestamp2(ctx().cmd(), VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, profiler.pool, slot * Profiler::MAX_MARKS + count);
    profiler.labels[slot][count++] = section;
}

void Renderer::addCpuTime(const char *section, const double ms) {
    if (profiler.enabled) profiler.accumulate(profiler.cpu, section, ms);
}

void Renderer::collectProfile() {
    if (!profiler.enabled) return;
    const uint32_t slot = ctx().currentFrame();
    uint32_t &count = profiler.count[slot];

    // This slot's previous frame has finished (its fence was waited on), so its timestamps are ready.
    if (count > 1) {
        uint64_t ticks[Profiler::MAX_MARKS];
        if (vkGetQueryPoolResults(ctx().device, profiler.pool, slot * Profiler::MAX_MARKS, count, sizeof(ticks), ticks,
                                  sizeof(uint64_t), VK_QUERY_RESULT_64_BIT) == VK_SUCCESS) {
            for (uint32_t i = 1; i < count; ++i) {
                profiler.accumulate(profiler.gpu, profiler.labels[slot][i], (ticks[i] - ticks[i - 1]) * profiler.nsPerTick * 1e-6);
            }
            profiler.accumulate(profiler.gpu, "TOTAL", (ticks[count - 1] - ticks[0]) * profiler.nsPerTick * 1e-6);
            ++profiler.frames;
        }
    }

    const double now = static_cast<double>(SDL_GetTicks());
    if (now - profiler.lastReport >= 2000.0 && profiler.frames > 0) {
        std::cout << "[PROFILE] " << profiler.frames << " frames, avg ms | GPU:";
        for (const auto &[name, total] : profiler.gpu) std::cout << " " << name << " " << total / profiler.frames;
        std::cout << " | CPU:";
        for (const auto &[name, total] : profiler.cpu) std::cout << " " << name << " " << total / profiler.frames;
        std::cout << std::endl;
        profiler.gpu.clear();
        profiler.cpu.clear();
        profiler.frames = 0;
        profiler.lastReport = now;
    }

    vkCmdResetQueryPool(ctx().cmd(), profiler.pool, slot * Profiler::MAX_MARKS, Profiler::MAX_MARKS);
    count = 0;
    mark("start");
}
