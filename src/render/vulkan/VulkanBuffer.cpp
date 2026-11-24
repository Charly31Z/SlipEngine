#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"

#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#include <vk_mem_alloc.h>

#include <cstring>
#include <iostream>

bool VulkanBuffer::Create(
    VulkanContext* context,
    VkDeviceSize byteSize,
    VkBufferUsageFlags usage,
    bool hostVisible,
    bool deviceLocalPreferred)
{
    if (!context || !context->IsValid() || byteSize == 0) return false;
    Destroy(context);

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = byteSize;
    bufInfo.usage = usage;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    if (hostVisible) {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else {
        allocInfo.usage = deviceLocalPreferred ? VMA_MEMORY_USAGE_GPU_ONLY : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    }

    VmaAllocationInfo outInfo{};
    VkResult res = vmaCreateBuffer(context->GetAllocator(), &bufInfo, &allocInfo, &m_Buffer, &m_Allocation, &outInfo);
    if (res != VK_SUCCESS) {
        std::cerr << "[VulkanBuffer] vmaCreateBuffer fallo (VkResult=" << res << ", size=" << byteSize << ")" << std::endl;
        m_Buffer = VK_NULL_HANDLE;
        m_Allocation = VK_NULL_HANDLE;
        return false;
    }

    m_Size = byteSize;
    m_HostVisible = hostVisible;
    m_Mapped = hostVisible ? outInfo.pMappedData : nullptr;
    return true;
}

bool VulkanBuffer::CreateDeviceLocalWithData(
    VulkanContext* context,
    const void* data,
    VkDeviceSize byteSize,
    VkBufferUsageFlags usage)
{
    if (!context || !context->IsValid() || !data || byteSize == 0) return false;

    VulkanBuffer staging;
    if (!staging.Create(context, byteSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, false)) {
        return false;
    }
    if (!staging.Upload(context, data, byteSize, 0)) {
        staging.Destroy(context);
        return false;
    }

    if (!Create(context, byteSize, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, true)) {
        staging.Destroy(context);
        return false;
    }

    VkCommandBuffer cmd = context->BeginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = byteSize;
    vkCmdCopyBuffer(cmd, staging.GetBuffer(), m_Buffer, 1, &copyRegion);
    context->EndSingleTimeCommands(cmd);

    staging.Destroy(context);
    return true;
}

bool VulkanBuffer::Upload(VulkanContext* context, const void* data, VkDeviceSize byteSize, VkDeviceSize offset) {
    if (!context || !context->IsValid() || !IsValid() || !data || byteSize == 0) return false;
    if (offset + byteSize > m_Size) return false;

    if (m_Mapped) {
        std::memcpy(static_cast<uint8_t*>(m_Mapped) + offset, data, static_cast<size_t>(byteSize));
        vmaFlushAllocation(context->GetAllocator(), m_Allocation, offset, byteSize);
        return true;
    }

    if (m_HostVisible) {
        void* mapped = nullptr;
        if (vmaMapMemory(context->GetAllocator(), m_Allocation, &mapped) != VK_SUCCESS) {
            return false;
        }
        std::memcpy(static_cast<uint8_t*>(mapped) + offset, data, static_cast<size_t>(byteSize));
        vmaFlushAllocation(context->GetAllocator(), m_Allocation, offset, byteSize);
        vmaUnmapMemory(context->GetAllocator(), m_Allocation);
        return true;
    }

    return false;
}

void VulkanBuffer::Destroy(VulkanContext* context) {
    if (context && context->GetAllocator() != VK_NULL_HANDLE && m_Buffer != VK_NULL_HANDLE && m_Allocation != VK_NULL_HANDLE) {
        vmaDestroyBuffer(context->GetAllocator(), m_Buffer, m_Allocation);
    }
    m_Buffer = VK_NULL_HANDLE;
    m_Allocation = VK_NULL_HANDLE;
    m_Size = 0;
    m_Mapped = nullptr;
    m_HostVisible = false;
}

VkDeviceAddress VulkanBuffer::GetDeviceAddress(VulkanContext* context) const {
    if (!context || !context->IsValid() || m_Buffer == VK_NULL_HANDLE || !vkGetBufferDeviceAddress) {
        return 0;
    }
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = m_Buffer;
    return vkGetBufferDeviceAddress(context->GetDevice(), &info);
}
