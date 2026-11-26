#include "SlipEngine/render/vulkan/VulkanContext.h"
#include "SlipEngine/render/vulkan/VulkanStreamline.h"

#include <SDL2/SDL.h>
#define NO_SDL_VULKAN_TYPEDEFS
#include <SDL2/SDL_vulkan.h>

#define VMA_VULKAN_HEADERS_ALREADY_INCLUDED
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void* /*userData*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT && callbackData && callbackData->pMessage) {
        std::cerr << "[Vulkan Validation] " << callbackData->pMessage << std::endl;
    }
    return VK_FALSE;
}

bool HasExtension(const std::vector<VkExtensionProperties>& available, const char* name) {
    for (const auto& ext : available) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool HasLayer(const std::vector<VkLayerProperties>& available, const char* name) {
    for (const auto& layer : available) {
        if (std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

} // namespace

VulkanContext::~VulkanContext() {
    Shutdown();
}

bool VulkanContext::Initialize(SDL_Window* window, bool headless) {
    if (m_Device != VK_NULL_HANDLE) {
        return true;
    }

    m_Headless = headless;

    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "[VulkanContext] Error: volkInitialize() fallo. Hay un driver Vulkan instalado?" << std::endl;
        return false;
    }
    m_VolkInitialized = true;

    if (!CreateInstance(window)) {
        Shutdown();
        return false;
    }

    if (!m_Headless && window != nullptr) {
        if (SDL_Vulkan_CreateSurface(window, m_Instance, &m_Surface) != SDL_TRUE) {
            std::cerr << "[VulkanContext] Error al crear VkSurfaceKHR via SDL2: " << SDL_GetError() << std::endl;
            Shutdown();
            return false;
        }
    }

    if (!PickPhysicalDevice()) {
        Shutdown();
        return false;
    }

    if (!CreateLogicalDevice()) {
        Shutdown();
        return false;
    }

    if (!CreateAllocator()) {
        Shutdown();
        return false;
    }

    if (!CreateCommandAndDescriptorPools()) {
        Shutdown();
        return false;
    }

    std::cout << "[VulkanContext] Inicializado en GPU: " << m_Capabilities.deviceName
              << " (Vulkan " << m_Capabilities.apiVersionMajor << "." << m_Capabilities.apiVersionMinor
              << ", HW RT=" << (m_Capabilities.supportsRayTracing ? "SI" : "NO")
              << ", RayQuery=" << (m_Capabilities.supportsRayQuery ? "SI" : "NO")
              << ")" << std::endl;

    return true;
}

void VulkanContext::Shutdown() {
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }

    if (m_DescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(m_Device, m_DescriptorPool, nullptr);
        m_DescriptorPool = VK_NULL_HANDLE;
    }

    if (m_CommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
        m_CommandPool = VK_NULL_HANDLE;
    }

    if (m_Allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_Allocator);
        m_Allocator = VK_NULL_HANDLE;
    }

    if (m_Device != VK_NULL_HANDLE) {
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
    }

    if (m_Surface != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
    }

    if (m_DebugMessenger != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT) {
        vkDestroyDebugUtilsMessengerEXT(m_Instance, m_DebugMessenger, nullptr);
        m_DebugMessenger = VK_NULL_HANDLE;
    }

    if (m_Instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
    }

    m_PhysicalDevice = VK_NULL_HANDLE;
    m_GraphicsQueue = VK_NULL_HANDLE;
    m_GraphicsQueueFamily = UINT32_MAX;
    m_Capabilities = {};
}

void VulkanContext::WaitIdle() {
    if (m_Device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(m_Device);
    }
}

PFN_vkGetInstanceProcAddr VulkanContext::GetInstanceProcAddrPtr() const {
    return vkGetInstanceProcAddr;
}

PFN_vkVoidFunction VulkanContext::LoadInstanceFunction(const char* name) const {
    if (!vkGetInstanceProcAddr || !name) return nullptr;
    return vkGetInstanceProcAddr(m_Instance, name);
}

bool VulkanContext::CreateInstance(SDL_Window* window) {
    uint32_t instanceVersion = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) {
        vkEnumerateInstanceVersion(&instanceVersion);
    }

    uint32_t targetApi = (instanceVersion >= VK_API_VERSION_1_3) ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "SlipEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "SlipEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = targetApi;

    std::vector<const char*> extensions;
    if (!m_Headless && window != nullptr) {
        unsigned int sdlExtCount = 0;
        if (SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, nullptr) != SDL_TRUE) {
            std::cerr << "[VulkanContext] SDL_Vulkan_GetInstanceExtensions fallo: " << SDL_GetError() << std::endl;
            return false;
        }
        extensions.resize(sdlExtCount);
        if (SDL_Vulkan_GetInstanceExtensions(window, &sdlExtCount, extensions.data()) != SDL_TRUE) {
            std::cerr << "[VulkanContext] SDL_Vulkan_GetInstanceExtensions (fill) fallo: " << SDL_GetError() << std::endl;
            return false;
        }
    }

    uint32_t availExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availExtCount, nullptr);
    std::vector<VkExtensionProperties> availExts(availExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availExtCount, availExts.data());

    VulkanStreamline::AppendRecommendedInstanceExtensions(availExts, extensions);

    uint32_t availLayerCount = 0;
    vkEnumerateInstanceLayerProperties(&availLayerCount, nullptr);
    std::vector<VkLayerProperties> availLayers(availLayerCount);
    vkEnumerateInstanceLayerProperties(&availLayerCount, availLayers.data());

    std::vector<const char*> layers;
#if !defined(NDEBUG)
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    if (HasLayer(availLayers, validationLayer)) {
        layers.push_back(validationLayer);
        m_ValidationEnabled = true;
        if (HasExtension(availExts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }
#endif

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();

    VkDebugUtilsMessengerCreateInfoEXT debugInfo{};
    if (m_ValidationEnabled && HasExtension(availExts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
        debugInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugInfo.pfnUserCallback = DebugUtilsCallback;
        createInfo.pNext = &debugInfo;
    }

    VkResult res = vkCreateInstance(&createInfo, nullptr, &m_Instance);
    if (res != VK_SUCCESS) {
        std::cerr << "[VulkanContext] vkCreateInstance fallo (VkResult=" << res << ")" << std::endl;
        return false;
    }

    volkLoadInstance(m_Instance);

    if (m_ValidationEnabled && vkCreateDebugUtilsMessengerEXT) {
        vkCreateDebugUtilsMessengerEXT(m_Instance, &debugInfo, nullptr, &m_DebugMessenger);
    }

    return true;
}

bool VulkanContext::PickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        std::cerr << "[VulkanContext] No se encontraron GPUs compatibles con Vulkan." << std::endl;
        return false;
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

    int bestScore = -1;
    VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
    uint32_t bestQueueFamily = UINT32_MAX;
    RHIHardwareCapabilities bestCaps{};

    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(dev, &props);

        if (props.apiVersion < VK_API_VERSION_1_2) {
            continue;
        }

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
        std::vector<VkQueueFamilyProperties> qFamilies(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qFamilies.data());

        uint32_t foundQueue = UINT32_MAX;
        for (uint32_t i = 0; i < qCount; ++i) {
            bool hasGraphicsCompute = (qFamilies[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                                      (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
            if (!hasGraphicsCompute) continue;

            if (!m_Headless && m_Surface != VK_NULL_HANDLE) {
                VkBool32 presentSupported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_Surface, &presentSupported);
                if (!presentSupported) continue;
            }
            foundQueue = i;
            break;
        }

        if (foundQueue == UINT32_MAX) continue;

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &extCount, exts.data());

        if (!m_Headless && !HasExtension(exts, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }

        bool hasAccelStruct = HasExtension(exts, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        bool hasRayQuery = HasExtension(exts, VK_KHR_RAY_QUERY_EXTENSION_NAME);
        bool hasDeferredHost = HasExtension(exts, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);

        VkPhysicalDeviceRayQueryFeaturesKHR rqFeatures{};
        rqFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

        VkPhysicalDeviceAccelerationStructureFeaturesKHR asFeatures{};
        asFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        asFeatures.pNext = &rqFeatures;

        VkPhysicalDeviceVulkan13Features vk13Features{};
        vk13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        vk13Features.pNext = &asFeatures;

        VkPhysicalDeviceVulkan12Features vk12Features{};
        vk12Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        vk12Features.pNext = &vk13Features;

        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &vk12Features;

        vkGetPhysicalDeviceFeatures2(dev, &features2);

        int score = 0;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 200;

        bool rtSupported = hasAccelStruct && hasDeferredHost && asFeatures.accelerationStructure && vk12Features.bufferDeviceAddress;
        bool rqSupported = rtSupported && hasRayQuery && rqFeatures.rayQuery;
        if (rtSupported) score += 500;
        if (rqSupported) score += 500;

        if (score > bestScore) {
            bestScore = score;
            bestDevice = dev;
            bestQueueFamily = foundQueue;
            bestCaps.deviceName = props.deviceName;
            bestCaps.apiVersionMajor = VK_API_VERSION_MAJOR(props.apiVersion);
            bestCaps.apiVersionMinor = VK_API_VERSION_MINOR(props.apiVersion);
            bestCaps.apiVersionPatch = VK_API_VERSION_PATCH(props.apiVersion);
            bestCaps.supportsRayTracing = rtSupported;
            bestCaps.supportsRayQuery = rqSupported;
            bestCaps.supportsDynamicRendering = (props.apiVersion >= VK_API_VERSION_1_3) && vk13Features.dynamicRendering;
        }
    }

    if (bestDevice == VK_NULL_HANDLE) {
        std::cerr << "[VulkanContext] No se encontro una GPU Vulkan 1.2+ adecuada." << std::endl;
        return false;
    }

    m_PhysicalDevice = bestDevice;
    m_GraphicsQueueFamily = bestQueueFamily;
    m_Capabilities = bestCaps;
    return true;
}

bool VulkanContext::CreateLogicalDevice() {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availExts(extCount);
    vkEnumerateDeviceExtensionProperties(m_PhysicalDevice, nullptr, &extCount, availExts.data());

    VkPhysicalDeviceRayQueryFeaturesKHR supportedRQ{};
    supportedRQ.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR supportedAS{};
    supportedAS.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    supportedAS.pNext = &supportedRQ;

    VkPhysicalDeviceVulkan13Features supported13{};
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    supported13.pNext = &supportedAS;

    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    supported12.pNext = &supported13;

    VkPhysicalDeviceFeatures2 supported2{};
    supported2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supported2.pNext = &supported12;

    vkGetPhysicalDeviceFeatures2(m_PhysicalDevice, &supported2);

    std::vector<const char*> deviceExts;
    if (!m_Headless) {
        deviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    if (m_Capabilities.supportsRayTracing) {
        deviceExts.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        deviceExts.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
        if (m_Capabilities.supportsRayQuery) {
            deviceExts.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
        }
        VulkanStreamline::AppendRecommendedDeviceExtensions(availExts, deviceExts);
    }

    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = m_GraphicsQueueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    VkPhysicalDeviceRayQueryFeaturesKHR enableRQ{};
    enableRQ.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    enableRQ.rayQuery = m_Capabilities.supportsRayQuery ? VK_TRUE : VK_FALSE;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR enableAS{};
    enableAS.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    enableAS.accelerationStructure = m_Capabilities.supportsRayTracing ? VK_TRUE : VK_FALSE;
    enableAS.pNext = m_Capabilities.supportsRayQuery ? &enableRQ : nullptr;

    VkPhysicalDeviceVulkan13Features enable13{};
    enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enable13.dynamicRendering = supported13.dynamicRendering;
    enable13.synchronization2 = supported13.synchronization2;
    enable13.maintenance4 = supported13.maintenance4;
    enable13.pNext = m_Capabilities.supportsRayTracing ? &enableAS : nullptr;

    VkPhysicalDeviceVulkan12Features enable12{};
    enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enable12.bufferDeviceAddress = supported12.bufferDeviceAddress;
    enable12.descriptorIndexing = supported12.descriptorIndexing;
    enable12.runtimeDescriptorArray = supported12.runtimeDescriptorArray;
    enable12.descriptorBindingPartiallyBound = supported12.descriptorBindingPartiallyBound;
    enable12.descriptorBindingSampledImageUpdateAfterBind = supported12.descriptorBindingSampledImageUpdateAfterBind;
    enable12.descriptorBindingStorageImageUpdateAfterBind = supported12.descriptorBindingStorageImageUpdateAfterBind;
    enable12.descriptorBindingStorageBufferUpdateAfterBind = supported12.descriptorBindingStorageBufferUpdateAfterBind;
    enable12.shaderSampledImageArrayNonUniformIndexing = supported12.shaderSampledImageArrayNonUniformIndexing;
    enable12.scalarBlockLayout = supported12.scalarBlockLayout;

    if (m_Capabilities.apiVersionMajor > 1 || m_Capabilities.apiVersionMinor >= 3) {
        enable12.pNext = &enable13;
    } else if (m_Capabilities.supportsRayTracing) {
        enable12.pNext = &enableAS;
    }

    VkPhysicalDeviceFeatures2 enableFeatures{};
    enableFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enableFeatures.features.samplerAnisotropy = supported2.features.samplerAnisotropy;
    enableFeatures.features.shaderInt64 = supported2.features.shaderInt64;
    enableFeatures.pNext = &enable12;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enableFeatures;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExts.size());
    deviceInfo.ppEnabledExtensionNames = deviceExts.empty() ? nullptr : deviceExts.data();

    VkResult res = vkCreateDevice(m_PhysicalDevice, &deviceInfo, nullptr, &m_Device);
    if (res != VK_SUCCESS) {
        std::cerr << "[VulkanContext] vkCreateDevice fallo (VkResult=" << res << ")" << std::endl;
        return false;
    }

    volkLoadDevice(m_Device);
    vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
    return true;
}

bool VulkanContext::CreateAllocator() {
    VmaVulkanFunctions vmaFuncs{};
    vmaFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFuncs.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocInfo{};
    allocInfo.instance = m_Instance;
    allocInfo.physicalDevice = m_PhysicalDevice;
    allocInfo.device = m_Device;
    allocInfo.vulkanApiVersion = (m_Capabilities.apiVersionMajor > 1 || m_Capabilities.apiVersionMinor >= 3)
                                 ? VK_API_VERSION_1_3
                                 : VK_API_VERSION_1_2;
    allocInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocInfo.pVulkanFunctions = &vmaFuncs;

    VkResult res = vmaCreateAllocator(&allocInfo, &m_Allocator);
    if (res != VK_SUCCESS) {
        std::cerr << "[VulkanContext] vmaCreateAllocator fallo (VkResult=" << res << ")" << std::endl;
        return false;
    }
    return true;
}

bool VulkanContext::CreateCommandAndDescriptorPools() {
    VkCommandPoolCreateInfo cmdPoolInfo{};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    cmdPoolInfo.queueFamilyIndex = m_GraphicsQueueFamily;

    if (vkCreateCommandPool(m_Device, &cmdPoolInfo, nullptr, &m_CommandPool) != VK_SUCCESS) {
        std::cerr << "[VulkanContext] vkCreateCommandPool fallo." << std::endl;
        return false;
    }

    std::vector<VkDescriptorPoolSize> poolSizes = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 256 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1024 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1024 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256 },
    };

    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    descPoolInfo.maxSets = 2048;
    descPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    descPoolInfo.pPoolSizes = poolSizes.data();

    if (vkCreateDescriptorPool(m_Device, &descPoolInfo, nullptr, &m_DescriptorPool) != VK_SUCCESS) {
        std::cerr << "[VulkanContext] vkCreateDescriptorPool fallo." << std::endl;
        return false;
    }

    return true;
}

VkCommandBuffer VulkanContext::BeginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = m_CommandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    return cmd;
}

void VulkanContext::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
    if (commandBuffer == VK_NULL_HANDLE) return;
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(m_GraphicsQueue);
    vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &commandBuffer);
}
