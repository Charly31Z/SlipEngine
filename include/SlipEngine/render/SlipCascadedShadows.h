#pragma once

#ifndef SLIP_CASCADED_SHADOWS_H
#define SLIP_CASCADED_SHADOWS_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/glm.hpp>
#include <vector>

class SLIPENGINE_API SlipCascadedShadows {
public:
    static SlipCascadedShadows& Get();

    SlipCascadedShadows();
    ~SlipCascadedShadows();

    void Init();
    void Clean();

    // Reconstruir recursos si cambiaron resolucion o numero de cascadas
    void RecreateResources(int resolution, int cascadeCount);

    // Actualiza matrices de cascadas para la camara dada y direccion de luz
    void UpdateCascades(const glm::mat4& cameraView, const glm::mat4& cameraProj,
                        float cameraNear, float cameraFar,
                        const glm::vec3& lightDir);

    // Ciclo de renderizado de profundidad CSM
    void BeginShadowPass();
    void BindCascade(int cascadeIndex);
    void EndShadowPass();

    // Getters
    unsigned int GetDepthTextureArray() const { return m_DepthTextureArray; }
    const std::vector<glm::mat4>& GetLightSpaceMatrices() const { return m_LightSpaceMatrices; }
    const std::vector<float>& GetCascadeEndClipSpace() const { return m_CascadeEndClipSpace; }
    int GetCascadeCount() const { return m_CascadeCount; }
    int GetResolution() const { return m_Resolution; }
    bool IsInitialized() const { return m_Initialized; }

private:
    void InitFBO();
    std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);

    unsigned int m_FBO = 0;
    unsigned int m_DepthTextureArray = 0;

    int m_Resolution = 2048;
    int m_CascadeCount = 4;
    bool m_Initialized = false;

    std::vector<glm::mat4> m_LightSpaceMatrices;
    std::vector<float> m_CascadeEndClipSpace;
    std::vector<float> m_CascadeSplitDistances;

    int m_PrevViewport[4] = { 0, 0, 0, 0 };
    int m_PrevFBO = 0;
};

#endif // !SLIP_CASCADED_SHADOWS_H

