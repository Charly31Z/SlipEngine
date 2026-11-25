#pragma once

#ifndef SLIP_VULKAN_TEXTURE_H
#define SLIP_VULKAN_TEXTURE_H

#include "SlipEngine/core/SlipExport.h"
#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif
#include <cstdint>

typedef struct VmaAllocation_T* VmaAllocation;

class VulkanContext;

class SLIPENGINE_API VulkanTexture {
public:
    VulkanTexture() = default;
    ~VulkanTexture() = default;

    bool CreateImage2D(
        VulkanContext* context,
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        bool createSampler = true,
        VkFilter filter = VK_FILTER_LINEAR,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        uint32_t mipLevels = 1
    );

    bool CreateFromRGBA8(
        VulkanContext* context,
        const uint8_t* pixels,
        uint32_t width,
        uint32_t height,
        bool srgb = false,
        VkFilter filter = VK_FILTER_LINEAR,
        VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT
    );

    void Destroy(VulkanContext* context);

    static void TransitionImageLayout(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT
    );

    VkImage GetImage() const { return m_Image; }
    VkImageView GetImageView() const { return m_ImageView; }
    VkSampler GetSampler() const { return m_Sampler; }
    VkFormat GetFormat() const { return m_Format; }
    uint32_t GetWidth() const { return m_Width; }
    uint32_t GetHeight() const { return m_Height; }
    bool IsValid() const { return m_Image != VK_NULL_HANDLE && m_ImageView != VK_NULL_HANDLE; }

private:
    VkImage m_Image = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    VkImageView m_ImageView = VK_NULL_HANDLE;
    VkSampler m_Sampler = VK_NULL_HANDLE;
    VkFormat m_Format = VK_FORMAT_UNDEFINED;
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    uint32_t m_MipLevels = 1;
};

#endif // !SLIP_VULKAN_TEXTURE_H
