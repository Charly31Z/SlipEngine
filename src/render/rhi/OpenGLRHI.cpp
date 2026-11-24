#include "SlipEngine/render/rhi/OpenGLRHI.h"
#include "SlipEngine/render/SlipGL.h"
#include <SDL2/SDL.h>
#include <cstdlib>
#include <string>

OpenGLRHI::~OpenGLRHI() {
    Shutdown();
}

bool OpenGLRHI::Initialize(SDL_Window* window, bool headless) {
    m_Window = window;
    m_Headless = headless;
    m_Capabilities = {};

    if (!headless && glGetString != nullptr) {
        const char* rendererStr = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* versionStr = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        if (rendererStr) {
            m_Capabilities.deviceName = rendererStr;
        }
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major > 0) {
            m_Capabilities.apiVersionMajor = static_cast<uint32_t>(major);
            m_Capabilities.apiVersionMinor = static_cast<uint32_t>(minor);
        } else if (versionStr) {
            m_Capabilities.apiVersionMajor = 3;
            m_Capabilities.apiVersionMinor = 0;
        }
    } else if (headless) {
        m_Capabilities.deviceName = "OpenGL Headless";
    }

    m_Initialized = true;
    return true;
}

void OpenGLRHI::Shutdown() {
    m_Initialized = false;
    m_Window = nullptr;
}

bool OpenGLRHI::BeginFrame() {
    if (m_Headless) return true;
    if (glClearColor && glClear) {
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    return true;
}

void OpenGLRHI::EndFrame() {
    if (!m_Headless && m_Window) {
        SDL_GL_SwapWindow(m_Window);
    }
}

void OpenGLRHI::OnResize(int width, int height) {
    if (!m_Headless && glViewport && width > 0 && height > 0) {
        glViewport(0, 0, width, height);
    }
}

void OpenGLRHI::SetVSync(bool enabled) {
    if (!m_Headless) {
        SDL_GL_SetSwapInterval(enabled ? 1 : 0);
    }
}

void OpenGLRHI::WaitIdle() {
    if (!m_Headless && glFinish) {
        glFinish();
    }
}
