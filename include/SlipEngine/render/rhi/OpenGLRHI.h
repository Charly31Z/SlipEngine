#pragma once

#ifndef SLIP_OPENGL_RHI_H
#define SLIP_OPENGL_RHI_H

#include "SlipEngine/render/rhi/SlipRHI.h"

class SLIPENGINE_API OpenGLRHI final : public ISlipRHI {
public:
    OpenGLRHI() = default;
    ~OpenGLRHI() override;

    GraphicsAPI GetAPI() const override { return GraphicsAPI::OpenGL; }
    bool Initialize(SDL_Window* window, bool headless = false) override;
    void Shutdown() override;
    bool BeginFrame() override;
    void EndFrame() override;
    void OnResize(int width, int height) override;
    void SetVSync(bool enabled) override;
    void WaitIdle() override;
    const RHIHardwareCapabilities& GetCapabilities() const override { return m_Capabilities; }

private:
    SDL_Window* m_Window = nullptr;
    bool m_Headless = false;
    bool m_Initialized = false;
    RHIHardwareCapabilities m_Capabilities{};
};

#endif // !SLIP_OPENGL_RHI_H
