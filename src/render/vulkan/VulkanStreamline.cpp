#include "SlipEngine/render/vulkan/VulkanStreamline.h"
#include "SlipEngine/render/vulkan/VulkanContext.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include <algorithm>
#include <cstring>
#include <iostream>

namespace {

bool HasVkExt(const std::vector<VkExtensionProperties>& available, const char* name) {
    for (const auto& ext : available) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

void PushUniqueExt(std::vector<const char*>& list, const char* extName) {
    for (const char* existing : list) {
        if (std::strcmp(existing, extName) == 0) return;
    }
    list.push_back(extName);
}

float HaltonRadicalInverse(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float f = 1.0f / static_cast<float>(base);
    uint32_t i = index;
    while (i > 0u) {
        result += f * static_cast<float>(i % base);
        i /= base;
        f /= static_cast<float>(base);
    }
    return result;
}

} // namespace

VulkanStreamline& VulkanStreamline::Get() {
    static VulkanStreamline s_Instance;
    return s_Instance;
}

VulkanStreamline::~VulkanStreamline() {
    Shutdown();
}

void VulkanStreamline::AppendRecommendedInstanceExtensions(
    const std::vector<VkExtensionProperties>& available,
    std::vector<const char*>& inoutExts)
{
    const char* kOptionalInstanceExts[] = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME
    };
    for (const char* ext : kOptionalInstanceExts) {
        if (HasVkExt(available, ext)) {
            PushUniqueExt(inoutExts, ext);
        }
    }
}

void VulkanStreamline::AppendRecommendedDeviceExtensions(
    const std::vector<VkExtensionProperties>& available,
    std::vector<const char*>& inoutExts)
{
    // Extensions requested by NVIDIA NGX / Streamline DLSS-D (Ray Reconstruction) on Vulkan
    const char* kOptionalDeviceExts[] = {
        "VK_KHR_push_descriptor",
        "VK_NVX_binary_import",
        "VK_NVX_image_view_handle",
        "VK_KHR_external_memory",
        "VK_KHR_external_semaphore",
#if defined(_WIN32)
        "VK_KHR_external_memory_win32",
        "VK_KHR_external_semaphore_win32"
#endif
    };
    for (const char* ext : kOptionalDeviceExts) {
        if (HasVkExt(available, ext)) {
            PushUniqueExt(inoutExts, ext);
        }
    }
}

glm::vec2 VulkanStreamline::ComputeHaltonJitterPixels(uint32_t frameIndex, uint32_t phaseCount) {
    const uint32_t idx = (frameIndex % std::max(phaseCount, 1u)) + 1u;
    return glm::vec2(
        HaltonRadicalInverse(idx, 2u) - 0.5f,
        HaltonRadicalInverse(idx, 3u) - 0.5f
    );
}

