#pragma once

#ifndef SLIP_VULKAN_CONTEXT_H
#define SLIP_VULKAN_CONTEXT_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/rhi/SlipRHI.h"

#if defined(SLIP_USE_VOLK) || defined(VK_NO_PROTOTYPES)
    #include <volk.h>
#else
    #include <vulkan/vulkan.h>
#endif

typedef struct VmaAllocator_T* VmaAllocator;

#include <cstdint>
#include <vector>

struct SDL_Window;

class SLIPENGINE_API VulkanContext {
public:
    static constexpr uint32_t kMaxFramesInFlight = 2;

    VulkanContext() = default;
    ~VulkanContext();

    bool Initialize(SDL_Window* window, bool headless = false);
    void Shutdown();

    void WaitIdle();

    VkInstance GetInstance() const { return m_Instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
    VkDevice GetDevice() const { return m_Device; }
    VmaAllocator GetAllocator() const { return m_Allocator; }

    uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
    VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
    VkCommandPool GetCommandPool() const { return m_CommandPool; }
    VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }

    VkSurfaceKHR GetSurface() const { return m_Surface; }
    bool IsHeadless() const { return m_Headless; }
    bool IsValid() const { return m_Device != VK_NULL_HANDLE; }
    const RHIHardwareCapabilities& GetCapabilities() const { return m_Capabilities; }

    PFN_vkGetInstanceProcAddr GetInstanceProcAddrPtr() const;
    PFN_vkVoidFunction LoadInstanceFunction(const char* name) const;

    VkCommandBuffer BeginSingleTimeCommands();
    void EndSingleTimeCommands(VkCommandBuffer commandBuffer);

private:
    bool CreateInstance(SDL_Window* window);
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateAllocator();
    bool CreateCommandAndDescriptorPools();

    bool m_Headless = false;
    bool m_VolkInitialized = false;
    bool m_ValidationEnabled = false;

    VkInstance m_Instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_Device = VK_NULL_HANDLE;
    VmaAllocator m_Allocator = VK_NULL_HANDLE;

    uint32_t m_GraphicsQueueFamily = UINT32_MAX;
    VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
    VkCommandPool m_CommandPool = VK_NULL_HANDLE;
    VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;

    RHIHardwareCapabilities m_Capabilities{};
};

#endif // !SLIP_VULKAN_CONTEXT_H
