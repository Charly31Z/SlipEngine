#pragma once

#ifndef SLIP_CLUSTERED_LIGHTING_H
#define SLIP_CLUSTERED_LIGHTING_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/SlipGL.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

class SlipShader;

/**
 * @brief Estructura std430 binaria para cada luz en el buffer lineal contiguo (64 bytes).
 */
struct alignas(16) GpuLight {
    glm::vec4 position_range{ 0.0f, 0.0f, 0.0f, 10.0f };       // xyz = pos mundo, w = range
    glm::vec4 color_intensity{ 1.0f, 1.0f, 1.0f, 1.0f };       // xyz = color, w = intensity
    glm::vec4 direction_spotAngle{ 0.0f, -1.0f, 0.0f, 45.0f };  // xyz = dir mundo, w = spotAngle
    glm::uvec4 params{ 0u, 0u, 0u, 0u };                        // x = type (0: Point, 1: Spot, 2: Dir), y = flags, z/w = pad
};

/**
 * @brief Estructura AABB en espacio de vista para cada celda de cluster (32 bytes).
 */
struct alignas(16) GpuClusterAABB {
    glm::vec4 minPoint{ 0.0f };
    glm::vec4 maxPoint{ 0.0f };
};

/**
 * @brief Administrador central del sistema Clustered Forward Shading para SlipEngine.
 * Soporta >1,000 luces dinámicas y mixtas en un único búfer continuo lineal.
 */
class SLIPENGINE_API SlipClusteredLighting {
public:
    static SlipClusteredLighting& Get();

    void Initialize();
    void Shutdown();

    bool IsSupported() const { return m_IsSupported; }
    bool IsEnabled() const { return m_IsEnabled && m_IsSupported; }
    void SetEnabled(bool enabled) { m_IsEnabled = enabled; }

    const glm::uvec3& GetGridDim() const { return m_GridDim; }
    uint32_t GetActiveLightCount() const { return m_UploadedLightCount; }

    // Actualiza los AABBs de la cuadrícula si cambia la proyección, FOV o resolución
    void UpdateGrid(
        const glm::mat4& projection,
        const glm::mat4& invProjection,
        float zNear,
        float zFar,
        uint32_t screenWidth,
        uint32_t screenHeight
    );

    // Carga las luces dinámicas/mixtas al SSBO contiguo lineal
    void UploadLights(const std::vector<GpuLight>& lights);

    // Ejecuta el culling espacial y asignación a clusters en GPU
    void CullLights(const glm::mat4& viewMatrix);

    // Enlaza los SSBOs y uniformes del Clustered Shading al shader PBR Lit
    void BindClusterData(SlipShader* shader, float zNear, float zFar, uint32_t screenW, uint32_t screenH);

private:
    SlipClusteredLighting() = default;
    ~SlipClusteredLighting();

    bool m_Initialized = false;
    bool m_IsSupported = false;
    bool m_IsEnabled = true;

    glm::uvec3 m_GridDim{ 16, 9, 24 };
    uint32_t m_TotalClusters = 16 * 9 * 24; // 3,456 clusters

    uint32_t m_MaxLights = 4096;
    uint32_t m_MaxIndices = 262144;
    uint32_t m_UploadedLightCount = 0;

    // GPU SSBOs
    GLuint m_LightsSSBO = 0;        // Binding 2
    GLuint m_ClusterAABBSSBO = 0;   // Binding 3
    GLuint m_ClusterGridSSBO = 0;   // Binding 4
    GLuint m_LightIndexSSBO = 0;    // Binding 5
    GLuint m_AtomicCounterSSBO = 0; // Binding 6

    // Shaders de cómputo
    SlipShader* m_BuildGridShader = nullptr;
    SlipShader* m_CullLightsShader = nullptr;

    // Caché para evitar recomputar AABBs de cluster innecesariamente
    glm::mat4 m_CachedProj{ 0.0f };
    uint32_t m_CachedWidth = 0;
    uint32_t m_CachedHeight = 0;
    float m_CachedNear = 0.0f;
    float m_CachedFar = 0.0f;
};

#endif // SLIP_CLUSTERED_LIGHTING_H
