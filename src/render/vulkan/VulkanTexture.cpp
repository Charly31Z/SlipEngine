#include "SlipEngine/render/vulkan/VulkanTexture.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"

#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#include <vk_mem_alloc.h>

#include <algorithm>
#include <iostream>

namespace {

void FillLayoutStageAccess(VkImageLayout layout, VkPipelineStageFlags& outStage, VkAccessFlags& outAccess) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            outStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            outAccess = 0;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            outStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            outAccess = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            outStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            outAccess = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            outStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            outAccess = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            outStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            outAccess = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            outStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            outAccess = VK_ACCESS_SHADER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_GENERAL:
            outStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            outAccess = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR:
            outStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
            outAccess = 0;
            break;
        default:
            outStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            outAccess = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
            break;
    }
}

} // namespace

bool VulkanTexture::CreateImage2D(
    VulkanContext* context,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImageAspectFlags aspectMask,
    bool createSampler,
    VkFilter filter,
    VkSamplerAddressMode addressMode)
{
    if (!context || !context->IsValid() || width == 0 || height == 0) return false;
    Destroy(context);

    m_Width = width;
    m_Height = height;
    m_Format = format;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = format;
    imgInfo.extent = { width, height, 1 };
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = usage;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(context->GetAllocator(), &imgInfo, &allocInfo, &m_Image, &m_Allocation, nullptr) != VK_SUCCESS) {
        std::cerr << "[VulkanTexture] vmaCreateImage fallo (" << width << "x" << height << ")" << std::endl;
        Destroy(context);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_Image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context->GetDevice(), &viewInfo, nullptr, &m_ImageView) != VK_SUCCESS) {
        Destroy(context);
        return false;
    }

    if (createSampler) {
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = filter;
        samplerInfo.minFilter = filter;
        samplerInfo.mipmapMode = (filter == VK_FILTER_NEAREST) ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = addressMode;
        samplerInfo.addressModeV = addressMode;
        samplerInfo.addressModeW = addressMode;
        samplerInfo.maxLod = 1.0f;
        if (vkCreateSampler(context->GetDevice(), &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
            Destroy(context);
            return false;
        }
    }

    return true;
}

bool VulkanTexture::CreateFromRGBA8(
    VulkanContext* context,
    const uint8_t* pixels,
    uint32_t width,
    uint32_t height,
    bool srgb,
    VkFilter filter,
    VkSamplerAddressMode addressMode)
{
    if (!context || !context->IsValid() || !pixels || width == 0 || height == 0) return false;

    VkFormat fmt = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    if (!CreateImage2D(
            context,
            width,
            height,
            fmt,
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            true,
            filter,
            addressMode))
    {
        return false;
    }

    const VkDeviceSize byteSize = static_cast<VkDeviceSize>(width) * height * 4;
    VulkanBuffer staging;
    if (!staging.Create(context, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false)) {
        Destroy(context);
        return false;
    }
    staging.Upload(context, pixels, byteSize, 0);

    VkCommandBuffer cmd = context->BeginSingleTimeCommands();
    TransitionImageLayout(cmd, m_Image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = { width, height, 1 };
    vkCmdCopyBufferToImage(cmd, staging.GetBuffer(), m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    TransitionImageLayout(cmd, m_Image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    context->EndSingleTimeCommands(cmd);

    staging.Destroy(context);
    return true;
}

void VulkanTexture::Destroy(VulkanContext* context) {
    if (!context || context->GetDevice() == VK_NULL_HANDLE) {
        m_Image = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        m_ImageView = VK_NULL_HANDLE;
        m_Sampler = VK_NULL_HANDLE;
        m_Format = VK_FORMAT_UNDEFINED;
        m_Width = 0;
        m_Height = 0;
        return;
    }

    VkDevice device = context->GetDevice();
    if (m_Sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, m_Sampler, nullptr);
        m_Sampler = VK_NULL_HANDLE;
    }
    if (m_ImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, m_ImageView, nullptr);
        m_ImageView = VK_NULL_HANDLE;
    }
    if (m_Image != VK_NULL_HANDLE && m_Allocation != VK_NULL_HANDLE && context->GetAllocator() != VK_NULL_HANDLE) {
        vmaDestroyImage(context->GetAllocator(), m_Image, m_Allocation);
        m_Image = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
    }
    m_Format = VK_FORMAT_UNDEFINED;
    m_Width = 0;
    m_Height = 0;
}

void VulkanTexture::TransitionImageLayout(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkImageAspectFlags aspectMask)
{
    if (cmd == VK_NULL_HANDLE || image == VK_NULL_HANDLE || oldLayout == newLayout) return;

    VkPipelineStageFlags srcStage = 0;
    VkAccessFlags srcAccess = 0;
    VkPipelineStageFlags dstStage = 0;
    VkAccessFlags dstAccess = 0;
    FillLayoutStageAccess(oldLayout, srcStage, srcAccess);
    FillLayoutStageAccess(newLayout, dstStage, dstAccess);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspectMask;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}
