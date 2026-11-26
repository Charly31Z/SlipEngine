#pragma once

#ifndef SLIP_VULKAN_STREAMLINE_H
#define SLIP_VULKAN_STREAMLINE_H

#include "SlipEngine/core/SlipExport.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

class VulkanContext;

struct DlssRayReconstructionAOVs {
    VkImage     diffuseAlbedo = VK_NULL_HANDLE;
    VkImageView diffuseAlbedoView = VK_NULL_HANDLE;
    VkImage     specularAlbedo = VK_NULL_HANDLE;
    VkImageView specularAlbedoView = VK_NULL_HANDLE;
    VkImage     normalsRoughness = VK_NULL_HANDLE;
    VkImageView normalsRoughnessView = VK_NULL_HANDLE;
    VkImage     linearDepth = VK_NULL_HANDLE;
    VkImageView linearDepthView = VK_NULL_HANDLE;
    VkImage     motionVectors = VK_NULL_HANDLE;
    VkImageView motionVectorsView = VK_NULL_HANDLE;
    VkImage     noisyDiffuse = VK_NULL_HANDLE;
    VkImageView noisyDiffuseView = VK_NULL_HANDLE;
    VkImage     noisySpecular = VK_NULL_HANDLE;
    VkImageView noisySpecularView = VK_NULL_HANDLE;
    VkImage     hitDistance = VK_NULL_HANDLE;
    VkImageView hitDistanceView = VK_NULL_HANDLE;
    VkImage     colorOut = VK_NULL_HANDLE;
    VkImageView colorOutView = VK_NULL_HANDLE;

    uint32_t    renderWidth = 0;
    uint32_t    renderHeight = 0;
    uint32_t    outputWidth = 0;
    uint32_t    outputHeight = 0;
    uint32_t    frameIndex = 0;
    int         qualityMode = 1; // 0=DLAA, 1=Quality, 2=Balanced, 3=Performance
    bool        resetHistory = false;
    glm::vec2   jitterOffsetPixels{ 0.0f };
    glm::mat4   cameraViewToClip{ 1.0f };
    glm::mat4   clipToCameraView{ 1.0f };
    glm::mat4   clipToPrevClip{ 1.0f };
    glm::mat4   prevClipToClip{ 1.0f };
};

class SLIPENGINE_API VulkanStreamline {
public:
    static VulkanStreamline& Get();

    // Required optional Vulkan extensions for NVIDIA NGX / Streamline DLSS-RR when supported by driver
    static void AppendRecommendedInstanceExtensions(const std::vector<VkExtensionProperties>& available, std::vector<const char*>& inoutExts);
    static void AppendRecommendedDeviceExtensions(const std::vector<VkExtensionProperties>& available, std::vector<const char*>& inoutExts);

    bool Initialize(VulkanContext* context);
    void Shutdown();

    bool IsNvidiaRTXDetected() const { return m_NvidiaRtxDetected; }
    bool IsStreamlineInterposerLoaded() const { return m_InterposerLoaded; }
    bool IsRayReconstructionAvailable() const { return m_RayReconstructionAvailable; }
    const std::string& GetStatusMessage() const { return m_StatusMessage; }

    // Halton(2, 3) subpixel jitter in [-0.5, +0.5] pixel units for DLSS / Ray Reconstruction
    static glm::vec2 ComputeHaltonJitterPixels(uint32_t frameIndex, uint32_t phaseCount = 16u);

    // Evaluates DLSS Ray Reconstruction into aovs.colorOut if NVIDIA Streamline + nvngx_dlssd.dll are active.
    // Returns false if runtime DLLs are absent so caller seamlessly falls back to NRD ReBLUR denoiser.
    bool EvaluateRayReconstruction(VkCommandBuffer cmd, const DlssRayReconstructionAOVs& aovs);

private:
    VulkanStreamline() = default;
    ~VulkanStreamline();

    VulkanContext* m_Context = nullptr;
    void* m_InterposerModule = nullptr;
    void* m_DlssRRModule = nullptr;
    bool m_Initialized = false;
    bool m_NvidiaRtxDetected = false;
    bool m_InterposerLoaded = false;
    bool m_RayReconstructionAvailable = false;
    std::string m_StatusMessage = "No inicializado";
};

#endif // !SLIP_VULKAN_STREAMLINE_H
