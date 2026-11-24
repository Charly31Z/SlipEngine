#pragma once

#ifndef SLIP_VULKAN_BINDLESS_MANAGER_H
#define SLIP_VULKAN_BINDLESS_MANAGER_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanTexture.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class VulkanContext;
class SlipMaterial;

struct alignas(16) SLIPENGINE_API GpuMaterialData {
    glm::vec4 albedoColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    glm::vec4 emissiveColor{ 0.0f, 0.0f, 0.0f, 0.0f }; // rgb + intensity
    glm::vec4 pbrParams{ 0.5f, 0.0f, 1.0f, 1.0f };     // x=roughness, y=metallic, z=ao, w=normalScale
    glm::vec4 extraParams{ 0.5f, 0.0f, 0.0f, 0.0f };   // x=alphaCutoff, y=alphaClip, z=invertRoughness, w=doubleSided
    glm::ivec4 texIndices0{ -1, -1, -1, -1 };          // x=albedo, y=normal, z=roughness, w=metallic
    glm::ivec4 texIndices1{ -1, -1, -1, 0 };           // x=ao, y=emissive, z=opacity, w=unused
};

class SLIPENGINE_API VulkanBindlessManager {
public:
    static constexpr uint32_t kMaxTextures = 1024;
    static constexpr uint32_t kMaxMaterials = 1024;

    VulkanBindlessManager() = default;
    ~VulkanBindlessManager() = default;

    bool Initialize(VulkanContext* context);
    void Shutdown(VulkanContext* context);

    int32_t RegisterTextureFromPath(VulkanContext* context, const std::string& path, bool srgb = false);

    void BeginFrameMaterials();
    uint32_t PushMaterial(const GpuMaterialData& data);
    uint32_t PushSlipMaterial(VulkanContext* context, const SlipMaterial* mat, const glm::vec4& fallbackColor = glm::vec4(1.0f));
    void UploadMaterials(VulkanContext* context);

    VkDescriptorSetLayout GetDescriptorSetLayout() const { return m_SetLayout; }
    VkDescriptorSet GetDescriptorSet() const { return m_DescriptorSet; }
    uint32_t GetRegisteredTextureCount() const { return static_cast<uint32_t>(m_Textures.size()); }
    uint32_t GetFrameMaterialCount() const { return static_cast<uint32_t>(m_FrameMaterials.size()); }

private:
    void WriteTextureDescriptor(VulkanContext* context, uint32_t slot, const VulkanTexture& tex);

    VkDescriptorSetLayout m_SetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE;

    VulkanBuffer m_MaterialBuffer;
    std::vector<VulkanTexture> m_Textures;
    std::unordered_map<std::string, int32_t> m_PathToSlot;
    std::vector<GpuMaterialData> m_FrameMaterials;
};

#endif // !SLIP_VULKAN_BINDLESS_MANAGER_H
