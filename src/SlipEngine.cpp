#include "SlipEngine.h"
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif
#include <SDL2/SDL.h>
#include <iostream>
#include <sstream>
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipSkyComponent.h"
#include "SlipEngine/render/SlipRender.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include <glm/gtc/matrix_transform.hpp>

namespace {

static std::string DiscoverGameStartScene() {
    std::string root = SlipFileSystem::GetDefaultWorkingPath();
    auto projFiles = SlipFileSystem::ListDirectory(root, ".slipproj");
    if (!projFiles.empty()) {
        std::string content;
        if (SlipFileSystem::ReadAllText(projFiles[0].fullPath, content)) {
            std::stringstream ss(content);
            std::string line;
            while (std::getline(ss, line)) {
                size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string k = line.substr(0, colon);
                std::string v = line.substr(colon + 1);
                k.erase(0, k.find_first_not_of(" \t\r\n")); k.erase(k.find_last_not_of(" \t\r\n") + 1);
                v.erase(0, v.find_first_not_of(" \t\r\n")); v.erase(v.find_last_not_of(" \t\r\n") + 1);
                if (k == "game_start_scene" && !v.empty()) {
                    bool isAbs = (v.size() >= 2 && v[1] == ':') || (v.size() >= 1 && (v[0] == '/' || v[0] == '\\'));
                    std::string p = isAbs ? v : SlipFileSystem::CombinePath(root, v);
                    if (SlipFileSystem::Exists(p)) return p;
                }
            }
        }
    }

    const std::vector<std::string> candidates = {
        SlipFileSystem::CombinePath(root, "scenes/Main.scene"),
        "assets/scenes/Main.scene",
        "scenes/Main.scene",
        "Main.scene"
    };

    for (const auto& c : candidates) {
        if (!c.empty() && SlipFileSystem::Exists(c)) {
            return c;
        }
    }

    return "";
}

} // namespace

SlipEngine::SlipEngine()
    : m_IsRunning(false)
    , m_Window(nullptr)
    , m_GLContext(nullptr)
    , m_FixedDeltaTime(1.0f / 60.0f)
    , m_Accumulator(0.0f)
    , m_TargetFPS(0)
{
}

SlipEngine::~SlipEngine() {
    Shutdown();
}

bool SlipEngine::Init(const char* title, int width, int height) {
    std::cout << "[SlipEngine] Inicializando SDL2..." << std::endl;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0) {
        std::cerr << "[SlipEngine Error] No se pudo inicializar SDL2: " << SDL_GetError() << std::endl;
        return false;
    }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
    // Configuración mínima de OpenGL (Versión 3.3 Core)
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // Crear la ventana de SDL2
    m_Window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        width,
        height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    );

    if (!m_Window) {
        std::cerr << "[SlipEngine Error] Fallo al crear SDL_Window: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    // Crear contexto OpenGL
    m_GLContext = SDL_GL_CreateContext(m_Window);
    if (!m_GLContext) {
        std::cerr << "[SlipEngine Error] Fallo al crear contexto OpenGL: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        SDL_Quit();
        return false;
    }

    // Vincular el contexto OpenGL al hilo actual
    SDL_GL_MakeCurrent(m_Window, m_GLContext);
    SDL_GL_SetSwapInterval(0); // V-Sync desactivado (0) para framerate ilimitado en PC

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    // CARGAR GLAD: Esencial antes de cualquier llamada de OpenGL
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "[SlipEngine Error] Fallo al inicializar GLAD (punteros OpenGL nulos)." << std::endl;
        SDL_GL_DeleteContext(m_GLContext);
        m_GLContext = nullptr;
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
        SDL_Quit();
        return false;
    }
