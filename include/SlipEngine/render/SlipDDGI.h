#pragma once

#ifndef SLIP_DDGI_H
#define SLIP_DDGI_H

#include "SlipEngine/core/SlipExport.h"
#include <glm/glm.hpp>
#include <cstdint>

class SlipShader;
class SlipLevel;

struct SLIPENGINE_API DDGIDirectionalLight {
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
};

struct SLIPENGINE_API DDGIPointLight {
    glm::vec3 position{ 0.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;
};

struct SLIPENGINE_API DDGISpotLight {
    glm::vec3 position{ 0.0f };
    glm::vec3 direction{ 0.0f, -1.0f, 0.0f };
    glm::vec3 color{ 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float range = 10.0f;
    float spotAngle = 30.0f;
};

// Iluminación global difusa dinámica por sondas. En Android los métodos no hacen trabajo de GPU.
class SLIPENGINE_API SlipDDGI {
public:
    static SlipDDGI& Get();

    bool IsActive() const;

    void Update(const glm::vec3& cameraPos, const glm::mat4& view, SlipLevel* level,
                const DDGIDirectionalLight* dirLights, int dirCount,
                const DDGIPointLight* pointLights, int pointCount,
                const DDGISpotLight* spotLights, int spotCount,
                bool shadowsReady,
                bool pauseSceneRebuild = false);

    void Bind(SlipShader* shader);
    void DrawDebug(const glm::mat4& view, const glm::mat4& projection);
    void DrawVolumeProbes(const glm::mat4& view, const glm::mat4& projection,
                          const glm::vec3& center, const glm::vec3& size, const glm::ivec3& counts);

    ~SlipDDGI();

private:
    struct VolumeGPU {
        unsigned int irr[2] = { 0, 0 };
        unsigned int vis[2] = { 0, 0 };
        int read = 0;
        int cursor = 0;
        glm::ivec3 counts{ 16, 8, 16 };
        glm::ivec3 base{ 0 };
        glm::vec3 origin{ 0.0f };
        glm::vec3 spacing{ 2.0f };
        bool hasBase = false;
        bool follow = false;
        bool used = false;
        int atlasW = 0;
        int atlasH = 0;
        int probesPerRow = 32;
        int pendingCount = 0;
        int pending[4096]{};
    };

    SlipDDGI() = default;

    bool EnsureGpu();
    void DestroyGpu();
    void DestroyVolume(VolumeGPU& volume);
    void EnsureVolume(VolumeGPU& volume, const glm::ivec3& counts);
    void RebuildScene(SlipLevel* level);
    void PrepareAtlas(VolumeGPU& volume, const glm::ivec3& newBase, bool reset);
    void DispatchVolume(VolumeGPU& volume, const glm::mat4& view,
                        const DDGIDirectionalLight* dirLights, int dirCount,
                        const DDGIPointLight* pointLights, int pointCount,
                        const DDGISpotLight* spotLights, int spotCount,
                        bool shadowsReady, int probesPerFrame, int raysPerProbe, float hysteresis);

    bool m_Attempted = false;
    bool m_Supported = false;
    bool m_Ready = false;

    unsigned int m_Program = 0;
    unsigned int m_ScrollProgram = 0;
    unsigned int m_DebugProgram = 0;
    unsigned int m_NodeSSBO = 0;
    unsigned int m_TriSSBO = 0;
    unsigned int m_TaskSSBO = 0;
    unsigned int m_DebugVAO = 0;
    unsigned int m_DebugVBO = 0;
    VolumeGPU m_Volumes[5];

    int m_TriCount = 0;
    int m_NodeCount = 0;
    uint32_t m_SceneHash = 0;
    SlipLevel* m_LastLevel = nullptr;
    float m_AppliedCameraSpacing = -1.0f;
    glm::vec3 m_SkyColor{ 0.24f, 0.27f, 0.32f };
    glm::vec3 m_GroundColor{ 0.08f, 0.085f, 0.095f };
};

#endif
