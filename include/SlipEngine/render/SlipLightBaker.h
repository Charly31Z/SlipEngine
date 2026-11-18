#pragma once

#ifndef SLIP_LIGHT_BAKER_H
#define SLIP_LIGHT_BAKER_H

#include "SlipEngine/core/SlipExport.h"
#include "SlipEngine/render/SlipGL.h"
#include <glm/glm.hpp>
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

class SlipLevel;
class SlipShader;

struct SLIPENGINE_API LightBakeSettings {
    uint32_t resolution = 1024;   // Resolución del atlas de lightmaps (1024x1024 por defecto)
    int indirectBounces = 1;      // Número de rebotes indirectos de radiosidad (0 a 2)
    int samplesPerTexel = 32;     // Rayos de muestreo hemisférico por texel
    float dilationDistance = 4.0f;// Distancia de dilatación de márgenes para prevenir costuras negras
};

/**
 * @brief Motor de bakeo de iluminación estática acelerado por GPU (Compute Shaders).
 * Produce mapas de irradiancia directa e indirecta en HDR persistentes en un atlas global.
 */
class SLIPENGINE_API SlipLightBaker {
public:
    static SlipLightBaker& Get();

    bool BakeLevel(
        SlipLevel* level,
        const LightBakeSettings& settings,
        std::function<void(float progress, const std::string& status)> progressCb = nullptr
    );

    bool LoadBakedLightmap(SlipLevel* level);
    void ClearBakedLightmap(SlipLevel* level);

    bool IsBaking() const { return m_IsBaking; }
    float GetProgress() const { return m_Progress; }

private:
    SlipLightBaker() = default;
    ~SlipLightBaker() = default;

    bool m_IsBaking = false;
    float m_Progress = 0.0f;
};

#endif // SLIP_LIGHT_BAKER_H
