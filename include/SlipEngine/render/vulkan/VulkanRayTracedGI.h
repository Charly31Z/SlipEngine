#pragma once

#ifndef SLIP_VULKAN_RAY_TRACED_GI_H
#define SLIP_VULKAN_RAY_TRACED_GI_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/vulkan/VulkanBuffer.h"
#include "SlipEngine/render/vulkan/VulkanTexture.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

#include <cstdint>

class VulkanContext;
struct GraphicsSettingsData;

struct alignas(16) SLIPENGINE_API GpuGiParamsUBO {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frameIndex = 0;
    uint32_t sampleCount = 1;
    int32_t  giMode = 1;           // 0=Off, 1=RTGI, 2=PT Progressive, 3=PT Hold, 4=PT Realtime SVGF
    int32_t  giBounces = 2;
    int32_t  giRaysPerPixel = 1;
    float    giIntensity = 1.0f;
    float    giTemporalAlpha = 0.08f;
    int32_t  giDenoiserEnabled = 1;
    int32_t  giRayTracedShadows = 1;
    int32_t  hasValidTLAS = 0;
    int32_t  svgfAtrousIterations = 3;
    float    svgfSigmaLuminance = 2.0f;
    float    svgfSigmaNormal = 128.0f;
    float    svgfSigmaDepth = 1.5f;
};
static_assert(sizeof(GpuGiParamsUBO) == 64, "GpuGiParamsUBO must be 64 bytes (4 x vec4)");

class SLIPENGINE_API VulkanRayTracedGI {
public:
    static constexpr uint32_t kFrameSlots = 2;

    VulkanRayTracedGI() = default;
    ~VulkanRayTracedGI() = default;

    bool Initialize(VulkanContext* context, VkDescriptorSetLayout bindlessSetLayout);
    void Shutdown(VulkanContext* context);

    bool EnsureSize(VulkanContext* context, uint32_t width, uint32_t height);
    void DestroyTargets(VulkanContext* context);

    void ResetAccumulation();
    uint32_t GetSampleCount() const { return m_PtSampleCount; }
    bool IsRayQueryReady() const { return m_RayQueryPipelinesReady; }

    void UpdateGiParamsBuffer(VulkanContext* context, uint32_t slot, const GpuGiParamsUBO& params);
    VkBuffer GetGiParamsBuffer(uint32_t slot) const;

    bool ExecuteRTGI(
        VulkanContext* context,
        VkCommandBuffer cmd,
        uint32_t slot,
        VkDescriptorSet bindlessSet,
        VkAccelerationStructureKHR tlas,
        VkBuffer rtInstanceInfoBuffer,
        VkBuffer lightingUboBuffer,
        const VulkanTexture& gbufAlbedo,
        const VulkanTexture& gbufNormalRough,
        const VulkanTexture& gbufMetalEmissive,
        const VulkanTexture& gbufDepth,
        const VulkanTexture& gbufMotion,
        uint64_t sceneStateHash,
        const GraphicsSettingsData& settings
    );

    bool ExecutePathTracing(
        VulkanContext* context,
        VkCommandBuffer cmd,
        uint32_t slot,
        VkDescriptorSet bindlessSet,
        VkAccelerationStructureKHR tlas,
        VkBuffer rtInstanceInfoBuffer,
        VkBuffer lightingUboBuffer,
        const VulkanTexture& hdrColorOut,
        uint64_t sceneStateHash,
        const GraphicsSettingsData& settings
    );

    const VulkanTexture& GetDenoisedGI() const { return m_RtgiDenoised; }
    const VulkanTexture& GetFallbackGI() const { return m_FallbackGI; }
    const VulkanTexture& GetDenoisedReflections() const { return m_RtReflDenoised; }
    const VulkanTexture& GetFallbackReflections() const { return m_FallbackRefl; }
    const VulkanTexture& GetPtDemodIrradiance() const { return m_PtAccum; }
    const VulkanTexture& GetPtSpecularRadiance() const { return m_PtSpecAccum; }
    const VulkanTexture& GetPtAlbedo() const { return m_PtAlbedo; }
    const VulkanTexture& GetPtEmissive() const { return m_PtEmissive; }
    const VulkanTexture& GetPtNormalRough(uint32_t idx) const { return m_PtNormalRough[idx & 1u]; }
    const VulkanTexture& GetPtDepthInst(uint32_t idx) const { return m_PtDepthInst[idx & 1u]; }
    const VulkanTexture& GetPtMotion() const { return m_PtMotion; }
    const VulkanTexture& GetPtIrradianceHistory(uint32_t idx) const { return m_PtIrradianceHistory[idx & 1u]; }
    const VulkanTexture& GetPtSpecularHistory(uint32_t idx) const { return m_PtSpecularHistory[idx & 1u]; }
    const VulkanTexture& GetPtMomentsHistory(uint32_t idx) const { return m_PtMomentsHistory[idx & 1u]; }
    const VulkanTexture& GetPtFilteredIrradiance() const { return m_PtAtrousFinalIdx == 0 ? m_PtAtrousPing : m_PtAtrousPong; }
    const VulkanTexture& GetPtFilteredSpecular() const { return m_PtAtrousFinalIdx == 0 ? m_PtSpecAtrousPing : m_PtSpecAtrousPong; }
    uint32_t GetPtCurrPingPongIndex() const { return m_PtLastWrittenIdx; }
    uint32_t GetPtPrevPingPongIndex() const { return 1u - (m_PtLastWrittenIdx & 1u); }

private:
    bool CreatePipelines(VulkanContext* context, VkDescriptorSetLayout bindlessSetLayout);

