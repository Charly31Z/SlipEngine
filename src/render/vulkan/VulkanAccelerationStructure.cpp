#include "SlipEngine/render/vulkan/VulkanAccelerationStructure.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"

#include <algorithm>
#include <cstring>
#include <iostream>

namespace {

VkDeviceAddress AlignUpAddress(VkDeviceAddress addr, VkDeviceAddress alignment) {
    if (alignment <= 1) return addr;
    return (addr + alignment - 1) & ~(alignment - 1);
}

} // namespace

void VulkanBLAS::Destroy(VulkanContext* context) {
    if (context && context->IsValid() && handle != VK_NULL_HANDLE) {
        if (vkDestroyAccelerationStructureKHR) {
            vkDestroyAccelerationStructureKHR(context->GetDevice(), handle, nullptr);
        }
    }
    handle = VK_NULL_HANDLE;
    deviceAddress = 0;
    vertexAddress = 0;
    indexAddress = 0;
    vertexCount = 0;
    indexCount = 0;
    buffer.Destroy(context);
}

bool VulkanAccelerationStructureManager::Initialize(VulkanContext* context) {
    if (!context || !context->IsValid()) return false;
    Shutdown(context);

    if (!context->GetCapabilities().supportsRayTracing) {
        return true;
    }

    VkPhysicalDeviceAccelerationStructurePropertiesKHR asProps{};
    asProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;

    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &asProps;
    vkGetPhysicalDeviceProperties2(context->GetPhysicalDevice(), &props2);

    m_MinScratchAlignment = std::max<uint32_t>(asProps.minAccelerationStructureScratchOffsetAlignment, 256u);
    return true;
}

void VulkanAccelerationStructureManager::Shutdown(VulkanContext* context) {
    if (context && context->IsValid()) {
        VkDevice device = context->GetDevice();
        for (uint32_t i = 0; i < kFrameSlots; ++i) {
            if (m_TlasHandles[i] != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR) {
                vkDestroyAccelerationStructureKHR(device, m_TlasHandles[i], nullptr);
            }
            m_TlasHandles[i] = VK_NULL_HANDLE;
            m_TlasBuffers[i].Destroy(context);
            m_TlasAllocatedBytes[i] = 0;
            m_InstanceBuffers[i].Destroy(context);
            m_InstanceInfoSSBOs[i].Destroy(context);
            m_InstanceCapacity[i] = 0;
            m_TlasScratchBuffers[i].Destroy(context);
            m_HasValidTlas[i] = false;
        }
    } else {
        for (uint32_t i = 0; i < kFrameSlots; ++i) {
            m_TlasHandles[i] = VK_NULL_HANDLE;
            m_TlasAllocatedBytes[i] = 0;
            m_InstanceCapacity[i] = 0;
            m_HasValidTlas[i] = false;
        }
    }
    m_LastInstanceCount = 0;
}

bool VulkanAccelerationStructureManager::BuildBLAS(
    VulkanContext* context,
    const VulkanBuffer& vertexBuffer,
    uint32_t totalVertexCount,
    const VulkanBuffer& indexBuffer,
    uint32_t firstIndex,
    uint32_t indexCount,
    int32_t vertexOffset,
    uint32_t vertexStrideBytes,
    uint32_t positionOffsetBytes,
    VulkanBLAS& outBlas)
{
    outBlas.Destroy(context);

    if (!context || !context->IsValid() || !context->GetCapabilities().supportsRayTracing) {
        return false;
    }
    if (!vkGetAccelerationStructureBuildSizesKHR || !vkCreateAccelerationStructureKHR ||
        !vkCmdBuildAccelerationStructuresKHR || !vkGetAccelerationStructureDeviceAddressKHR)
    {
        return false;
    }
    if (!vertexBuffer.IsValid() || !indexBuffer.IsValid() || totalVertexCount == 0 || indexCount < 3) {
        return false;
    }

    const uint64_t vertexBaseAddr = vertexBuffer.GetDeviceAddress(context);
    const uint64_t indexBaseAddr = indexBuffer.GetDeviceAddress(context);
    if (vertexBaseAddr == 0 || indexBaseAddr == 0) {
        return false;
    }

    const uint32_t safeVOffset = (vertexOffset > 0) ? static_cast<uint32_t>(vertexOffset) : 0u;
    if (safeVOffset >= totalVertexCount) {
        return false;
    }

    const uint32_t maxVertex = (totalVertexCount - safeVOffset) - 1u;
    const uint32_t primitiveCount = indexCount / 3u;
    if (primitiveCount == 0) {
        return false;
    }

    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertexBaseAddr + static_cast<uint64_t>(safeVOffset) * vertexStrideBytes + positionOffsetBytes;
    triangles.vertexStride = vertexStrideBytes;
    triangles.maxVertex = maxVertex;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indexBaseAddr;
    triangles.transformData.deviceAddress = 0;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.triangles = triangles;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        context->GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &primitiveCount,
        &sizeInfo
    );

    if (!outBlas.buffer.Create(
            context,
            sizeInfo.accelerationStructureSize,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            false,
            true))
    {
        return false;
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = outBlas.buffer.GetBuffer();
    createInfo.offset = 0;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    if (vkCreateAccelerationStructureKHR(context->GetDevice(), &createInfo, nullptr, &outBlas.handle) != VK_SUCCESS) {
        outBlas.Destroy(context);
        return false;
    }

    VulkanBuffer scratch;
    const VkDeviceSize scratchAllocBytes = sizeInfo.buildScratchSize + m_MinScratchAlignment;
    if (!scratch.Create(
            context,
            scratchAllocBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
            false,
            true))
    {
        outBlas.Destroy(context);
        return false;
    }

    const VkDeviceAddress scratchAddr = AlignUpAddress(scratch.GetDeviceAddress(context), m_MinScratchAlignment);
    buildInfo.dstAccelerationStructure = outBlas.handle;
    buildInfo.scratchData.deviceAddress = scratchAddr;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    rangeInfo.primitiveOffset = firstIndex * static_cast<uint32_t>(sizeof(uint32_t));
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    VkCommandBuffer cmd = context->BeginSingleTimeCommands();
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);

    VkMemoryBarrier memBarrier{};
    memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    memBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0,
        1, &memBarrier,
        0, nullptr,
        0, nullptr
    );
    context->EndSingleTimeCommands(cmd);

    scratch.Destroy(context);

    VkAccelerationStructureDeviceAddressInfoKHR addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    addrInfo.accelerationStructure = outBlas.handle;
    outBlas.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(context->GetDevice(), &addrInfo);
    outBlas.vertexAddress = vertexBaseAddr;
    outBlas.indexAddress = indexBaseAddr;
    outBlas.vertexCount = totalVertexCount;
    outBlas.indexCount = indexCount;
    return outBlas.IsValid();
}

