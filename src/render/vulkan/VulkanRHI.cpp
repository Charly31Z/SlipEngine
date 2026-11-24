#include "SlipEngine/render/vulkan/VulkanRHI.h"
#include "SlipEngine/render/vulkan/VulkanSceneRenderer.h"
#include "SlipEngine/render/vulkan/VulkanTexture.h"

#include <SDL2/SDL.h>
#define NO_SDL_VULKAN_TYPEDEFS
#include <SDL2/SDL_vulkan.h>
#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#include <vk_mem_alloc.h>

#include <algorithm>
#include <iostream>

VulkanRHI::VulkanRHI() = default;

VulkanRHI::~VulkanRHI() {
    Shutdown();
}

bool VulkanRHI::Initialize(SDL_Window* window, bool headless) {
    if (m_Initialized) return true;

    m_Window = window;
    m_Headless = headless;

    if (!m_Context.Initialize(window, headless)) {
        return false;
    }

    if (!m_Headless && m_Window != nullptr) {
        int w = 1280, h = 720;
        SDL_Vulkan_GetDrawableSize(m_Window, &w, &h);
        if (w <= 0 || h <= 0) {
            SDL_GetWindowSize(m_Window, &w, &h);
        }

        if (!m_Swapchain.Initialize(&m_Context, std::max(1, w), std::max(1, h), m_VSyncEnabled)) {
            Shutdown();
            return false;
        }

        if (!CreateFrameSyncObjects()) {
            Shutdown();
            return false;
        }

        if (!CreateViewportResources(640, 360)) {
            Shutdown();
            return false;
        }
    }

    m_SceneRenderer = std::make_unique<VulkanSceneRenderer>();
    if (!m_SceneRenderer->Initialize(&m_Context)) {
        std::cerr << "[VulkanRHI] Advertencia: VulkanSceneRenderer fallo al inicializar." << std::endl;
        m_SceneRenderer.reset();
    }

    m_Initialized = true;
    return true;
}

void VulkanRHI::Shutdown() {
    if (!m_Initialized && !m_Context.IsValid()) return;

    WaitIdle();
    if (m_SceneRenderer) {
        m_SceneRenderer->Shutdown();
        m_SceneRenderer.reset();
    }
    DestroyViewportResources();
    DestroyFrameSyncObjects();
    m_Swapchain.Shutdown();
    m_Context.Shutdown();

    m_Window = nullptr;
    m_FrameActive = false;
    m_SwapchainPassActive = false;
    m_SwapchainBlitted = false;
    m_Initialized = false;
}

const RHIHardwareCapabilities& VulkanRHI::GetCapabilities() const {
    return m_Context.GetCapabilities();
}

uint32_t VulkanRHI::GetLastDrawCallCount() const {
    return m_SceneRenderer ? m_SceneRenderer->GetLastDrawCallCount() : 0;
}

uint32_t VulkanRHI::GetLastRtInstanceCount() const {
    return m_SceneRenderer ? m_SceneRenderer->GetLastRtInstanceCount() : 0;
}

void VulkanRHI::InvalidateSceneCache() {
    if (m_SceneRenderer) {
        m_SceneRenderer->InvalidateSceneCache();
    }
}

void VulkanRHI::ResetPathTracingAccumulation() {
    if (m_SceneRenderer) {
        m_SceneRenderer->ResetPathTracingAccumulation();
    }
}

uint32_t VulkanRHI::GetPathTracingSampleCount() const {
    return m_SceneRenderer ? m_SceneRenderer->GetPathTracingSampleCount() : 0;
}

