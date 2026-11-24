#include <iostream>
#include <cstring>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h> // OBLIGATORIO para Android: redirige main() a la actividad nativa
#include "SlipEngine.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/tools/SlipFileSystem.h"

#if defined(SLIP_PLATFORM_ANDROID)
    #include <android/log.h>
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SlipEngine", __VA_ARGS__)
#else
    #define LOGI(...) std::cout << __VA_ARGS__ << std::endl
#endif

int main(int argc, char* argv[]) {
    LOGI("Arrancando SlipEngine en la plataforma...");

    if (SlipFileSystem::Exists("config/GraphicsSettings.ini")) {
        SlipGraphicsSettings::Get().LoadFromFile("config/GraphicsSettings.ini");
    }

    GraphicsAPI api = SlipGraphicsSettings::Get().GetData().graphicsAPI;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--vulkan") == 0) api = GraphicsAPI::Vulkan;
        else if (std::strcmp(argv[i], "--opengl") == 0) api = GraphicsAPI::OpenGL;
    }

    SlipEngine engine;
    if (!engine.Init("SlipEngine Game", 1280, 720, api)) {
        LOGI("Error al inicializar SlipEngine.");
        return -1;
    }

    engine.Run();

    engine.Shutdown();
    LOGI("SlipEngine finalizado limpiamente.");
    return 0;
}