bool VulkanAccelerationStructureManager::EnsureScratchBuffer(VulkanContext* context, uint32_t slot, VkDeviceSize minAlignedBytes) {
    const VkDeviceSize neededBytes = minAlignedBytes + m_MinScratchAlignment;
    if (m_TlasScratchBuffers[slot].IsValid() && m_TlasScratchBuffers[slot].GetSize() >= neededBytes) {
        return true;
    }
    m_TlasScratchBuffers[slot].Destroy(context);
    const VkDeviceSize allocBytes = std::max<VkDeviceSize>(neededBytes, 256ULL * 1024ULL);
    return m_TlasScratchBuffers[slot].Create(
        context,
        allocBytes,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        false,
        true
    );
}

bool VulkanAccelerationStructureManager::EnsureInstanceBuffers(VulkanContext* context, uint32_t slot, uint32_t instanceCount) {
    const uint32_t required = std::max<uint32_t>(instanceCount, 1u);
    if (m_InstanceCapacity[slot] >= required &&
        m_InstanceBuffers[slot].IsValid() &&
        m_InstanceInfoSSBOs[slot].IsValid())
    {
        return true;
    }

    const uint32_t newCap = std::max<uint32_t>(required, std::max<uint32_t>(64u, m_InstanceCapacity[slot] * 2u));
    m_InstanceBuffers[slot].Destroy(context);
    m_InstanceInfoSSBOs[slot].Destroy(context);
    m_InstanceCapacity[slot] = 0;

    if (!m_InstanceBuffers[slot].Create(
            context,
            static_cast<VkDeviceSize>(newCap) * sizeof(VkAccelerationStructureInstanceKHR),
            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR,
            true,
            false))
    {
        return false;
    }

    if (!m_InstanceInfoSSBOs[slot].Create(
            context,
            static_cast<VkDeviceSize>(newCap) * sizeof(GpuRtInstanceInfo),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            true,
            false))
    {
        m_InstanceBuffers[slot].Destroy(context);
        return false;
    }

    m_InstanceCapacity[slot] = newCap;
    return true;
}

bool VulkanAccelerationStructureManager::EnsureTlasStorage(VulkanContext* context, uint32_t slot, VkDeviceSize requiredAsBytes) {
    if (m_TlasHandles[slot] != VK_NULL_HANDLE && m_TlasAllocatedBytes[slot] >= requiredAsBytes) {
        return true;
    }

    VkDevice device = context->GetDevice();
    if (m_TlasHandles[slot] != VK_NULL_HANDLE && vkDestroyAccelerationStructureKHR) {
        vkDestroyAccelerationStructureKHR(device, m_TlasHandles[slot], nullptr);
        m_TlasHandles[slot] = VK_NULL_HANDLE;
    }
    m_TlasAllocatedBytes[slot] = 0;

    const VkDeviceSize allocAsBytes = std::max<VkDeviceSize>(requiredAsBytes, 256ULL * 1024ULL);
    if (!m_TlasBuffers[slot].IsValid() || m_TlasBuffers[slot].GetSize() < allocAsBytes) {
        m_TlasBuffers[slot].Destroy(context);
        if (!m_TlasBuffers[slot].Create(
                context,
                allocAsBytes,
                VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                false,
                true))
        {
            return false;
        }
    }

    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_TlasBuffers[slot].GetBuffer();
    createInfo.offset = 0;
    createInfo.size = m_TlasBuffers[slot].GetSize();
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    if (vkCreateAccelerationStructureKHR(device, &createInfo, nullptr, &m_TlasHandles[slot]) != VK_SUCCESS) {
        return false;
    }
    m_TlasAllocatedBytes[slot] = m_TlasBuffers[slot].GetSize();
    return true;
}

