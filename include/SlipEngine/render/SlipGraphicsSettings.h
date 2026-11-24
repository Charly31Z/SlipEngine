#pragma once

#ifndef SLIP_GRAPHICS_SETTINGS_H
#define SLIP_GRAPHICS_SETTINGS_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#include <string>
#include <glm/glm.hpp>

enum class GlobalIlluminationMode : int {
    Off = 0,
    RTGI = 1,
    PathTracing = 2
};

enum class PathTracingMode : int {
    Progressive = 0,
    RealtimeSVGF = 1
};

struct SLIPENGINE_API GraphicsSettingsData {
    SlipGraphicsAPI graphicsAPI = SlipGraphicsAPI::OpenGL;

    bool shadowsEnabled = true;
    int shadowResolution = 2048;      // 1024, 2048, 4096
    int cascadeCount = 4;             // 2, 3, 4
    float shadowDistance = 100.0f;    // Distancia máxima de proyección de sombras
    float shadowBias = 0.002f;        // Factor de sesgo base
    int pcfQuality = 1;               // 0 = Off (1x1), 1 = PCF 3x3, 2 = PCF 5x5
    float cascadeLambda = 0.85f;      // Factor de partición logarítmica vs uniforme (0 a 1)

    // Anti-Aliasing (FXAA)
    bool fxAAEnabled = true;
    int fxAAQuality = 1;              // 0 = Off, 1 = Estándar, 2 = Ultra

    // Iluminación Global por Hardware Ray Tracing (Vulkan VK_KHR_ray_query)
    GlobalIlluminationMode giMode = GlobalIlluminationMode::RTGI;
    PathTracingMode ptMode = PathTracingMode::Progressive; // Progresivo vs Tiempo Real (SVGF)
    int giBounces = 2;                // 1..8
    int giRaysPerPixel = 2;           // 1..4 (SPP/frame en PathTracing)
    float giIntensity = 1.0f;         // 0.0..4.0
    bool giDenoiserEnabled = true;    // Denoiser temporal/espacial para RTGI y Path Tracing
    float giTemporalAlpha = 0.08f;    // Mezcla temporal en RTGI/SVGF (menor = más estable)
    bool giRayTracedShadows = true;   // Sombras por ray query en RTGI
    int ptMaxSamples = 256;           // Máximo de muestras en Path Tracing (0 = ilimitado)

    // Calibración en vivo del Denoiser SVGF (Path Tracing en Tiempo Real)
    int svgfAtrousIterations = 3;     // 1..5 (default 3 para preservar sombras de contacto)
    float svgfSigmaLuminance = 2.0f;  // 0.5..5.0 (default 2.0 para respetar bordes de sombra)
    float svgfSigmaNormal = 128.0f;   // 16..256 (default 128.0 para no mezclar caras iluminadas/sombra)
    float svgfSigmaDepth = 1.5f;      // 0.5..4.0 (default 1.5 para penalizar saltos de profundidad)

    // SSR (Screen Space Reflections)
    bool ssrEnabled = true;
    int ssrQuality = 2;               // 0 = baja, 1 = media, 2 = alta (resolución completa)
    float ssrRoughnessCutoff = 0.75f; // Corte suave de rugosidad
    float ssrThickness = 0.45f;       // Espesor para refinamiento de intersección
    float ssrEdgeFade = 0.08f;        // Suavizado en bordes de pantalla

    // Cielo Gradiente Personalizable y Sol Ficticio
    bool skyEnabled = true;
    glm::vec3 skyZenithColor = glm::vec3(0.22f, 0.48f, 0.92f);
    glm::vec3 skyHorizonColor = glm::vec3(0.72f, 0.82f, 0.95f);
    glm::vec3 skyGroundColor = glm::vec3(0.25f, 0.22f, 0.20f);
    float skyHorizonSharpness = 2.5f;

    bool sunDiscEnabled = true;
    float sunSize = 0.045f;
    float sunGlow = 0.18f;
    float sunIntensity = 1.8f;
    glm::vec3 sunColor = glm::vec3(1.0f, 0.96f, 0.88f);

    // Bakeo de Reflejos de Cubemap
    bool reflectionProbeEnabled = true;
    int reflectionProbeResolution = 256;
    bool reflectionIncludeDynamic = true;
    float reflectionIntensity = 1.0f;
    float reflectionRoughnessCutoff = 0.35f; // Solo materiales lisos (roughness bajo) muestran reflejos de entorno

    // Rendimiento y Sincronización en PC
    bool vSyncEnabled = false;        // V-Sync desactivado por defecto (framerate ilimitado)
    bool frustumCullingEnabled = true;
    bool occlusionCullingEnabled = false;

    // Calidad global de deformación / Skinned Mesh (0 = Desactivado, 1 = 1 Hueso, 2 = 2 Huesos, 4 = 4 Huesos)
    int skinningQuality = 4;
};

class SLIPENGINE_API SlipGraphicsSettings {
public:
    static SlipGraphicsSettings& Get();

    GraphicsSettingsData& GetData() { return m_Data; }
    const GraphicsSettingsData& GetData() const { return m_Data; }

    bool LoadFromFile(const std::string& filePath);
    bool SaveToFile(const std::string& filePath);

    void ResetToDefaults();

    bool HasPendingRebuild() const { return m_PendingRebuild; }
    void MarkRebuildHandled() { m_PendingRebuild = false; }
    void RequestRebuild() { m_PendingRebuild = true; }

private:
    SlipGraphicsSettings();
    GraphicsSettingsData m_Data;
    bool m_PendingRebuild = false;
};

#endif // !SLIP_GRAPHICS_SETTINGS_H

