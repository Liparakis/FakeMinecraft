/**
 * @file VkContext.h
 * @brief Vulkan instance/device/swapchain, frames in flight and GPU memory.
 */
#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <SDL2/SDL.h>
#include <array>
#include <functional>
#include <mutex>
#include <vector>

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    void *mapped = nullptr; ///< Persistently mapped pointer for host-visible buffers.
    VkDeviceSize size = 0;
    explicit operator bool() const { return buffer != VK_NULL_HANDLE; }
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint32_t layers = 1, mips = 1;
    explicit operator bool() const { return image != VK_NULL_HANDLE; }
};

/// Records a layout transition with synchronization2.
void imageBarrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);

class VkContext {
public:
    static constexpr uint32_t FRAMES_IN_FLIGHT = 2;

    static VkContext &get();

    void init(SDL_Window *window, bool vsync);
    void shutdown();

    /// Rebuilds the swapchain for the window's current size and the given present mode.
    void recreateSwapchain(bool vsync);

    /// Waits for this frame slot, frees its deferred resources and acquires a swapchain image.
    /// Returns false when the swapchain must be recreated; the frame should then be skipped.
    bool beginFrame();
    /// Submits the frame's command buffer and presents. Returns false if the swapchain is stale.
    bool endFrame();

    [[nodiscard]] VkCommandBuffer cmd() const { return frames[frameIndex].cmd; }
    [[nodiscard]] uint32_t currentFrame() const { return frameIndex; }
    [[nodiscard]] VkImage swapchainImage() const { return swapchainImages[imageIndex]; }
    [[nodiscard]] VkImageView swapchainView() const { return swapchainViews[imageIndex]; }

    GpuBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(GpuBuffer &buffer);
    /// Destroys the buffer once every frame that might still read it has finished. Thread-safe.
    void destroyLater(GpuBuffer &buffer);

    GpuImage createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage,
                         VkImageAspectFlags aspect, uint32_t layers = 1, uint32_t mips = 1,
                         VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT,
                         VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D);
    void destroyImage(GpuImage &image);

    /// Records and runs a one-off command buffer, blocking until the GPU finishes it.
    void immediateSubmit(const std::function<void(VkCommandBuffer)> &record);

    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VmaAllocator allocator = nullptr;
    VkPhysicalDeviceProperties properties{};
    bool wideLines = false;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent{};

    /// Number of validation errors reported so far (0 when validation is off).
    uint32_t validationErrors = 0;

private:
    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkSemaphore imageAcquired = VK_NULL_HANDLE;
    };

    struct PendingDestroy {
        GpuBuffer buffer;
        uint64_t lastUsableFrame; ///< Frame number that may still read the buffer.
    };

    void createInstance();
    void pickDevice();
    void createSwapchain(bool vsync);
    void destroySwapchain();

    SDL_Window *window = nullptr;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    std::array<Frame, FRAMES_IN_FLIGHT> frames{};
    uint32_t frameIndex = 0;
    uint32_t imageIndex = 0;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkSemaphore> renderFinished; ///< One per swapchain image, so presents never reuse a busy semaphore.
    std::mutex destroyMutex;
    std::vector<PendingDestroy> pendingDestroy;
    uint64_t frameNumber = 0; ///< Number of the frame being recorded (or last recorded).

    VkCommandPool uploadPool = VK_NULL_HANDLE;
    VkFence uploadFence = VK_NULL_HANDLE;
};
