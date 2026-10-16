#define VMA_IMPLEMENTATION
#include "VkContext.h"

#include <SDL2/SDL_vulkan.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
    void check(const VkResult result, const char *what) {
        if (result != VK_SUCCESS) {
            throw std::runtime_error(std::string("[Vulkan] ") + what + " failed: " + std::to_string(result));
        }
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT,
                                                 const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            std::cerr << "[Vulkan] " << data->pMessage << std::endl;
            if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
                ++static_cast<VkContext *>(user)->validationErrors;
            }
        }
        return VK_FALSE;
    }

    bool validationRequested() {
#ifndef NDEBUG
        return true;
#else
        const char *env = std::getenv("FAKEMC_VALIDATION");
        return env && env[0] == '1';
#endif
    }

    bool layerAvailable(const char *name) {
        uint32_t count = 0;
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> layers(count);
        vkEnumerateInstanceLayerProperties(&count, layers.data());
        return std::any_of(layers.begin(), layers.end(),
                           [&](const VkLayerProperties &l) { return std::strcmp(l.layerName, name) == 0; });
    }
}

void imageBarrier(const VkCommandBuffer cmd, const VkImage image, const VkImageAspectFlags aspect,
                  const VkImageLayout oldLayout, const VkImageLayout newLayout,
                  const VkPipelineStageFlags2 srcStage, const VkAccessFlags2 srcAccess,
                  const VkPipelineStageFlags2 dstStage, const VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dep);
}

VkContext &VkContext::get() {
    static VkContext context;
    return context;
}

void VkContext::init(SDL_Window *sdlWindow, const bool vsync) {
    window = sdlWindow;
    createInstance();
    if (!SDL_Vulkan_CreateSurface(window, instance, &surface)) {
        throw std::runtime_error(std::string("[Vulkan] SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    pickDevice();

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocInfo.physicalDevice = physicalDevice;
    allocInfo.device = device;
    allocInfo.instance = instance;
    check(vmaCreateAllocator(&allocInfo, &allocator), "vmaCreateAllocator");

    for (Frame &frame : frames) {
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        poolInfo.queueFamilyIndex = queueFamily;
        check(vkCreateCommandPool(device, &poolInfo, nullptr, &frame.pool), "vkCreateCommandPool");

        VkCommandBufferAllocateInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cmdInfo.commandPool = frame.pool;
        cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdInfo.commandBufferCount = 1;
        check(vkAllocateCommandBuffers(device, &cmdInfo, &frame.cmd), "vkAllocateCommandBuffers");

        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(device, &fenceInfo, nullptr, &frame.inFlight), "vkCreateFence");

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semInfo, nullptr, &frame.imageAcquired), "vkCreateSemaphore");
    }

    VkCommandPoolCreateInfo uploadPoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    uploadPoolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    uploadPoolInfo.queueFamilyIndex = queueFamily;
    check(vkCreateCommandPool(device, &uploadPoolInfo, nullptr, &uploadPool), "vkCreateCommandPool");
    VkFenceCreateInfo uploadFenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    check(vkCreateFence(device, &uploadFenceInfo, nullptr, &uploadFence), "vkCreateFence");

    createSwapchain(vsync);
}

void VkContext::createInstance() {
    unsigned extCount = 0;
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, nullptr);
    std::vector<const char *> extensions(extCount);
    SDL_Vulkan_GetInstanceExtensions(window, &extCount, extensions.data());

    std::vector<const char *> layers;
    const bool validation = validationRequested() && layerAvailable("VK_LAYER_KHRONOS_validation");
    if (validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Fake Minecraft";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &appInfo;
    info.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    info.ppEnabledExtensionNames = extensions.data();
    info.enabledLayerCount = static_cast<uint32_t>(layers.size());
    info.ppEnabledLayerNames = layers.data();
    check(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");

    if (validation) {
        VkDebugUtilsMessengerCreateInfoEXT msgInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        msgInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        msgInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        msgInfo.pfnUserCallback = debugCallback;
        msgInfo.pUserData = this;
        const auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create) create(instance, &msgInfo, nullptr, &messenger);
        std::cout << "[Vulkan] Validation layer enabled" << std::endl;
    }
}

