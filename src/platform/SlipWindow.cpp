#include "SlipWindow.h"

#include <iostream>
#include <assert.h>

#include "SlipFrameBuffer.h"

void SlipWindow::OnResize(int newWidth, int newHeight)
{
    width = newWidth;
    height = newHeight;
    glViewport(0, 0, width, height);
    if (SlipFrameBuffer::GetInstance() != nullptr)
    {
        SlipFrameBuffer::GetInstance()->resize();
    }
}

SlipWindow::SlipWindow(const char* title, int width, int height) : title(title), width(width), height(height), m_window(nullptr), m_glContext(nullptr)
{
    assert(!m_instance && "Is already open the application...");
    m_instance = this;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::cout << "Failed to initialize SDL2: " << SDL_GetError() << std::endl;
        return;
    }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    // 4.3 Core: compute shaders para DDGI. Android permanece en GLES 3.0.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI;
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    windowFlags |= SDL_WINDOW_FULLSCREEN;
#else
    windowFlags |= SDL_WINDOW_RESIZABLE;
#endif

    m_window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        windowFlags
    );

    if (m_window == nullptr)
    {
        std::cout << "Failed to create " << title << " window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return;
    }

    m_glContext = SDL_GL_CreateContext(m_window);
    if (!m_glContext)
    {
        std::cout << "Failed to create OpenGL context: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        SDL_Quit();
        return;
    }

    SDL_GL_MakeCurrent(m_window, m_glContext);
    // V-Sync desactivado por defecto (0) para framerate ilimitado en PC
    SDL_GL_SetSwapInterval(0);

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return;
    }
#endif
}

void SlipWindow::SetVSync(bool enabled)
{
    SDL_GL_SetSwapInterval(enabled ? 1 : 0);
}

SlipWindow::~SlipWindow()
{
    if (m_glContext)
    {
        SDL_GL_DeleteContext(m_glContext);
        m_glContext = nullptr;
    }
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
    m_instance = nullptr;
}