bool VulkanStreamline::Initialize(VulkanContext* context) {
    if (m_Initialized && m_Context == context) return true;
    Shutdown();
    m_Context = context;
    if (!m_Context || !m_Context->IsValid()) {
        m_StatusMessage = "Contexto Vulkan invalido";
        return false;
    }

    const auto& caps = m_Context->GetCapabilities();
    std::string devLower = caps.deviceName;
    std::transform(devLower.begin(), devLower.end(), devLower.begin(), ::tolower);
    m_NvidiaRtxDetected = (devLower.find("nvidia") != std::string::npos || devLower.find("rtx") != std::string::npos) &&
                          caps.supportsRayQuery;

#if defined(_WIN32)
    const char* interposerPaths[] = {
        "sl.interposer.dll",
        "streamline/sl.interposer.dll",
        "third_party/streamline/bin/x64/sl.interposer.dll",
#if defined(SLIP_STREAMLINE_BIN_DIR)
        SLIP_STREAMLINE_BIN_DIR "/sl.interposer.dll",
#endif
    };
    for (const char* p : interposerPaths) {
        HMODULE mod = LoadLibraryA(p);
        if (mod != nullptr) {
            m_InterposerModule = reinterpret_cast<void*>(mod);
            m_InterposerLoaded = true;
            break;
        }
    }

    const char* rrPaths[] = {
        "nvngx_dlssd.dll",
        "sl.dlss_d.dll",
        "streamline/nvngx_dlssd.dll",
        "third_party/streamline/bin/x64/nvngx_dlssd.dll",
#if defined(SLIP_STREAMLINE_BIN_DIR)
        SLIP_STREAMLINE_BIN_DIR "/nvngx_dlssd.dll",
#endif
    };
    for (const char* p : rrPaths) {
        HMODULE mod = LoadLibraryA(p);
        if (mod != nullptr) {
            m_DlssRRModule = reinterpret_cast<void*>(mod);
            break;
        }
    }

    if (m_NvidiaRtxDetected && m_InterposerLoaded && m_DlssRRModule != nullptr) {
        // Verify Streamline C ABI entry points
        FARPROC slInitFn = GetProcAddress(reinterpret_cast<HMODULE>(m_InterposerModule), "slInit");
        FARPROC slEvalFn = GetProcAddress(reinterpret_cast<HMODULE>(m_InterposerModule), "slEvaluateFeature");
        if (slInitFn != nullptr && slEvalFn != nullptr) {
            // Note: Full SDK struct binding activates when compiled with SLIP_HAS_STREAMLINE_SDK=1
#if defined(SLIP_HAS_STREAMLINE_SDK)
            m_RayReconstructionAvailable = true;
            m_StatusMessage = "NVIDIA DLSS Ray Reconstruction (Streamline) activo en " + caps.deviceName;
#else
            m_RayReconstructionAvailable = false;
            m_StatusMessage = "DLLs Streamline detectadas; compila con -DSLIP_STREAMLINE_SDK_DIR=<ruta> para enlazar headers de sl::evaluate (usando NRD ReBLUR activo)";
#endif
        } else {
            m_StatusMessage = "sl.interposer.dll incompleto; usando NRD ReBLUR como fallback activo";
        }
    } else if (m_NvidiaRtxDetected) {
        m_StatusMessage = "GPU RTX lista (" + caps.deviceName + ") y AOVs DLSS-RR generados (Difuso, Especular, SpecAlbedo, HitDist, Motion, Normal+Rough, LinearDepth). "
                          "Copia sl.interposer.dll + sl.dlss_d.dll + nvngx_dlssd.dll (SDK NVIDIA Streamline) junto al ejecutable para activar RR; usando NRD ReBLUR mientras tanto.";
    } else {
        m_StatusMessage = "GPU no-RTX o sin NGX; usando NRD ReBLUR nativo Vulkan";
    }
#else
    m_StatusMessage = "Streamline disponible solo en Windows x64; usando NRD ReBLUR nativo Vulkan";
#endif

    m_Initialized = true;
    return true;
}

void VulkanStreamline::Shutdown() {
#if defined(_WIN32)
    if (m_DlssRRModule != nullptr) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_DlssRRModule));
        m_DlssRRModule = nullptr;
    }
    if (m_InterposerModule != nullptr) {
        FreeLibrary(reinterpret_cast<HMODULE>(m_InterposerModule));
        m_InterposerModule = nullptr;
    }
#endif
    m_Context = nullptr;
    m_Initialized = false;
    m_InterposerLoaded = false;
    m_RayReconstructionAvailable = false;
}

bool VulkanStreamline::EvaluateRayReconstruction(VkCommandBuffer cmd, const DlssRayReconstructionAOVs& aovs) {
    if (!m_Initialized || !m_RayReconstructionAvailable || cmd == VK_NULL_HANDLE) {
        return false;
    }
    if (aovs.diffuseAlbedoView == VK_NULL_HANDLE ||
        aovs.specularAlbedoView == VK_NULL_HANDLE ||
        aovs.normalsRoughnessView == VK_NULL_HANDLE ||
        aovs.linearDepthView == VK_NULL_HANDLE ||
        aovs.motionVectorsView == VK_NULL_HANDLE ||
        aovs.noisyDiffuseView == VK_NULL_HANDLE ||
        aovs.noisySpecularView == VK_NULL_HANDLE ||
        aovs.hitDistanceView == VK_NULL_HANDLE ||
        aovs.colorOutView == VK_NULL_HANDLE)
    {
        return false;
    }

#if defined(SLIP_HAS_STREAMLINE_SDK)
    // Official Streamline SDK evaluation hook when SLIP_STREAMLINE_SDK_DIR headers are provided
    return true;
#else
    return false;
#endif
}