#endif

    std::cout << "[SlipEngine] OpenGL inicializado con exito. Version: " 
              << glGetString(GL_VERSION) << std::endl;

    glEnable(GL_DEPTH_TEST);

    // Inicializar shaders
    SlipShaderLibrary::Init();

    // Cargar escena inicial configurada para el juego
    std::string startScene = DiscoverGameStartScene();
    if (!startScene.empty() && SlipFileSystem::Exists(startScene)) {
        std::cout << "[SlipEngine] Cargando escena de juego: " << startScene << std::endl;
        SlipLevel::Get().LoadScene(startScene, false);
    } else {
        std::cout << "[SlipEngine] No se especifico o no existe escena inicial de juego (" << startScene << "). Inicializando escena por defecto con cielo y nubes." << std::endl;

        // Entidad 1: Cielo y Nubes Volumétricas Híbridas
        auto skyEnt = new SlipEntity("Cielo y Nubes");
        skyEnt->AddComponent<SlipSkyComponent>();
        SlipLevel::Get().AddEntity(skyEnt);

        // Entidad 2: Luz Direccional Solar
        auto lightEnt = new SlipEntity("Luz Solar");
        auto lightComp = lightEnt->AddComponent<LightComponent>();
        lightComp->type = LightType::Directional;
        lightComp->color = glm::vec3(1.0f, 0.98f, 0.92f);
        lightComp->intensity = 1.0f;
        lightEnt->rotation = glm::vec3(35.0f, 45.0f, 0.0f);
        SlipLevel::Get().AddEntity(lightEnt);

        // Entidad 3: Cámara Principal de Juego
        auto camEnt = new SlipEntity("Camara Principal");
        auto camComp = camEnt->AddComponent<CameraComponent>();
        camComp->isPrimary = true;
        camEnt->position = glm::vec3(0.0f, 2.5f, 10.0f);
        SlipLevel::Get().AddEntity(camEnt);

        // Entidad 4: Plano de Suelo
        auto planeEnt = new SlipEntity("Plano Suelo");
        planeEnt->AddComponent<MeshComponent>("primitive:plane");
        SlipLevel::Get().AddEntity(planeEnt);
    }

    m_IsRunning = true;
    return true;
}

bool SlipEngine::IsRunning() const {
    return m_IsRunning && m_Window != nullptr;
}

void SlipEngine::ProcessEvents() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        SlipInput::ProcessEvent(event);
        if (event.type == SDL_QUIT) {
            m_IsRunning = false;
        }
        else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(m_Window)) {
            m_IsRunning = false;
        }
    }
}

void SlipEngine::FixedUpdate(float fixedDt) {
    SlipGlobals::Get().SetFixedDeltaTime(fixedDt);

    // Simulación física con Bullet a paso fijo
    if (SlipPhysics::IsInitialized()) {
        SlipPhysics::Get().update(fixedDt);
    }

    // Sincronizar colliders y cuerpos 2D
    for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
        if (ent) {
            if (auto col = ent->GetComponent<ColliderComponent>()) {
                col->SyncTransformFromEntity();
            }
            if (auto rb2d = ent->GetComponent<RigidBody2DComponent>()) {
                if (rb2d->bodyType == BodyType2D::Dynamic) {
                    rb2d->SyncTransformFromPhysics();
                }
            }
        }
    }
}

void SlipEngine::Update(float dt) {
    if (dt <= 0.0f) {
        dt = SlipGlobals::Get().GetDeltaTime();
    } else {
        SlipGlobals::Get().SetDeltaTime(dt);
    }

    // Lógica dinámica, scripts y animaciones
    SlipLevel::Get().scripts();

    for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
        if (ent) {
            ent->ejecStart();
            ent->Update(dt);
        }
    }
}

