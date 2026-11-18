#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(SLIP_BUILD_DLL)
        // Se exporta mientras se compila SlipEngine.dll
        #define SLIPENGINE_API __declspec(dllexport)
    #elif defined(SLIP_USE_DLL)
        // Se importa cuando el Editor o Runner consumen la DLL
        #define SLIPENGINE_API __declspec(dllimport)
    #else
        // Compilación estática
        #define SLIPENGINE_API
    #endif
#else
    // Android / Linux (visibilidad pública para libSlipEngine.so)
    #define SLIPENGINE_API __attribute__ ((visibility ("default")))
#endif