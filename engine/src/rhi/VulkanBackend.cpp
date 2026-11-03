// ErMao Engine - Vulkan 阶段一实现
#include "rhi/VulkanBackend.h"

#if defined(ERMAO_HAS_VULKAN) && ERMAO_HAS_VULKAN

#include "core/FileSystem.h"
#include "core/Log.h"
#include "platform/Window.h"

#define VK_USE_PLATFORM_WIN32_KHR
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <vector>

#pragma comment(lib, "vulkan-1.lib")

namespace erm {

namespace {

// 由 dxc -spirv 生成的三角形着色器（见 engine/shaders/vk/*.hlsl）
#include "rhi/VulkanTriangleSpirv.h"

bool SaveBgraPng(const std::string& path, u32 width, u32 height, const u8* pixels) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
        return false;
    }
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;
    do {
        if (FAILED(factory->CreateStream(&stream))) break;
        const std::wstring wide = FileSystem::Utf8ToWide(path);
        if (FAILED(stream->InitializeFromFilename(wide.c_str(), GENERIC_WRITE))) break;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) break;
        if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) break;
        if (FAILED(frame->Initialize(nullptr))) break;
        if (FAILED(frame->SetSize(width, height))) break;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format))) break;
        if (FAILED(frame->WritePixels(height, width * 4, width * height * 4, const_cast<BYTE*>(pixels)))) break;
        if (FAILED(frame->Commit())) break;
        if (FAILED(encoder->Commit())) break;
        ok = true;
    } while (false);
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    factory->Release();
    return ok;
}

} // namespace

