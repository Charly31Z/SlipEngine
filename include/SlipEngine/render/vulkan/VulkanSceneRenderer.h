#pragma once

#ifndef SLIP_VULKAN_SCENE_RENDERER_H
#define SLIP_VULKAN_SCENE_RENDERER_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/vulkan/VulkanAccelerationStructure.h"
#include "SlipEngine/render/vulkan/VulkanBindlessManager.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanRayTracedGI.h"
#include "SlipEngine/render/vulkan/VulkanTexture.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>
#include <cstdint>
#include <unordered_map>
#include <vector>

class VulkanContext;
class SlipLevel;
class SlipMesh;
class SlipEntity;

class SLIPENGINE_API VulkanSceneRenderer {
public:
    static constexpr uint32_t kFrameSlots = 2;
    static constexpr uint32_t kMaxInstances = 4096;

    VulkanSceneRenderer() = default;
    ~VulkanSceneRenderer();

    bool Initialize(VulkanContext* context);
    void Shutdown();

    bool RenderScene(
        VkCommandBuffer cmd,
        uint32_t frameIndex,
        SlipLevel* level,
        const glm::mat4& view,
        const glm::mat4& glProjection,
        const glm::vec3& cameraPos,
        VkImage targetImage,
        VkImageView targetView,
        uint32_t width,
        uint32_t height,
        VkImageLayout targetInitialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VkImageLayout targetFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        uint64_t targetGeneration = 0
    );

    uint32_t GetLastDrawCallCount() const { return m_LastDrawCallCount; }
    uint32_t GetLastRtInstanceCount() const { return m_ASManager.GetLastInstanceCount(); }
    uint32_t GetPathTracingSampleCount() const { return m_RtGI.GetSampleCount(); }
    void ResetPathTracingAccumulation() { m_RtGI.ResetAccumulation(); }
    void InvalidateSceneCache();
    void SetSelectedEntity(const SlipEntity* entity) { m_SelectedEntity = entity; }
    bool HasValidTLAS(uint32_t frameIndex = 0) const { return m_ASManager.HasValidTLAS(frameIndex % kFrameSlots); }
    const VulkanTexture& GetGBufAlbedo() const { return m_GBufAlbedo; }
    const VulkanTexture& GetGBufDepth() const { return m_GBufDepth; }
    const VulkanRayTracedGI& GetRayTracedGI() const { return m_RtGI; }

private:
    struct SubmeshRange {
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        int32_t materialIndex = 0;
        VulkanBLAS blas;
    };

    struct GpuMesh {
        VulkanBuffer vertexBuffer;
        VulkanBuffer indexBuffer;
        uint32_t vertexCount = 0;
        std::vector<SubmeshRange> submeshes;
    };

    struct DrawCall {
        const GpuMesh* mesh = nullptr;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        int32_t vertexOffset = 0;
        uint32_t instanceIndex = 0;
    };

    bool CreatePipelines();
    bool CreatePtDepthPipeline();
    bool CreateParticlePipelines();
    bool ResolvePathTracingDepth(VkCommandBuffer cmd, uint32_t slot, uint32_t width, uint32_t height);
    // Dibuja los sistemas de particulas (chispas, fuego, etc.) en m_HdrColor con prueba de profundidad.
    // Deja m_HdrColor en SHADER_READ_ONLY. Devuelve false (sin tocar layouts) si no hay nada que dibujar.
    bool RenderParticles(VkCommandBuffer cmd, uint32_t slot, SlipLevel* level, const glm::mat4& view,
                         const glm::mat4& viewProj, const glm::vec3& cameraPos, uint32_t width, uint32_t height);
    bool CreatePerFrameResources();
    bool EnsureRenderTargets(uint32_t width, uint32_t height);
    void DestroyRenderTargets();
    void UpdateComputeDescriptors(uint32_t slot, VkImageView tonemapTargetView, uint64_t targetGeneration);

    const GpuMesh* GetOrUploadMesh(SlipMesh* mesh);
    const GpuMesh* GetOrUploadPrimitive(int primitiveType);

    VulkanContext* m_Context = nullptr;
    VulkanBindlessManager m_Bindless;
    VulkanAccelerationStructureManager m_ASManager;
    VulkanRayTracedGI m_RtGI;
    bool m_Initialized = false;

    // Render targets (G-Buffer + HDR)
    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    VulkanTexture m_GBufAlbedo;
    VulkanTexture m_GBufNormalRough;
    VulkanTexture m_GBufMetalEmissive;
    VulkanTexture m_GBufMotion;
    VulkanTexture m_GBufDepth;
    VulkanTexture m_HdrColor;
    VulkanTexture m_BloomPing;
    VulkanTexture m_BloomPong;
    bool m_TargetsInitialized = false;
    bool m_BloomInitialized = false;
    VkImageLayout m_GBufDepthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Descriptor layouts & pools
    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_GBufSceneLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_LightingLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_BloomLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_TonemapLayout = VK_NULL_HANDLE;

    VkDescriptorSet m_GBufSceneSets[kFrameSlots]{};
    VkDescriptorSet m_LightingSets[kFrameSlots]{};
    VkDescriptorSet m_BloomSets[kFrameSlots][3]{};
    VkDescriptorSet m_TonemapSets[kFrameSlots]{};
    VkImageView m_LastTonemapTargets[kFrameSlots]{};
    VkImageView m_LastLightingGiViews[kFrameSlots]{};
    VkImageView m_LastLightingReflViews[kFrameSlots]{};
    uint64_t m_LastTonemapTargetGens[kFrameSlots]{};
    bool m_ComputeDescriptorsDirty[kFrameSlots]{ true, true };
    bool m_BloomDescriptorsDirty[kFrameSlots]{ true, true };

    // Pipelines
    VkPipelineLayout m_GBufPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_GBufPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_LightingPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_LightingPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_BloomPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_BloomExtractPipeline = VK_NULL_HANDLE;
    VkPipeline m_BloomBlurPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_TonemapPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_TonemapPipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_PtDepthLayout = VK_NULL_HANDLE;
    VkDescriptorSet m_PtDepthSets[kFrameSlots]{};
    VkPipelineLayout m_PtDepthPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_PtDepthPipeline = VK_NULL_HANDLE;

    // Particle pass (forward, additive / alpha blended, depth-tested against G-Buffer depth)
    static constexpr uint32_t kMaxParticleVertices = 6u * 32768u;
    VkPipelineLayout m_ParticlePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_ParticleAdditivePipeline = VK_NULL_HANDLE;
    VkPipeline m_ParticleAlphaPipeline = VK_NULL_HANDLE;
    bool m_ParticlePipelinesTried = false;
    VulkanBuffer m_ParticleVertexBuffers[kFrameSlots];

    // Per-frame buffers
    VulkanBuffer m_CameraUBOs[kFrameSlots];
    VulkanBuffer m_InstanceSSBOs[kFrameSlots];
    VulkanBuffer m_LightingUBOs[kFrameSlots];
    VulkanBuffer m_PostProcessUBOs[kFrameSlots];

    const class SlipEntity* m_SelectedEntity = nullptr;

    // Mesh caches
    std::unordered_map<const SlipMesh*, GpuMesh> m_MeshCache;
    std::unordered_map<int, GpuMesh> m_PrimitiveCache;

    // Temporal motion state
    glm::mat4 m_PrevViewProj{ 1.0f };
    bool m_HasPrevViewProj = false;
    std::unordered_map<const SlipEntity*, glm::mat4> m_PrevModels;

    uint32_t m_LastDrawCallCount = 0;
};

#endif // !SLIP_VULKAN_SCENE_RENDERER_H
