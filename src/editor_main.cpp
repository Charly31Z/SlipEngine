#include <iostream>
#include <cstring>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <SDL2/SDL.h>
    #include <SDL2/SDL_main.h> // Redirige main a la actividad de Android
    #include <GLES3/gl3.h>     // OpenGL ES 3.0 nativo del NDK
    #include <android/log.h>
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SlipEditor", __VA_ARGS__)
#else
    #define SDL_MAIN_HANDLED
    #include <SDL2/SDL.h>
    #include <glad/glad.h>
    #define LOGI(...) std::cout << __VA_ARGS__ << std::endl
#endif

#include "SlipEngine/platform/SlipWindow.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include "SlipEngine/tools/SlipFileSystem.h"

int main(int argc, char* argv[]) {
    LOGI("Arrancando SlipEditor...");

    if (SlipFileSystem::Exists("config/GraphicsSettings.ini")) {
        SlipGraphicsSettings::Get().LoadFromFile("config/GraphicsSettings.ini");
    }

    GraphicsAPI api = SlipGraphicsSettings::Get().GetData().graphicsAPI;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--vulkan") == 0) api = GraphicsAPI::Vulkan;
        else if (std::strcmp(argv[i], "--opengl") == 0) api = GraphicsAPI::OpenGL;
    }

    SlipWindow window("SlipEditor - Level Editor", 1600, 900, api);
    if (!window.IsValid()) {
        LOGI("Error al inicializar SlipWindow");
        return -1;
    }

    // Inicializar SlipInput para manejo de eventos y ratón/teclado
    SlipInput input;

    // Inicializar el orquestador del editor (configura ImGui)
    SlipEditor editor;
    if (!editor.Init(window)) {
        LOGI("Error al inicializar SlipEditor");
        return -1;
    }

    bool running = true;
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    while (running) {
        Uint64 perfCounter = SDL_GetPerformanceCounter();
        SlipGlobals::Get().update(static_cast<double>(perfCounter) / static_cast<double>(perfFreq));

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            editor.ProcessEvent(event);
            SlipInput::ProcessEvent(event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_WINDOWEVENT &&
                event.window.event == SDL_WINDOWEVENT_CLOSE &&
                event.window.windowID == SDL_GetWindowID(window.getWindow())) {
                running = false;
            }
        }

        if (window.getGraphicsAPI() == GraphicsAPI::Vulkan) {
            ISlipRHI* rhi = SlipRHI::Get();
            if (rhi && rhi->BeginFrame()) {
                editor.BeginFrame();
                editor.RenderPanels();
                editor.EndFrame();
                rhi->EndFrame();
            }
        } else {
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            editor.BeginFrame();
            editor.RenderPanels();
            editor.EndFrame();

            window.SwapBuffers();
        }
    }

    editor.Shutdown();
    LOGI("SlipEditor finalizado limpiamente.");
    return 0;
}