void VkContext::pickDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.apiVersion < VK_API_VERSION_1_3) continue;

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());

        for (uint32_t i = 0; i < familyCount; ++i) {
            VkBool32 present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present);
            if (!(families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) continue;

            const int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 : 1;
            if (score > bestScore) {
                bestScore = score;
                physicalDevice = candidate;
                queueFamily = i;
                properties = props;
            }
            break;
        }
    }
    if (!physicalDevice) throw std::runtime_error("[Vulkan] No GPU with Vulkan 1.3 and presentation support");
    std::cout << "[Vulkan] Using " << properties.deviceName << std::endl;

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
    wideLines = supported.wideLines;

    VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &features13;
    features2.features.wideLines = supported.wideLines;
    features2.features.samplerAnisotropy = supported.samplerAnisotropy;
    features2.features.shaderClipDistance = VK_TRUE; // Reflection pass clips in hardware.

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char *deviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &features2;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queueInfo;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = deviceExtensions;
    check(vkCreateDevice(physicalDevice, &info, nullptr, &device), "vkCreateDevice");
    vkGetDeviceQueue(device, queueFamily, 0, &queue);
}

void VkContext::createSwapchain(const bool vsync) {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);

    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    swapchainExtent = caps.currentExtent.width != UINT32_MAX
                          ? caps.currentExtent
                          : VkExtent2D{std::clamp(static_cast<uint32_t>(w), caps.minImageExtent.width, caps.maxImageExtent.width),
                                       std::clamp(static_cast<uint32_t>(h), caps.minImageExtent.height, caps.maxImageExtent.height)};

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    // UNORM matches the GL build, which never converted to sRGB.
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR &f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    swapchainFormat = chosen.format;

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &modeCount, modes.data());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        for (VkPresentModeKHR preferred : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end()) {
                presentMode = preferred;
                break;
            }
        }
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0) imageCount = std::min(imageCount, caps.maxImageCount);

    VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = chosen.format;
    info.imageColorSpace = chosen.colorSpace;
    info.imageExtent = swapchainExtent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device, &info, nullptr, &swapchain), "vkCreateSwapchainKHR");

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, swapchain, &count, nullptr);
    swapchainImages.resize(count);
    vkGetSwapchainImagesKHR(device, swapchain, &count, swapchainImages.data());

    swapchainViews.resize(count);
    renderFinished.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device, &viewInfo, nullptr, &swapchainViews[i]), "vkCreateImageView");

        VkSemaphoreCreateInfo semInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(device, &semInfo, nullptr, &renderFinished[i]), "vkCreateSemaphore");
    }
}

void VkContext::destroySwapchain() {
    for (VkImageView view : swapchainViews) vkDestroyImageView(device, view, nullptr);
    for (VkSemaphore sem : renderFinished) vkDestroySemaphore(device, sem, nullptr);
    swapchainViews.clear();
    renderFinished.clear();
    swapchainImages.clear();
    if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

VkExtent2D VkContext::surfaceExtent() const {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps);
    if (caps.currentExtent.width != UINT32_MAX) return caps.currentExtent;
    int w = 0, h = 0;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    return {static_cast<uint32_t>(w), static_cast<uint32_t>(h)};
}

void VkContext::recreateSwapchain(const bool vsync) {
    vkDeviceWaitIdle(device);
    destroySwapchain();
    createSwapchain(vsync);
}

bool VkContext::beginFrame() {
    Frame &frame = frames[frameIndex];
    vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    // The queue runs frames in order, so this slot's fence means every frame up to
    // frameNumber + 1 - FRAMES_IN_FLIGHT has finished on the GPU.
    if (frameNumber + 1 >= FRAMES_IN_FLIGHT) {
        const uint64_t completed = frameNumber + 1 - FRAMES_IN_FLIGHT;
        std::lock_guard lock(destroyMutex);
        std::erase_if(pendingDestroy, [&](PendingDestroy &p) {
            if (p.lastUsableFrame > completed) return false;
            destroyBuffer(p.buffer);
            return true;
        });
    }

    const VkResult acquired = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, frame.imageAcquired,
                                                    VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) return false;
    if (acquired != VK_SUBOPTIMAL_KHR) check(acquired, "vkAcquireNextImageKHR");

    vkResetFences(device, 1, &frame.inFlight);
    vkResetCommandPool(device, frame.pool, 0);
    ++frameNumber;

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(frame.cmd, &begin), "vkBeginCommandBuffer");
    return true;
}