struct VulkanDemo::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    u32 queueFamily = 0;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D extent{};
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
    VkFence inFlight = VK_NULL_HANDLE;
    VkShaderModule vertModule = VK_NULL_HANDLE;
    VkShaderModule fragModule = VK_NULL_HANDLE;

    ~Impl() { Shutdown(); }

    bool CreateInstance() {
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName = "ErMao Vulkan Demo";
        app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app.pEngineName = "ErMao";
        app.engineVersion = VK_MAKE_VERSION(0, 6, 0);
        app.apiVersion = VK_API_VERSION_1_1;

        const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
        VkInstanceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = 2;
        info.ppEnabledExtensionNames = extensions;
        if (vkCreateInstance(&info, nullptr, &instance) != VK_SUCCESS) {
            LogError("[Vulkan] vkCreateInstance 失败");
            return false;
        }
        LogInfo("[Vulkan] instance 创建成功（API 1.1）");
        return true;
    }

    bool CreateSurface(HWND hwnd, HINSTANCE hinstance) {
        VkWin32SurfaceCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        info.hwnd = hwnd;
        info.hinstance = hinstance;
        if (vkCreateWin32SurfaceKHR(instance, &info, nullptr, &surface) != VK_SUCCESS) {
            LogError("[Vulkan] 创建 Win32 surface 失败");
            return false;
        }
        return true;
    }

    bool PickDevice() {
        u32 count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0) {
            LogError("[Vulkan] 没有可用物理设备");
            return false;
        }
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());

        for (VkPhysicalDevice dev : devices) {
            u32 familyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, nullptr);
            std::vector<VkQueueFamilyProperties> families(familyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &familyCount, families.data());
            for (u32 i = 0; i < familyCount; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &present);
                if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) {
                    physical = dev;
                    queueFamily = i;
                    VkPhysicalDeviceProperties props{};
                    vkGetPhysicalDeviceProperties(dev, &props);
                    LogInfo("[Vulkan] 物理设备: %s", props.deviceName);
                    return true;
                }
            }
        }
        LogError("[Vulkan] 找不到同时支持图形与呈现的队列");
        return false;
    }

    bool CreateDevice() {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queueInfo;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        if (vkCreateDevice(physical, &info, nullptr, &device) != VK_SUCCESS) {
            LogError("[Vulkan] vkCreateDevice 失败");
            return false;
        }
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
        LogInfo("[Vulkan] 逻辑设备与队列就绪");
        return true;
    }

    bool CreateSwapchain(u32 width, u32 height) {
        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface, &caps);

        extent = caps.currentExtent;
        if (extent.width == 0xFFFFFFFFu) {
            extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
        }

        u32 formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, &formatCount, formats.data());
        swapFormat = formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats[0].format;
        VkColorSpaceKHR colorSpace = formats.empty() ? VK_COLORSPACE_SRGB_NONLINEAR_KHR : formats[0].colorSpace;

        VkSwapchainCreateInfoKHR info{};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = std::max(2u, caps.minImageCount);
        info.imageFormat = swapFormat;
        info.imageColorSpace = colorSpace;
        info.imageExtent = extent;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        info.clipped = VK_TRUE;
        if (vkCreateSwapchainKHR(device, &info, nullptr, &swapchain) != VK_SUCCESS) {
            LogError("[Vulkan] 创建 swapchain 失败");
            return false;
        }

        u32 imageCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, nullptr);
        images.resize(imageCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imageCount, images.data());

        views.resize(imageCount);
        framebuffers.resize(imageCount);
        return true;
    }

    bool CreateRenderPass() {
        VkAttachmentDescription color{};
        color.format = swapFormat;
        color.samples = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{};
        ref.attachment = 0;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &ref;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = 1;
        info.pAttachments = &color;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        return vkCreateRenderPass(device, &info, nullptr, &renderPass) == VK_SUCCESS;
    }

    bool CreateFramebuffers() {
        for (size_t i = 0; i < images.size(); ++i) {
            VkImageViewCreateInfo view{};
            view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view.image = images[i];
            view.viewType = VK_IMAGE_VIEW_TYPE_2D;
            view.format = swapFormat;
            view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            if (vkCreateImageView(device, &view, nullptr, &views[i]) != VK_SUCCESS) return false;

            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = renderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = &views[i];
            fb.width = extent.width;
            fb.height = extent.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(device, &fb, nullptr, &framebuffers[i]) != VK_SUCCESS) return false;
        }
        return true;
    }

    bool CreatePipeline() {
        VkShaderModuleCreateInfo vert{};
        vert.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        vert.codeSize = sizeof(kTriangleVertSpirv);
        vert.pCode = kTriangleVertSpirv;
        if (vkCreateShaderModule(device, &vert, nullptr, &vertModule) != VK_SUCCESS) return false;

        VkShaderModuleCreateInfo frag{};
        frag.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        frag.codeSize = sizeof(kTriangleFragSpirv);
        frag.pCode = kTriangleFragSpirv;
        if (vkCreateShaderModule(device, &frag, nullptr, &fragModule) != VK_SUCCESS) return false;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule;
        stages[0].pName = "VSMain";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule;
        stages[1].pName = "PSMain";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        VkRect2D scissor{{0, 0}, extent};
        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.pViewports = &viewport;
        viewportState.scissorCount = 1;
        viewportState.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisample{};
        multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = 0xF;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(device, &layout, nullptr, &pipelineLayout) != VK_SUCCESS) return false;

        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInput;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewportState;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pColorBlendState = &blend;
        info.layout = pipelineLayout;
        info.renderPass = renderPass;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) != VK_SUCCESS) {
            LogError("[Vulkan] 创建 graphics pipeline 失败");
            return false;
        }
        return true;
    }

    bool CreateCommandsAndSync() {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool.queueFamilyIndex = queueFamily;
        if (vkCreateCommandPool(device, &pool, nullptr, &commandPool) != VK_SUCCESS) return false;

        VkCommandBufferAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.commandPool = commandPool;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &alloc, &commandBuffer) != VK_SUCCESS) return false;

        VkSemaphoreCreateInfo sem{};
        sem.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fence{};
        fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        return vkCreateSemaphore(device, &sem, nullptr, &imageAvailable) == VK_SUCCESS &&
               vkCreateSemaphore(device, &sem, nullptr, &renderFinished) == VK_SUCCESS &&
               vkCreateFence(device, &fence, nullptr, &inFlight) == VK_SUCCESS;
    }

    void Transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkPipelineStageFlags src = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkPipelineStageFlags dst = VK_PIPELINE_STAGE_TRANSFER_BIT;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        if (to == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
            src = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = 0;
        }
        vkCmdPipelineBarrier(cmd, src, dst, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    bool Run(u32 frames, const std::string& screenshotPath) {
        if (!CreateRenderPass()) {
            LogError("[Vulkan] render pass 创建失败");
            return false;
        }
        if (!CreateFramebuffers() || !CreatePipeline() || !CreateCommandsAndSync()) {
            LogError("[Vulkan] framebuffer/pipeline/命令资源创建失败");
            return false;
        }

        std::vector<u8> readback;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        bool stagingCoherent = false;
        auto releaseStaging = [&]() {
            if (stagingBuffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(device, stagingBuffer, nullptr);
                stagingBuffer = VK_NULL_HANDLE;
            }
            if (stagingMemory != VK_NULL_HANDLE) {
                vkFreeMemory(device, stagingMemory, nullptr);
                stagingMemory = VK_NULL_HANDLE;
            }
        };
        if (!screenshotPath.empty()) {
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = static_cast<VkDeviceSize>(extent.width) * extent.height * 4;
            bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
                LogError("[Vulkan] 截图 staging buffer 创建失败");
                return false;
            }
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(device, stagingBuffer, &req);
            VkMemoryAllocateInfo alloc{};
            alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            alloc.allocationSize = req.size;
            VkPhysicalDeviceMemoryProperties memProps{};
            vkGetPhysicalDeviceMemoryProperties(physical, &memProps);
            u32 fallbackIndex = ~0u;
            bool found = false;
            for (u32 i = 0; i < memProps.memoryTypeCount; ++i) {
                if (!(req.memoryTypeBits & (1u << i))) continue;
                const VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
                if (!(flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
                if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
                    alloc.memoryTypeIndex = i;
                    stagingCoherent = true;
                    found = true;
                    break;
                }
                if (fallbackIndex == ~0u) fallbackIndex = i;
            }
            if (!found && fallbackIndex != ~0u) {
                alloc.memoryTypeIndex = fallbackIndex;
                stagingCoherent = false;
                found = true;
            }
            if (!found) {
                LogError("[Vulkan] 找不到 HOST_VISIBLE 的截图 staging 内存类型");
                releaseStaging();
                return false;
            }
            if (vkAllocateMemory(device, &alloc, nullptr, &stagingMemory) != VK_SUCCESS) {
                LogError("[Vulkan] 截图 staging 内存分配失败");
                releaseStaging();
                return false;
            }
            if (vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
                LogError("[Vulkan] 截图 staging 内存绑定失败");
                releaseStaging();
                return false;
            }
        }

        u32 imageIndex = 0;
        for (u32 frame = 0; frame < frames; ++frame) {
            vkWaitForFences(device, 1, &inFlight, VK_TRUE, UINT64_MAX);
            vkResetFences(device, 1, &inFlight);

            const VkResult acquire = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailable,
                                                           VK_NULL_HANDLE, &imageIndex);
            if (acquire != VK_SUCCESS) {
                LogError("[Vulkan] vkAcquireNextImageKHR 失败: %d", acquire);
                releaseStaging();
                return false;
            }

            vkResetCommandBuffer(commandBuffer, 0);
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            vkBeginCommandBuffer(commandBuffer, &begin);

            VkClearValue clear{};
            clear.color = {{0.05f, 0.07f, 0.12f, 1.0f}};
            VkRenderPassBeginInfo pass{};
            pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            pass.renderPass = renderPass;
            pass.framebuffer = framebuffers[imageIndex];
            pass.renderArea = {{0, 0}, extent};
            pass.clearValueCount = 1;
            pass.pClearValues = &clear;
            vkCmdBeginRenderPass(commandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            vkCmdDraw(commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(commandBuffer);

            const bool capture = (frame + 1 == frames) && stagingBuffer != VK_NULL_HANDLE;
            if (capture) {
                Transition(commandBuffer, images[imageIndex], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {extent.width, extent.height, 1};
                vkCmdCopyImageToBuffer(commandBuffer, images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       stagingBuffer, 1, &region);
                Transition(commandBuffer, images[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
            }
            vkEndCommandBuffer(commandBuffer);

            VkSubmitInfo submit{};
            submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            VkSemaphore waitSemaphores[] = {imageAvailable};
            VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
            submit.waitSemaphoreCount = 1;
            submit.pWaitSemaphores = waitSemaphores;
            submit.pWaitDstStageMask = waitStages;
            submit.commandBufferCount = 1;
            submit.pCommandBuffers = &commandBuffer;
            VkSemaphore signalSemaphores[] = {renderFinished};
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = signalSemaphores;
            if (vkQueueSubmit(queue, 1, &submit, inFlight) != VK_SUCCESS) {
                LogError("[Vulkan] vkQueueSubmit 失败");
                releaseStaging();
                return false;
            }

            VkPresentInfoKHR present{};
            present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
            present.waitSemaphoreCount = 1;
            present.pWaitSemaphores = signalSemaphores;
            present.swapchainCount = 1;
            present.pSwapchains = &swapchain;
            present.pImageIndices = &imageIndex;
            vkQueuePresentKHR(queue, &present);
            vkQueueWaitIdle(queue);
        }

        if (stagingBuffer != VK_NULL_HANDLE) {
            void* mapped = nullptr;
            if (vkMapMemory(device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS || !mapped) {
                LogError("[Vulkan] 截图 staging 内存映射失败");
                releaseStaging();
                return false;
            }
            if (!stagingCoherent) {
                VkMappedMemoryRange range{};
                range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
                range.memory = stagingMemory;
                range.offset = 0;
                range.size = VK_WHOLE_SIZE;
                if (vkInvalidateMappedMemoryRanges(device, 1, &range) != VK_SUCCESS) {
                    LogError("[Vulkan] 截图 staging 内存失效（invalidate）失败");
                    vkUnmapMemory(device, stagingMemory);
                    releaseStaging();
                    return false;
                }
            }
            readback.assign(static_cast<u8*>(mapped), static_cast<u8*>(mapped) + extent.width * extent.height * 4);
            vkUnmapMemory(device, stagingMemory);
            releaseStaging();
            if (!readback.empty() && !screenshotPath.empty()) {
                // Vulkan 的 swapchain 常见格式是 BGRA，WIC 直接按 BGRA 写
                if (SaveBgraPng(screenshotPath, extent.width, extent.height, readback.data())) {
                    LogInfo("[Vulkan] 截图已保存: %s (%ux%u)", screenshotPath.c_str(), extent.width, extent.height);
                } else {
                    LogError("[Vulkan] 截图保存失败: %s", screenshotPath.c_str());
                    return false;
                }
            }
        }

        LogInfo("[Vulkan] 渲染 %u 帧完成（%ux%u）", frames, extent.width, extent.height);
        return true;
    }

    void Shutdown() {
        if (!device && !instance) return;
        if (device) {
            vkDeviceWaitIdle(device);
            if (vertModule) vkDestroyShaderModule(device, vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(device, fragModule, nullptr);
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            if (renderPass) vkDestroyRenderPass(device, renderPass, nullptr);
            if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
            if (imageAvailable) vkDestroySemaphore(device, imageAvailable, nullptr);
            if (renderFinished) vkDestroySemaphore(device, renderFinished, nullptr);
            if (inFlight) vkDestroyFence(device, inFlight, nullptr);
            for (size_t i = 0; i < framebuffers.size(); ++i) {
                if (framebuffers[i]) vkDestroyFramebuffer(device, framebuffers[i], nullptr);
            }
            for (size_t i = 0; i < views.size(); ++i) {
                if (views[i]) vkDestroyImageView(device, views[i], nullptr);
            }
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);

        vertModule = fragModule = VK_NULL_HANDLE;
        pipeline = VK_NULL_HANDLE;
        pipelineLayout = VK_NULL_HANDLE;
        renderPass = VK_NULL_HANDLE;
        commandPool = VK_NULL_HANDLE;
        imageAvailable = renderFinished = VK_NULL_HANDLE;
        inFlight = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
        framebuffers.clear();
        views.clear();
        images.clear();
        device = VK_NULL_HANDLE;
        surface = VK_NULL_HANDLE;
        instance = VK_NULL_HANDLE;
    }
};

bool VulkanDemo::IsAvailable() { return true; }

bool VulkanDemo::Init(Window& window) {
    if (!m_impl) m_impl = new Impl();
    HWND hwnd = static_cast<HWND>(window.NativeHandle());
    HINSTANCE hinstance = GetModuleHandleW(nullptr);

    if (!m_impl->CreateInstance()) return false;
    if (!m_impl->CreateSurface(hwnd, hinstance)) return false;
    if (!m_impl->PickDevice()) return false;
    if (!m_impl->CreateDevice()) return false;
    if (!m_impl->CreateSwapchain(window.Width(), window.Height())) return false;
    m_device = m_impl->device;
    return true;
}

bool VulkanDemo::Run(u32 frames, const std::string& screenshotPath) {
    if (!m_impl || !m_impl->device) return false;
    return m_impl->Run(frames, screenshotPath);
}

void VulkanDemo::Shutdown() {
    if (m_impl) {
        delete m_impl; // ~Impl 里调用 Shutdown()
        m_impl = nullptr;
    }
    m_device = nullptr;
}

} // namespace erm
#else // 没有 Vulkan SDK：空实现，保证 D3D11 构建不受影响
#include "core/Log.h"

namespace erm {

bool VulkanDemo::IsAvailable() { return false; }

bool VulkanDemo::Init(Window&) {
    LogWarn("[Vulkan] 本机未安装 Vulkan SDK，--vulkan-test 不可用（引擎仍走 D3D11）");
    return false;
}
bool VulkanDemo::Run(u32, const std::string&) { return false; }
void VulkanDemo::Shutdown() {}

} // namespace erm

#endif // ERMAO_HAS_VULKAN
