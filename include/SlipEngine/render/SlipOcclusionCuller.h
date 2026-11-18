#pragma once

#ifndef SLIP_OCCLUSION_CULLER_H
#define SLIP_OCCLUSION_CULLER_H

#include "SlipEngine.h"
#include <glm/glm.hpp>
#include <unordered_map>
#include <vector>
#include <cstdint>

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    #include <GLES3/gl3.h>
    #include <GLES3/gl32.h>
#else
    #include <glad/glad.h>
#endif

class SlipShader;

class SLIPENGINE_API SlipOcclusionCuller
{
public:
    static SlipOcclusionCuller& Get();

    void Init();
    void Shutdown();

    // Comprueba el resultado del frame anterior de manera no bloqueante.
    // Retorna true si es visible (o aún no hay resultado / primera vez o la cámara está dentro de la AABB).
    bool IsVisible(uint64_t objectId, const glm::vec3& eye, const glm::vec3& minPos, const glm::vec3& maxPos);
    bool IsVisible(uint64_t objectId);

    // Comienza el pase de queries de oclusión para el frame actual
    void BeginOcclusionPass(const glm::mat4& viewProjection);
    void RenderBoundingBoxQuery(uint64_t objectId, const glm::vec3& minPos, const glm::vec3& maxPos);
    void EndOcclusionPass();

    void EndFrame();
    void SetEnabled(bool enabled) { m_Enabled = enabled; }
    bool IsEnabled() const { return m_Enabled; }

private:
    SlipOcclusionCuller() = default;
    ~SlipOcclusionCuller() = default;

    struct QueryRecord {
        GLuint queryId = 0;
        bool lastResultVisible = true;
        bool queryActive = false;
        uint32_t lastFrameQueried = 0;
    };

    bool m_Enabled = true;
    bool m_Initialized = false;
    uint32_t m_FrameIndex = 0;

    std::unordered_map<uint64_t, QueryRecord> m_Queries;
    std::vector<GLuint> m_QueryPool;

    GLuint m_BoxVAO = 0;
    GLuint m_BoxVBO = 0;
    GLuint m_BoxEBO = 0;
    SlipShader* m_BoxShader = nullptr;
    glm::mat4 m_CurrentVP = glm::mat4(1.0f);
    GLint m_PrevDepthFunc = GL_LESS;

    GLuint AllocateQuery();
    void ReleaseQuery(GLuint q);
    void InitBoxMesh();
};

#endif // !SLIP_OCCLUSION_CULLER_H