bool VulkanAccelerationStructureManager::BuildTLAS(
    VulkanContext* context,
    VkCommandBuffer cmd,
    uint32_t frameSlot,
    const std::vector<RtInstanceInput>& instances)
{
    const uint32_t slot = frameSlot % kFrameSlots;
    m_HasValidTlas[slot] = false;
    m_LastInstanceCount = 0;

    if (!context || !context->IsValid() || cmd == VK_NULL_HANDLE || !context->GetCapabilities().supportsRayTracing) {
        return false;
    }
    if (!vkGetAccelerationStructureBuildSizesKHR || !vkCmdBuildAccelerationStructuresKHR) {
        return false;
    }
    if (instances.empty()) {
        return false;
    }

    std::vector<VkAccelerationStructureInstanceKHR> vkInstances;
    std::vector<GpuRtInstanceInfo> rtInfos;
    vkInstances.reserve(instances.size());
    rtInfos.reserve(instances.size());

    for (const auto& inst : instances) {
        if (!inst.blas || !inst.blas->IsValid()) continue;

        const uint32_t customIdx = static_cast<uint32_t>(rtInfos.size());
        VkAccelerationStructureInstanceKHR asInst{};
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 4; ++c) {
                asInst.transform.matrix[r][c] = inst.transform[c][r];
            }
        }
        asInst.instanceCustomIndex = customIdx & 0x00FFFFFFu;
        asInst.mask = 0xFF;
        asInst.instanceShaderBindingTableRecordOffset = 0;
        asInst.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
        asInst.accelerationStructureReference = inst.blas->deviceAddress;
        vkInstances.push_back(asInst);

        GpuRtInstanceInfo info{};
        info.vertexAddress = inst.blas->vertexAddress;
        info.indexAddress = inst.blas->indexAddress;
        info.materialIndex = inst.materialIndex;
        info.firstIndex = inst.firstIndex;
        info.vertexOffset = inst.vertexOffset;
        info.vertexStrideFloats = inst.vertexStrideFloats;
        info.prevModel = inst.prevTransform;
        rtInfos.push_back(info);
    }

    if (vkInstances.empty()) {
        return false;
    }

    const uint32_t count = static_cast<uint32_t>(vkInstances.size());
    if (!EnsureInstanceBuffers(context, slot, count)) {
        return false;
    }

    m_InstanceBuffers[slot].Upload(context, vkInstances.data(), count * sizeof(VkAccelerationStructureInstanceKHR), 0);
    m_InstanceInfoSSBOs[slot].Upload(context, rtInfos.data(), count * sizeof(GpuRtInstanceInfo), 0);

    const VkDeviceAddress instAddr = m_InstanceBuffers[slot].GetDeviceAddress(context);
    if (instAddr == 0) {
        return false;
    }

    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;
    instancesData.data.deviceAddress = instAddr;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.instances = instancesData;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(
        context->GetDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo,
        &count,
        &sizeInfo
    );

    if (!EnsureTlasStorage(context, slot, sizeInfo.accelerationStructureSize)) {
        return false;
    }
    if (!EnsureScratchBuffer(context, slot, sizeInfo.buildScratchSize)) {
        return false;
    }

    buildInfo.dstAccelerationStructure = m_TlasHandles[slot];
    buildInfo.scratchData.deviceAddress = AlignUpAddress(m_TlasScratchBuffers[slot].GetDeviceAddress(context), m_MinScratchAlignment);

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = count;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &buildInfo, &pRangeInfo);

    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        cmd,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        1, &barrier,
        0, nullptr,
        0, nullptr
    );

    m_HasValidTlas[slot] = true;
    m_LastInstanceCount = count;
    return true;
}

bool VulkanAccelerationStructureManager::HasValidTLAS(uint32_t frameSlot) const {
    return m_HasValidTlas[frameSlot % kFrameSlots] && m_TlasHandles[frameSlot % kFrameSlots] != VK_NULL_HANDLE;
}

VkAccelerationStructureKHR VulkanAccelerationStructureManager::GetTLAS(uint32_t frameSlot) const {
    return m_TlasHandles[frameSlot % kFrameSlots];
}

VkBuffer VulkanAccelerationStructureManager::GetInstanceInfoBuffer(uint32_t frameSlot) const {
    return m_InstanceInfoSSBOs[frameSlot % kFrameSlots].GetBuffer();
}
