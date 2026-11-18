#pragma once

#include "SlipExport.h"

struct SDL_Window;
typedef void* SDL_GLContext;

class SLIPENGINE_API SlipEngine {
public:
    SlipEngine();
    ~SlipEngine();

    bool Init(const char* title = "SlipEngine", int width = 1280, int height = 720);
    bool IsRunning() const;
    void Run();
    void ProcessEvents();
    void FixedUpdate(float fixedDt);
    void Update(float dt = -1.0f);
    void Render();
    void Shutdown();

    SDL_Window* GetWindow() const { return m_Window; }
    SDL_GLContext GetGLContext() const { return m_GLContext; }

    float GetFixedDeltaTime() const { return m_FixedDeltaTime; }
    void SetFixedDeltaTime(float dt) { m_FixedDeltaTime = dt; }

    int GetTargetFPS() const { return m_TargetFPS; }
    void SetTargetFPS(int fps) { m_TargetFPS = fps; }

private:
    bool m_IsRunning;
    SDL_Window* m_Window;
    SDL_GLContext m_GLContext;
    float m_FixedDeltaTime;
    float m_Accumulator;
    int m_TargetFPS;
};
