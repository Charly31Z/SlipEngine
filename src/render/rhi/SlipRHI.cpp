#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/render/rhi/OpenGLRHI.h"

#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    #include "SlipEngine/render/vulkan/VulkanRHI.h"
#endif

#include <iostream>
#include <memory>

namespace {
    std::unique_ptr<ISlipRHI> s_Instance;
    GraphicsAPI s_ActiveAPI = GraphicsAPI::OpenGL;
}

ISlipRHI* SlipRHI::Create(GraphicsAPI api, SDL_Window* window, bool headless) {
    Destroy();

    std::unique_ptr<ISlipRHI> backend;
    if (api == GraphicsAPI::Vulkan) {
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
        backend = std::make_unique<VulkanRHI>();
#else
        std::cerr << "[SlipRHI] Vulkan no está habilitado en esta plataforma; usando OpenGL." << std::endl;
        api = GraphicsAPI::OpenGL;
        backend = std::make_unique<OpenGLRHI>();
#endif
    } else {
        backend = std::make_unique<OpenGLRHI>();
    }

    if (!backend || !backend->Initialize(window, headless)) {
        std::cerr << "[SlipRHI] Fallo al inicializar backend " << GetAPIName(api) << "." << std::endl;
        return nullptr;
    }

    s_ActiveAPI = backend->GetAPI();
    s_Instance = std::move(backend);
    return s_Instance.get();
}

ISlipRHI* SlipRHI::Get() {
    return s_Instance.get();
}

void SlipRHI::Destroy() {
    if (s_Instance) {
        s_Instance->Shutdown();
        s_Instance.reset();
    }
    s_ActiveAPI = GraphicsAPI::OpenGL;
}

GraphicsAPI SlipRHI::GetActiveAPI() {
    if (s_Instance) {
        return s_Instance->GetAPI();
    }
    return s_ActiveAPI;
}

const char* SlipRHI::GetAPIName(GraphicsAPI api) {
    return (api == GraphicsAPI::Vulkan) ? "Vulkan" : "OpenGL";
}
