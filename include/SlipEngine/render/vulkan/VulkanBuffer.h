#pragma once

#ifndef SLIP_VULKAN_BUFFER_H
#define SLIP_VULKAN_BUFFER_H

#include "SlipEngine/core/SlipExport.h"
#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

typedef struct VmaAllocation_T* VmaAllocation;

class VulkanContext;

class SLIPENGINE_API VulkanBuffer {
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() = default;

    bool Create(
        VulkanContext* context,
        VkDeviceSize byteSize,
        VkBufferUsageFlags usage,
        bool hostVisible,
        bool deviceLocalPreferred = false
    );

    bool CreateDeviceLocalWithData(
        VulkanContext* context,
        const void* data,
        VkDeviceSize byteSize,
        VkBufferUsageFlags usage
    );

    bool Upload(VulkanContext* context, const void* data, VkDeviceSize byteSize, VkDeviceSize offset = 0);
    void Destroy(VulkanContext* context);

    VkBuffer GetBuffer() const { return m_Buffer; }
    VkDeviceSize GetSize() const { return m_Size; }
    void* GetMapped() const { return m_Mapped; }
    bool IsValid() const { return m_Buffer != VK_NULL_HANDLE; }
    VkDeviceAddress GetDeviceAddress(VulkanContext* context) const;

private:
    VkBuffer m_Buffer = VK_NULL_HANDLE;
    VmaAllocation m_Allocation = VK_NULL_HANDLE;
    VkDeviceSize m_Size = 0;
    void* m_Mapped = nullptr;
    bool m_HostVisible = false;
};

#endif // !SLIP_VULKAN_BUFFER_H