bool VulkanRHI::CreateFrameSyncObjects() {
    VkDevice device = m_Context.GetDevice();
    VkCommandPool cmdPool = m_Context.GetCommandPool();

    m_Frames.resize(VulkanContext::kMaxFramesInFlight);
    for (uint32_t i = 0; i < VulkanContext::kMaxFramesInFlight; ++i) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = cmdPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(device, &allocInfo, &m_Frames[i].commandBuffer) != VK_SUCCESS) {
            return false;
        }

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (vkCreateSemaphore(device, &semInfo, nullptr, &m_Frames[i].imageAvailable) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semInfo, nullptr, &m_Frames[i].renderFinished) != VK_SUCCESS) {
            return false;
        }

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        if (vkCreateFence(device, &fenceInfo, nullptr, &m_Frames[i].inFlightFence) != VK_SUCCESS) {
            return false;
        }
    }

    return true;
}

void VulkanRHI::DestroyFrameSyncObjects() {
    VkDevice device = m_Context.GetDevice();
    if (device == VK_NULL_HANDLE) {
        m_Frames.clear();
        return;
    }

    for (auto& frame : m_Frames) {
        if (frame.inFlightFence != VK_NULL_HANDLE) vkDestroyFence(device, frame.inFlightFence, nullptr);
        if (frame.renderFinished != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.renderFinished, nullptr);
        if (frame.imageAvailable != VK_NULL_HANDLE) vkDestroySemaphore(device, frame.imageAvailable, nullptr);
        if (frame.commandBuffer != VK_NULL_HANDLE && m_Context.GetCommandPool() != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, m_Context.GetCommandPool(), 1, &frame.commandBuffer);
        }
    }
    m_Frames.clear();
    m_CurrentFrame = 0;
}

bool VulkanRHI::CreateViewportResources(int width, int height) {
    DestroyViewportResources();

    width = std::max(1, width);
    height = std::max(1, height);

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT |
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocCreate{};
    allocCreate.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(m_Context.GetAllocator(), &imgInfo, &allocCreate, &m_ViewportImage, &m_ViewportAlloc, nullptr) != VK_SUCCESS) {
        std::cerr << "[VulkanRHI] Error creando VkImage offscreen del viewport." << std::endl;
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_ViewportImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(m_Context.GetDevice(), &viewInfo, nullptr, &m_ViewportView) != VK_SUCCESS) {
        DestroyViewportResources();
        return false;
    }

    if (m_ViewportSampler == VK_NULL_HANDLE) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        if (vkCreateSampler(m_Context.GetDevice(), &samplerInfo, nullptr, &m_ViewportSampler) != VK_SUCCESS) {
            DestroyViewportResources();
            return false;
        }
    }

    m_ViewportWidth = width;
    m_ViewportHeight = height;
    m_ViewportNeedsClear = true;
    ++m_ViewportGeneration;

    VkCommandBuffer cmd = m_Context.BeginSingleTimeCommands();
    RecordViewportClearAndTransition(cmd);
    m_Context.EndSingleTimeCommands(cmd);
    m_ViewportNeedsClear = false;

    return true;
}

void VulkanRHI::DestroyViewportResources() {
    VkDevice device = m_Context.GetDevice();
    if (device == VK_NULL_HANDLE) return;

    if (m_ViewportView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_ViewportView, nullptr);
        m_ViewportView = VK_NULL_HANDLE;
    }
    if (m_ViewportImage != VK_NULL_HANDLE && m_ViewportAlloc != VK_NULL_HANDLE) {
        vmaDestroyImage(m_Context.GetAllocator(), m_ViewportImage, m_ViewportAlloc);
        m_ViewportImage = VK_NULL_HANDLE;
        m_ViewportAlloc = VK_NULL_HANDLE;
    }
    if (m_ViewportSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_ViewportSampler, nullptr);
        m_ViewportSampler = VK_NULL_HANDLE;
    }
    m_ViewportWidth = 0;
    m_ViewportHeight = 0;
}