void SlipEngine::Render() {
    if (!m_Window) return;

    int w = 1280, h = 720;
    SDL_GetWindowSize(m_Window, &w, &h);
    glViewport(0, 0, w, h);

    // 1. Configuración de Cámara (Principal de la escena o fallback por defecto)
    CameraComponent* mainCam = SlipLevel::Get().GetMainCamera();
    glm::vec3 eye(0.0f, 3.0f, 10.0f);
    glm::mat4 view = glm::lookAt(eye, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.777f;
    glm::mat4 proj = glm::perspective(glm::radians(60.0f), aspect, 0.1f, 1000.0f);
    float nearP = 0.1f;
    float farP = 1000.0f;
    glm::vec4 clearColor(0.25f, 0.45f, 0.70f, 1.0f);

    if (mainCam) {
        eye = mainCam->GetPosition();
        view = mainCam->GetViewMatrix();
        proj = mainCam->GetProjectionMatrix(aspect);
        nearP = mainCam->nearClip;
        farP = mainCam->farClip;
        clearColor = mainCam->clearColor;
    }

    // 2. Luz Direccional Solar
    glm::vec3 mainLightDir = glm::normalize(glm::vec3(0.35f, -0.75f, 0.45f));
    glm::vec3 mainLightColor(1.0f, 0.98f, 0.92f);
    for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
        if (!ent) continue;
        if (auto lc = ent->GetComponent<LightComponent>()) {
            if (lc->type == LightType::Directional && lc->IsEnabled()) {
                glm::mat4 rotMat = ent->getMatrix();
                mainLightDir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));
                mainLightColor = lc->color * lc->intensity;
                break;
            }
        }
    }

    // 3. Atmósfera Dinámica y Color de Fondo
    SlipSkyComponent* skyComp = SlipSkyComponent::FindActive(&SlipLevel::Get());
    if (skyComp) {
        if (skyComp->params.followDirectionalLight && glm::length(mainLightDir) > 0.0001f) {
            skyComp->params.sunDirection = -glm::normalize(mainLightDir);
        }
        if (skyComp->params.dynamicAtmosphere) {
            skyComp->UpdateAtmosphereFromSun();
            clearColor = glm::vec4(skyComp->currentAtmosphere.horizonColor, 1.0f);
        }
    }

    glClearColor(clearColor.r, clearColor.g, clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);

    static float s_RuntimeCloudTime = 0.0f;
    s_RuntimeCloudTime += 0.016f;
    float cloudTime = (skyComp && skyComp->GetCurrentTime() > 0.0f) ? skyComp->GetCurrentTime() : s_RuntimeCloudTime;

    // 4. Dibujar Entidades 3D (MeshComponent y Primitivos)
    SlipShader* litShader = SlipShaderLibrary::LoadOrGet("Lit");
    if (litShader) {
        litShader->use();
        litShader->setMat4("u_Projection", proj);
        litShader->setMat4("u_View", view);
        litShader->setVec3("u_CameraPos", eye);
        litShader->setInt("u_NumDirLights", 1);
        litShader->setVec3("u_DirLights[0].direction", mainLightDir);
        litShader->setVec3("u_DirLights[0].color", mainLightColor);
        litShader->setFloat("u_DirLights[0].intensity", 1.0f);
        litShader->setInt("u_NumPointLights", 0);
        litShader->setInt("u_NumSpotLights", 0);

        if (skyComp && skyComp->params.cloudShadowsEnabled && skyComp->IsEnabled()) {
            litShader->setBool("u_CloudShadowsEnabled", true);
            litShader->setFloat("u_CloudShadowIntensity", skyComp->params.cloudShadowIntensity);
            litShader->setFloat("u_CloudBaseHeight", skyComp->params.cloudBaseHeight);
            litShader->setFloat("u_CloudLayerThickness", skyComp->params.cloudLayerThickness);
            litShader->setFloat("u_CloudScale", skyComp->params.cloudScale);
            litShader->setFloat("u_CloudCoverage", skyComp->params.cloudCoverage);
            litShader->setFloat("u_CloudDensity", skyComp->params.cloudDensity);
            litShader->setFloat("u_CloudEdgeErosion", skyComp->params.cloudEdgeErosion);
            litShader->setFloat("u_CloudDetailScale", skyComp->params.cloudDetailScale);
            litShader->setFloat("u_Absorption", skyComp->params.absorption);

            glm::vec2 windDirNorm = glm::length(skyComp->params.windDirection) > 0.0001f
                                    ? glm::normalize(skyComp->params.windDirection)
                                    : glm::vec2(1.0f, 0.0f);
            glm::vec2 windOffset = windDirNorm * (skyComp->params.windSpeed * cloudTime * skyComp->params.cloudScale);
            litShader->setVec2("u_CloudWindOffset", windOffset);

            skyComp->EnsureDefaultTextures();
            const int CLOUD_SHADOW_SLOT = 19;
            glActiveTexture(GL_TEXTURE19);
            unsigned int noiseID = (skyComp->cloudNoiseTexture && skyComp->cloudNoiseTexture->getID() != 0)
                                   ? skyComp->cloudNoiseTexture->getID()
                                   : skyComp->GetDefaultNoiseTex();
            glBindTexture(GL_TEXTURE_2D, noiseID);
            litShader->setInt("u_CloudShadowNoise", CLOUD_SHADOW_SLOT);
            glActiveTexture(GL_TEXTURE0);
        } else {
            litShader->setBool("u_CloudShadowsEnabled", false);
        }

        for (SlipEntity* ent : SlipLevel::Get().GetEntities()) {
            if (!ent) continue;
            glm::mat4 modelMat = ent->getMatrix();

            if (auto meshComp = ent->GetComponent<MeshComponent>()) {
                if (meshComp->IsEnabled() && meshComp->model) {
                    meshComp->model->draw(modelMat);
                }
            }
            if (auto prim = dynamic_cast<SlipPrimitiveEntity*>(ent)) {
                prim->Draw(view, proj, litShader->ID);
            }
        }
    }

    // 5. Dibujar Nubes Volumétricas (Android: Slices Instanciados / PC: Raymarching)
    if (skyComp && skyComp->IsEnabled()) {
        SlipRender::GetInstance().RenderClouds(
            skyComp,
            view,
            proj,
            eye,
            0,
            w,
            h,
            nearP,
            farP,
            cloudTime
        );
    }

    // 6. Objetos heredados (BSP/Terrain/Spawns)
    SlipLevel::Get().draw();

    // 7. Presentar frame
    SDL_GL_SwapWindow(m_Window);

    // Procesar cualquier cambio de escena diferido tras el renderizado
    SlipLevel::Get().ProcessPendingSceneLoad();
}

