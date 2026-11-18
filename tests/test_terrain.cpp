#include <glad/glad.h>
#include <SDL2/SDL.h>
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipTerrainComponent.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include <iostream>
#include <cassert>

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    std::cout << "=================================================" << std::endl;
    std::cout << "      SlipEngine: Terrain & V-Clust Test Suite   " << std::endl;
    std::cout << "=================================================" << std::endl;

    // 0. Inicializar contexto OpenGL oculto para pruebas que usen buffers gráficos
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        std::cerr << "Error al inicializar SDL2: " << SDL_GetError() << std::endl;
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* window = SDL_CreateWindow("TestTerrainContext", 0, 0, 128, 128, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) {
        std::cerr << "Error al crear ventana SDL: " << SDL_GetError() << std::endl;
        return 1;
    }
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext || !gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        std::cerr << "Error al cargar GLAD / contexto OpenGL" << std::endl;
        return 1;
    }
    std::cout << "[TEST] 0. Contexto OpenGL headless cargado con éxito." << std::endl;

    // 1. Inicializar Física
    SlipPhysics::Get();
    std::cout << "[TEST] 1. Inicializacion del modulo de fisica Bullet..." << std::endl;
    assert(SlipPhysics::IsInitialized());
    std::cout << "  -> Bullet Physics inicializado correctamente." << std::endl;

    // 2. Crear Entidad de Terreno y Componente
    std::cout << "[TEST] 2. Creacion de SlipEntity con SlipTerrainComponent..." << std::endl;
    SlipEntity* terrainEnt = new SlipEntity("TerrenoTest");
    terrainEnt->SetStatic(true);
    terrainEnt->position = glm::vec3(10.0f, 0.0f, 20.0f);
    if (terrainEnt->GetTransform()) {
        terrainEnt->GetTransform()->position = glm::vec3(10.0f, 0.0f, 20.0f);
    }

    auto terrainComp = terrainEnt->AddComponent<SlipTerrainComponent>();
    terrainComp->width = 128.0f;
    terrainComp->length = 128.0f;
    terrainComp->resolutionX = 32;
    terrainComp->resolutionZ = 32;
    terrainComp->heightScale = 25.0f;
    terrainComp->uvScale = glm::vec2(16.0f, 16.0f);
    terrainComp->enableCollision = true;

    // Generar geometría, clusters V-Clust y colisión
    terrainComp->RebuildTerrain();

    assert(terrainComp->GetMesh() != nullptr);
    assert(terrainComp->GetVertexCount() == (32 + 1) * (32 + 1));
    assert(terrainComp->GetTriangleCount() == 32 * 32 * 2);
    std::cout << "  -> Geometria generada: " << terrainComp->GetVertexCount() 
              << " vertices, " << terrainComp->GetTriangleCount() << " triangulos." << std::endl;

    // 3. Validar integración con V-Clust
    std::cout << "[TEST] 3. Validacion de Jerarquia V-Clust en SlipTerrainComponent..." << std::endl;
    SlipMesh* mesh = terrainComp->GetMesh();
    assert(mesh->hasClusterLOD == true);
    assert(!mesh->clusters.empty());
    assert(!mesh->clusterVertices.empty());
    assert(!mesh->clusterIndices.empty());
    assert(mesh->clusterHeader.totalClusters > 0);
    std::cout << "  -> Clusters V-Clust construidos: " << terrainComp->GetClusterCount()
              << " clusters totales. LOD maximo: " << mesh->clusterHeader.maxLodLevel << std::endl;

    // 4. Validar Colisión Bullet Physics
    std::cout << "[TEST] 4. Validacion de colision automatica btBvhTriangleMeshShape..." << std::endl;
    btRigidBody* rb = terrainComp->GetRigidBody();
    assert(rb != nullptr);
    assert(terrainComp->GetCollisionShape() != nullptr);
    assert(rb->isStaticObject());

    // Sincronizar transformación con entidad
    terrainComp->SyncTransformToPhysics();
    btTransform worldTrans = rb->getWorldTransform();
    assert(std::abs(worldTrans.getOrigin().getX() - 10.0f) < 1e-3f);
    assert(std::abs(worldTrans.getOrigin().getZ() - 20.0f) < 1e-3f);
    std::cout << "  -> btRigidBody estatico activo en Bullet Physics en (" 
              << worldTrans.getOrigin().getX() << ", " 
              << worldTrans.getOrigin().getY() << ", " 
              << worldTrans.getOrigin().getZ() << ")." << std::endl;

    // 5. Validar Serialización y Deserialización de Escena
    std::cout << "[TEST] 5. Serializacion y Deserializacion en SlipLevel..." << std::endl;
    SlipLevel testLevel;
    testLevel.AddEntity(terrainEnt);

    std::string scenePath = "test_terrain_scene.scene";
    bool saved = testLevel.SaveScene(scenePath);
    assert(saved);
    std::cout << "  -> Escena con SlipTerrainComponent guardada exitosamente." << std::endl;

    // Cargar en un nuevo nivel
    SlipLevel loadedLevel;
    bool loaded = loadedLevel.LoadScene(scenePath, false);
    assert(loaded);
    assert(loadedLevel.GetEntities().size() == 1);

    SlipEntity* loadedEnt = loadedLevel.GetEntities()[0];
    assert(loadedEnt != nullptr);
    auto loadedTerrain = loadedEnt->GetComponent<SlipTerrainComponent>();
    assert(loadedTerrain != nullptr);
    assert(loadedTerrain->width == 128.0f);
    assert(loadedTerrain->length == 128.0f);
    assert(loadedTerrain->resolutionX == 32);
    assert(loadedTerrain->resolutionZ == 32);
    assert(loadedTerrain->heightScale == 25.0f);
    assert(loadedTerrain->uvScale.x == 16.0f);
    assert(loadedTerrain->GetClusterCount() > 0);
    assert(loadedTerrain->GetRigidBody() != nullptr);
    std::cout << "  -> Escena cargada: terreno reconstruido con "
              << loadedTerrain->GetClusterCount() << " clusters V-Clust y collider Bullet activo." << std::endl;

    // 6. Validar Control de Versión (Revision) y Wireframe
    std::cout << "[TEST] 6. Validacion de Revision (dirty BVH) y Wireframe..." << std::endl;
    uint32_t prevRev = loadedTerrain->GetRevision();
    loadedTerrain->wireframe = true;
    loadedTerrain->heightScale = 45.0f;
    loadedTerrain->RebuildTerrain();
    assert(loadedTerrain->GetRevision() > prevRev);
    assert(loadedTerrain->wireframe == true);
    std::cout << "  -> Revision incrementada tras RebuildTerrain: " << prevRev << " -> " << loadedTerrain->GetRevision() << std::endl;

    // 7. Validar Volumetric Fog en PostProcessVolumeComponent
    std::cout << "[TEST] 7. Validacion de Niebla Volumetrica en PostProcessVolumeComponent..." << std::endl;
    SlipEntity* postEnt = new SlipEntity("PostProcessVolume");
    auto postComp = postEnt->AddComponent<PostProcessVolumeComponent>();
    postComp->fogEnabled = true;
    postComp->fogDensity = 0.005f;
    postComp->fogHeightFalloff = 0.04f;
    postComp->fogMaxDistance = 15000.0f;
    postComp->fogColor = glm::vec3(0.8f, 0.7f, 0.6f);

    PostProcessSettings resolved = PostProcessVolumeComponent::Resolve(&loadedLevel, glm::vec3(0.0f));
    loadedLevel.AddEntity(postEnt);
    PostProcessSettings resolvedWithFog = PostProcessVolumeComponent::Resolve(&loadedLevel, glm::vec3(0.0f));
    assert(resolvedWithFog.fog == true);
    assert(std::abs(resolvedWithFog.fogDensity - 0.005f) < 1e-4f);
    assert(std::abs(resolvedWithFog.fogMaxDistance - 15000.0f) < 1.0f);
    std::cout << "  -> PostProcessSettings resolvio niebla volumetrica: Density=" 
              << resolvedWithFog.fogDensity << ", MaxDist=" << resolvedWithFog.fogMaxDistance << " m." << std::endl;

    // Limpieza de archivo temporal
    std::remove(scenePath.c_str());

    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::cout << "=================================================" << std::endl;
    std::cout << "   ¡TODAS LAS PRUEBAS DE TERRENO SUPERADAS!     " << std::endl;
    std::cout << "=================================================" << std::endl;

    return 0;
}