bool VkContext::endFrame() {
    Frame &frame = frames[frameIndex];
    check(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer");

    VkSemaphoreSubmitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    waitInfo.semaphore = frame.imageAcquired;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = renderFinished[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = frame.cmd;

    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;
    check(vkQueueSubmit2(queue, 1, &submit, frame.inFlight), "vkQueueSubmit2");

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &imageIndex;
    const VkResult presented = vkQueuePresentKHR(queue, &present);

    frameIndex = (frameIndex + 1) % FRAMES_IN_FLIGHT;
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) return false;
    check(presented, "vkQueuePresentKHR");
    return true;
}

GpuBuffer VkContext::createBuffer(const VkDeviceSize size, const VkBufferUsageFlags usage, const bool hostVisible) {
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (hostVisible) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    GpuBuffer buffer;
    VmaAllocationInfo result{};
    check(vmaCreateBuffer(allocator, &info, &allocInfo, &buffer.buffer, &buffer.allocation, &result), "vmaCreateBuffer");
    buffer.mapped = result.pMappedData;
    buffer.size = size;
    return buffer;
}

void VkContext::destroyBuffer(GpuBuffer &buffer) {
    if (buffer.buffer) vmaDestroyBuffer(allocator, buffer.buffer, buffer.allocation);
    buffer = {};
}

void VkContext::destroyLater(GpuBuffer &buffer) {
    if (!buffer) return;
    std::lock_guard lock(destroyMutex);
    pendingDestroy.push_back({buffer, frameNumber});
    buffer = {};
}

GpuImage VkContext::createImage(const VkExtent2D extent, const VkFormat format, const VkImageUsageFlags usage,
                                const VkImageAspectFlags aspect, const uint32_t layers, const uint32_t mips,
                                const VkSampleCountFlagBits samples, const VkImageViewType viewType) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = mips;
    info.arrayLayers = layers;
    info.samples = samples;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    allocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT * (extent.width * extent.height >= 1024u * 1024u);

    GpuImage image;
    check(vmaCreateImage(allocator, &info, &allocInfo, &image.image, &image.allocation, nullptr), "vmaCreateImage");

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image.image;
    viewInfo.viewType = viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspect, 0, mips, 0, layers};
    check(vkCreateImageView(device, &viewInfo, nullptr, &image.view), "vkCreateImageView");

    image.format = format;
    image.extent = extent;
    image.layers = layers;
    image.mips = mips;
    return image;
}

void VkContext::destroyImage(GpuImage &image) {
    if (image.view) vkDestroyImageView(device, image.view, nullptr);
    if (image.image) vmaDestroyImage(allocator, image.image, image.allocation);
    image = {};
}

void VkContext::immediateSubmit(const std::function<void(VkCommandBuffer)> &record) {
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = uploadPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd;
    check(vkAllocateCommandBuffers(device, &allocInfo, &cmd), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &begin);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    check(vkQueueSubmit2(queue, 1, &submit, uploadFence), "vkQueueSubmit2");
    vkWaitForFences(device, 1, &uploadFence, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &uploadFence);
    vkFreeCommandBuffers(device, uploadPool, 1, &cmd);
}

void VkContext::shutdown() {
    if (!device) return;
    vkDeviceWaitIdle(device);
    for (Frame &frame : frames) {
        vkDestroyCommandPool(device, frame.pool, nullptr);
        vkDestroyFence(device, frame.inFlight, nullptr);
        vkDestroySemaphore(device, frame.imageAcquired, nullptr);
    }
    for (PendingDestroy &p : pendingDestroy) destroyBuffer(p.buffer);
    pendingDestroy.clear();
    vkDestroyCommandPool(device, uploadPool, nullptr);
    vkDestroyFence(device, uploadFence, nullptr);
    destroySwapchain();
    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
    vkDestroySurfaceKHR(instance, surface, nullptr);
    if (messenger) {
        const auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance, messenger, nullptr);
    }
    vkDestroyInstance(instance, nullptr);
    device = VK_NULL_HANDLE;
}
