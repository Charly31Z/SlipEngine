#pragma once

#ifndef SLIP_VULKAN_ACCELERATION_STRUCTURE_H
#define SLIP_VULKAN_ACCELERATION_STRUCTURE_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

class VulkanContext;

struct SLIPENGINE_API VulkanBLAS {
    VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
    VulkanBuffer buffer;
    VkDeviceAddress deviceAddress = 0;
    uint64_t vertexAddress = 0;
    uint64_t indexAddress = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    bool IsValid() const { return handle != VK_NULL_HANDLE && deviceAddress != 0; }
    void Destroy(VulkanContext* context);
};

struct alignas(16) SLIPENGINE_API GpuRtInstanceInfo {
    uint64_t vertexAddress = 0;
    uint64_t indexAddress = 0;
    uint32_t materialIndex = 0;
    uint32_t firstIndex = 0;
    int32_t  vertexOffset = 0;
    uint32_t vertexStrideFloats = 0;
    glm::mat4 prevModel{ 1.0f };
};
static_assert(sizeof(GpuRtInstanceInfo) == 96, "GpuRtInstanceInfo must match GLSL std430/scalar 96-byte stride");

struct SLIPENGINE_API RtInstanceInput {
    const VulkanBLAS* blas = nullptr;
    glm::mat4 transform{ 1.0f };
    glm::mat4 prevTransform{ 1.0f };
    uint32_t materialIndex = 0;
    uint32_t firstIndex = 0;
    int32_t  vertexOffset = 0;
    uint32_t vertexStrideFloats = 0;
};

class SLIPENGINE_API VulkanAccelerationStructureManager {
public:
    static constexpr uint32_t kFrameSlots = 2;

    VulkanAccelerationStructureManager() = default;
    ~VulkanAccelerationStructureManager() = default;

    bool Initialize(VulkanContext* context);
    void Shutdown(VulkanContext* context);

    bool BuildBLAS(
        VulkanContext* context,
        const VulkanBuffer& vertexBuffer,
        uint32_t totalVertexCount,
        const VulkanBuffer& indexBuffer,
        uint32_t firstIndex,
        uint32_t indexCount,
        int32_t vertexOffset,
        uint32_t vertexStrideBytes,
        uint32_t positionOffsetBytes,
        VulkanBLAS& outBlas
    );

    bool BuildTLAS(
        VulkanContext* context,
        VkCommandBuffer cmd,
        uint32_t frameSlot,
        const std::vector<RtInstanceInput>& instances
    );

    bool HasValidTLAS(uint32_t frameSlot) const;
    VkAccelerationStructureKHR GetTLAS(uint32_t frameSlot) const;
    VkBuffer GetInstanceInfoBuffer(uint32_t frameSlot) const;
    uint32_t GetLastInstanceCount() const { return m_LastInstanceCount; }

private:
    bool EnsureScratchBuffer(VulkanContext* context, uint32_t slot, VkDeviceSize minAlignedBytes);
    bool EnsureInstanceBuffers(VulkanContext* context, uint32_t slot, uint32_t instanceCount);
    bool EnsureTlasStorage(VulkanContext* context, uint32_t slot, VkDeviceSize requiredAsBytes);

    uint32_t m_MinScratchAlignment = 256;
    uint32_t m_LastInstanceCount = 0;

    VkAccelerationStructureKHR m_TlasHandles[kFrameSlots]{};
    VulkanBuffer m_TlasBuffers[kFrameSlots];
    VkDeviceSize m_TlasAllocatedBytes[kFrameSlots]{ 0, 0 };

    VulkanBuffer m_InstanceBuffers[kFrameSlots];
    VulkanBuffer m_InstanceInfoSSBOs[kFrameSlots];
    uint32_t m_InstanceCapacity[kFrameSlots]{ 0, 0 };

    VulkanBuffer m_TlasScratchBuffers[kFrameSlots];
    bool m_HasValidTlas[kFrameSlots]{ false, false };
};

#endif // !SLIP_VULKAN_ACCELERATION_STRUCTURE_H
