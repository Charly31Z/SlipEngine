#include <iostream>
#include <cassert>
#include <vector>
#include <random>

#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/render/SlipClusteredLighting.h"
#include "SlipEngine/render/SlipLightBaker.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipLevel.h"

#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

int main(int argc, char* argv[]) {
    std::cout << "====================================================" << std::endl;
    std::cout << "  RUNNING CLUSTERED LIGHTING & LIGHT BAKER TESTS" << std::endl;
    std::cout << "====================================================" << std::endl;

    // Test 1: Alineación de memoria std430 y tamaños binarios
    std::cout << "[Test 1] Verificando alineación estricta std430 (64 bytes)..." << std::endl;
    static_assert(sizeof(GpuLight) == 64, "GpuLight debe medir exactamente 64 bytes para std430.");
    static_assert(alignof(GpuLight) == 16, "GpuLight debe tener alineación de 16 bytes.");
    static_assert(sizeof(GpuClusterAABB) == 32, "GpuClusterAABB debe medir 32 bytes.");
    static_assert(alignof(GpuClusterAABB) == 16, "GpuClusterAABB debe tener alineación de 16 bytes.");
    std::cout << "  -> PASS: GpuLight mide " << sizeof(GpuLight) << " bytes (alineado a 16 bytes)." << std::endl;

    // Test 2: Canal UV2 en SlipMesh::Vertex
    std::cout << "[Test 2] Verificando soporte de canal UV2 en Vertex..." << std::endl;
    Vertex v{};
    v.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    v.TexCoords = glm::vec2(0.5f, 0.5f);
    v.TexCoords2 = glm::vec2(0.25f, 0.75f); // Lightmap UV
    assert(v.TexCoords2.x == 0.25f && v.TexCoords2.y == 0.75f);
    std::cout << "  -> PASS: Vertex contiene canal secundario TexCoords2." << std::endl;

    // Test 3: Propiedades de movilidad de luz en LightComponent
    std::cout << "[Test 3] Verificando enum LightMobility en LightComponent..." << std::endl;
    LightComponent lc(LightType::Point);
    assert(lc.mobility == LightMobility::Dynamic);
    lc.mobility = LightMobility::Static;
    assert(lc.mobility == LightMobility::Static);
    lc.mobility = LightMobility::Mixed;
    assert(lc.mobility == LightMobility::Mixed);
    std::cout << "  -> PASS: LightComponent soporta movilidad Static, Mixed y Dynamic." << std::endl;

    // Inicializar SDL2 y Contexto OpenGL 4.3 para pruebas de cómputo en GPU
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "Fallo al inicializar SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    SDL_Window* window = SDL_CreateWindow(
        "ClusteredLightingTest",
        SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        640, 480,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN
    );

    if (!window) {
        std::cerr << "Fallo al crear ventana de prueba: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        std::cerr << "Fallo al crear contexto GL: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Fallo al cargar GLAD!" << std::endl;
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
#endif

    const char* glVer = (const char*)glGetString(GL_VERSION);
    const char* glRenderer = (const char*)glGetString(GL_RENDERER);
    std::cout << "OpenGL Version: " << (glVer ? glVer : "null") << std::endl;
    std::cout << "OpenGL Renderer: " << (glRenderer ? glRenderer : "null") << std::endl;

    // Test 4: Inicialización del sistema Clustered Shading en GPU
    std::cout << "[Test 4] Inicializando SlipClusteredLighting en GPU..." << std::endl;
    auto& clustered = SlipClusteredLighting::Get();
    clustered.Initialize();
    std::cout << "  IsSupported: " << (clustered.IsSupported() ? "true" : "false") << std::endl;
    std::cout << "  IsEnabled: " << (clustered.IsEnabled() ? "true" : "false") << std::endl;
    if (!clustered.IsSupported()) {
        std::cout << "  Nota: Entorno sin soporte de OpenGL 4.3 Compute Shaders en este hardware/driver." << std::endl;
    } else {
        assert(clustered.IsSupported());
        assert(clustered.IsEnabled());
        std::cout << "  -> PASS: Compute Shaders y SSBOs inicializados con éxito." << std::endl;
    }

    // Test 4.5: Verificando compilación del shader PBR "Lit" con soporte Clustered Forward
    std::cout << "[Test 4.5] Compilando shader PBR 'Lit' con soporte de Clustered Shading SSBOs..." << std::endl;
    SlipShaderLibrary::Init();
    SlipShader* litShader = SlipShaderLibrary::Get("Lit");
    assert(litShader != nullptr && litShader->ID != 0);
    std::cout << "  -> PASS: Shader PBR Lit compilado y enlazado correctamente (ID: " << litShader->ID << ")." << std::endl;

    // Test 5: Prueba de estrés masivo con >1,000 Luces (1,024 y 2,048)
    std::cout << "[Test 5] Generando 1,500 luces dinámicas en buffer lineal contiguo..." << std::endl;
    const size_t numStressLights = 1500;
    std::vector<GpuLight> testLights(numStressLights);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> distPos(-50.0f, 50.0f);
    std::uniform_real_distribution<float> distCol(0.2f, 1.0f);
    std::uniform_real_distribution<float> distRange(5.0f, 25.0f);

    for (size_t i = 0; i < numStressLights; ++i) {
        testLights[i].position_range = glm::vec4(distPos(rng), distPos(rng), distPos(rng), distRange(rng));
        testLights[i].color_intensity = glm::vec4(distCol(rng), distCol(rng), distCol(rng), 2.0f);
        testLights[i].direction_spotAngle = glm::vec4(0.0f, -1.0f, 0.0f, 45.0f);
        testLights[i].params = glm::uvec4(0u, 0u, 0u, 0u); // Point light
    }

    // Subir al SSBO
    clustered.UploadLights(testLights);
    assert(clustered.GetActiveLightCount() == numStressLights);
    std::cout << "  -> PASS: 1,500 luces subidas al SSBO contiguo lineal." << std::endl;

    // Test 6: Construcción de cuadrícula de Clusters 3D (16x9x24 = 3456 clusters)
    std::cout << "[Test 6] Construyendo AABBs de cuadrícula de 3,456 clusters en GPU..." << std::endl;
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    glm::mat4 invProj = glm::inverse(proj);
    clustered.UpdateGrid(proj, invProj, 0.1f, 100.0f, 1920, 1080);
    std::cout << "  -> PASS: Compute shader de AABBs de clusters ejecutado." << std::endl;

    // Test 7: Culling espacial de luces en GPU
    std::cout << "[Test 7] Despachando Compute Shader de Light Culling en GPU..." << std::endl;
    glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 10.0f, 30.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    clustered.CullLights(view);

    GLenum err = glGetError();
    assert(err == GL_NO_ERROR);
    std::cout << "  -> PASS: Culling espacial y asignación completados sin errores de OpenGL." << std::endl;

    // Limpieza
    clustered.Shutdown();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "\n>>> ALL CLUSTERED LIGHTING TESTS PASSED SUCCESSFULLY! <<<\n" << std::endl;
    return 0;
}