    VulkanTexture m_RtgiRaw;
    VulkanTexture m_RtgiHistory[2];
    VulkanTexture m_RtgiTemp;
    VulkanTexture m_RtgiDenoised;
    VulkanTexture m_RtReflRaw;
    VulkanTexture m_RtReflHistory[2];
    VulkanTexture m_RtReflTemp;
    VulkanTexture m_RtReflDenoised;
    VulkanTexture m_PtAccum;
    VulkanTexture m_PtSpecAccum;
    VulkanTexture m_PtAlbedo;
    VulkanTexture m_PtEmissive;
    VulkanTexture m_PtNormalRough[2];
    VulkanTexture m_PtDepthInst[2];
    VulkanTexture m_PtIrradianceHistory[2];
    VulkanTexture m_PtSpecularHistory[2];
    VulkanTexture m_PtMomentsHistory[2];
    VulkanTexture m_PtAtrousPing;
    VulkanTexture m_PtAtrousPong;
    VulkanTexture m_PtSpecAtrousPing;
    VulkanTexture m_PtSpecAtrousPong;
    VulkanTexture m_PtMotion;
    VulkanTexture m_FallbackGI;
    VulkanTexture m_FallbackRefl;

    uint32_t m_Width = 0;
    uint32_t m_Height = 0;
    bool m_TargetsCreated = false;
    bool m_HistoryInitialized[2]{ false, false };
    bool m_PtAccumInitialized = false;
    bool m_PtPingPongInitialized[2]{ false, false };
    bool m_PtAtrousInitialized = false;
    uint32_t m_HistoryReadIdx = 0;
    uint32_t m_PtPingPongIdx = 0;
    uint32_t m_PtLastWrittenIdx = 0;
    uint32_t m_PtAtrousFinalIdx = 1;
    uint32_t m_RtgiTemporalFrame = 0;
    uint32_t m_PtFrameIndex = 0;

    uint32_t m_PtSampleCount = 0;
    uint64_t m_LastPtSceneHash = 0;
    uint64_t m_LastRtgiSceneHash = 0;
    bool m_ForceReset = true;

    bool m_RayQueryPipelinesReady = false;

    VkDescriptorPool m_Pool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_RtgiTraceLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_RtgiDenoiseLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_RtgiAtrousLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_PtLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_PtDenoiseLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_PtSvgfTemporalLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_PtSvgfAtrousLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_PtSvgfModulateLayout = VK_NULL_HANDLE;

    VkDescriptorSet m_RtgiTraceSets[kFrameSlots]{};
    VkDescriptorSet m_RtgiDenoiseSets[kFrameSlots]{};
    VkDescriptorSet m_RtgiAtrousSets[kFrameSlots][3]{};
    VkDescriptorSet m_PtSets[kFrameSlots]{};
    VkDescriptorSet m_PtDenoiseSets[kFrameSlots]{};
    VkDescriptorSet m_PtSvgfTemporalSets[kFrameSlots]{};
    VkDescriptorSet m_PtSvgfAtrousSets[kFrameSlots][5]{};
    VkDescriptorSet m_PtSvgfModulateSets[kFrameSlots]{};

    VkPipelineLayout m_RtgiTracePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_RtgiTracePipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_RtgiDenoisePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_RtgiDenoisePipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_RtgiAtrousPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_RtgiAtrousPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_PtPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_PtPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_PtDenoisePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_PtDenoisePipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_PtSvgfTemporalPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_PtSvgfTemporalPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_PtSvgfAtrousPipeLayout = VK_NULL_HANDLE;
    VkPipeline m_PtSvgfAtrousPipeline = VK_NULL_HANDLE;

    VkPipelineLayout m_PtSvgfModulatePipeLayout = VK_NULL_HANDLE;
    VkPipeline m_PtSvgfModulatePipeline = VK_NULL_HANDLE;

    VulkanBuffer m_GiParamsUBOs[kFrameSlots];
};

#endif // !SLIP_VULKAN_RAY_TRACED_GI_H
