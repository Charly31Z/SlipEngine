#include "SlipEngine/render/vulkan/VulkanBindlessManager.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipTexture.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#include "stb_image.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>

namespace {

std::string ResolveVulkanTexturePath(const std::string& rawPath) {
    if (rawPath.empty()) return "";

    std::string norm = rawPath;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    if (SlipFileSystem::Exists(norm)) return norm;

    std::string assets = SlipFileSystem::GetProjectAssetsPath();
    std::string root = SlipFileSystem::GetProjectRootPath();

    std::vector<std::string> candidates;
    if (!assets.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(assets, norm));
        if (norm.rfind("assets/", 0) == 0) {
            candidates.push_back(SlipFileSystem::CombinePath(assets, norm.substr(7)));
        }
    }
    if (!root.empty()) {
        candidates.push_back(SlipFileSystem::CombinePath(root, norm));
        candidates.push_back(SlipFileSystem::CombinePath(root, "assets/" + norm));
    }
    candidates.push_back("assets/" + norm);

    for (const auto& c : candidates) {
        if (SlipFileSystem::Exists(c)) {
            std::string found = c;
            std::replace(found.begin(), found.end(), '\\', '/');
            return found;
        }
    }
    return "";
}

std::string PickMaterialTexPath(const SlipTexture* tex, const char* rawPath) {
    if (tex && !tex->path.empty()) return tex->path;
    if (rawPath && rawPath[0] != '\0') return std::string(rawPath);
    return "";
}

} // namespace

bool VulkanBindlessManager::Initialize(VulkanContext* context) {
    if (!context || !context->IsValid()) return false;
    Shutdown(context);

    VkDevice device = context->GetDevice();

    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = kMaxTextures;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    std::array<VkDescriptorBindingFlags, 2> bindingFlags = {
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
    flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
    flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
    flagsInfo.pBindingFlags = bindingFlags.data();

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.pNext = &flagsInfo;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &m_SetLayout) != VK_SUCCESS) {
        std::cerr << "[VulkanBindlessManager] vkCreateDescriptorSetLayout fallo." << std::endl;
        Shutdown(context);
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxTextures };
    poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4 };

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 2;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &m_Pool) != VK_SUCCESS) {
        std::cerr << "[VulkanBindlessManager] vkCreateDescriptorPool fallo." << std::endl;
        Shutdown(context);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_Pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_SetLayout;

    if (vkAllocateDescriptorSets(device, &allocInfo, &m_DescriptorSet) != VK_SUCCESS) {
        std::cerr << "[VulkanBindlessManager] vkAllocateDescriptorSets fallo." << std::endl;
        Shutdown(context);
        return false;
    }

    if (!m_MaterialBuffer.Create(
            context,
            static_cast<VkDeviceSize>(kMaxMaterials) * sizeof(GpuMaterialData),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            true,
            false))
    {
        Shutdown(context);
        return false;
    }

    VkDescriptorBufferInfo matBufInfo{};
    matBufInfo.buffer = m_MaterialBuffer.GetBuffer();
    matBufInfo.offset = 0;
    matBufInfo.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet matWrite{};
    matWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    matWrite.dstSet = m_DescriptorSet;
    matWrite.dstBinding = 1;
    matWrite.dstArrayElement = 0;
    matWrite.descriptorCount = 1;
    matWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    matWrite.pBufferInfo = &matBufInfo;
    vkUpdateDescriptorSets(device, 1, &matWrite, 0, nullptr);

    // Slot 0: 1x1 white, Slot 1: 1x1 flat normal
    m_Textures.reserve(64);
    const uint8_t whitePixel[4] = { 255, 255, 255, 255 };
    VulkanTexture whiteTex;
    if (whiteTex.CreateFromRGBA8(context, whitePixel, 1, 1, false, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT)) {
        WriteTextureDescriptor(context, 0, whiteTex);
        m_Textures.push_back(whiteTex);
        m_PathToSlot["__builtin_white__"] = 0;
    }

    const uint8_t flatNormal[4] = { 128, 128, 255, 255 };
    VulkanTexture normTex;
    if (normTex.CreateFromRGBA8(context, flatNormal, 1, 1, false, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT)) {
        WriteTextureDescriptor(context, 1, normTex);
        m_Textures.push_back(normTex);
        m_PathToSlot["__builtin_normal__"] = 1;
    }

    return true;
}

void VulkanBindlessManager::Shutdown(VulkanContext* context) {
    if (context && context->IsValid()) {
        VkDevice device = context->GetDevice();
        for (auto& tex : m_Textures) {
            tex.Destroy(context);
        }
        m_MaterialBuffer.Destroy(context);

        if (m_Pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, m_Pool, nullptr);
            m_Pool = VK_NULL_HANDLE;
        }
        if (m_SetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, m_SetLayout, nullptr);
            m_SetLayout = VK_NULL_HANDLE;
        }
    }

    m_Textures.clear();
    m_PathToSlot.clear();
    m_FrameMaterials.clear();
    m_DescriptorSet = VK_NULL_HANDLE;
}

