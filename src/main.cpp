#include <iostream>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h> // OBLIGATORIO para Android: redirige main() a la actividad nativa
#include "SlipEngine.h"

#if defined(SLIP_PLATFORM_ANDROID)
    #include <android/log.h>
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "SlipEngine", __VA_ARGS__)
#else
    #define LOGI(...) std::cout << __VA_ARGS__ << std::endl
#endif

int main(int argc, char* argv[]) {
    LOGI("Arrancando SlipEngine en la plataforma...");

    SlipEngine engine;
    if (!engine.Init("SlipEngine Game", 1280, 720)) {
        LOGI("Error al inicializar SlipEngine.");
        return -1;
    }

    engine.Run();

    engine.Shutdown();
    LOGI("SlipEngine finalizado limpiamente.");
    return 0;
}