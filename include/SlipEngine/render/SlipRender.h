#pragma once

#ifndef SLIP_RENDER_H
#define SLIP_RENDER_H

#include "SlipEngine.h"
#include "SlipEngine/render/SlipCluster.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/scene/SlipCamera.h"
#include "SlipEngine/scene/SlipFrustum.h"

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif

// ============================================================================
// Entrada de clúster visible tras el traversal jerárquico V-Clust
// ============================================================================
struct SLIPENGINE_API SlipVisibleCluster
{
    uint32_t clusterId     = 0;
    uint32_t materialIndex = 0;
    uint32_t submeshIndex  = 0;
    uint32_t lodLevel      = 0;
    uint32_t indexOffset   = 0;
    uint32_t indexCount    = 0;
    int32_t  vertexOffset  = 0;
    float    distanceToCam = 0.0f;
    float    projectedSSE  = 0.0f;
};

// ============================================================================
// Estadísticas de telemetría por frame para V-Clust
// ============================================================================
struct SLIPENGINE_API SlipVClustStats
{
    uint32_t totalClustersTraversed = 0;
    uint32_t frustumCulledClusters  = 0;
    uint32_t backfaceCulledClusters = 0;
    uint32_t visibleClusters        = 0;
    uint32_t drawCallsIssued        = 0;
    uint32_t trianglesSubmitted     = 0;

    void Reset()
    {
        totalClustersTraversed = 0;
        frustumCulledClusters  = 0;
        backfaceCulledClusters = 0;
        visibleClusters        = 0;
        drawCallsIssued        = 0;
        trianglesSubmitted     = 0;
    }
};

// ============================================================================
// SlipRender: Pipeline de Renderizado Runtime Multiplataforma V-Clust
// - Evaluación de Screen-Space Error (SSE) en espacio de pantalla.
// - Frustum Culling esférico contra los 6 planos de SlipCamera/SlipFrustum.
// - Backface Cluster Culling mediante cono normal promedio (coneAxis, coneCutoff).
// - Ruta PC (OpenGL 4.5+): Compute Shader opcional + glMultiDrawElementsIndirect.
// - Ruta Android (OpenGL ES 3.0 TBDR): CPU-Driven batching ordenado por material
//   y profundidad front-to-back sin romper cachés de tiles ni usar compute pesado.
// ============================================================================
class SLIPENGINE_API SlipRender
{
public:
    SlipRender() = default;
    ~SlipRender();

    // Inicializa recursos de GPU para el despacho de clusters (Indirect Command Buffer, SSBOs en PC)
    void Initialize();
    void Shutdown();

    // ------------------------------------------------------------------------
    // FASE 3.1: Proyección del Screen-Space Error (SSE)
    // ErrorPixeles = (lodError * ViewportHeight) / (2.0f * Distancia * tan(FOV * 0.5f))
    // ------------------------------------------------------------------------
    static float ComputeScreenSpaceError(
        float lodError,
        float distance,
        float viewportHeight,
        float fovRadians
    );

    // ------------------------------------------------------------------------
    // FASE 3.2: Culling Geométrico por Clúster
    // ------------------------------------------------------------------------
    // Frustum Culling esférico contra los 6 planos de SlipFrustum
    static bool IsClusterFrustumVisible(
        const SlipCluster& cluster,
        const glm::mat4& worldTransform,
        const SlipFrustum& frustum,
        glm::vec3* outWorldCenter = nullptr,
        float* outWorldRadius = nullptr
    );

    // Backface Cluster Culling evaluando el cono de normales respecto al vector de visión
    static bool IsClusterBackfaceCulled(
        const SlipCluster& cluster,
        const glm::mat4& worldTransform,
        const glm::vec3& cameraWorldPos
    );

    // ------------------------------------------------------------------------
    // FASE 3.3: Traversal Jerárquico y Selección de LOD (Corte continuo del DAG/Árbol)
    // - Si ErrorPixeles <= sseThreshold (1.0f por defecto): procesa el clúster padre.
    // - Si ErrorPixeles > sseThreshold (1.0f): desciende recursivamente a los hijos.
    // ------------------------------------------------------------------------
    static void TraverseClusterHierarchy(
        const std::vector<SlipCluster>& clusters,
        uint32_t rootClusterCount,
        const glm::mat4& worldTransform,
        const SlipFrustum& frustum,
        const glm::vec3& cameraWorldPos,
        float viewportHeight,
        float fovRadians,
        std::vector<SlipVisibleCluster>& outVisibleClusters,
        SlipVClustStats* inoutStats = nullptr,
        float ssePixelThreshold = 1.0f,
        bool enableBackfaceConeCulling = true
    );