void VulkanBindlessManager::WriteTextureDescriptor(VulkanContext* context, uint32_t slot, const VulkanTexture& tex) {
    if (!context || m_DescriptorSet == VK_NULL_HANDLE || !tex.IsValid()) return;

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = tex.GetSampler();
    imgInfo.imageView = tex.GetImageView();
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = m_DescriptorSet;
    write.dstBinding = 0;
    write.dstArrayElement = slot;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imgInfo;
    vkUpdateDescriptorSets(context->GetDevice(), 1, &write, 0, nullptr);
}

int32_t VulkanBindlessManager::RegisterTextureFromPath(VulkanContext* context, const std::string& path, bool srgb) {
    if (!context || !context->IsValid() || path.empty()) return -1;

    std::string resolved = ResolveVulkanTexturePath(path);
    if (resolved.empty()) return -1;

    std::string cacheKey = (srgb ? "srgb:" : "lin:") + resolved;
    auto it = m_PathToSlot.find(cacheKey);
    if (it != m_PathToSlot.end()) {
        return it->second;
    }

    if (m_Textures.size() >= kMaxTextures) {
        m_PathToSlot[cacheKey] = -1;
        return -1;
    }

    int w = 0, h = 0, comp = 0;
    std::vector<uint8_t> bytes;
    unsigned char* pixels = nullptr;
    if (SlipFileSystem::ReadAllBytes(resolved, bytes) && !bytes.empty()) {
        pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &w, &h, &comp, STBI_rgb_alpha);
    } else {
        pixels = stbi_load(resolved.c_str(), &w, &h, &comp, STBI_rgb_alpha);
    }

    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        m_PathToSlot[cacheKey] = -1;
        return -1;
    }

    VulkanTexture tex;
    bool ok = tex.CreateFromRGBA8(context, pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), srgb, VK_FILTER_LINEAR, VK_SAMPLER_ADDRESS_MODE_REPEAT);
    stbi_image_free(pixels);

    if (!ok) {
        m_PathToSlot[cacheKey] = -1;
        return -1;
    }

    uint32_t slot = static_cast<uint32_t>(m_Textures.size());
    WriteTextureDescriptor(context, slot, tex);
    m_Textures.push_back(tex);
    m_PathToSlot[cacheKey] = static_cast<int32_t>(slot);
    return static_cast<int32_t>(slot);
}

void VulkanBindlessManager::BeginFrameMaterials() {
    m_FrameMaterials.clear();
}

uint32_t VulkanBindlessManager::PushMaterial(const GpuMaterialData& data) {
    if (m_FrameMaterials.size() >= kMaxMaterials) {
        return 0;
    }
    uint32_t idx = static_cast<uint32_t>(m_FrameMaterials.size());
    m_FrameMaterials.push_back(data);
    return idx;
}

uint32_t VulkanBindlessManager::PushSlipMaterial(VulkanContext* context, const SlipMaterial* mat, const glm::vec4& fallbackColor) {
    GpuMaterialData gpuMat{};
    if (!mat) {
        gpuMat.albedoColor = fallbackColor;
        return PushMaterial(gpuMat);
    }

    gpuMat.albedoColor = glm::vec4(mat->color, fallbackColor.a);
    gpuMat.emissiveColor = glm::vec4(mat->emissiveColor, mat->emissiveIntensity);
    gpuMat.pbrParams = glm::vec4(mat->roughness, mat->metallic, mat->ao, mat->normalScale);
    gpuMat.extraParams = glm::vec4(
        mat->alphaCutoff,
        mat->alphaClip ? 1.0f : 0.0f,
        mat->invertRoughness ? 1.0f : 0.0f,
        mat->doubleSided ? 1.0f : 0.0f
    );

    gpuMat.texIndices0.x = RegisterTextureFromPath(context, PickMaterialTexPath(mat->diffuseTexture, mat->diffPath), true);
    gpuMat.texIndices0.y = RegisterTextureFromPath(context, PickMaterialTexPath(mat->normalTexture, mat->normalPath), false);
    gpuMat.texIndices0.z = RegisterTextureFromPath(context, PickMaterialTexPath(mat->roughnessTexture, mat->roughnessPath), false);
    gpuMat.texIndices0.w = RegisterTextureFromPath(context, PickMaterialTexPath(mat->metallicTexture, mat->metallicPath), false);
    gpuMat.texIndices1.x = RegisterTextureFromPath(context, PickMaterialTexPath(mat->aoTexture, mat->aoPath), false);
    gpuMat.texIndices1.y = RegisterTextureFromPath(context, PickMaterialTexPath(mat->emissiveTexture, mat->emissivePath), true);
    gpuMat.texIndices1.z = RegisterTextureFromPath(context, PickMaterialTexPath(mat->opacityTexture, mat->opacityPath), false);

    return PushMaterial(gpuMat);
}

void VulkanBindlessManager::UploadMaterials(VulkanContext* context) {
    if (!context || m_FrameMaterials.empty()) return;
    VkDeviceSize byteSize = static_cast<VkDeviceSize>(m_FrameMaterials.size()) * sizeof(GpuMaterialData);
    m_MaterialBuffer.Upload(context, m_FrameMaterials.data(), byteSize, 0);
}
