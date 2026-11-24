#pragma once

#ifndef SLIP_VULKAN_RHI_H
#define SLIP_VULKAN_RHI_H

#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"
#include "SlipEngine/render/vulkan/VulkanSwapchain.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

typedef struct VmaAllocation_T* VmaAllocation;

class SlipLevel;
class VulkanSceneRenderer;

class SLIPENGINE_API VulkanRHI final : public ISlipRHI {
public:
    VulkanRHI();
    ~VulkanRHI() override;
    VulkanRHI(const VulkanRHI&) = delete;
    VulkanRHI& operator=(const VulkanRHI&) = delete;

    GraphicsAPI GetAPI() const override { return GraphicsAPI::Vulkan; }
    bool Initialize(SDL_Window* window, bool headless = false) override;
    void Shutdown() override;
    bool BeginFrame() override;
    void EndFrame() override;
    void OnResize(int width, int height) override;
    void SetVSync(bool enabled) override;
    void WaitIdle() override;
    const RHIHardwareCapabilities& GetCapabilities() const override;

    void BeginSwapchainPass();
    bool RenderSceneToViewport(SlipLevel* level, const glm::mat4& view, const glm::mat4& glProjection, const glm::vec3& cameraPos);
    bool RenderSceneToSwapchain(SlipLevel* level, const glm::mat4& view, const glm::mat4& glProjection, const glm::vec3& cameraPos);
    void InvalidateSceneCache();
    void ResetPathTracingAccumulation();
    uint32_t GetPathTracingSampleCount() const;
    uint32_t GetLastDrawCallCount() const;
    uint32_t GetLastRtInstanceCount() const;
    void SetSelectedEntity(const class SlipEntity* entity);
    VulkanSceneRenderer* GetSceneRenderer() const { return m_SceneRenderer.get(); }

    // Offscreen viewport target (for Editor ViewportPanel)
    void EnsureViewportSize(int width, int height);
    VkImageView GetViewportImageView() const { return m_ViewportView; }
    VkSampler GetViewportSampler() const { return m_ViewportSampler; }
    uint64_t GetViewportGeneration() const { return m_ViewportGeneration; }
    int GetViewportWidth() const { return m_ViewportWidth; }
    int GetViewportHeight() const { return m_ViewportHeight; }

    // Accessors for Editor ImGui Vulkan backend
    VulkanContext& GetContext() { return m_Context; }
    const VulkanContext& GetContext() const { return m_Context; }
    VkInstance GetInstance() const { return m_Context.GetInstance(); }
    VkPhysicalDevice GetPhysicalDevice() const { return m_Context.GetPhysicalDevice(); }
    VkDevice GetDevice() const { return m_Context.GetDevice(); }
    uint32_t GetGraphicsQueueFamily() const { return m_Context.GetGraphicsQueueFamily(); }
    VkQueue GetGraphicsQueue() const { return m_Context.GetGraphicsQueue(); }
    VkDescriptorPool GetDescriptorPool() const { return m_Context.GetDescriptorPool(); }
    VkRenderPass GetSwapchainRenderPass() const { return m_Swapchain.GetRenderPass(); }
    uint32_t GetSwapchainImageCount() const { return m_Swapchain.GetImageCount(); }
    VkCommandBuffer GetCurrentCommandBuffer() const;
    bool IsFrameActive() const { return m_FrameActive; }
    PFN_vkVoidFunction LoadInstanceFunction(const char* name) const { return m_Context.LoadInstanceFunction(name); }

private:
    struct FrameSync {
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlightFence = VK_NULL_HANDLE;
    };

    bool CreateFrameSyncObjects();
    void DestroyFrameSyncObjects();
    bool CreateViewportResources(int width, int height);
    void DestroyViewportResources();
    void RecordViewportClearAndTransition(VkCommandBuffer cmd);

    VulkanContext m_Context;
    VulkanSwapchain m_Swapchain;
    std::unique_ptr<VulkanSceneRenderer> m_SceneRenderer;

    SDL_Window* m_Window = nullptr;
    bool m_Headless = false;
    bool m_Initialized = false;
    bool m_FrameActive = false;
    bool m_SwapchainPassActive = false;
    bool m_SwapchainBlitted = false;
    bool m_PendingResize = false;
    int m_PendingWidth = 0;
    int m_PendingHeight = 0;
    bool m_VSyncEnabled = false;

    std::vector<FrameSync> m_Frames;
    uint32_t m_CurrentFrame = 0;
    uint32_t m_CurrentImageIndex = 0;

    // Offscreen viewport image (rendered into, then transitioned to SHADER_READ_ONLY_OPTIMAL for ImGui)
    VkImage m_ViewportImage = VK_NULL_HANDLE;
    VmaAllocation m_ViewportAlloc = VK_NULL_HANDLE;
    VkImageView m_ViewportView = VK_NULL_HANDLE;
    VkSampler m_ViewportSampler = VK_NULL_HANDLE;
    int m_ViewportWidth = 0;
    int m_ViewportHeight = 0;
    uint64_t m_ViewportGeneration = 0;
    bool m_ViewportNeedsClear = true;
};

#endif // !SLIP_VULKAN_RHI_H