void VulkanRHI::RecordViewportClearAndTransition(VkCommandBuffer cmd) {
    if (m_ViewportImage == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) return;

    VulkanTexture::TransitionImageLayout(cmd, m_ViewportImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkClearColorValue clearColor{};
    clearColor.float32[0] = 0.08f;
    clearColor.float32[1] = 0.10f;
    clearColor.float32[2] = 0.14f;
    clearColor.float32[3] = 1.0f;

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    vkCmdClearColorImage(cmd, m_ViewportImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearColor, 1, &range);

    VulkanTexture::TransitionImageLayout(cmd, m_ViewportImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
}

void VulkanRHI::EnsureViewportSize(int width, int height) {
    if (m_Headless || !m_Initialized) return;
    if (width <= 0 || height <= 0) return;
    if (width == m_ViewportWidth && height == m_ViewportHeight && m_ViewportImage != VK_NULL_HANDLE) {
        return;
    }

    WaitIdle();
    CreateViewportResources(width, height);
}

VkCommandBuffer VulkanRHI::GetCurrentCommandBuffer() const {
    if (!m_FrameActive || m_Frames.empty()) return VK_NULL_HANDLE;
    return m_Frames[m_CurrentFrame].commandBuffer;
}

bool VulkanRHI::BeginFrame() {
    if (!m_Initialized) return false;
    if (m_Headless || m_Frames.empty()) return true;

    if (m_PendingResize || m_Swapchain.IsOutOfDate()) {
        int w = m_PendingWidth;
        int h = m_PendingHeight;
        if (w <= 0 || h <= 0) {
            SDL_Vulkan_GetDrawableSize(m_Window, &w, &h);
        }
        if (w <= 0 || h <= 0) {
            return false;
        }
        if (!m_Swapchain.Recreate(w, h)) {
            return false;
        }
        m_PendingResize = false;
    }

    FrameSync& frame = m_Frames[m_CurrentFrame];
    VkDevice device = m_Context.GetDevice();

    vkWaitForFences(device, 1, &frame.inFlightFence, VK_TRUE, UINT64_MAX);

    if (!m_Swapchain.AcquireNextImage(frame.imageAvailable, m_CurrentImageIndex)) {
        if (m_Swapchain.IsOutOfDate()) {
            m_PendingResize = true;
        }
        return false;
    }

    vkResetFences(device, 1, &frame.inFlightFence);
    vkResetCommandBuffer(frame.commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
        return false;
    }

    if (m_ViewportNeedsClear) {
        RecordViewportClearAndTransition(frame.commandBuffer);
        m_ViewportNeedsClear = false;
    }

    m_FrameActive = true;
    m_SwapchainPassActive = false;
    m_SwapchainBlitted = false;
    return true;
}

void VulkanRHI::BeginSwapchainPass() {
    if (!m_Initialized || m_Headless || !m_FrameActive || m_SwapchainPassActive) return;

    FrameSync& frame = m_Frames[m_CurrentFrame];
    VkClearValue clearVal{};
    clearVal.color = { { 0.10f, 0.10f, 0.11f, 1.0f } };

    VkRenderPassBeginInfo rpBegin{};
    rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBegin.renderPass = m_Swapchain.GetRenderPass();
    rpBegin.framebuffer = m_Swapchain.GetFramebuffer(m_CurrentImageIndex);
    rpBegin.renderArea.offset = { 0, 0 };
    rpBegin.renderArea.extent = m_Swapchain.GetExtent();
    rpBegin.clearValueCount = 1;
    rpBegin.pClearValues = &clearVal;

    vkCmdBeginRenderPass(frame.commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
    m_SwapchainPassActive = true;
}

bool VulkanRHI::RenderSceneToViewport(SlipLevel* level, const glm::mat4& view, const glm::mat4& glProjection, const glm::vec3& cameraPos) {
    if (!m_Initialized || m_Headless || !m_FrameActive || m_SwapchainPassActive || !m_SceneRenderer) {
        return false;
    }
    if (m_ViewportImage == VK_NULL_HANDLE || m_ViewportView == VK_NULL_HANDLE || m_ViewportWidth <= 0 || m_ViewportHeight <= 0) {
        return false;
    }

    m_ViewportNeedsClear = false;
    return m_SceneRenderer->RenderScene(
        GetCurrentCommandBuffer(),
        m_CurrentFrame,
        level,
        view,
        glProjection,
        cameraPos,
        m_ViewportImage,
        m_ViewportView,
        static_cast<uint32_t>(m_ViewportWidth),
        static_cast<uint32_t>(m_ViewportHeight),
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        m_ViewportGeneration
    );
}

bool VulkanRHI::RenderSceneToSwapchain(SlipLevel* level, const glm::mat4& view, const glm::mat4& glProjection, const glm::vec3& cameraPos) {
    if (!m_Initialized || m_Headless || !m_FrameActive || m_SwapchainPassActive || !m_SceneRenderer) {
        return false;
    }

    VkExtent2D extent = m_Swapchain.GetExtent();
    if (extent.width == 0 || extent.height == 0) return false;

    EnsureViewportSize(static_cast<int>(extent.width), static_cast<int>(extent.height));
    if (!RenderSceneToViewport(level, view, glProjection, cameraPos)) {
        return false;
    }

    if (m_Swapchain.SupportsTransferDst()) {
        VkCommandBuffer cmd = GetCurrentCommandBuffer();
        VkImage scImage = m_Swapchain.GetImage(m_CurrentImageIndex);

        VulkanTexture::TransitionImageLayout(cmd, m_ViewportImage, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, scImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

        VkImageBlit blit{};
        blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.srcOffsets[0] = { 0, 0, 0 };
        blit.srcOffsets[1] = { m_ViewportWidth, m_ViewportHeight, 1 };
        blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        blit.dstOffsets[0] = { 0, 0, 0 };
        blit.dstOffsets[1] = { static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1 };

        vkCmdBlitImage(cmd, m_ViewportImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, scImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);

        VulkanTexture::TransitionImageLayout(cmd, m_ViewportImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
        VulkanTexture::TransitionImageLayout(cmd, scImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT);
        m_SwapchainBlitted = true;
        return true;
    }

    return false;
}

void VulkanRHI::EndFrame() {
    if (!m_Initialized || m_Headless || !m_FrameActive) return;

    FrameSync& frame = m_Frames[m_CurrentFrame];
    if (!m_SwapchainPassActive && !m_SwapchainBlitted) {
        BeginSwapchainPass();
    }
    if (m_SwapchainPassActive) {
        vkCmdEndRenderPass(frame.commandBuffer);
        m_SwapchainPassActive = false;
    }

    vkEndCommandBuffer(frame.commandBuffer);

    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &frame.imageAvailable;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame.commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &frame.renderFinished;

    vkQueueSubmit(m_Context.GetGraphicsQueue(), 1, &submitInfo, frame.inFlightFence);
    m_Swapchain.Present(frame.renderFinished, m_CurrentImageIndex);

    m_FrameActive = false;
    m_SwapchainBlitted = false;
    m_CurrentFrame = (m_CurrentFrame + 1) % VulkanContext::kMaxFramesInFlight;
}

void VulkanRHI::OnResize(int width, int height) {
    if (m_Headless || !m_Initialized) return;
    if (width > 0 && height > 0) {
        m_PendingWidth = width;
        m_PendingHeight = height;
        m_PendingResize = true;
    }
}

void VulkanRHI::SetVSync(bool enabled) {
    m_VSyncEnabled = enabled;
    if (!m_Headless && m_Initialized) {
        m_Swapchain.SetVSync(enabled);
    }
}

void VulkanRHI::WaitIdle() {
    m_Context.WaitIdle();
}

void VulkanRHI::SetSelectedEntity(const SlipEntity* entity) {
    if (m_SceneRenderer) {
        m_SceneRenderer->SetSelectedEntity(entity);
    }
}
