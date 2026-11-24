#pragma once

#ifndef SLIP_VULKAN_SWAPCHAIN_H
#define SLIP_VULKAN_SWAPCHAIN_H

#include "SlipEngine/core/SlipExport.h"
#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif
#include <cstdint>
#include <vector>

class VulkanContext;

class SLIPENGINE_API VulkanSwapchain {
public:
    VulkanSwapchain() = default;
    ~VulkanSwapchain();

    bool Initialize(VulkanContext* context, int width, int height, bool vsync);
    void Shutdown();

    bool Recreate(int width, int height);
    void SetVSync(bool enabled);

    bool AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& outImageIndex);
    bool Present(VkSemaphore renderFinishedSemaphore, uint32_t imageIndex);

    VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
    VkFormat GetImageFormat() const { return m_ImageFormat; }
    VkExtent2D GetExtent() const { return m_Extent; }
    VkRenderPass GetRenderPass() const { return m_RenderPass; }
    uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Images.size()); }
    VkFramebuffer GetFramebuffer(uint32_t index) const { return m_Framebuffers[index]; }
    VkImage GetImage(uint32_t index) const { return m_Images[index]; }
    bool SupportsTransferDst() const { return m_SupportsTransferDst; }
    bool IsOutOfDate() const { return m_OutOfDate; }
    void ClearOutOfDate() { m_OutOfDate = false; }

private:
    bool CreateSwapchainInternal(int width, int height);
    bool CreateRenderPass();
    bool CreateFramebuffers();
    void CleanupSwapchainOnly();

    VulkanContext* m_Context = nullptr;
    VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
    VkFormat m_ImageFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkColorSpaceKHR m_ColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D m_Extent{ 0, 0 };
    VkRenderPass m_RenderPass = VK_NULL_HANDLE;

    std::vector<VkImage> m_Images;
    std::vector<VkImageView> m_ImageViews;
    std::vector<VkFramebuffer> m_Framebuffers;

    bool m_VSync = false;
    bool m_OutOfDate = false;
    bool m_SupportsTransferDst = false;
};

#endif // !SLIP_VULKAN_SWAPCHAIN_H
