#pragma once

#ifndef SLIP_POST_PROCESS_H
#define SLIP_POST_PROCESS_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/glm.hpp>

struct SLIPENGINE_API PostProcessSettings {
    bool bloom = false;
    float bloomIntensity = 0.0f;
    float bloomThreshold = 1.0f;
    float bloomSoftKnee = 0.5f;
    float bloomRadius = 0.85f;
    glm::vec3 bloomTint = glm::vec3(1.0f);

    bool lensBloom = false;
    float lensIntensity = 0.0f;
    float lensThreshold = 2.5f;
    float lensStreak = 0.55f;
    float lensHalo = 0.35f;

    bool ssao = false;
    float ssaoRadius = 0.45f;
    float ssaoBias = 0.025f;
    float ssaoIntensity = 1.0f;
    float ssaoPower = 1.4f;

    bool autoExposure = false;
    float exposureMin = 0.05f;
    float exposureMax = 8.0f;
    float exposureCompensation = 0.0f;
    float exposureSpeed = 2.0f;
    float exposureKey = 0.18f;

    float contrast = 1.0f;
    float saturation = 1.0f;
    float temperature = 0.0f;
    glm::vec3 lift = glm::vec3(0.0f);
    glm::vec3 gamma = glm::vec3(1.0f);
    glm::vec3 gain = glm::vec3(1.0f);
    glm::vec3 colorFilter = glm::vec3(1.0f);
    int tonemap = 0;

    float vignetteIntensity = 0.0f;
    float vignetteSmoothness = 0.45f;

    // Bordes negros cinematográficos (0.0 a 0.5)
    float letterbox = 0.0f;
    float pillarbox = 0.0f;

    // Niebla Volumétrica de Altura y Distancia (Volumetric Height Fog)
    bool fog = false;
    float fogDensity = 0.003f;
    float fogHeightFalloff = 0.035f;
    float fogHeight = 0.0f;
    float fogStartDistance = 5.0f;
    float fogMaxDistance = 12000.0f;
    glm::vec3 fogColor = glm::vec3(0.65f, 0.72f, 0.85f);
    float fogSunScattering = 0.65f;
    float fogSkyBlend = 0.90f;
    glm::vec3 fogSunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 fogSunColor = glm::vec3(1.0f, 0.95f, 0.85f);

    float blurRadius = 0.0f;
    float radialBlurStrength = 0.0f;
    glm::vec2 radialBlurCenter = glm::vec2(0.5f);

    bool fxAA = true;
    int fxAAQuality = 1;
};

class SlipShader;

class SLIPENGINE_API SlipPostProcess {
public:
    static SlipPostProcess& Get();

    void ApplySSAO(unsigned int sceneColor, unsigned int depthTex, unsigned int normalTex,
                   const glm::mat4& view, const glm::mat4& projection, const glm::mat4& invProjection,
                   const PostProcessSettings& settings,
                   unsigned int targetFbo, int width, int height);

    void Apply(unsigned int sceneColor, unsigned int depthTex, unsigned int normalTex,
               const glm::mat4& view, const glm::mat4& projection, const glm::mat4& invProjection,
               float deltaTime, const PostProcessSettings& settings,
               unsigned int presentFbo, int width, int height);

    void Shutdown();

private:
    struct Target {
        unsigned int tex = 0;
        unsigned int fbo = 0;
        int w = 0;
        int h = 0;
        void Destroy();
        void Alloc(int width, int height);
    };

    void Init();
    void Resize(int width, int height);
    void DrawTo(unsigned int fbo, int w, int h);
    void BindTex(int unit, unsigned int tex);

    Target m_Bloom[5];
    Target m_BloomUp[4];
    Target m_LensExtract;
    Target m_Lens;
    Target m_Ao;
    Target m_AoBlur;
    Target m_Base;
    Target m_FogTarget;
    Target m_Blur[2];
    Target m_Radial;
    Target m_Fxaa;
    Target m_Lum[8];
    int m_LumLevels = 0;
    Target m_Adapt[2];
    int m_AdaptIndex = 0;

    unsigned int m_VAO = 0;
    unsigned int m_VBO = 0;
    unsigned int m_Noise = 0;
    unsigned int m_Black = 0;

    SlipShader* m_Prefilter = nullptr;
    SlipShader* m_Down = nullptr;
    SlipShader* m_Up = nullptr;
    SlipShader* m_LensShader = nullptr;
    SlipShader* m_Ssao = nullptr;
    SlipShader* m_AoBlurShader = nullptr;
    SlipShader* m_ApplyAo = nullptr;
    SlipShader* m_Gauss = nullptr;
    SlipShader* m_RadialShader = nullptr;
    SlipShader* m_LogLum = nullptr;
    SlipShader* m_Reduce = nullptr;
    SlipShader* m_AdaptShader = nullptr;
    SlipShader* m_Composite = nullptr;
    SlipShader* m_FogShader = nullptr;
    SlipShader* m_FxaaShader = nullptr;

    int m_Width = 0;
    int m_Height = 0;
    bool m_Ready = false;
};

#endif
