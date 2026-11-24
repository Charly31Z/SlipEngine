#include "SlipEngine/render/vulkan/VulkanSwapchain.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"

#include <algorithm>
#include <iostream>

VulkanSwapchain::~VulkanSwapchain() {
    Shutdown();
}

bool VulkanSwapchain::Initialize(VulkanContext* context, int width, int height, bool vsync) {
    m_Context = context;
    m_VSync = vsync;

    if (!m_Context || !m_Context->IsValid() || m_Context->GetSurface() == VK_NULL_HANDLE) {
        return false;
    }

    if (!CreateSwapchainInternal(width, height)) {
        return false;
    }
    if (!CreateRenderPass()) {
        return false;
    }
    if (!CreateFramebuffers()) {
        return false;
    }
    return true;
}

void VulkanSwapchain::Shutdown() {
    if (!m_Context || m_Context->GetDevice() == VK_NULL_HANDLE) {
        return;
    }

    VkDevice device = m_Context->GetDevice();
    CleanupSwapchainOnly();

    if (m_RenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, m_RenderPass, nullptr);
        m_RenderPass = VK_NULL_HANDLE;
    }

    m_Context = nullptr;
}

void VulkanSwapchain::CleanupSwapchainOnly() {
    if (!m_Context || m_Context->GetDevice() == VK_NULL_HANDLE) return;
    VkDevice device = m_Context->GetDevice();

    for (VkFramebuffer fb : m_Framebuffers) {
        if (fb != VK_NULL_HANDLE) vkDestroyFramebuffer(device, fb, nullptr);
    }
    m_Framebuffers.clear();

    for (VkImageView view : m_ImageViews) {
        if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
    }
    m_ImageViews.clear();
    m_Images.clear();

    if (m_Swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }
}

bool VulkanSwapchain::Recreate(int width, int height) {
    if (!m_Context || !m_Context->IsValid()) return false;
    if (width <= 0 || height <= 0) return false;

    m_Context->WaitIdle();
    CleanupSwapchainOnly();

    if (!CreateSwapchainInternal(width, height)) {
        return false;
    }
    if (!CreateFramebuffers()) {
        return false;
    }
    m_OutOfDate = false;
    return true;
}

void VulkanSwapchain::SetVSync(bool enabled) {
    if (m_VSync != enabled) {
        m_VSync = enabled;
        m_OutOfDate = true;
    }
}

bool VulkanSwapchain::CreateSwapchainInternal(int width, int height) {
    VkPhysicalDevice phys = m_Context->GetPhysicalDevice();
    VkSurfaceKHR surface = m_Context->GetSurface();
    VkDevice device = m_Context->GetDevice();

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats.empty()
        ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR }
        : formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }
    m_ImageFormat = chosenFormat.format;
    m_ColorSpace = chosenFormat.colorSpace;

    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &presentModeCount, presentModes.data());

    VkPresentModeKHR chosenMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!m_VSync) {
        bool hasMailbox = false;
        bool hasImmediate = false;
        for (VkPresentModeKHR m : presentModes) {
            if (m == VK_PRESENT_MODE_MAILBOX_KHR) hasMailbox = true;
            if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true;
        }
        if (hasMailbox) chosenMode = VK_PRESENT_MODE_MAILBOX_KHR;
        else if (hasImmediate) chosenMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    }

    if (caps.currentExtent.width != UINT32_MAX) {
        m_Extent = caps.currentExtent;
    } else {
        m_Extent.width = std::clamp(static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        m_Extent.height = std::clamp(static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    if (m_Extent.width == 0 || m_Extent.height == 0) {
        return false;
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) {
        imageCount = caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = m_ImageFormat;
    createInfo.imageColorSpace = m_ColorSpace;
    createInfo.imageExtent = m_Extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    m_SupportsTransferDst = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0;
    if (m_SupportsTransferDst) {
        createInfo.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = caps.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = chosenMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    VkResult res = vkCreateSwapchainKHR(device, &createInfo, nullptr, &m_Swapchain);
    if (res != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] vkCreateSwapchainKHR fallo (VkResult=" << res << ")" << std::endl;
        return false;
    }

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(device, m_Swapchain, &actualCount, nullptr);
    m_Images.resize(actualCount);
    vkGetSwapchainImagesKHR(device, m_Swapchain, &actualCount, m_Images.data());

    m_ImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_ImageFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device, &viewInfo, nullptr, &m_ImageViews[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanSwapchain] vkCreateImageView fallo." << std::endl;
            return false;
        }
    }

    return true;
}

bool VulkanSwapchain::CreateRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_ImageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{};
    rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpInfo.attachmentCount = 1;
    rpInfo.pAttachments = &colorAttachment;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;

    if (vkCreateRenderPass(m_Context->GetDevice(), &rpInfo, nullptr, &m_RenderPass) != VK_SUCCESS) {
        std::cerr << "[VulkanSwapchain] vkCreateRenderPass fallo." << std::endl;
        return false;
    }
    return true;
}

bool VulkanSwapchain::CreateFramebuffers() {
    VkDevice device = m_Context->GetDevice();
    m_Framebuffers.resize(m_ImageViews.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < m_ImageViews.size(); ++i) {
        VkImageView attachments[] = { m_ImageViews[i] };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_RenderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = attachments;
        fbInfo.width = m_Extent.width;
        fbInfo.height = m_Extent.height;
        fbInfo.layers = 1;

        if (vkCreateFramebuffer(device, &fbInfo, nullptr, &m_Framebuffers[i]) != VK_SUCCESS) {
            std::cerr << "[VulkanSwapchain] vkCreateFramebuffer fallo." << std::endl;
            return false;
        }
    }
    return true;
}

bool VulkanSwapchain::AcquireNextImage(VkSemaphore imageAvailableSemaphore, uint32_t& outImageIndex) {
    if (m_Swapchain == VK_NULL_HANDLE) return false;

    VkResult res = vkAcquireNextImageKHR(
        m_Context->GetDevice(),
        m_Swapchain,
        UINT64_MAX,
        imageAvailableSemaphore,
        VK_NULL_HANDLE,
        &outImageIndex
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR) {
        m_OutOfDate = true;
        return false;
    }
    if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
        return false;
    }
    return true;
}

bool VulkanSwapchain::Present(VkSemaphore renderFinishedSemaphore, uint32_t imageIndex) {
    if (m_Swapchain == VK_NULL_HANDLE) return false;

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_Swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult res = vkQueuePresentKHR(m_Context->GetGraphicsQueue(), &presentInfo);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        m_OutOfDate = true;
        return res == VK_SUBOPTIMAL_KHR;
    }
    return res == VK_SUCCESS;
}
