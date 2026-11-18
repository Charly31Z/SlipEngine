#include "SlipEngine/render/SlipCascadedShadows.h"
#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <algorithm>
#include <cmath>

SlipCascadedShadows& SlipCascadedShadows::Get() {
    static SlipCascadedShadows s_Instance;
    return s_Instance;
}

SlipCascadedShadows::SlipCascadedShadows() {
    const auto& settings = SlipGraphicsSettings::Get().GetData();
    m_Resolution = settings.shadowResolution;
    m_CascadeCount = settings.cascadeCount;
}

SlipCascadedShadows::~SlipCascadedShadows() {
    Clean();
}

void SlipCascadedShadows::Clean() {
    if (m_DepthTextureArray != 0) {
        glDeleteTextures(1, &m_DepthTextureArray);
        m_DepthTextureArray = 0;
    }
    if (m_FBO != 0) {
        glDeleteFramebuffers(1, &m_FBO);
        m_FBO = 0;
    }
    m_Initialized = false;
}

void SlipCascadedShadows::Init() {
    if (m_Initialized) return;

    const auto& settings = SlipGraphicsSettings::Get().GetData();
    m_Resolution = settings.shadowResolution;
    m_CascadeCount = std::clamp(settings.cascadeCount, 1, 4);

    InitFBO();
    m_Initialized = true;
}

void SlipCascadedShadows::RecreateResources(int resolution, int cascadeCount) {
    Clean();
    m_Resolution = resolution;
    m_CascadeCount = std::clamp(cascadeCount, 1, 4);
    InitFBO();
    m_Initialized = true;
}

void SlipCascadedShadows::InitFBO() {
    glGenFramebuffers(1, &m_FBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    glGenTextures(1, &m_DepthTextureArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, m_DepthTextureArray);

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT24,
                 m_Resolution, m_Resolution, m_CascadeCount, 0,
                 GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_DEPTH_COMPONENT32F,
                 m_Resolution, m_Resolution, m_CascadeCount, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, borderColor);
#endif

    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_DepthTextureArray, 0, 0);

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    GLenum none = GL_NONE;
    glDrawBuffers(1, &none);
#else
    glDrawBuffer(GL_NONE);
#endif
    glReadBuffer(GL_NONE);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[SlipCascadedShadows] Error: Framebuffer incompleto! Estado: 0x"
                  << std::hex << status << std::dec << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    m_LightSpaceMatrices.resize(m_CascadeCount, glm::mat4(1.0f));
    m_CascadeEndClipSpace.resize(m_CascadeCount, 0.0f);
    m_CascadeSplitDistances.resize(m_CascadeCount, 0.0f);
}

std::vector<glm::vec4> SlipCascadedShadows::GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view) {
    glm::mat4 inv = glm::inverse(proj * view);
    std::vector<glm::vec4> frustumCorners;
    frustumCorners.reserve(8);

    for (unsigned int x = 0; x < 2; ++x) {
        for (unsigned int y = 0; y < 2; ++y) {
            for (unsigned int z = 0; z < 2; ++z) {
                glm::vec4 pt = inv * glm::vec4(
                    2.0f * x - 1.0f,
                    2.0f * y - 1.0f,
                    2.0f * z - 1.0f,
                    1.0f);
                frustumCorners.push_back(pt / pt.w);
            }
        }
    }
    return frustumCorners;
}

