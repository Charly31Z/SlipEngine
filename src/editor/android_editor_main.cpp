#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)

#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h> // Redirige main a la actividad de Android
#include <GLES3/gl3.h>     // OpenGL ES 3.0 nativo del NDK
#include <android/log.h>

#include "SlipEngine/platform/SlipWindow.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/editor/SlipEditor.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SlipEditor", __VA_ARGS__)

int main(int argc, char* argv[]) {
    LOGI("Arrancando SlipEditor en Android...");

    // SlipWindow inicializa SDL, EGL y la superficie OpenGL ES 3.0
    SlipWindow window("SlipEditor", 1280, 720);
    if (!window.IsValid()) {
        LOGI("Error al inicializar SlipWindow en Android");
        return -1;
    }

    SlipInput input;

    SlipEditor editor;
    if (!editor.Init(window)) {
        LOGI("Error al inicializar SlipEditor en Android");
        return -1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            editor.ProcessEvent(event);
            SlipInput::ProcessEvent(event);

            if (event.type == SDL_QUIT) {
                running = false;
            }
        }

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        editor.BeginFrame();
        editor.RenderPanels();
        editor.EndFrame();

        window.SwapBuffers();
    }

    editor.Shutdown();
    LOGI("SlipEditor en Android finalizado limpiamente.");
    return 0;
}

#endif