    // Sobrecarga directa usando SlipCamera
    static void SelectVisibleClusters(
        const SlipMesh& mesh,
        const glm::mat4& worldTransform,
        SlipCamera& camera,
        std::vector<SlipVisibleCluster>& outVisibleClusters,
        SlipVClustStats* inoutStats = nullptr,
        float ssePixelThreshold = 1.0f,
        bool enableBackfaceConeCulling = true
    );

    // Construye la lista de comandos DrawElementsIndirectCommand a partir de los clusters seleccionados
    static void BuildIndirectCommands(
        const std::vector<SlipVisibleCluster>& visibleClusters,
        std::vector<DrawElementsIndirectCommand>& outCommands
    );

    // ------------------------------------------------------------------------
    // FASE 4: Despacho Multiplataforma (PC OpenGL 4.5+ vs Android OpenGL ES 3.0 TBDR)
    // ------------------------------------------------------------------------
    void RenderClusteredMesh(
        SlipMesh& mesh,
        const glm::mat4& worldTransform,
        SlipCamera& camera,
        SlipShader* activeShader,
        float ssePixelThreshold = 1.0f
    );

    // Ruta PC (High-End OpenGL 4.5+): Llena GL_DRAW_INDIRECT_BUFFER y ejecuta glMultiDrawElementsIndirect
    void RenderClustersPC_Indirect(
        SlipMesh& mesh,
        const std::vector<SlipVisibleCluster>& visibleClusters,
        SlipShader* activeShader
    );

    // Ruta Android (OpenGL ES 3.0 / TBDR): Ordena por material + profundidad (Early-Z) y agrupa rangos contiguos
    void RenderClustersAndroid_TBDR(
        SlipMesh& mesh,
        std::vector<SlipVisibleCluster>& visibleClusters,
        SlipShader* activeShader
    );

    // Cdigo fuente del Compute Shader GLSL 450 para GPU Culling y llenado de DrawElementsIndirectCommand en PC
    static const char* GetPCClusterCullingComputeShaderSource();

    // ------------------------------------------------------------------------
    // FASE 5: Sistema Hbrido de Nubes Volumtricas (PC + Android)
    // ------------------------------------------------------------------------
    static SlipRender& GetInstance();

    void InitCloudSystem();
    void ShutdownCloudSystem();

    void RenderClouds(
        class SlipSkyComponent* skyComp,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        unsigned int sceneDepthTexture,
        int viewportWidth,
        int viewportHeight,
        float nearPlane,
        float farPlane,
        float currentTime
    );

    const SlipVClustStats& GetLastFrameStats() const { return m_Stats; }
    void ResetStats() { m_Stats.Reset(); }

private:
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    void RenderCloudsAndroid_Slices(
        class SlipSkyComponent* skyComp,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        float currentTime
    );
#else
    void RenderCloudsPC_Raymarch(
        class SlipSkyComponent* skyComp,
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& cameraPos,
        unsigned int sceneDepthTexture,
        int viewportWidth,
        int viewportHeight,
        float nearPlane,
        float farPlane,
        float currentTime
    );
#endif

    // Malla de planos subdivididos para Volume Slices en Android
    unsigned int m_SliceVAO = 0;
    unsigned int m_SliceVBO = 0;
    unsigned int m_SliceEBO = 0;
    uint32_t     m_SliceIndexCount = 0;
    void InitSliceMesh();

    // Framebuffer secundario a 1/4 resolución para Raymarching en PC
    unsigned int m_CloudFBO = 0;
    unsigned int m_CloudColorTex = 0;
    int          m_CloudFboWidth = 0;
    int          m_CloudFboHeight = 0;
    void EnsureCloudFBO(int width, int height);

    // Framebuffer full-resolution para Bilateral Upscale desacoplado (evita feedback loops de OpenGL)
    unsigned int m_CloudUpscaleFBO = 0;
    unsigned int m_CloudUpscaleTex = 0;
    int          m_CloudUpscaleWidth = 0;
    int          m_CloudUpscaleHeight = 0;
    unsigned int m_CloudBlitProgram = 0;
    void EnsureCloudUpscaleFBO(int width, int height);

    // Quad de pantalla completa para despachos en PC
    unsigned int m_FullscreenQuadVAO = 0;
    unsigned int m_FullscreenQuadVBO = 0;
    void InitFullscreenQuad();

private:
    bool            m_Initialized       = false;
    unsigned int    m_IndirectBuffer    = 0;
    size_t          m_IndirectCapacity  = 0;
    unsigned int    m_ComputeProgram    = 0;
    unsigned int    m_ClusterSSBO       = 0;
    unsigned int    m_AtomicCounterBuf  = 0;
    SlipVClustStats m_Stats{};
};

#endif // !SLIP_RENDER_H