void SlipCascadedShadows::UpdateCascades(const glm::mat4& cameraView, const glm::mat4& cameraProj,
                                         float cameraNear, float cameraFar,
                                         const glm::vec3& lightDir) {
    if (!m_Initialized) Init();

    const auto& settings = SlipGraphicsSettings::Get().GetData();
    if (m_Resolution != settings.shadowResolution || m_CascadeCount != settings.cascadeCount) {
        RecreateResources(settings.shadowResolution, settings.cascadeCount);
    }

    float maxDist = (std::min)(settings.shadowDistance, cameraFar);
    if (maxDist <= cameraNear + 1.0f) maxDist = cameraNear + 10.0f;

    m_CascadeSplitDistances.resize(m_CascadeCount);
    m_CascadeEndClipSpace.resize(m_CascadeCount);
    m_LightSpaceMatrices.resize(m_CascadeCount);

    float lambda = settings.cascadeLambda;
    float clipRange = maxDist - cameraNear;

    for (int i = 0; i < m_CascadeCount; ++i) {
        float p = (float)(i + 1) / (float)m_CascadeCount;
        float logDist = cameraNear * std::pow(maxDist / cameraNear, p);
        float uniformDist = cameraNear + clipRange * p;
        m_CascadeSplitDistances[i] = lambda * logDist + (1.0f - lambda) * uniformDist;
        m_CascadeEndClipSpace[i] = m_CascadeSplitDistances[i];
    }

    // Normalizar dirección de la luz
    glm::vec3 normLightDir = glm::normalize(lightDir);
    if (glm::length(normLightDir) < 0.001f) {
        normLightDir = glm::vec3(0.0f, -1.0f, 0.0f);
    }

    // Extraer relación de aspecto y FOV de la proyección de cámara
    // tan(fov/2) = 1.0 / proj[1][1]
    float tanHalfFov = 1.0f / cameraProj[1][1];
    float aspect = cameraProj[1][1] / cameraProj[0][0];

    for (int i = 0; i < m_CascadeCount; ++i) {
        float prevDist = (i == 0) ? cameraNear : m_CascadeSplitDistances[i - 1];
        float currDist = m_CascadeSplitDistances[i];

        // Construir proyección para esta cascada
        glm::mat4 subProj = glm::perspective(2.0f * std::atan(tanHalfFov), aspect, prevDist, currDist);
        std::vector<glm::vec4> corners = GetFrustumCornersWorldSpace(subProj, cameraView);

        // Centro del frustum
        glm::vec3 center(0.0f);
        for (const auto& v : corners) {
            center += glm::vec3(v);
        }
        center /= static_cast<float>(corners.size());

        // Radio de esfera envolvente para estabilidad de rotación
        float radius = 0.0f;
        for (const auto& v : corners) {
            float dist = glm::length(glm::vec3(v) - center);
            radius = (std::max)(radius, dist);
        }
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Construir matriz de vista de luz orientada hacia el centro
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        if (std::abs(glm::dot(up, normLightDir)) > 0.99f) {
            up = glm::vec3(0.0f, 0.0f, 1.0f);
        }

        // Colocar la cámara de luz suficientemente atrás para capturar casters
        float zMargin = (std::max)(radius * 1.5f, 60.0f);
        glm::vec3 eyePos = center - normLightDir * (radius + zMargin);
        glm::mat4 lightView = glm::lookAt(eyePos, center, up);

        // Proyección ortogonal centrada en el radio de la esfera
        float nearZ = 0.1f;
        float farZ = radius * 2.0f + zMargin * 2.0f;
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, nearZ, farZ);

        // Texel snapping para estabilización y eliminar shimmering
        float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(m_Resolution);
        glm::vec4 centerLight = lightView * glm::vec4(center, 1.0f);
        float offsetX = std::fmod(centerLight.x, worldUnitsPerTexel);
        float offsetY = std::fmod(centerLight.y, worldUnitsPerTexel);

        lightProj[3][0] -= offsetX / radius;
        lightProj[3][1] -= offsetY / radius;

        m_LightSpaceMatrices[i] = lightProj * lightView;
    }
}

void SlipCascadedShadows::BeginShadowPass() {
    glGetIntegerv(GL_VIEWPORT, m_PrevViewport);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &m_PrevFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_Resolution, m_Resolution);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
}

void SlipCascadedShadows::BindCascade(int cascadeIndex) {
    if (cascadeIndex < 0 || cascadeIndex >= m_CascadeCount) return;

    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, m_DepthTextureArray, 0, cascadeIndex);
    glClear(GL_DEPTH_BUFFER_BIT);
}

void SlipCascadedShadows::EndShadowPass() {
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glBindFramebuffer(GL_FRAMEBUFFER, m_PrevFBO);
    glViewport(m_PrevViewport[0], m_PrevViewport[1], m_PrevViewport[2], m_PrevViewport[3]);
}

