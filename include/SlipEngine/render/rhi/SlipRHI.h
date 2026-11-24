#pragma once

#ifndef SLIP_RHI_H
#define SLIP_RHI_H

#include "SlipEngine/core/SlipExport.h"
#include <cstdint>
#include <memory>
#include <string>

struct SDL_Window;

enum class GraphicsAPI : int {
    OpenGL = 0,
    Vulkan = 1
};

using SlipGraphicsAPI = GraphicsAPI;

struct SLIPENGINE_API RHIBufferHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }
};

struct SLIPENGINE_API RHITextureHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }
};

struct SLIPENGINE_API RHIShaderHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }
};

struct SLIPENGINE_API RHIPipelineHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }
};

struct SLIPENGINE_API RHIFramebufferHandle {
    uint64_t id = 0;
    bool IsValid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }
};

struct SLIPENGINE_API RHIHardwareCapabilities {
    std::string deviceName = "Unknown";
    uint32_t apiVersionMajor = 0;
    uint32_t apiVersionMinor = 0;
    uint32_t apiVersionPatch = 0;
    bool supportsRayTracing = false;
    bool supportsRayQuery = false;
    bool supportsDynamicRendering = false;
};

class SLIPENGINE_API ISlipRHI {
public:
    virtual ~ISlipRHI() = default;

    virtual GraphicsAPI GetAPI() const = 0;
    virtual bool Initialize(SDL_Window* window, bool headless = false) = 0;
    virtual void Shutdown() = 0;
    virtual bool BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void OnResize(int width, int height) = 0;
    virtual void SetVSync(bool enabled) = 0;
    virtual void WaitIdle() = 0;
    virtual const RHIHardwareCapabilities& GetCapabilities() const = 0;
};

class SLIPENGINE_API SlipRHI {
public:
    static ISlipRHI* Create(GraphicsAPI api, SDL_Window* window, bool headless = false);
    static ISlipRHI* Get();
    static void Destroy();
    static GraphicsAPI GetActiveAPI();
    static const char* GetAPIName(GraphicsAPI api);
};

#endif // !SLIP_RHI_H