void SlipEngine::Run() {
    Uint64 perfFreq = SDL_GetPerformanceFrequency();
    Uint64 lastCounter = SDL_GetPerformanceCounter();
    m_Accumulator = 0.0f;

    while (m_IsRunning) {
        Uint64 currentCounter = SDL_GetPerformanceCounter();
        float frameTime = static_cast<float>(currentCounter - lastCounter) / static_cast<float>(perfFreq);
        lastCounter = currentCounter;

        // Limitar tiempo de cuadro para prevenir 'spiral of death'
        if (frameTime > 0.25f) frameTime = 0.25f;

        SlipGlobals::Get().update(static_cast<double>(currentCounter) / static_cast<double>(perfFreq));
        SlipGlobals::Get().SetDeltaTime(frameTime);

        m_Accumulator += frameTime;

        ProcessEvents();

        // Subpasos de tiempo fijo para física y scripts de FixedUpdate
        while (m_Accumulator >= m_FixedDeltaTime) {
            FixedUpdate(m_FixedDeltaTime);
            m_Accumulator -= m_FixedDeltaTime;
        }

        Update(frameTime);
        Render();
    }
}

void SlipEngine::Shutdown() {
    if (m_GLContext) {
        SDL_GL_DeleteContext(m_GLContext);
        m_GLContext = nullptr;
    }
    if (m_Window) {
        std::cout << "[SlipEngine] Destruyendo ventana..." << std::endl;
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }
    SDL_Quit();
    m_IsRunning = false;
    std::cout << "[SlipEngine] Subsistemas cerrados limpiamente." << std::endl;
}
