#include "SlipEngine/editor/panels/ViewportPanel.h"
#include "SlipEngine/editor/SlipEditor.h"
#include "SlipEngine/scene/SlipLevel.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/scene/SlipTerrainComponent.h"
#include "SlipEngine/scene/SlipPrefab.h"
#include "SlipEngine/scene/SlipSortingLayers.h"
#include "SlipEngine/render/SlipPrimitive.h"
#include "SlipEngine/render/SlipFrameBuffer.h"
#include "SlipEngine/render/SlipGL.h"
#include "SlipEngine/render/rhi/SlipRHI.h"
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    #include "SlipEngine/render/vulkan/VulkanRHI.h"
    #include <imgui_impl_vulkan.h>
#endif
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipCascadedShadows.h"
#include "SlipEngine/render/SlipGraphicsSettings.h"
#include "SlipEngine/tools/SlipDebug.h"
#include "SlipEngine/render/SlipPostProcess.h"
#include "SlipEngine/render/SlipFontManager.h"
#include "SlipEngine/scene/SlipFrustum.h"
#include "SlipEngine/scene/SlipBVH.h"
#include "SlipEngine/render/SlipOcclusionCuller.h"
#include "SlipEngine/render/SlipRenderQueue.h"
#include "SlipEngine/render/SlipTextureArray.h"
#include "SlipEngine/render/SlipClusteredLighting.h"
#include "SlipEngine/render/SlipLightBaker.h"
#include "SlipEngine/platform/SlipInput.h"
#include "SlipEngine/platform/SlipInputMap.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/core/SlipGlobals.h"
#include "SlipEngine/editor/SlipModelExtract.h"
#include "SlipEngine/editor/SlipClusterBuilder.h"
#include "SlipEngine/tools/SlipDebug.h"
#include <unordered_set>
#include <SDL2/SDL.h>

#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/euler_angles.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace {

static inline void SlipDrawBuffer(GLenum buf = GL_COLOR_ATTACHMENT0) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    glDrawBuffers(1, &buf);
#else
    glDrawBuffer(buf);
#endif
}

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
static const char* s_VertexShaderSource =
    "#version 300 es\n"
    "precision mediump float;\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aNormal;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "out vec3 vNormal;\n"
    "out vec3 vFragPos;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "void main() {\n"
    "    vFragPos = vec3(u_Model * vec4(aPos, 1.0));\n"
    "    vNormal = normalize(mat3(u_Model) * aNormal);\n"
    "    gl_Position = u_Projection * u_View * vec4(vFragPos, 1.0);\n"
    "}\n";

static const char* s_FragmentShaderSource =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec3 vNormal;\n"
    "in vec3 vFragPos;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_Color;\n"
    "uniform vec3 u_LightPos;\n"
    "uniform vec3 u_ViewPos;\n"
    "void main() {\n"
    "    vec3 norm = normalize(vNormal);\n"
    "    vec3 lightDir = normalize(u_LightPos - vFragPos);\n"
    "    float diff = max(dot(norm, lightDir), 0.0);\n"
    "    vec3 ambient = 0.35 * u_Color.rgb;\n"
    "    vec3 diffuse = diff * u_Color.rgb;\n"
    "    FragColor = vec4(ambient + diffuse, u_Color.a);\n"
    "}\n";
#else
static const char* s_VertexShaderSource =
    "#version 330 core\n"
    "layout (location = 0) in vec3 aPos;\n"
    "layout (location = 1) in vec3 aNormal;\n"
    "layout (location = 2) in vec2 aTexCoords;\n"
    "out vec3 vNormal;\n"
    "out vec3 vFragPos;\n"
    "uniform mat4 u_Model;\n"
    "uniform mat4 u_View;\n"
    "uniform mat4 u_Projection;\n"
    "void main() {\n"
    "    vFragPos = vec3(u_Model * vec4(aPos, 1.0));\n"
    "    vNormal = mat3(transpose(inverse(u_Model))) * aNormal;\n"
    "    gl_Position = u_Projection * u_View * vec4(vFragPos, 1.0);\n"
    "}\n";

static const char* s_FragmentShaderSource =
    "#version 330 core\n"
    "in vec3 vNormal;\n"
    "in vec3 vFragPos;\n"
    "out vec4 FragColor;\n"
    "uniform vec4 u_Color;\n"
    "uniform vec3 u_LightPos;\n"
    "uniform vec3 u_ViewPos;\n"
    "void main() {\n"
    "    vec3 norm = normalize(vNormal);\n"
    "    vec3 lightDir = normalize(u_LightPos - vFragPos);\n"
    "    float diff = max(dot(norm, lightDir), 0.0);\n"
    "    vec3 ambient = 0.35 * u_Color.rgb;\n"
    "    vec3 diffuse = diff * u_Color.rgb;\n"
    "    FragColor = vec4(ambient + diffuse, u_Color.a);\n"
    "}\n";
#endif

static GLuint CompileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[ViewportPanel Shader Error]: " << log << std::endl;
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint CreateProgram(const char* vertSrc, const char* fragSrc) {
    GLuint vert = CompileShader(GL_VERTEX_SHADER, vertSrc);
    GLuint frag = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
    if (vert == 0 || frag == 0) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    GLint success = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &success);
    if (!success) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        std::cerr << "[ViewportPanel Program Link Error]: " << log << std::endl;
        glDeleteProgram(prog);
        prog = 0;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return prog;
}

static float DistancePointToSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    float l2 = (b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y);
    if (l2 < 0.0001f) return std::sqrt((p.x - a.x) * (p.x - a.x) + (p.y - a.y) * (p.y - a.y));
    float t = std::max(0.0f, std::min(1.0f, ((p.x - a.x) * (b.x - a.x) + (p.y - a.y) * (b.y - a.y)) / l2));
    ImVec2 proj = ImVec2(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
    return std::sqrt((p.x - proj.x) * (p.x - proj.x) + (p.y - proj.y) * (p.y - proj.y));
}

using ViewFrustum = SlipFrustum;

static bool PointInTriangle2D(ImVec2 p, ImVec2 a, ImVec2 b, ImVec2 c) {
    auto sign = [](ImVec2 p1, ImVec2 p2, ImVec2 p3) {
        return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y);
    };
    float d1 = sign(p, a, b);
    float d2 = sign(p, b, c);
    float d3 = sign(p, c, a);
    bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

static bool PointInQuad2D(ImVec2 p, ImVec2 q0, ImVec2 q1, ImVec2 q2, ImVec2 q3) {
    if (PointInTriangle2D(p, q0, q1, q2) || PointInTriangle2D(p, q0, q2, q3)) return true;
    ImVec2 center((q0.x + q1.x + q2.x + q3.x) * 0.25f, (q0.y + q1.y + q2.y + q3.y) * 0.25f);
    float dx = p.x - center.x;
    float dy = p.y - center.y;
    return (dx * dx + dy * dy) <= 100.0f; // Radio de captura táctil/ratón de 10px
}

} // namespace

ViewportPanel::ViewportPanel(SelectionCallback onSelect)
    : m_OnSelectCallback(onSelect) {}

ViewportPanel::~ViewportPanel() {
    if (m_GizmoMouseCaptured) {
        EndGizmoMouseCapture();
    }
    CleanUpRenderer();
}

void ViewportPanel::InitRenderer() {
    if (m_RendererInitialized) return;

    m_ShaderProgram = CreateProgram(s_VertexShaderSource, s_FragmentShaderSource);
    if (m_ShaderProgram == 0) {
        std::cerr << "[ViewportPanel] Fallo al compilar los shaders del Viewport." << std::endl;
        return;
    }

    // Grilla 3D de referencia en el suelo (plano XZ)
    std::vector<glm::vec3> gridVertices;
    const int gridHalfSize = 10;
    const float step = 1.0f;

    for (int i = -gridHalfSize; i <= gridHalfSize; ++i) {
        // Línea paralela a Z
        gridVertices.push_back(glm::vec3((float)i * step, 0.0f, (float)-gridHalfSize * step));
        gridVertices.push_back(glm::vec3((float)i * step, 0.0f, (float)gridHalfSize * step));

        // Línea paralela a X
        gridVertices.push_back(glm::vec3((float)-gridHalfSize * step, 0.0f, (float)i * step));
        gridVertices.push_back(glm::vec3((float)gridHalfSize * step, 0.0f, (float)i * step));
    }

    m_GridVertexCount = (uint32_t)gridVertices.size();

    glGenVertexArrays(1, &m_GridVAO);
    glGenBuffers(1, &m_GridVBO);

    glBindVertexArray(m_GridVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_GridVBO);
    glBufferData(GL_ARRAY_BUFFER, gridVertices.size() * sizeof(glm::vec3), gridVertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    // Quad unitario centrado [-0.5, 0.5] para Sprites 2D y Sistema UI (pos3 + norm3 + uv2)
    float quadVertices[] = {
        // Pos                // Normal          // UV
        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
         0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  1.0f, 0.0f,
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  1.0f, 1.0f,

        -0.5f, -0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  0.0f, 0.0f,
         0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, 0.0f,   0.0f, 0.0f, 1.0f,  0.0f, 1.0f
    };

    glGenVertexArrays(1, &m_QuadVAO);
    glGenBuffers(1, &m_QuadVBO);
    glBindVertexArray(m_QuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_QuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    m_RendererInitialized = true;
}

void ViewportPanel::ResizeFBO(int width, int height) {
    if (width <= 0 || height <= 0) return;
    if (m_FBO != 0 && m_FboWidth == width && m_FboHeight == height) return;

    m_FboWidth = width;
    m_FboHeight = height;

    if (m_FBO == 0) {
        glGenFramebuffers(1, &m_FBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    if (m_ColorTexture == 0) {
        glGenTextures(1, &m_ColorTexture);
    }
    glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);
    }
#endif

    if (m_DepthTexture == 0) glGenTextures(1, &m_DepthTexture);
    glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#if defined(GL_DEPTH_STENCIL_TEXTURE_MODE)
    glTexParameteri(GL_TEXTURE_2D, GL_DEPTH_STENCIL_TEXTURE_MODE, GL_DEPTH_COMPONENT);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_DepthTexture, 0);

    if (m_NormalTexture == 0) glGenTextures(1, &m_NormalTexture);
    glBindTexture(GL_TEXTURE_2D, m_NormalTexture);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_NormalTexture, 0);

    if (m_ColorCopy == 0) glGenTextures(1, &m_ColorCopy);
    glBindTexture(GL_TEXTURE_2D, m_ColorCopy);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLenum drawBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    glDrawBuffers(2, drawBuffers);

    if (m_PresentTexture == 0) glGenTextures(1, &m_PresentTexture);
    glBindTexture(GL_TEXTURE_2D, m_PresentTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (m_PresentDepthTexture == 0) glGenTextures(1, &m_PresentDepthTexture);
    glBindTexture(GL_TEXTURE_2D, m_PresentDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, width, height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (m_PresentFBO == 0) glGenFramebuffers(1, &m_PresentFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_PresentFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_PresentTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_PresentDepthTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ViewportPanel Error] Framebuffer de presentación incompleto!" << std::endl;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "[ViewportPanel Error] Framebuffer incompleto!" << std::endl;
    }

    // Framebuffer de máscara de silueta para Outliner en Screen-Space
    if (m_OutlineTexture == 0) glGenTextures(1, &m_OutlineTexture);
    glBindTexture(GL_TEXTURE_2D, m_OutlineTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (m_OutlineFBO == 0) glGenFramebuffers(1, &m_OutlineFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_OutlineFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_OutlineTexture, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void ViewportPanel::RenderScene() {
    if (!m_RendererInitialized || m_FBO == 0 || m_FboWidth <= 0 || m_FboHeight <= 0) {
        return;
    }

    GLint last_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &last_fbo);
    GLint last_viewport[4];
    glGetIntegerv(GL_VIEWPORT, last_viewport);

    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_FboWidth, m_FboHeight);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDepthMask(GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    CameraComponent* activeGameCam = nullptr;
    if (m_ActiveCameraType == ViewportCameraType::GameCamera && m_ContextLevel) {
        activeGameCam = m_ContextLevel->GetMainCamera();
    }

    if (activeGameCam) {
        glClearColor(activeGameCam->clearColor.r, activeGameCam->clearColor.g, activeGameCam->clearColor.b, activeGameCam->clearColor.a);
    } else {
        glm::vec4 defaultClear(0.12f, 0.12f, 0.14f, 1.0f);
        if (m_ContextLevel) {
            if (auto* skyComp = SlipSkyComponent::FindActive(m_ContextLevel)) {
                if (skyComp->params.dynamicAtmosphere) {
                    skyComp->UpdateAtmosphereFromSun();
                    defaultClear = glm::vec4(skyComp->currentAtmosphere.horizonColor, 1.0f);
                }
            }
        }
        glClearColor(defaultClear.r, defaultClear.g, defaultClear.b, defaultClear.a);
    }
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    {
        GLfloat clearLinear[4];
        glGetFloatv(GL_COLOR_CLEAR_VALUE, clearLinear);
        clearLinear[0] = std::pow(clearLinear[0], 2.2f);
        clearLinear[1] = std::pow(clearLinear[1], 2.2f);
        clearLinear[2] = std::pow(clearLinear[2], 2.2f);
        glClearColor(clearLinear[0], clearLinear[1], clearLinear[2], clearLinear[3]);
    }
#endif
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    {
        float geoClear[4] = { 0.5f, 0.5f, 1.0f, 0.0f };
        glClearBufferfv(GL_COLOR, 1, geoClear);
    }
#endif
    SlipDrawBuffer(GL_COLOR_ATTACHMENT0);

    glUseProgram(m_ShaderProgram);

    float aspect = (m_FboHeight > 0) ? ((float)m_FboWidth / (float)m_FboHeight) : 1.0f;
    glm::mat4 projection;
    glm::mat4 view;
    glm::vec3 eye;
    float camNear = 0.1f;
    float camFar = 1000.0f;

    if (activeGameCam) {
        if (SlipEditor::GetInstance() && !SlipEditor::GetInstance()->IsPlaying()) {
            activeGameCam->OnUpdate(ImGui::GetIO().DeltaTime);
}
        view = activeGameCam->GetViewMatrix();
        projection = activeGameCam->GetProjectionMatrix(aspect);
        eye = activeGameCam->GetPosition();
        camNear = activeGameCam->nearClip;
        camFar = activeGameCam->farClip;
    } else if (m_Is2DMode) {
        // Modo 2D Ortográfico mirando hacia -Z
        float orthoHalfH = (std::max)(0.5f, m_CameraDistance * 0.65f);
        float orthoHalfW = orthoHalfH * aspect;
        eye = glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 15.0f);
        view = glm::lookAt(eye, glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        projection = glm::ortho(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, -100.0f, 100.0f);
    } else {
        // Cámara del editor: Fly Cam en PC (primera persona / libre) y Orbital en Android
        float radPitch = glm::radians(m_CameraPitch);
        float radYaw = glm::radians(m_CameraYaw);
        glm::vec3 fwd(
            -cos(radPitch) * sin(radYaw),
            -sin(radPitch),
            -cos(radPitch) * cos(radYaw)
        );
        fwd = glm::normalize(fwd);

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
        if (!m_CameraPosInitialized) {
            m_CameraPos = m_CameraTarget - fwd * m_CameraDistance;
            m_CameraPosInitialized = true;
}
        eye = m_CameraPos;
        m_CameraTarget = m_CameraPos + fwd * m_CameraDistance;
        view = glm::lookAt(eye, eye + fwd, glm::vec3(0.0f, 1.0f, 0.0f));
#else
        eye.x = m_CameraTarget.x + m_CameraDistance * cos(radPitch) * sin(radYaw);
        eye.y = m_CameraTarget.y + m_CameraDistance * sin(radPitch);
        eye.z = m_CameraTarget.z + m_CameraDistance * cos(radPitch) * cos(radYaw);
        m_CameraPos = eye;
        view = glm::lookAt(eye, m_CameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
#endif
        camNear = 0.1f;
        camFar = 1000.0f;
        projection = glm::perspective(glm::radians(45.0f), aspect, camNear, camFar);
    }

    SlipFrustum frustum(projection * view);
    const auto& gfxSettings = SlipGraphicsSettings::Get().GetData();

    GLint uModelLoc = glGetUniformLocation(m_ShaderProgram, "u_Model");
    GLint uViewLoc = glGetUniformLocation(m_ShaderProgram, "u_View");
    GLint uProjLoc = glGetUniformLocation(m_ShaderProgram, "u_Projection");
    GLint uColorLoc = glGetUniformLocation(m_ShaderProgram, "u_Color");
    GLint uLightLoc = glGetUniformLocation(m_ShaderProgram, "u_LightPos");
    GLint uViewLocPos = glGetUniformLocation(m_ShaderProgram, "u_ViewPos");

    if (uViewLoc >= 0) glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
    if (uProjLoc >= 0) glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
    if (uLightLoc >= 0) glUniform3f(uLightLoc, 5.0f, 8.0f, 6.0f);
    if (uViewLocPos >= 0) glUniform3f(uViewLocPos, eye.x, eye.y, eye.z);

    // 2. Recolectar luces activas de la escena (LightComponent)
    // Simular sistemas de partículas en modo Editor (en Play Mode ya los actualiza SlipEditor::BeginFrame)
    if (m_ContextLevel && !(SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession())) {
        float edDt = ImGui::GetIO().DeltaTime;
        if (edDt <= 0.0f || edDt > 0.1f) edDt = 1.0f / 60.0f;
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            if (auto ps = ent->GetComponent<ParticleSystemComponent>()) {
                if (ps->IsEnabled() && ps->IsPlaying()) {
                    ps->Simulate(edDt);
        }
    }
}
    }

    struct VpDirLight { glm::vec3 direction; glm::vec3 color; float intensity; bool castShadows; };
    struct VpPointLight { glm::vec3 position; glm::vec3 color; float intensity; float range; bool castShadows; };
    struct VpSpotLight { glm::vec3 position; glm::vec3 direction; glm::vec3 color; float intensity; float range; float spotAngle; bool castShadows; };

    std::vector<VpDirLight> dirLights;
    std::vector<VpPointLight> pointLights;
    std::vector<VpSpotLight> spotLights;
    std::vector<GpuLight> gpuLights;

    if (m_ContextLevel) {
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            auto lightComp = ent->GetComponent<LightComponent>();
            if (!lightComp || !lightComp->IsEnabled()) continue;

            // Las luces marcadas como estáticas se omiten del pipeline dinámico (ya están horneadas)
            if (lightComp->mobility == LightMobility::Static) continue;

            glm::vec3 pos = ent->GetTransform() ? ent->GetTransform()->position : ent->position;
            glm::vec3 rot = ent->GetTransform() ? ent->GetTransform()->rotation : ent->rotation;
            if (ent->GetParent()) {
                glm::mat4 wm = ent->getMatrix();
                pos = glm::vec3(wm[3]);
            }
            glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0)) *
                               glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0)) *
                               glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
            glm::vec3 dir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));

            if (lightComp->type == LightType::Directional) {
                if (dirLights.size() < 4) {
                    dirLights.push_back({ dir, lightComp->color, lightComp->intensity, lightComp->castShadows });
                }
            } else if (lightComp->type == LightType::Point) {
                GpuLight gl;
                gl.position_range = glm::vec4(pos, lightComp->range);
                gl.color_intensity = glm::vec4(lightComp->color, lightComp->intensity);
                gl.direction_spotAngle = glm::vec4(dir, 0.0f);
                gl.params = glm::uvec4(0u, lightComp->castShadows ? 1u : 0u, 0u, 0u);
                gpuLights.push_back(gl);

                if (pointLights.size() < 32) {
                    pointLights.push_back({ pos, lightComp->color, lightComp->intensity, lightComp->range, lightComp->castShadows });
                }
            } else if (lightComp->type == LightType::Spot) {
                GpuLight gl;
                gl.position_range = glm::vec4(pos, lightComp->range);
                gl.color_intensity = glm::vec4(lightComp->color, lightComp->intensity);
                gl.direction_spotAngle = glm::vec4(dir, lightComp->spotAngle);
                gl.params = glm::uvec4(1u, lightComp->castShadows ? 1u : 0u, 0u, 0u);
                gpuLights.push_back(gl);

                if (spotLights.size() < 4) {
                    spotLights.push_back({ pos, dir, lightComp->color, lightComp->intensity, lightComp->range, lightComp->spotAngle, lightComp->castShadows });
                }
            }
        }

        // Emisión de luz real hacia la escena por Sistemas de Partículas (Particle Light Emission)
        struct EmitterLightInfo {
            ParticleSystemComponent* ps;
            std::vector<VpPointLight> lights;
            float distSq;
};
        std::vector<EmitterLightInfo> emitterLights;

        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            auto ps = ent->GetComponent<ParticleSystemComponent>();
            if (!ps || !ps->IsEnabled() || !ps->lightEmissionEnabled) continue;
            if (ps->GetParticles().empty()) continue;

            glm::mat4 emitterWorld = ent->getMatrix();
            glm::vec3 psMin, psMax;
            ps->GetWorldBounds(emitterWorld, psMin, psMax);

            // Frustum Culling para emisión de luz: si la luz ni siquiera alcanza el cono de visión, omitir
            if (gfxSettings.frustumCullingEnabled) {
                glm::vec3 margin(ps->lightRange);
                if (!frustum.IntersectsAABB(psMin - margin, psMax + margin)) {
                    continue;
        }
    }

            int count = (std::max)(1, (std::min)(ps->maxLights, 4));
            const auto& parts = ps->GetParticles();
            std::vector<VpPointLight> eLights;

            if (count == 1) {
                glm::vec3 centroid(0.0f);
                glm::vec3 avgCol(0.0f);
                float totalW = 0.0f;
                size_t step = (parts.size() > 50) ? (parts.size() / 25) : 1;
                for (size_t pIdx = 0; pIdx < parts.size(); pIdx += step) {
                    const auto& p = parts[pIdx];
                    float w = p.color.a;
                    if (w <= 0.01f) continue;
                    glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
                    centroid += wPos * w;
                    avgCol += glm::vec3(p.color) * w;
                    totalW += w;
        }
                if (totalW > 0.001f) {
                    centroid /= totalW;
                    avgCol /= totalW;
        } else {
                    centroid = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(parts[0].position, 1.0f)) : parts[0].position;
                    avgCol = glm::vec3(parts[0].color);
        }
                glm::vec3 finalCol = ps->useParticleColorForLight ? avgCol : ps->lightColor;
                eLights.push_back({ centroid, finalCol, ps->lightIntensity, ps->lightRange, false });
    } else {
                size_t step = (parts.size() + count - 1) / count;
                for (int l = 0; l < count && (size_t)l * step < parts.size(); ++l) {
                    const auto& p = parts[l * step];
                    glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
                    glm::vec3 finalCol = ps->useParticleColorForLight ? glm::vec3(p.color) : ps->lightColor;
                    eLights.push_back({ wPos, finalCol, ps->lightIntensity, ps->lightRange, false });
        }
    }

            if (!eLights.empty()) {
                glm::vec3 center = 0.5f * (psMin + psMax);
                float distSq = glm::dot(center - eye, center - eye);
                emitterLights.push_back({ ps.get(), std::move(eLights), distSq });
    }
}

        const size_t maxTotalPointLights = 32;

        // Asignación equitativa garantizada: Cada emisor recibe al menos su luz principal
        for (auto& el : emitterLights) {
            for (const auto& pl : el.lights) {
                GpuLight gl;
                gl.position_range = glm::vec4(pl.position, pl.range);
                gl.color_intensity = glm::vec4(pl.color, pl.intensity);
                gl.direction_spotAngle = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
                gl.params = glm::uvec4(0u, 0u, 0u, 0u);
                gpuLights.push_back(gl);
            }
            if (pointLights.size() < maxTotalPointLights && !el.lights.empty()) {
                pointLights.push_back(el.lights[0]);
            }
        }

        // Asignación de luces secundarias/adicionales (hasta 4 por emisor) si hay slots disponibles
        for (size_t lightIdx = 1; lightIdx < 4; ++lightIdx) {
            for (auto& el : emitterLights) {
                if (pointLights.size() >= maxTotalPointLights) break;
                if (lightIdx < el.lights.size()) {
                    pointLights.push_back(el.lights[lightIdx]);
        }
    }
            if (pointLights.size() >= maxTotalPointLights) break;
}
    }

    // Luz por defecto si no hay ninguna luz creada en la escena
    if (dirLights.empty() && pointLights.empty() && spotLights.empty()) {
        dirLights.push_back({ glm::normalize(glm::vec3(-0.4f, -0.9f, -0.4f)), glm::vec3(1.0f, 0.98f, 0.95f), 1.0f, true });
    }

    // Determinar si la luz direccional principal tiene habilitadas las sombras
    bool canCastShadows = false;
    glm::vec3 mainLightDir(0.0f, -1.0f, 0.0f);
    for (const auto& dl : dirLights) {
        if (dl.castShadows) {
            mainLightDir = dl.direction;
            canCastShadows = true;
            break;
}
    }

    // Sincronización anticipada de SlipSkyComponent antes de los pases de renderizado
    SlipSkyComponent* activeSky = m_ContextLevel ? SlipSkyComponent::FindActive(m_ContextLevel) : nullptr;
    if (activeSky && activeSky->IsEnabled()) {
        if (activeSky->params.followDirectionalLight && glm::length(mainLightDir) > 0.0001f) {
            activeSky->params.sunDirection = -glm::normalize(mainLightDir);
        }
        if (activeSky->params.dynamicAtmosphere) {
            activeSky->UpdateAtmosphereFromSun();
        }
    }

    static float s_CloudTime = 0.0f;
    s_CloudTime += 0.016f;
    float currentCloudTime = (activeSky && activeSky->GetCurrentTime() > 0.0f) ? activeSky->GetCurrentTime() : s_CloudTime;

    // 2.5 Pase de Sombras en Cascada (CSM) si las sombras están habilitadas
    if (gfxSettings.shadowsEnabled && canCastShadows) {
        float nearClip = activeGameCam ? activeGameCam->nearClip : 0.1f;
        float farClip = activeGameCam ? activeGameCam->farClip : 100.0f;

        SlipCascadedShadows::Get().UpdateCascades(view, projection, nearClip, farClip, mainLightDir);
        SlipCascadedShadows::Get().BeginShadowPass();

        SlipShader* depthShader = SlipShaderLibrary::Get("ShadowDepth");
        if (depthShader) {
            depthShader->use();
            int numCascades = SlipCascadedShadows::Get().GetCascadeCount();
            const auto& lightMats = SlipCascadedShadows::Get().GetLightSpaceMatrices();

            for (int c = 0; c < numCascades; ++c) {
                SlipCascadedShadows::Get().BindCascade(c);
                depthShader->setMat4("u_LightSpaceMatrix", lightMats[c]);

                if (m_ContextLevel) {
                    for (SlipEntity* entity : m_ContextLevel->GetEntities()) {
                        if (!entity) continue;
                        glm::mat4 modelMat = entity->getMatrix();
                        depthShader->setMat4("u_Model", modelMat);

                        auto meshComp = entity->GetComponent<MeshComponent>();
                        auto skinnedComp = entity->GetComponent<SkinnedMeshComponent>();
                        auto terrainComp = entity->GetComponent<SlipTerrainComponent>();
                        SlipMesh* shadowModel = nullptr;
                        if (meshComp && meshComp->IsEnabled() && meshComp->model) {
                            shadowModel = meshComp->model;
                            depthShader->setBool("u_HasBones", false);
                } else if (skinnedComp && skinnedComp->IsEnabled() && skinnedComp->model) {
                            shadowModel = skinnedComp->model;
                            skinnedComp->UpdateBoneMatrices();
                            depthShader->setBool("u_HasBones", true);
                            depthShader->setInt("u_SkinningQuality", gfxSettings.skinningQuality);
                            depthShader->setMat4Array("u_BoneTransforms", skinnedComp->boneTransforms.data(), static_cast<int>(skinnedComp->boneTransforms.size()));
                } else if (terrainComp && terrainComp->IsEnabled() && terrainComp->castShadows && terrainComp->GetMesh()) {
                            shadowModel = terrainComp->GetMesh();
                            depthShader->setBool("u_HasBones", false);
                }

                        if (shadowModel) {
                            if (m_StressTestEntities.size() > 50) {
                                float distToEye = glm::length(glm::vec3(modelMat[3]) - eye);
                                if (distToEye > 45.0f) continue;
                            }
                            bool isUnified = shadowModel->isUnifiedBuffer;
                            for (const auto& sm : shadowModel->meshes) {
                                if (sm.VAO == 0) continue;
                                uint32_t count = isUnified ? sm.indexCount : static_cast<uint32_t>(sm.indices.size());
                                if (count == 0) continue;

                                size_t slotIdx = (sm.materialIndex >= 0) ? static_cast<size_t>(sm.materialIndex) : 0;
                                SlipMaterial* subMat = meshComp ? meshComp->GetSubmeshMaterial(slotIdx) : (skinnedComp ? skinnedComp->GetSubmeshMaterial(slotIdx) : nullptr);
                                bool useCutout = (subMat != nullptr && (subMat->alphaClip || subMat->opacityTexture != nullptr));
                                bool doubleSided = (subMat != nullptr && subMat->doubleSided);
                                if (doubleSided) {
                                    glDisable(GL_CULL_FACE);
                        }
                                if (useCutout) {
                                    if (subMat->opacityTexture != nullptr) {
                                        depthShader->setBool("u_HasAlphaMap", true);
                                        depthShader->setInt("u_AlphaMap", 0);
                                        depthShader->setBool("u_AlphaFromRed", true);
                                        depthShader->setFloat("u_AlphaCutoff", subMat->alphaCutoff);
                                        subMat->opacityTexture->bind(0);
                            } else if (subMat->diffuseTexture != nullptr) {
                                        depthShader->setBool("u_HasAlphaMap", true);
                                        depthShader->setInt("u_AlphaMap", 0);
                                        depthShader->setBool("u_AlphaFromRed", false);
                                        depthShader->setFloat("u_AlphaCutoff", subMat->alphaCutoff);
                                        subMat->diffuseTexture->bind(0);
                            } else {
                                        depthShader->setBool("u_HasAlphaMap", false);
                            }
                        } else {
                                    depthShader->setBool("u_HasAlphaMap", false);
                        }

                                glBindVertexArray(sm.VAO);
                                void* offset = isUnified ? (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int)) : 0;
                                glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                                glBindVertexArray(0);

                                if (doubleSided) {
                                    glEnable(GL_CULL_FACE);
                        }
                    }
                            depthShader->setBool("u_HasAlphaMap", false);
                } else if (auto prim = dynamic_cast<SlipPrimitiveEntity*>(entity)) {
                            depthShader->setBool("u_HasAlphaMap", false);
                            if (prim->GetVAO() != 0) {
                                glBindVertexArray(prim->GetVAO());
                                glDrawElements(GL_TRIANGLES, prim->GetIndexCount(), GL_UNSIGNED_INT, 0);
                                glBindVertexArray(0);
                    }
                }
            }

                    // Renderizar partículas que proyectan sombras (castShadows = true)
                    SlipShader* psShadowShader = SlipShaderLibrary::Get("ParticleShadowDepth");
                    if (psShadowShader) {
                        struct ShadowVert {
                            glm::vec3 pos;
                            glm::vec2 uv;
                            float alpha;
                            float pointSize;
                };
                        std::vector<ShadowVert> shadowVerts;
                        glm::vec3 lightFwd = glm::normalize(mainLightDir);
                        glm::vec3 lightUpRef = (std::abs(lightFwd.y) > 0.9f) ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                        glm::vec3 lightRight = glm::normalize(glm::cross(lightUpRef, lightFwd));
                        glm::vec3 lightUp = glm::normalize(glm::cross(lightFwd, lightRight));

                        psShadowShader->use();
                        psShadowShader->setMat4("u_LightSpaceMatrix", lightMats[c]);
                        if (m_ParticleVAO == 0) {
                            glGenVertexArrays(1, &m_ParticleVAO);
                            glGenBuffers(1, &m_ParticleVBO);
                }

                        for (SlipEntity* entity : m_ContextLevel->GetEntities()) {
                            if (!entity) continue;
                            auto ps = entity->GetComponent<ParticleSystemComponent>();
                            if (!ps || !ps->IsEnabled() || !ps->castShadows || ps->GetParticles().empty()) continue;

                            glm::mat4 emitterWorld = entity->getMatrix();
                            shadowVerts.clear();

                            bool isPointMode = (ps->renderMode == ParticleRenderMode::PointNiagara);
                            psShadowShader->setBool("u_IsPointMode", isPointMode);
                            psShadowShader->setFloat("u_AlphaCutoff", ps->shadowAlphaCutoff);

                            bool hasTex = (!isPointMode && ps->texture != nullptr && ps->texture->getID() != 0);
                            psShadowShader->setBool("u_HasTexture", hasTex);
                            if (hasTex) {
                                psShadowShader->setInt("u_MainTexture", 0);
                                ps->texture->draw();
                    }

                            if (isPointMode) {
                                shadowVerts.reserve(ps->GetParticles().size());
                                for (const auto& p : ps->GetParticles()) {
                                    if (p.color.a < ps->shadowAlphaCutoff) continue;
                                    glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
                                    glm::vec3 shadowCenter = wPos + lightFwd * (p.size * 0.65f);
                                    shadowVerts.push_back({ shadowCenter, glm::vec2(0.5f), p.color.a, p.size * ps->pointBaseScale });
                        }
                                if (!shadowVerts.empty()) {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                                    glEnable(GL_PROGRAM_POINT_SIZE);
#endif
                                    glBindVertexArray(m_ParticleVAO);
                                    glBindBuffer(GL_ARRAY_BUFFER, m_ParticleVBO);
                                    glBufferData(GL_ARRAY_BUFFER, shadowVerts.size() * sizeof(ShadowVert), shadowVerts.data(), GL_STREAM_DRAW);
                                    glEnableVertexAttribArray(0);
                                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)0);
                                    glEnableVertexAttribArray(1);
                                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)(sizeof(glm::vec3)));
                                    glEnableVertexAttribArray(2);
                                    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)(sizeof(glm::vec3) + sizeof(glm::vec2)));
                                    glEnableVertexAttribArray(3);
                                    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)(sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(float)));
                                    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(shadowVerts.size()));
                                    glBindVertexArray(0);
                        }
                    } else {
                                shadowVerts.reserve(ps->GetParticles().size() * 6);
                                for (const auto& p : ps->GetParticles()) {
                                    if (p.color.a < ps->shadowAlphaCutoff) continue;
                                    glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
                                    glm::vec3 shadowCenter = wPos + lightFwd * (p.size * 0.65f);
                                    float halfSz = p.size * 0.5f;
                                    glm::vec3 rVec = lightRight * halfSz;
                                    glm::vec3 uVec = lightUp * halfSz;
                                    float normAge = (p.lifetime > 0.0001f) ? (p.age / p.lifetime) : 0.0f;
                                    glm::vec4 uvR = ps->GetUVRectForNormalizedAge(normAge, p.randomSeed);
                                    glm::vec2 uv00(uvR.x, uvR.y);
                                    glm::vec2 uv10(uvR.x + uvR.z, uvR.y);
                                    glm::vec2 uv11(uvR.x + uvR.z, uvR.y + uvR.w);
                                    glm::vec2 uv01(uvR.x, uvR.y + uvR.w);

                                    glm::vec3 p0 = shadowCenter - rVec - uVec;
                                    glm::vec3 p1 = shadowCenter + rVec - uVec;
                                    glm::vec3 p2 = shadowCenter + rVec + uVec;
                                    glm::vec3 p3 = shadowCenter - rVec + uVec;

                                    shadowVerts.push_back({ p0, uv00, p.color.a, 1.0f });
                                    shadowVerts.push_back({ p1, uv10, p.color.a, 1.0f });
                                    shadowVerts.push_back({ p2, uv11, p.color.a, 1.0f });
                                    shadowVerts.push_back({ p0, uv00, p.color.a, 1.0f });
                                    shadowVerts.push_back({ p2, uv11, p.color.a, 1.0f });
                                    shadowVerts.push_back({ p3, uv01, p.color.a, 1.0f });
                        }
                                if (!shadowVerts.empty()) {
                                    glBindVertexArray(m_ParticleVAO);
                                    glBindBuffer(GL_ARRAY_BUFFER, m_ParticleVBO);
                                    glBufferData(GL_ARRAY_BUFFER, shadowVerts.size() * sizeof(ShadowVert), shadowVerts.data(), GL_STREAM_DRAW);
                                    glEnableVertexAttribArray(0);
                                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)0);
                                    glEnableVertexAttribArray(1);
                                    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)(sizeof(glm::vec3)));
                                    glEnableVertexAttribArray(2);
                                    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)(sizeof(glm::vec3) + sizeof(glm::vec2)));
                                    glEnableVertexAttribArray(3);
                                    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(ShadowVert), (void*)(sizeof(glm::vec3) + sizeof(glm::vec2) + sizeof(float)));
                                    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(shadowVerts.size()));
                                    glBindVertexArray(0);
                        }
                    }
                }
                        depthShader->use();
                        depthShader->setMat4("u_LightSpaceMatrix", lightMats[c]);
            }
        }
    }
}
        SlipCascadedShadows::Get().EndShadowPass();

        // Restaurar estado para renderizado de la escena en el FBO del Viewport
        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glViewport(0, 0, m_FboWidth, m_FboHeight);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
    }

    // 2.6 Bake de Cubemap de Reflejos bajo demanda o al cargar escena
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (gfxSettings.reflectionProbeEnabled) {
        bool hasEntities = (m_ContextLevel && !m_ContextLevel->GetEntities().empty());
        if (m_ReflectionBakeNeeded) {
            m_ReflectionBakeNeeded = false;
            if (hasEntities) {
                BakeReflectionCubemap(eye, gfxSettings.reflectionIncludeDynamic);
                BakeAllReflectionProbes();
            }
        }
    }
#endif

    // 3. Dibujar entidades y primitivos con shaders Lit / Unlit (únicamente caras exteriores)
    std::vector<SlipVisibleCluster> selectedEntityVisRuns;
    unsigned int selectedEntityVClustVAO = 0;
    if (m_ContextLevel) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

        if (m_SelectedEntity) {
            glEnable(GL_STENCIL_TEST);
            glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
} else {
            glDisable(GL_STENCIL_TEST);
}

        frustum.Update(projection * view);

        // 1. Detección automática en tiempo real de cambios en geometría estática o estado IsStatic
        uint64_t currentStaticHash = 14695981039346656037ULL;
        size_t entityCount = m_ContextLevel->GetEntities().size();
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            if (ent->IsStatic()) {
                currentStaticHash ^= reinterpret_cast<uint64_t>(ent);
                currentStaticHash *= 1099511628211ULL;
                const float* pos = &ent->position.x;
                for (int k = 0; k < 3; ++k) {
                    uint32_t val = *reinterpret_cast<const uint32_t*>(&pos[k]);
                    currentStaticHash ^= val;
                    currentStaticHash *= 1099511628211ULL;
                }
                const float* rot = &ent->rotation.x;
                for (int k = 0; k < 3; ++k) {
                    uint32_t val = *reinterpret_cast<const uint32_t*>(&rot[k]);
                    currentStaticHash ^= val;
                    currentStaticHash *= 1099511628211ULL;
                }
                const float* scl = &ent->scale.x;
                for (int k = 0; k < 3; ++k) {
                    uint32_t val = *reinterpret_cast<const uint32_t*>(&scl[k]);
                    currentStaticHash ^= val;
                    currentStaticHash *= 1099511628211ULL;
                }

                if (auto tc = ent->GetComponent<SlipTerrainComponent>()) {
                    currentStaticHash ^= static_cast<uint64_t>(tc->GetRevision());
                    currentStaticHash *= 1099511628211ULL;
                    currentStaticHash ^= reinterpret_cast<uint64_t>(tc->GetMesh());
                    currentStaticHash *= 1099511628211ULL;
                }
                if (auto mc = ent->GetComponent<MeshComponent>()) {
                    currentStaticHash ^= reinterpret_cast<uint64_t>(mc->model);
                    currentStaticHash *= 1099511628211ULL;
                }
            }
        }

        bool isDraggingGizmo = (m_ActiveGizmoAxis != -1);
        if (!isDraggingGizmo) {
            if (m_SpatialScene.GetStaticBVH().IsEmpty() || m_SpatialScene.IsStaticDirty() || entityCount != m_LastEntityCount || currentStaticHash != m_LastStaticHash) {
                m_SpatialScene.RebuildStatic(m_ContextLevel->GetEntities());
                m_LastEntityCount = entityCount;
                m_LastStaticHash = currentStaticHash;
            }
        }
        m_SpatialScene.UpdateDynamic(m_ContextLevel->GetEntities());

        std::vector<BVHItem> visibleCandidates;
        size_t totalSceneSubmeshes = m_SpatialScene.GetStaticBVH().GetItems().size() + m_SpatialScene.GetDynamicBVH().GetItems().size();
        m_StatTotalSubmeshes = static_cast<int>(totalSceneSubmeshes);

        if (gfxSettings.frustumCullingEnabled) {
            m_SpatialScene.QueryFrustum(frustum, visibleCandidates);
            m_StatFrustumCulled = static_cast<int>(totalSceneSubmeshes - visibleCandidates.size());
} else {
            const auto& sItems = m_SpatialScene.GetStaticBVH().GetItems();
            const auto& dItems = m_SpatialScene.GetDynamicBVH().GetItems();
            visibleCandidates.insert(visibleCandidates.end(), sItems.begin(), sItems.end());
            visibleCandidates.insert(visibleCandidates.end(), dItems.begin(), dItems.end());
            m_StatFrustumCulled = 0;
}

        m_RenderQueue.Clear();
        m_VClustFrameStats.Reset();
        int occlusionCulledCount = 0;

        // Pase de oclusión solo si está explícitamente activado en Graphics Settings
        bool useOcclusion = gfxSettings.occlusionCullingEnabled;
        if (useOcclusion) {
            SlipOcclusionCuller::Get().BeginOcclusionPass(projection * view);
}

        float vpHeightForSSE = (std::max)(1.0f, static_cast<float>(m_FboHeight));
        float fovRadForSSE = activeGameCam ? glm::radians(activeGameCam->fov) : glm::radians(45.0f);
        std::unordered_set<SlipEntity*> vclustProcessedEntities;
        std::vector<SlipVisibleCluster> entityVisClusters;

            // 0. Pre-cálculo de jerarquía V-Clust por Malla Compartida:
            // Si hay N instancias del mismo modelo pesado (ej. 1,000 instancias de 5M triángulos),
            // para instancias con distancias similares agrupadas en cubos de distancia o mallas masivas (> 4,000 clusters),
            // evaluar el árbol de forma ultrarrápida y fusionar rangos contiguos en todos los modos (incluyendo Vista de Clusters y Heatmap).
            struct CachedMeshLODBucket {
                std::vector<SlipVisibleCluster> mergedRuns;
                uint32_t rawVisibleClusters = 0;
                uint32_t rawTriangles = 0;
                uint32_t rawTraversed = 0;
                uint32_t rawBackfaceCulled = 0;
            };
            // Clave de caché por (SlipMesh*, bucket de distancia logarítmica) para mallas masivas instanciadas
            std::unordered_map<uint64_t, CachedMeshLODBucket> massiveMeshBucketCache;

            for (const auto& item : visibleCandidates) {
                if (!item.entity) continue;

                uint64_t occId = reinterpret_cast<uint64_t>(item.entity) ^ (static_cast<uint64_t>(item.submeshIndex) << 32);

                if (useOcclusion) {
                    // Si la cámara está dentro de la AABB o cerca de ella, IsVisible siempre devuelve true
                    if (!SlipOcclusionCuller::Get().IsVisible(occId, eye, item.worldAabbMin, item.worldAabbMax)) {
                        SlipOcclusionCuller::Get().RenderBoundingBoxQuery(occId, item.worldAabbMin, item.worldAabbMax);
                        occlusionCulledCount++;
                        continue;
                    }

                    if (item.isStatic) {
                        SlipOcclusionCuller::Get().RenderBoundingBoxQuery(occId, item.worldAabbMin, item.worldAabbMax);
                    }
                }

                auto meshComp = item.entity->GetComponent<MeshComponent>();
                auto skinnedComp = item.entity->GetComponent<SkinnedMeshComponent>();
                auto terrainComp = item.entity->GetComponent<SlipTerrainComponent>();
                bool isStaticSkinnedFallback = (skinnedComp && (!skinnedComp->skeleton || skinnedComp->skeleton->GetBoneCount() == 0));

                // Ruta V-Clust (Voxel-Anchored Cluster LOD) para mallas estáticas con jerarquía de clusters
                if (m_VClustEnabled && (meshComp || isStaticSkinnedFallback || terrainComp) && (!skinnedComp || isStaticSkinnedFallback) &&
                    item.mesh && item.mesh->hasClusterLOD && !item.mesh->clusters.empty()) {
                    if (vclustProcessedEntities.find(item.entity) != vclustProcessedEntities.end()) {
                        continue;
                    }
                    vclustProcessedEntities.insert(item.entity);

                    glm::mat4 worldMat = (m_SelectedEntity && item.entity == m_SelectedEntity) ? item.entity->getMatrix() : item.worldTransform;
                    bool isSel = (m_SelectedEntity && item.entity == m_SelectedEntity);
                    unsigned int targetVAO = (item.mesh->clusterVAO != 0) ? item.mesh->clusterVAO : item.mesh->masterVAO;

                    if (targetVAO != 0) {
                        glm::vec3 entWorldPos = glm::vec3(worldMat[3]);
                        float entDist = glm::length(entWorldPos - eye);
                        bool isMassiveInstanced = (item.mesh->clusters.size() > 2048 && m_StressTestEntities.size() > 16 && !isSel);

                        const std::vector<SlipVisibleCluster>* runsToSubmit = nullptr;
                        std::vector<SlipVisibleCluster> localMergedRuns;

                        if (isMassiveInstanced) {
                            // Discretizar distancia en 24 anillos para reutilizar el corte del árbol entre las 1,000 instancias de 71,362 clusters
                            uint32_t distBucket = static_cast<uint32_t>( std::clamp(entDist * 0.5f, 0.0f, 31.0f) );
                            uint64_t cacheKey = (reinterpret_cast<uint64_t>(item.mesh) << 6) | (distBucket & 0x3FULL);
                            auto itCache = massiveMeshBucketCache.find(cacheKey);
                            if (itCache == massiveMeshBucketCache.end()) {
                                CachedMeshLODBucket newBucket{};
                                SlipVClustStats localStats{};
                                entityVisClusters.clear();

                                // Para evaluar el corte representativo a distancia entDist sin rechazar por frustum lateral:
                                SlipFrustum openFrustum;
                                for (int p = 0; p < 6; ++p) {
                                    openFrustum.planes[p].normal = glm::vec3(0.0f, 1.0f, 0.0f);
                                    openFrustum.planes[p].distance = 1e20f;
                                }

                                // Si es un modelo fotogramétrico de millones de triángulos en modo Stress Test de 1,000 instancias,
                                // escalar adaptativamente el umbral SSE según la densidad para mantener 60 FPS fluidos
                                float effectiveSse = m_VClustSseThreshold;
                                if (item.mesh->clusters.size() > 10000) {
                                    effectiveSse = (std::max)(m_VClustSseThreshold, 4.0f + distBucket * 2.5f);
                                }

                                SlipRender::TraverseClusterHierarchy(
                                    item.mesh->clusters,
                                    item.mesh->clusterHeader.rootClusterCount,
                                    worldMat,
                                    openFrustum,
                                    eye,
                                    vpHeightForSSE,
                                    fovRadForSSE,
                                    entityVisClusters,
                                    &localStats,
                                    effectiveSse,
                                    false
                                );

                                // Ordenar estrictamente por (materialIndex, indexOffset) para que TODOS los clusters adyacentes
                                // en el EBO se fusionen en 1-2 Draw Calls por instancia en lugar de 500 Draw Calls por instancia
                                if (entityVisClusters.size() > 1) {
                                    std::sort(entityVisClusters.begin(), entityVisClusters.end(),
                                        [this](const SlipVisibleCluster& a, const SlipVisibleCluster& b) {
                                            if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
                                            return a.indexOffset < b.indexOffset;
                                        });
                                }

                                size_t cIdx = 0;
                                while (cIdx < entityVisClusters.size()) {
                                    SlipVisibleCluster merged = entityVisClusters[cIdx];
                                    size_t nextIdx = cIdx + 1;
                                    while (nextIdx < entityVisClusters.size() &&
                                           entityVisClusters[nextIdx].materialIndex == merged.materialIndex &&
                                           entityVisClusters[nextIdx].indexOffset == (merged.indexOffset + merged.indexCount)) {
                                        merged.indexCount += entityVisClusters[nextIdx].indexCount;
                                        ++nextIdx;
                                    }
                                    newBucket.mergedRuns.push_back(merged);
                                    cIdx = nextIdx;
                                }

                                newBucket.rawVisibleClusters = localStats.visibleClusters;
                                newBucket.rawTriangles = localStats.trianglesSubmitted;
                                newBucket.rawTraversed = localStats.totalClustersTraversed;
                                newBucket.rawBackfaceCulled = localStats.backfaceCulledClusters;
                                itCache = massiveMeshBucketCache.emplace(cacheKey, std::move(newBucket)).first;
                            }

                            m_VClustFrameStats.visibleClusters += itCache->second.rawVisibleClusters;
                            m_VClustFrameStats.trianglesSubmitted += itCache->second.rawTriangles;
                            m_VClustFrameStats.totalClustersTraversed += itCache->second.rawTraversed;
                            m_VClustFrameStats.backfaceCulledClusters += itCache->second.rawBackfaceCulled;
                            runsToSubmit = &itCache->second.mergedRuns;
                        } else {
                            entityVisClusters.clear();
                            SlipRender::TraverseClusterHierarchy(
                                item.mesh->clusters,
                                item.mesh->clusterHeader.rootClusterCount,
                                worldMat,
                                frustum,
                                eye,
                                vpHeightForSSE,
                                fovRadForSSE,
                                entityVisClusters,
                                &m_VClustFrameStats,
                                m_VClustSseThreshold,
                                true
                            );

                            // Ordenar y fusionar rangos contiguos SIEMPRE (tanto en Material Real como en Vista de Clusters y Heatmap LOD).
                            // En Vista de Clusters (modo 1), el Fragment Shader colorea cada clúster usando gl_PrimitiveID / 124,
                            // reduciendo 48,926 draw calls a ~1-5 draw calls y subiendo los FPS de 12 FPS a 60+ FPS.
                            if (entityVisClusters.size() > 1) {
                                std::sort(entityVisClusters.begin(), entityVisClusters.end(),
                                    [this](const SlipVisibleCluster& a, const SlipVisibleCluster& b) {
                                        if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
                                        if (m_VClustDebugMode == 2 && a.lodLevel != b.lodLevel) return a.lodLevel < b.lodLevel;
                                        return a.indexOffset < b.indexOffset;
                                    });
                            }

                            localMergedRuns.reserve(16);
                            size_t cIdx = 0;
                            while (cIdx < entityVisClusters.size()) {
                                SlipVisibleCluster merged = entityVisClusters[cIdx];
                                size_t nextIdx = cIdx + 1;
                                while (nextIdx < entityVisClusters.size() &&
                                       entityVisClusters[nextIdx].materialIndex == merged.materialIndex &&
                                       (m_VClustDebugMode != 2 || entityVisClusters[nextIdx].lodLevel == merged.lodLevel) &&
                                       entityVisClusters[nextIdx].indexOffset == (merged.indexOffset + merged.indexCount)) {
                                    merged.indexCount += entityVisClusters[nextIdx].indexCount;
                                    ++nextIdx;
                                }
                                localMergedRuns.push_back(merged);
                                cIdx = nextIdx;
                            }
                            runsToSubmit = &localMergedRuns;
                        }

                        if (runsToSubmit) {
                            if (isSel) {
                                selectedEntityVisRuns = *runsToSubmit;
                                selectedEntityVClustVAO = targetVAO;
                            }
                            for (const auto& vc : *runsToSubmit) {
                                SlipMaterial* subMat = nullptr;
                                if (meshComp) {
                                    subMat = meshComp->GetSubmeshMaterial(vc.materialIndex);
                                    if (!subMat) subMat = meshComp->material;
                                } else if (skinnedComp) {
                                    subMat = skinnedComp->GetSubmeshMaterial(vc.materialIndex);
                                    if (!subMat) subMat = skinnedComp->material;
                                } else if (terrainComp) {
                                    subMat = terrainComp->GetMaterial();
                                }

                                RenderCommand cmd;
                                cmd.entity = item.entity;
                                cmd.mesh = item.mesh;
                                cmd.submeshIndex = vc.submeshIndex;
                                cmd.transform = worldMat;
                                cmd.isStencilSelected = isSel;
                                cmd.material = subMat;
                                cmd.doubleSided = (subMat && subMat->doubleSided);
                                cmd.wireframe = (terrainComp ? terrainComp->wireframe : false);
                                cmd.useCustomIndexRange = true;
                                cmd.customVAO = targetVAO;
                                cmd.customFirstIndex = vc.indexOffset;
                                cmd.customIndexCount = vc.indexCount;
                                cmd.debugClusterVisMode = m_VClustDebugMode;

                                if (m_VClustDebugMode == 1) {
                                    // Color por clúster calculado directamente en GPU (gl_PrimitiveID / 124) manteniendo fusión de Draw Calls
                                    uint32_t h = (vc.clusterId + 1u) * 2654435761u;
                                    float r = 0.25f + 0.72f * (float((h >>  0) & 0xFF) / 255.0f);
                                    float g = 0.25f + 0.72f * (float((h >>  8) & 0xFF) / 255.0f);
                                    float b = 0.25f + 0.72f * (float((h >> 16) & 0xFF) / 255.0f);
                                    cmd.useDebugTint = true;
                                    cmd.debugTintColor = glm::vec3(r, g, b);
                                } else if (m_VClustDebugMode == 2) {
                                    // Mapa de calor por nivel LOD (LOD 0: Verde -> LOD 1: Cian -> LOD 2: Amarillo -> LOD 3: Naranja -> LOD 4+: Rojo/Magenta)
                                    static const glm::vec3 kLodColors[6] = {
                                        glm::vec3(0.18f, 0.92f, 0.32f), // LOD 0 (Malla base densa)
                                        glm::vec3(0.15f, 0.82f, 0.95f), // LOD 1
                                        glm::vec3(0.95f, 0.88f, 0.18f), // LOD 2
                                        glm::vec3(0.98f, 0.52f, 0.12f), // LOD 3
                                        glm::vec3(0.95f, 0.18f, 0.22f), // LOD 4
                                        glm::vec3(0.85f, 0.20f, 0.95f)  // LOD 5+
                                    };
                                    uint32_t lvl = (std::min)(vc.lodLevel, 5u);
                                    cmd.useDebugTint = true;
                                    cmd.debugTintColor = kLodColors[lvl];
                                }

                                uint32_t shaderId = (subMat && subMat->shader)
                                    ? static_cast<uint32_t>(subMat->shader->ID)
                                    : ((subMat && std::string(subMat->shaderType) == "Unlit") ? 1 : 2);
                                uint32_t matId = subMat ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(subMat) & 0xFFFF) : 0;
                                uint32_t vaoId = targetVAO & 0xFF;

                                cmd.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, shaderId, matId, vaoId, entDist, true);
                                m_RenderQueue.Submit(cmd);
                                m_VClustFrameStats.drawCallsIssued++;
                            }
                        }
                        continue;
                    }
                }

            RenderCommand cmd;
            cmd.entity = item.entity;
            cmd.mesh = item.mesh;
            cmd.submeshIndex = item.submeshIndex;
            cmd.transform = (m_SelectedEntity && item.entity == m_SelectedEntity) ? item.entity->getMatrix() : item.worldTransform;
            cmd.isStencilSelected = IsEntitySelected(item.entity);

            SlipMaterial* subMat = nullptr;
            SlipPrimitiveEntity* prim = nullptr;
            if (meshComp) {
                size_t slotIdx = (item.mesh && item.submeshIndex < item.mesh->meshes.size()) ?
                                 static_cast<size_t>(item.mesh->meshes[item.submeshIndex].materialIndex) : 0;
                subMat = meshComp->GetSubmeshMaterial(slotIdx);
                if (!subMat) subMat = meshComp->material;
    } else if (skinnedComp) {
                skinnedComp->UpdateBoneMatrices();
                cmd.hasBones = true;
                cmd.skinningQuality = gfxSettings.skinningQuality;
                cmd.boneTransforms = skinnedComp->boneTransforms.data();
                cmd.boneTransformCount = skinnedComp->boneTransforms.size();

                size_t slotIdx = (item.mesh && item.submeshIndex < item.mesh->meshes.size()) ?
                                 static_cast<size_t>(item.mesh->meshes[item.submeshIndex].materialIndex) : 0;
                subMat = skinnedComp->GetSubmeshMaterial(slotIdx);
                if (!subMat) subMat = skinnedComp->material;
    } else if (terrainComp) {
                subMat = terrainComp->GetMaterial();
    } else {
                prim = dynamic_cast<SlipPrimitiveEntity*>(item.entity);
                if (prim) {
                    cmd.isPrimitive = true;
                    cmd.primEntity = prim;
                    subMat = nullptr;
        }
    }

            cmd.material = subMat;
            cmd.doubleSided = (subMat && subMat->doubleSided);
            cmd.wireframe = (terrainComp ? terrainComp->wireframe : (prim && prim->wireframe));

            float dist = glm::distance(eye, glm::vec3(item.worldTransform[3]));
            uint32_t shaderId = (subMat && subMat->shader)
                ? static_cast<uint32_t>(subMat->shader->ID)
                : ((subMat && std::string(subMat->shaderType) == "Unlit") ? 1 : 2);
            uint32_t matId = subMat ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(subMat) & 0xFFFF) : 0;
            uint32_t vaoId = (item.mesh && item.submeshIndex < item.mesh->meshes.size()) ?
                             (item.mesh->meshes[item.submeshIndex].VAO & 0xFF) : 0;

            cmd.sortKey = RenderCommand::MakeSortKey(RenderPassType::Opaque, shaderId, matId, vaoId, dist, true);
            m_RenderQueue.Submit(cmd);
}

        if (useOcclusion) {
            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                if (!ent) continue;
                auto ps = ent->GetComponent<ParticleSystemComponent>();
                if (!ps || !ps->IsEnabled() || ps->GetParticles().empty()) continue;
                glm::mat4 emitterWorld = ent->getMatrix();
                glm::vec3 psMin, psMax;
                ps->GetWorldBounds(emitterWorld, psMin, psMax);
                if (!gfxSettings.frustumCullingEnabled || frustum.IntersectsAABB(psMin, psMax)) {
                    uint64_t occId = reinterpret_cast<uint64_t>(ps.get());
                    SlipOcclusionCuller::Get().RenderBoundingBoxQuery(occId, psMin, psMax);
        }
    }
            SlipOcclusionCuller::Get().EndOcclusionPass();
            SlipOcclusionCuller::Get().EndFrame();
}

        m_StatOcclusionCulled = occlusionCulledCount;
        m_StatVisibleSubmeshes = static_cast<int>(m_RenderQueue.GetCommandCount());
        m_StatDrawCalls = static_cast<int>(m_RenderQueue.GetCommandCount());

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        GLenum litBuffers[1] = { GL_COLOR_ATTACHMENT0 };
        glDrawBuffers(1, litBuffers);
#else
        GLenum litBuffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers(2, litBuffers);
#endif

        // Despacho de Clustered Forward Shading en GPU (>1,000 luces dinámicas y mixtas con culling de frustum)
        SlipClusteredLighting::Get().Initialize();
        SlipClusteredLighting::Get().UpdateGrid(
            projection,
            glm::inverse(projection),
            camNear,
            camFar,
            static_cast<uint32_t>(std::max(1.0f, m_ViewportSize.x)),
            static_cast<uint32_t>(std::max(1.0f, m_ViewportSize.y))
        );
        SlipClusteredLighting::Get().UploadLights(gpuLights);
        SlipClusteredLighting::Get().CullLights(view);

        // Vaciado agrupado por material y shader (Material Sorting)
        m_RenderQueue.Flush([&](SlipShader* shader) {
            shader->setFloat("u_Time", SlipGlobals::Get().GetTime());
            shader->setFloat("u_DeltaTime", SlipGlobals::Get().GetDeltaTime());
            shader->setMat4("u_Projection", projection);
            shader->setMat4("u_View", view);
            shader->setMat4("projection", projection);
            shader->setMat4("view", view);
            shader->setVec3("u_CameraPos", eye.x, eye.y, eye.z);

            // Enlazar datos de clusters e índices de luz
            SlipClusteredLighting::Get().BindClusterData(
                shader,
                camNear,
                camFar,
                static_cast<uint32_t>(std::max(1.0f, m_ViewportSize.x)),
                static_cast<uint32_t>(std::max(1.0f, m_ViewportSize.y))
            );

            // Enlazar textura de atlas de lightmaps si la escena tiene iluminación horneada
            if (m_ContextLevel && m_ContextLevel->lightmapTexture != 0) {
                glActiveTexture(GL_TEXTURE18);
                glBindTexture(GL_TEXTURE_2D, m_ContextLevel->lightmapTexture);
                shader->setInt("u_LightmapAtlas", 18);
                shader->setBool("u_HasLightmap", true);
            } else {
                shader->setBool("u_HasLightmap", false);
            }

            shader->setInt("u_NumDirLights", (int)dirLights.size());
            for (size_t i = 0; i < dirLights.size(); ++i) {
                std::string base = "u_DirLights[" + std::to_string(i) + "].";
                shader->setVec3(base + "direction", dirLights[i].direction.x, dirLights[i].direction.y, dirLights[i].direction.z);
                shader->setVec3(base + "color", dirLights[i].color.x, dirLights[i].color.y, dirLights[i].color.z);
                shader->setFloat(base + "intensity", dirLights[i].intensity);
    }

            int numPts = (std::min)((int)pointLights.size(), 32);
            shader->setInt("u_NumPointLights", numPts);
            for (size_t i = 0; i < (size_t)numPts; ++i) {
                std::string base = "u_PointLights[" + std::to_string(i) + "].";
                shader->setVec3(base + "position", pointLights[i].position.x, pointLights[i].position.y, pointLights[i].position.z);
                shader->setVec3(base + "color", pointLights[i].color.x, pointLights[i].color.y, pointLights[i].color.z);
                shader->setFloat(base + "intensity", pointLights[i].intensity);
                shader->setFloat(base + "range", pointLights[i].range);
    }

            shader->setInt("u_NumSpotLights", (int)spotLights.size());
            for (size_t i = 0; i < spotLights.size(); ++i) {
                std::string base = "u_SpotLights[" + std::to_string(i) + "].";
                shader->setVec3(base + "position", spotLights[i].position.x, spotLights[i].position.y, spotLights[i].position.z);
                shader->setVec3(base + "direction", spotLights[i].direction.x, spotLights[i].direction.y, spotLights[i].direction.z);
                shader->setVec3(base + "color", spotLights[i].color.x, spotLights[i].color.y, spotLights[i].color.z);
                shader->setFloat(base + "intensity", spotLights[i].intensity);
                shader->setFloat(base + "range", spotLights[i].range);
                shader->setFloat(base + "spotAngle", spotLights[i].spotAngle);
    }

            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D_ARRAY, SlipCascadedShadows::Get().GetDepthTextureArray());
            shader->setInt("u_ShadowMapArray", 3);
            shader->setBool("u_ShadowsEnabled", gfxSettings.shadowsEnabled && canCastShadows);
            shader->setFloat("u_ShadowBias", gfxSettings.shadowBias);
            shader->setInt("u_PCFQuality", gfxSettings.pcfQuality);
            shader->setInt("u_CascadeCount", SlipCascadedShadows::Get().GetCascadeCount());

            const auto& lightMats = SlipCascadedShadows::Get().GetLightSpaceMatrices();
            for (size_t c = 0; c < lightMats.size() && c < 4; ++c) {
                shader->setMat4("u_LightSpaceMatrices[" + std::to_string(c) + "]", lightMats[c]);
    }
            const auto& clipSplits = SlipCascadedShadows::Get().GetCascadeEndClipSpace();
            for (size_t c = 0; c < clipSplits.size() && c < 4; ++c) {
                shader->setFloat("u_CascadeEndClipSpace[" + std::to_string(c) + "]", clipSplits[c]);
    }

            shader->setBool("u_DDGIEnabled", false);

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
            ReflectionProbeComponent* activeProbe = nullptr;
            glm::vec3 activeProbePos(0.0f);
            if (m_ContextLevel) {
                for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                    if (!ent) continue;
                    auto probe = ent->GetComponent<ReflectionProbeComponent>();
                    if (probe && probe->IsEnabled() && probe->isBaked && probe->cubemap != 0) {
                        glm::vec3 entPos = ent->GetTransform() ? ent->GetTransform()->position : ent->position;
                        if (probe->IsInside(eye, entPos)) {
                            activeProbe = probe.get();
                            activeProbePos = entPos;
                            break;
                        }
                    }
                }
            }

            if (activeProbe && gfxSettings.reflectionProbeEnabled) {
                glActiveTexture(GL_TEXTURE20);
                glBindTexture(GL_TEXTURE_CUBE_MAP, activeProbe->cubemap);
                shader->setInt("u_EnvCube", 20);
                shader->setBool("u_HasEnvCube", true);
                shader->setFloat("u_EnvIntensity", activeProbe->intensity);
                shader->setFloat("u_ReflectionRoughnessCutoff", activeProbe->roughnessCutoff);

                shader->setBool("u_ProbeBoxProjection", activeProbe->boxProjection);
                shader->setVec3("u_ProbeOrigin", activeProbePos.x, activeProbePos.y, activeProbePos.z);
                glm::vec3 bmin, bmax;
                activeProbe->GetBoxBounds(activeProbePos, bmin, bmax);
                shader->setVec3("u_ProbeBoxMin", bmin.x, bmin.y, bmin.z);
                shader->setVec3("u_ProbeBoxMax", bmax.x, bmax.y, bmax.z);
            } else if (m_ReflectionCubemap != 0 && gfxSettings.reflectionProbeEnabled) {
                glActiveTexture(GL_TEXTURE20);
                glBindTexture(GL_TEXTURE_CUBE_MAP, m_ReflectionCubemap);
                shader->setInt("u_EnvCube", 20);
                shader->setBool("u_HasEnvCube", true);
                shader->setFloat("u_EnvIntensity", gfxSettings.reflectionIntensity);
                shader->setFloat("u_ReflectionRoughnessCutoff", gfxSettings.reflectionRoughnessCutoff);
                shader->setBool("u_ProbeBoxProjection", false);
            } else {
                glActiveTexture(GL_TEXTURE20);
                glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
                shader->setBool("u_HasEnvCube", false);
                shader->setBool("u_ProbeBoxProjection", false);
            }
#endif

            if (activeSky && activeSky->params.cloudShadowsEnabled && activeSky->IsEnabled()) {
                shader->setBool("u_CloudShadowsEnabled", true);
                shader->setFloat("u_CloudShadowIntensity", activeSky->params.cloudShadowIntensity);
                shader->setFloat("u_CloudBaseHeight", activeSky->params.cloudBaseHeight);
                shader->setFloat("u_CloudLayerThickness", activeSky->params.cloudLayerThickness);
                shader->setFloat("u_CloudScale", activeSky->params.cloudScale);
                shader->setFloat("u_CloudCoverage", activeSky->params.cloudCoverage);
                shader->setFloat("u_CloudDensity", activeSky->params.cloudDensity);
                shader->setFloat("u_CloudEdgeErosion", activeSky->params.cloudEdgeErosion);
                shader->setFloat("u_CloudDetailScale", activeSky->params.cloudDetailScale);
                shader->setFloat("u_Absorption", activeSky->params.absorption);

                glm::vec2 windDirNorm = glm::length(activeSky->params.windDirection) > 0.0001f
                                        ? glm::normalize(activeSky->params.windDirection)
                                        : glm::vec2(1.0f, 0.0f);
                glm::vec2 windOffset = windDirNorm * (activeSky->params.windSpeed * currentCloudTime * activeSky->params.cloudScale);
                shader->setVec2("u_CloudWindOffset", windOffset);

                activeSky->EnsureDefaultTextures();
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
                const int CLOUD_SHADOW_SLOT = 14;
                glActiveTexture(GL_TEXTURE14);
#else
                const int CLOUD_SHADOW_SLOT = 19;
                glActiveTexture(GL_TEXTURE19);
#endif
                unsigned int noiseID = (activeSky->cloudNoiseTexture && activeSky->cloudNoiseTexture->getID() != 0)
                                       ? activeSky->cloudNoiseTexture->getID()
                                       : activeSky->GetDefaultNoiseTex();
                glBindTexture(GL_TEXTURE_2D, noiseID);
                shader->setInt("u_CloudShadowNoise", CLOUD_SHADOW_SLOT);
            } else {
                shader->setBool("u_CloudShadowsEnabled", false);
            }

            glActiveTexture(GL_TEXTURE0);
});

        m_StatDrawCalls = static_cast<int>(m_RenderQueue.GetActualDrawCalls());

        glFrontFace(GL_CCW);
    }

    // 3. Grilla 3D de referencia en el suelo con prueba de profundidad contra la geometría de la escena
    if (m_ShowGrid && m_GridVAO != 0 && (m_ActiveCameraType == ViewportCameraType::EditorCamera)) {
        SlipDrawBuffer(GL_COLOR_ATTACHMENT0);
        glm::mat4 gridModel = glm::mat4(1.0f);
        if (m_Is2DMode) {
            gridModel = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        SlipShader* lineShader = SlipShaderLibrary::Get("Line");
        if (lineShader) {
            lineShader->use();
            lineShader->setMat4("u_Model", gridModel);
            lineShader->setMat4("u_View", view);
            lineShader->setMat4("u_Projection", projection);
            lineShader->setVec4("u_Color", glm::vec4(0.45f, 0.45f, 0.48f, 0.35f));
        }

        glBindVertexArray(m_GridVAO);
        glDrawArrays(GL_LINES, 0, m_GridVertexCount);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    if (gfxSettings.skyEnabled) {
        DrawProceduralSky(view, projection, eye, mainLightDir);
    }
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (gfxSettings.ssrEnabled) {
        DrawSSR(view, projection, eye, mainLightDir);
    }
#endif

    // 3.4 Renderizado de Nubes Volumétricas Híbridas (SlipSkyComponent)
    if (activeSky && activeSky->IsEnabled()) {
        float nearP = activeGameCam ? activeGameCam->nearClip : camNear;
        float farP = activeGameCam ? activeGameCam->farClip : camFar;

        SlipRender::GetInstance().RenderClouds(
            activeSky,
            view,
            projection,
            eye,
            m_DepthTexture,
            m_FboWidth,
            m_FboHeight,
            nearP,
            farP,
            currentCloudTime
        );

        glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
        glViewport(0, 0, m_FboWidth, m_FboHeight);
        SlipDrawBuffer(GL_COLOR_ATTACHMENT0);
    }
    PostProcessSettings postSettings = PostProcessVolumeComponent::Resolve(m_ContextLevel, eye);
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (postSettings.ssao && postSettings.ssaoIntensity > 0.001f && m_NormalTexture != 0) {
        SlipPostProcess::Get().ApplySSAO(m_ColorTexture, m_DepthTexture, m_NormalTexture,
                                         view, projection, glm::inverse(projection),
                                         postSettings, m_FBO, m_FboWidth, m_FboHeight);
    }
#endif
    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_FboWidth, m_FboHeight);
    SlipDrawBuffer(GL_COLOR_ATTACHMENT0);

    // 3.5 Renderizado de Sistemas de Partículas (Billboards estilo Unity y GL_POINTS estilo Unreal Niagara)
    // Se renderizan DESPUÉS del SSAO (para que el AO de la geometría opaca no ensucie las partículas)
    // pero ANTES del Bloom/Tonemapping en el FBO HDR para que el modo Emissive active el Bloom real
    if (m_ContextLevel) {
        SlipShader* billboardShader = SlipShaderLibrary::Get("ParticleBillboard");
        SlipShader* pointShader = SlipShaderLibrary::Get("ParticlePoint");
        if (billboardShader && pointShader) {
            if (m_ParticleVAO == 0) {
                glGenVertexArrays(1, &m_ParticleVAO);
                glGenBuffers(1, &m_ParticleVBO);
    }

            glDisable(GL_STENCIL_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);

            glm::mat4 invView = glm::inverse(view);
            glm::vec3 camRight = glm::normalize(glm::vec3(invView[0]));
            glm::vec3 camUp = glm::normalize(glm::vec3(invView[1]));
            glm::vec3 camBack = glm::normalize(glm::vec3(invView[2]));

            auto bindParticleLightsAndShadows = [&](SlipShader* sh, ParticleSystemComponent* ps) {
                sh->setMat4("u_View", view);
                sh->setMat4("u_Projection", projection);
                sh->setVec3("u_CameraPos", eye.x, eye.y, eye.z);
                sh->setInt("u_MaterialMode", static_cast<int>(ps->materialMode));
                sh->setVec3("u_EmissiveColor", ps->emissiveColor.r, ps->emissiveColor.g, ps->emissiveColor.b);
                sh->setFloat("u_EmissiveIntensity", ps->emissiveIntensity);
                sh->setFloat("u_Roughness", ps->roughness);
                sh->setFloat("u_Metallic", ps->metallic);
                sh->setBool("u_ReceiveLights", ps->receiveLights);
                sh->setBool("u_ReceiveShadows", ps->receiveShadows);

                sh->setInt("u_NumDirLights", (int)dirLights.size());
                for (size_t i = 0; i < dirLights.size(); ++i) {
                    std::string base = "u_DirLights[" + std::to_string(i) + "].";
                    sh->setVec3(base + "direction", dirLights[i].direction.x, dirLights[i].direction.y, dirLights[i].direction.z);
                    sh->setVec3(base + "color", dirLights[i].color.x, dirLights[i].color.y, dirLights[i].color.z);
                    sh->setFloat(base + "intensity", dirLights[i].intensity);
        }

                int numPts = (std::min)((int)pointLights.size(), 32);
                sh->setInt("u_NumPointLights", numPts);
                for (size_t i = 0; i < (size_t)numPts; ++i) {
                    std::string base = "u_PointLights[" + std::to_string(i) + "].";
                    sh->setVec3(base + "position", pointLights[i].position.x, pointLights[i].position.y, pointLights[i].position.z);
                    sh->setVec3(base + "color", pointLights[i].color.x, pointLights[i].color.y, pointLights[i].color.z);
                    sh->setFloat(base + "intensity", pointLights[i].intensity);
                    sh->setFloat(base + "range", pointLights[i].range);
        }

                sh->setInt("u_NumSpotLights", (int)spotLights.size());
                for (size_t i = 0; i < spotLights.size(); ++i) {
                    std::string base = "u_SpotLights[" + std::to_string(i) + "].";
                    sh->setVec3(base + "position", spotLights[i].position.x, spotLights[i].position.y, spotLights[i].position.z);
                    sh->setVec3(base + "direction", spotLights[i].direction.x, spotLights[i].direction.y, spotLights[i].direction.z);
                    sh->setVec3(base + "color", spotLights[i].color.x, spotLights[i].color.y, spotLights[i].color.z);
                    sh->setFloat(base + "intensity", spotLights[i].intensity);
                    sh->setFloat(base + "range", spotLights[i].range);
                    sh->setFloat(base + "spotAngle", spotLights[i].spotAngle);
        }

                glActiveTexture(GL_TEXTURE3);
                glBindTexture(GL_TEXTURE_2D_ARRAY, SlipCascadedShadows::Get().GetDepthTextureArray());
                sh->setInt("u_ShadowMapArray", 3);
                sh->setBool("u_ShadowsEnabled", gfxSettings.shadowsEnabled && canCastShadows);
                sh->setFloat("u_ShadowBias", gfxSettings.shadowBias);
                sh->setInt("u_PCFQuality", gfxSettings.pcfQuality);
                sh->setInt("u_CascadeCount", SlipCascadedShadows::Get().GetCascadeCount());

                const auto& lightMats = SlipCascadedShadows::Get().GetLightSpaceMatrices();
                for (size_t c = 0; c < lightMats.size() && c < 4; ++c) {
                    sh->setMat4("u_LightSpaceMatrices[" + std::to_string(c) + "]", lightMats[c]);
        }
                const auto& clipSplits = SlipCascadedShadows::Get().GetCascadeEndClipSpace();
                for (size_t c = 0; c < clipSplits.size() && c < 4; ++c) {
                    sh->setFloat("u_CascadeEndClipSpace[" + std::to_string(c) + "]", clipSplits[c]);
        }
    };

            struct BillboardVert {
                glm::vec3 pos;
                glm::vec3 normal;
                glm::vec2 uv;
                glm::vec4 color;
    };
            struct PointVert {
                glm::vec3 pos;
                float size;
                glm::vec4 color;
    };
            struct EmitterEntry {
                ParticleSystemComponent* ps;
                glm::mat4 emitterWorld;
                float distSq;
            };

            std::vector<EmitterEntry> activeEmitters;
            int particleFrustumCulled = 0;
            int particleOcclusionCulled = 0;

            for (SlipEntity* entity : m_ContextLevel->GetEntities()) {
                if (!entity) continue;
                auto ps = entity->GetComponent<ParticleSystemComponent>();
                if (!ps || !ps->IsEnabled() || ps->GetParticles().empty()) continue;

                glm::mat4 emitterWorld = entity->getMatrix();
                glm::vec3 psMin, psMax;
                ps->GetWorldBounds(emitterWorld, psMin, psMax);

                // 1. Culling por distancia máxima y tamaño sub-pixel
                glm::vec3 psCenter = (psMin + psMax) * 0.5f;
                float distToEmitter = glm::distance(eye, psCenter);
                float emitterRadius = glm::length(psMax - psMin) * 0.5f;
                float projPx = (distToEmitter > 0.001f) ? (emitterRadius / distToEmitter * static_cast<float>(m_FboHeight)) : 999.0f;
                if (distToEmitter > 90.0f || projPx < 1.5f) {
                    particleFrustumCulled += static_cast<int>(ps->GetParticles().size());
                    continue;
                }

                // 2. Frustum Culling a nivel de Emitter de Partículas
                if (gfxSettings.frustumCullingEnabled && !frustum.IntersectsAABB(psMin, psMax)) {
                    particleFrustumCulled += static_cast<int>(ps->GetParticles().size());
                    continue;
                }

                // 3. Occlusion Culling a nivel de Emitter de Partículas
                if (gfxSettings.occlusionCullingEnabled) {
                    uint64_t occId = reinterpret_cast<uint64_t>(ps.get());
                    if (!SlipOcclusionCuller::Get().IsVisible(occId, eye, psMin, psMax)) {
                        particleOcclusionCulled += static_cast<int>(ps->GetParticles().size());
                        continue;
                    }
                }

                activeEmitters.push_back({ ps.get(), emitterWorld, distToEmitter * distToEmitter });
            }

            // Ordenar emisores back-to-front (de más lejos a más cerca de la cámara)
            if (activeEmitters.size() > 1) {
                std::sort(activeEmitters.begin(), activeEmitters.end(), [](const EmitterEntry& a, const EmitterEntry& b) {
                    return a.distSq > b.distSq;
                });
            }

            std::vector<BillboardVert> bVerts;
            std::vector<PointVert> pVerts;

            for (const auto& entry : activeEmitters) {
                ParticleSystemComponent* ps = entry.ps;
                const auto& emitterWorld = entry.emitterWorld;

                if (ps->blendMode == ParticleBlendMode::Additive) {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                } else {
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                }

                if (ps->renderMode == ParticleRenderMode::PointNiagara) {
                    pointShader->use();
                    bindParticleLightsAndShadows(pointShader, ps);
                    pointShader->setFloat("u_ViewportHeight", static_cast<float>((std::max)(1, m_FboHeight)));
                    pointShader->setFloat("u_PointBaseScale", ps->pointBaseScale);
                    pointShader->setInt("u_PointShape", static_cast<int>(ps->pointShape));
                    pointShader->setMat4("u_InvView", invView);

                    pVerts.clear();
                    pVerts.reserve(ps->GetParticles().size());
                    for (const auto& p : ps->GetParticles()) {
                        glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
                        if (gfxSettings.frustumCullingEnabled) {
                            float r = (std::max)(0.12f, p.size * 0.85f);
                            if (!frustum.IntersectsAABB(wPos - glm::vec3(r), wPos + glm::vec3(r))) {
                                particleFrustumCulled++;
                                continue;
                            }
                        }
                        pVerts.push_back({ wPos, p.size, p.color });
                    }

                    if (!pVerts.empty()) {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                        glEnable(GL_PROGRAM_POINT_SIZE);
#endif
                        glBindVertexArray(m_ParticleVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, m_ParticleVBO);
                        glBufferData(GL_ARRAY_BUFFER, pVerts.size() * sizeof(PointVert), pVerts.data(), GL_STREAM_DRAW);
                        glEnableVertexAttribArray(0);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointVert), (void*)0);
                        glEnableVertexAttribArray(1);
                        glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(PointVert), (void*)(sizeof(glm::vec3)));
                        glEnableVertexAttribArray(2);
                        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PointVert), (void*)(sizeof(glm::vec3) + sizeof(float)));
                        glDisableVertexAttribArray(3);
                        glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(pVerts.size()));
                        m_StatDrawCalls++;
                        glBindVertexArray(0);
                    }
                } else {
                    billboardShader->use();
                    bindParticleLightsAndShadows(billboardShader, ps);

                    bool hasTex = (ps->texture != nullptr && ps->texture->getID() != 0);
                    billboardShader->setBool("u_HasTexture", hasTex);
                    if (hasTex) {
                        glActiveTexture(GL_TEXTURE0);
                        billboardShader->setInt("u_MainTexture", 0);
                        ps->texture->draw();
                    }

                    bVerts.clear();
                    bVerts.reserve(ps->GetParticles().size() * 6);

                    for (const auto& p : ps->GetParticles()) {
                        glm::vec3 wPos = ps->simulationSpaceLocal ? glm::vec3(emitterWorld * glm::vec4(p.position, 1.0f)) : p.position;
                        if (gfxSettings.frustumCullingEnabled) {
                            float r = (std::max)(0.12f, p.size * 0.85f);
                            if (!frustum.IntersectsAABB(wPos - glm::vec3(r), wPos + glm::vec3(r))) {
                                particleFrustumCulled++;
                                continue;
                            }
                        }

                        float rad = glm::radians(p.rotation);
                        float c = std::cos(rad);
                        float s = std::sin(rad);
                        float halfSz = p.size * 0.5f;

                        glm::vec3 rAxis = (camRight * c + camUp * s) * halfSz;
                        glm::vec3 uAxis = (-camRight * s + camUp * c) * halfSz;

                        float normAge = (p.lifetime > 0.0001f) ? (p.age / p.lifetime) : 0.0f;
                        glm::vec4 uvR = ps->GetUVRectForNormalizedAge(normAge, p.randomSeed);
                        glm::vec2 uv00(uvR.x, uvR.y);
                        glm::vec2 uv10(uvR.x + uvR.z, uvR.y);
                        glm::vec2 uv11(uvR.x + uvR.z, uvR.y + uvR.w);
                        glm::vec2 uv01(uvR.x, uvR.y + uvR.w);

                        glm::vec3 p0 = wPos - rAxis - uAxis;
                        glm::vec3 p1 = wPos + rAxis - uAxis;
                        glm::vec3 p2 = wPos + rAxis + uAxis;
                        glm::vec3 p3 = wPos - rAxis + uAxis;

                        bVerts.push_back({ p0, wPos, uv00, p.color });
                        bVerts.push_back({ p1, wPos, uv10, p.color });
                        bVerts.push_back({ p2, wPos, uv11, p.color });
                        bVerts.push_back({ p0, wPos, uv00, p.color });
                        bVerts.push_back({ p2, wPos, uv11, p.color });
                        bVerts.push_back({ p3, wPos, uv01, p.color });
                    }

                    if (!bVerts.empty()) {
                        glBindVertexArray(m_ParticleVAO);
                        glBindBuffer(GL_ARRAY_BUFFER, m_ParticleVBO);
                        glBufferData(GL_ARRAY_BUFFER, bVerts.size() * sizeof(BillboardVert), bVerts.data(), GL_STREAM_DRAW);
                        glEnableVertexAttribArray(0);
                        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardVert), (void*)0);
                        glEnableVertexAttribArray(1);
                        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BillboardVert), (void*)(sizeof(glm::vec3)));
                        glEnableVertexAttribArray(2);
                        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BillboardVert), (void*)(2 * sizeof(glm::vec3)));
                        glEnableVertexAttribArray(3);
                        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(BillboardVert), (void*)(2 * sizeof(glm::vec3) + sizeof(glm::vec2)));
                        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(bVerts.size()));
                        m_StatDrawCalls++;
                        glBindVertexArray(0);
                    }
                }
            }

            m_StatFrustumCulled += particleFrustumCulled;
            m_StatOcclusionCulled += particleOcclusionCulled;

            glDisable(GL_BLEND);
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
            glActiveTexture(GL_TEXTURE0);
}
    }

    if (m_PresentFBO != 0) {
        PostProcessSettings postSettingsNoAo = postSettings;
        if (activeGameCam) {
            postSettingsNoAo.letterbox = activeGameCam->letterbox;
            postSettingsNoAo.pillarbox = activeGameCam->pillarbox;
}
        postSettingsNoAo.ssao = false; // El SSAO ya se aplicó antes de las partículas
        postSettingsNoAo.fxAA = gfxSettings.fxAAEnabled;
        postSettingsNoAo.fxAAQuality = gfxSettings.fxAAQuality;
        if (m_ContextLevel) {
            if (SlipSkyComponent* skyComp = SlipSkyComponent::FindActive(m_ContextLevel)) {
                postSettingsNoAo.fogSunDirection = skyComp->params.sunDirection;
                postSettingsNoAo.fogSunColor = skyComp->params.sunColor;
            } else if (glm::length(mainLightDir) > 0.0001f) {
                postSettingsNoAo.fogSunDirection = -glm::normalize(mainLightDir);
            }
        }
        float deltaTime = ImGui::GetIO().DeltaTime;
        SlipPostProcess::Get().Apply(m_ColorTexture, m_DepthTexture, m_NormalTexture,
                                     view, projection, glm::inverse(projection),
                                     deltaTime, postSettingsNoAo, m_PresentFBO, m_FboWidth, m_FboHeight);

        // Blit del buffer de profundidad y stencil de m_FBO a m_PresentFBO para oclusión de gizmos y editor
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_FBO);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_PresentFBO);
        glBlitFramebuffer(0, 0, m_FboWidth, m_FboHeight,
                          0, 0, m_FboWidth, m_FboHeight,
                          GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, m_PresentFBO);
        glViewport(0, 0, m_FboWidth, m_FboHeight);
        SlipDrawBuffer(GL_COLOR_ATTACHMENT0);
    }

    // 4. Outliner de silueta en Screen-Space (~2.5px uniforme alrededor del contorno real) en Azul ImGui (#4296FA)
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);

    if (m_SelectedEntity && m_OutlineFBO != 0 && m_OutlineTexture != 0 && m_QuadVAO != 0) {
        auto selMesh = m_SelectedEntity->GetComponent<MeshComponent>();
        auto selSkinned = m_SelectedEntity->GetComponent<SkinnedMeshComponent>();
        auto selPrim = dynamic_cast<SlipPrimitiveEntity*>(m_SelectedEntity);
        if ((selMesh && selMesh->IsEnabled() && selMesh->model) ||
            (selSkinned && selSkinned->IsEnabled() && selSkinned->model) ||
            (selPrim && selPrim->GetVAO() != 0)) {
            SlipShader* unlitShader = SlipShaderLibrary::Get("Unlit");
            SlipShader* outlinePostShader = SlipShaderLibrary::Get("OutlinePost");
            if (unlitShader && outlinePostShader) {
                uint32_t targetFBO = (m_PresentFBO != 0) ? m_PresentFBO : m_FBO;

                // 4.1 Renderizar silueta blanca del objeto seleccionado en m_OutlineFBO (R8) usando exactamente
                // los mismos clusters visibles ya fusionados (selectedEntityVisRuns) para obtener la silueta 100% exacta en ~1-5 Draw Calls
                glBindFramebuffer(GL_FRAMEBUFFER, m_OutlineFBO);
                glViewport(0, 0, m_FboWidth, m_FboHeight);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                glDisable(GL_CULL_FACE);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                glm::mat4 modelMat = m_SelectedEntity->getMatrix();
                unlitShader->use();
                unlitShader->setMat4("u_Projection", projection);
                unlitShader->setMat4("u_View", view);
                unlitShader->setMat4("u_Model", modelMat);
                unlitShader->setVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);
                unlitShader->setBool("u_HasTexture", false);
                unlitShader->setBool("u_ToLinear", false);
                unlitShader->setFloat("u_OutlineWidth", 0.0f);

                SlipMesh* om = nullptr;
                if (selMesh && selMesh->IsEnabled() && selMesh->model) om = selMesh->model;
                else if (selSkinned && selSkinned->IsEnabled() && selSkinned->model) om = selSkinned->model;

                if (om) {
                    if (selectedEntityVClustVAO != 0 && !selectedEntityVisRuns.empty()) {
                        glBindVertexArray(selectedEntityVClustVAO);
                        for (const auto& vc : selectedEntityVisRuns) {
                            if (vc.indexCount > 0) {
                                void* offset = (void*)(uintptr_t)(vc.indexOffset * sizeof(unsigned int));
                                glDrawElements(GL_TRIANGLES, vc.indexCount, GL_UNSIGNED_INT, offset);
                            }
                        }
                        glBindVertexArray(0);
                    } else if (om->hasClusterLOD && om->clusterVAO != 0 && !om->clusters.empty()) {
                        glBindVertexArray(om->clusterVAO);
                        uint32_t rootCnt = (std::min)(om->clusterHeader.rootClusterCount, static_cast<uint32_t>(om->clusters.size()));
                        for (uint32_t r = 0; r < rootCnt; ++r) {
                            const auto& rc = om->clusters[r];
                            if (rc.indexCount > 0) {
                                void* offset = (void*)(uintptr_t)(rc.indexOffset * sizeof(unsigned int));
                                glDrawElements(GL_TRIANGLES, rc.indexCount, GL_UNSIGNED_INT, offset);
                            }
                        }
                        glBindVertexArray(0);
                    } else {
                        for (const auto& sm : om->meshes) {
                            if (sm.VAO == 0) continue;
                            uint32_t cnt = om->isUnifiedBuffer ? sm.indexCount : static_cast<uint32_t>(sm.indices.size());
                            if (cnt == 0) continue;
                            glBindVertexArray(sm.VAO);
                            void* offset = om->isUnifiedBuffer ? (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int)) : 0;
                            glDrawElements(GL_TRIANGLES, cnt, GL_UNSIGNED_INT, offset);
                            glBindVertexArray(0);
                        }
                    }
                } else if (selPrim && selPrim->GetVAO() != 0) {
                    glBindVertexArray(selPrim->GetVAO());
                    glDrawElements(GL_TRIANGLES, selPrim->GetIndexCount(), GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
                }

                // 4.2 Componer borde de silueta de grosor constante en el FBO activo del Viewport
                glBindFramebuffer(GL_FRAMEBUFFER, targetFBO);
                glViewport(0, 0, m_FboWidth, m_FboHeight);
                glDisable(GL_CULL_FACE);
                glFrontFace(GL_CCW);
                glDisable(GL_DEPTH_TEST);
                glDepthMask(GL_FALSE);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                outlinePostShader->use();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, m_OutlineTexture);
                outlinePostShader->setInt("u_MaskTex", 0);
                outlinePostShader->setVec2("u_TexelSize",
                    1.0f / static_cast<float>((std::max)(1, m_FboWidth)),
                    1.0f / static_cast<float>((std::max)(1, m_FboHeight)));
                outlinePostShader->setVec4("u_Color", 0.26f, 0.59f, 0.98f, 1.0f);

                glBindVertexArray(m_QuadVAO);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glBindVertexArray(0);

                // Restaurar estado de renderizado
                glDisable(GL_BLEND);
                glDepthMask(GL_TRUE);
                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
    }
}
    }

    // 4.5 Renderizado de Sprites 2D en el Mundo
    RenderSprites(view, projection);

    // 5. Wireframes y Gizmos auxiliares en la vista del editor
    if (m_ShowGizmos && (m_ActiveCameraType == ViewportCameraType::EditorCamera)) {
        DrawColliderWireframe(view, projection);

        if (m_SelectedEntity) {
            if (auto camComp = m_SelectedEntity->GetComponent<CameraComponent>()) {
                DrawCameraFrustum(view, projection, camComp.get());
    }
            if (auto lightComp = m_SelectedEntity->GetComponent<LightComponent>()) {
                DrawLightGizmo(view, projection, lightComp.get());
    }
            if (auto volume = m_SelectedEntity->GetComponent<DDGIVolumeComponent>()) {
                DrawVolumeGizmo(view, projection, volume.get());
            }
            if (auto probe = m_SelectedEntity->GetComponent<ReflectionProbeComponent>()) {
                DrawReflectionProbeGizmo(view, projection, probe.get());
            }
            if (auto postVolume = m_SelectedEntity->GetComponent<PostProcessVolumeComponent>()) {
                DrawPostProcessGizmo(view, projection, postVolume.get());
    }
            if (auto psComp = m_SelectedEntity->GetComponent<ParticleSystemComponent>()) {
                DrawParticleGizmo(view, projection, psComp.get());
    }
}
    }

    if (m_VClustEnabled && m_VClustShowBounds && (m_ActiveCameraType == ViewportCameraType::EditorCamera)) {
        DrawVClustBoundsGizmo(view, projection, eye, frustum);
    }

    // 5.5 Gizmos y Depuración en tiempo real (SlipDebug: predicción de drift, telemetría de vehículo, ruedas, etc.)
    if (m_ShowGizmos) {
        SlipDebug::Get().draw(view, projection);
    } else {
        SlipDebug::Get().cleanLines();
    }

    // 6. Sistema UI Estilo Unity3D (Canvas + RectTransform + Widgets)
    RenderCanvasUI(view, projection);

    glUseProgram(0);

    glBindFramebuffer(GL_FRAMEBUFFER, last_fbo);
    glViewport(last_viewport[0], last_viewport[1], last_viewport[2], last_viewport[3]);
}

void ViewportPanel::RenderSprites(const glm::mat4& view, const glm::mat4& projection) {
    if (!m_ContextLevel || m_QuadVAO == 0) return;

    struct RenderItem2D {
        SlipEntity* entity = nullptr;
        SpriteRendererComponent* sprite = nullptr;
        TileGridComponent* tileGrid = nullptr;
        int layerIndex = 0;
        int orderInLayer = 0;
        float z = 0.0f;
    };

    std::vector<RenderItem2D> items;
    std::vector<std::pair<SlipEntity*, Light2DComponent*>> lights2D;
    std::vector<std::pair<SlipEntity*, ShadowCaster2DComponent*>> shadowCasters2D;

    for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
        if (!ent) continue;

        if (auto sr = ent->GetComponent<SpriteRendererComponent>()) {
            if (sr->IsEnabled()) {
                int layerIdx = SlipSortingLayers::Get().GetLayerIndex(sr->sortingLayerName);
                items.push_back({ ent, sr.get(), nullptr, layerIdx, sr->sortingOrder, ent->position.z });
    }
}
        if (auto tg = ent->GetComponent<TileGridComponent>()) {
            if (tg->IsEnabled()) {
                int layerIdx = SlipSortingLayers::Get().GetLayerIndex(tg->sortingLayerName);
                items.push_back({ ent, nullptr, tg.get(), layerIdx, tg->orderInLayer, ent->position.z });
    }
}
        if (auto l2d = ent->GetComponent<Light2DComponent>()) {
            if (l2d->IsEnabled()) {
                lights2D.push_back({ ent, l2d.get() });
    }
}
        if (auto sc = ent->GetComponent<ShadowCaster2DComponent>()) {
            if (sc->IsEnabled() && sc->castShadows) {
                shadowCasters2D.push_back({ ent, sc.get() });
    }
}
    }

    if (items.empty() && lights2D.empty()) return;

    // Ordenar elementos 2D: Capa de dibujo (Sorting Layer) -> Orden en capa (Order in Layer) -> Profundidad Z
    std::sort(items.begin(), items.end(), [](const RenderItem2D& a, const RenderItem2D& b) {
        if (a.layerIndex != b.layerIndex) return a.layerIndex < b.layerIndex;
        if (a.orderInLayer != b.orderInLayer) return a.orderInLayer < b.orderInLayer;
        return a.z < b.z;
    });

    SlipShader* spriteShader = SlipShaderLibrary::Get("Sprite2D");
    if (!spriteShader) spriteShader = SlipShaderLibrary::Get("Unlit");
    if (!spriteShader) return;

    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    spriteShader->use();
    spriteShader->setBool("u_ToLinear", false);
    spriteShader->setMat4("u_Projection", projection);
    spriteShader->setMat4("u_View", view);

    glBindVertexArray(m_QuadVAO);

    // Función auxiliar para chequear oclusión de sombra 2D ("colisión de luces")
    auto testShadowOcclusion = [&](const glm::vec2& lightPos, const glm::vec2& targetPos) -> float {
        glm::vec2 dir = targetPos - lightPos;
        float dist = glm::length(dir);
        if (dist < 0.001f) return 1.0f;
        glm::vec2 rayDir = dir / dist;

        for (const auto& casterPair : shadowCasters2D) {
            SlipEntity* cEnt = casterPair.first;
            ShadowCaster2DComponent* caster = casterPair.second;
            glm::vec2 cPos = glm::vec2(cEnt->position.x + caster->offset.x, cEnt->position.y + caster->offset.y);

            if (caster->shape == ShadowCasterShape2D::Circle) {
                glm::vec2 toCircle = cPos - lightPos;
                float proj = glm::dot(toCircle, rayDir);
                if (proj > 0.1f && proj < dist) {
                    glm::vec2 closest = lightPos + rayDir * proj;
                    if (glm::length(cPos - closest) <= caster->radius) {
                        return 0.15f; // Oclusión de sombra
            }
        }
    } else {
                glm::vec2 h = caster->size * 0.5f;
                glm::vec2 bMin = cPos - h;
                glm::vec2 bMax = cPos + h;

                float tMin = 0.0f;
                float tMax = dist;
                bool hit = true;
                for (int d = 0; d < 2; ++d) {
                    float invD = (std::abs(rayDir[d]) > 0.0001f) ? (1.0f / rayDir[d]) : 100000.0f;
                    float t0 = (bMin[d] - lightPos[d]) * invD;
                    float t1 = (bMax[d] - lightPos[d]) * invD;
                    if (invD < 0.0f) std::swap(t0, t1);
                    tMin = (std::max)(tMin, t0);
                    tMax = (std::min)(tMax, t1);
                    if (tMax < tMin) { hit = false; break; }
        }
                if (hit && tMin > 0.1f && tMin < dist) {
                    return 0.15f; // Oclusión de sombra
        }
    }
}
        return 1.0f;
    };

    m_FrustumCulled2DCount = 0;
    m_Visible2DSpritesCount = 0;
    glm::mat4 vp = projection * view;

    for (const auto& item : items) {
        if (item.sprite) {
            glm::vec2 baseSize = item.sprite->GetBaseQuadSize();
            glm::vec3 worldPos = item.entity->position;
            float halfW = baseSize.x * 0.5f * std::abs(item.entity->scale.x);
            float halfH = baseSize.y * 0.5f * std::abs(item.entity->scale.y);

            // 2D Frustum Culling
            if (m_FrustumCulling2D) {
                glm::vec4 c0 = vp * glm::vec4(worldPos.x - halfW, worldPos.y - halfH, worldPos.z, 1.0f);
                glm::vec4 c1 = vp * glm::vec4(worldPos.x + halfW, worldPos.y - halfH, worldPos.z, 1.0f);
                glm::vec4 c2 = vp * glm::vec4(worldPos.x + halfW, worldPos.y + halfH, worldPos.z, 1.0f);
                glm::vec4 c3 = vp * glm::vec4(worldPos.x - halfW, worldPos.y + halfH, worldPos.z, 1.0f);

                bool allLeft = (c0.x < -c0.w && c1.x < -c1.w && c2.x < -c2.w && c3.x < -c3.w);
                bool allRight = (c0.x > c0.w && c1.x > c1.w && c2.x > c2.w && c3.x > c3.w);
                bool allBottom = (c0.y < -c0.w && c1.y < -c1.w && c2.y < -c2.w && c3.y < -c3.w);
                bool allTop = (c0.y > c0.w && c1.y > c1.w && c2.y > c2.w && c3.y > c3.w);

                if (allLeft || allRight || allBottom || allTop) {
                    m_FrustumCulled2DCount++;
                    continue;
                }
            }
            m_Visible2DSpritesCount++;

            if (item.sprite->isPixelArt && item.sprite->texture) {
                item.sprite->texture->SetFilterMode(true);
            }

            glm::mat4 modelMat = item.entity->getMatrix() * glm::scale(glm::mat4(1.0f), glm::vec3(baseSize.x, baseSize.y, 1.0f));
            glm::vec4 uvRect = item.sprite->GetUVRect();

            // Sombreado 2D dinámico (Lit 2D) con luces 2D y oclusión de sombras
            glm::vec4 finalColor = item.sprite->color;
            if (item.sprite->isLit) {
                glm::vec3 ambient2D = glm::vec3(0.35f, 0.35f, 0.40f);
                glm::vec3 accumulatedLight(0.0f);
                glm::vec2 spritePos(item.entity->position.x, item.entity->position.y);

                for (const auto& lPair : lights2D) {
                    SlipEntity* lEnt = lPair.first;
                    Light2DComponent* light = lPair.second;
                    glm::vec2 lPos(lEnt->position.x, lEnt->position.y);
                    float d = glm::distance(spritePos, lPos);

                    if (d <= light->radius) {
                        float atten = glm::clamp(1.0f - (d - light->innerRadius) / ((std::max)(0.001f, light->radius - light->innerRadius)), 0.0f, 1.0f);
                        float shadowFactor = 1.0f;
                        if (light->castShadows) {
                            shadowFactor = testShadowOcclusion(lPos, spritePos);
                            shadowFactor = 1.0f - (1.0f - shadowFactor) * light->shadowIntensity;
                }
                        accumulatedLight += light->color * (light->intensity * atten * shadowFactor);
            }
        }
                finalColor.r *= glm::clamp(ambient2D.r + accumulatedLight.r, 0.0f, 2.5f);
                finalColor.g *= glm::clamp(ambient2D.g + accumulatedLight.g, 0.0f, 2.5f);
                finalColor.b *= glm::clamp(ambient2D.b + accumulatedLight.b, 0.0f, 2.5f);
    }

            spriteShader->setMat4("u_Model", modelMat);
            spriteShader->setVec4("u_Color", finalColor.r, finalColor.g, finalColor.b, finalColor.a);
            spriteShader->setVec4("u_UVRect", uvRect.x, uvRect.y, uvRect.z, uvRect.w);
            spriteShader->setBool("u_FlipX", item.sprite->flipX);
            spriteShader->setBool("u_FlipY", item.sprite->flipY);

            bool hasTex = (item.sprite->texture != nullptr && item.sprite->texture->getID() != 0);
            spriteShader->setBool("u_HasTexture", hasTex);
            if (hasTex) {
                spriteShader->setInt("u_MainTexture", 0);
                item.sprite->texture->draw();
    }

            glDrawArrays(GL_TRIANGLES, 0, 6);
}
        else if (item.tileGrid) {
            // Renderizado de TileGrid 2D
            if (item.tileGrid->isPixelArt && item.tileGrid->tileSheet) {
                item.tileGrid->tileSheet->SetFilterMode(true);
            }
            bool hasTileTex = (item.tileGrid->tileSheet != nullptr && item.tileGrid->tileSheet->getID() != 0);
            spriteShader->setBool("u_HasTexture", hasTileTex);
            if (hasTileTex) {
                spriteShader->setInt("u_MainTexture", 0);
                item.tileGrid->tileSheet->draw();
            }
            spriteShader->setBool("u_FlipX", false);
            spriteShader->setBool("u_FlipY", false);

            for (const auto& [coord, tileIdx] : item.tileGrid->tiles) {
                glm::vec2 worldPos = item.tileGrid->CellToWorld(coord.first, coord.second);
                float halfW = item.tileGrid->cellSize.x * 0.5f;
                float halfH = item.tileGrid->cellSize.y * 0.5f;

                if (m_FrustumCulling2D) {
                    glm::vec4 c0 = vp * glm::vec4(worldPos.x - halfW, worldPos.y - halfH, item.entity->position.z, 1.0f);
                    glm::vec4 c1 = vp * glm::vec4(worldPos.x + halfW, worldPos.y - halfH, item.entity->position.z, 1.0f);
                    glm::vec4 c2 = vp * glm::vec4(worldPos.x + halfW, worldPos.y + halfH, item.entity->position.z, 1.0f);
                    glm::vec4 c3 = vp * glm::vec4(worldPos.x - halfW, worldPos.y + halfH, item.entity->position.z, 1.0f);

                    bool allLeft = (c0.x < -c0.w && c1.x < -c1.w && c2.x < -c2.w && c3.x < -c3.w);
                    bool allRight = (c0.x > c0.w && c1.x > c1.w && c2.x > c2.w && c3.x > c3.w);
                    bool allBottom = (c0.y < -c0.w && c1.y < -c1.w && c2.y < -c2.w && c3.y < -c3.w);
                    bool allTop = (c0.y > c0.w && c1.y > c1.w && c2.y > c2.w && c3.y > c3.w);

                    if (allLeft || allRight || allBottom || allTop) {
                        m_FrustumCulled2DCount++;
                        continue;
                    }
                }
                m_Visible2DSpritesCount++;

                glm::mat4 tileModel = glm::translate(glm::mat4(1.0f), glm::vec3(worldPos.x, worldPos.y, item.entity->position.z))
                                    * glm::scale(glm::mat4(1.0f), glm::vec3(item.tileGrid->cellSize.x, item.tileGrid->cellSize.y, 1.0f));
                glm::vec4 uvRect = item.tileGrid->GetTileUV(tileIdx);

                spriteShader->setMat4("u_Model", tileModel);
                spriteShader->setVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);
                spriteShader->setVec4("u_UVRect", uvRect.x, uvRect.y, uvRect.z, uvRect.w);

                glDrawArrays(GL_TRIANGLES, 0, 6);
            }
}
    }

    // Renderizar halos / auras visuales de Luces 2D en modo aditivo
    if (!lights2D.empty()) {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        spriteShader->setBool("u_HasTexture", false);
        spriteShader->setBool("u_FlipX", false);
        spriteShader->setBool("u_FlipY", false);
        spriteShader->setVec4("u_UVRect", 0.0f, 0.0f, 1.0f, 1.0f);

        for (const auto& [lEnt, light] : lights2D) {
            float dia = light->radius * 2.0f;
            glm::mat4 lightModel = glm::translate(glm::mat4(1.0f), glm::vec3(lEnt->position.x, lEnt->position.y, lEnt->position.z + 0.01f))
                                 * glm::scale(glm::mat4(1.0f), glm::vec3(dia, dia, 1.0f));
            glm::vec4 lCol = glm::vec4(light->color.r, light->color.g, light->color.b, glm::clamp(light->intensity * 0.18f, 0.0f, 0.45f));

            spriteShader->setMat4("u_Model", lightModel);
            spriteShader->setVec4("u_Color", lCol.r, lCol.g, lCol.b, lCol.a);
            glDrawArrays(GL_TRIANGLES, 0, 6);
}

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glBindVertexArray(0);

    // Contorno de selección para Sprite 2D o TileGrid seleccionado en modo Editor
    if (m_SelectedEntity && m_ActiveCameraType == ViewportCameraType::EditorCamera) {
        if (auto selSprite = m_SelectedEntity->GetComponent<SpriteRendererComponent>()) {
            if (selSprite->IsEnabled()) {
                glm::vec2 baseSize = selSprite->GetBaseQuadSize();
                float hw = baseSize.x * 0.5f;
                float hh = baseSize.y * 0.5f;
                std::vector<glm::vec3> rectLines = {
                    glm::vec3(-hw, -hh, 0.0f), glm::vec3( hw, -hh, 0.0f),
                    glm::vec3( hw, -hh, 0.0f), glm::vec3( hw,  hh, 0.0f),
                    glm::vec3( hw,  hh, 0.0f), glm::vec3(-hw,  hh, 0.0f),
                    glm::vec3(-hw,  hh, 0.0f), glm::vec3(-hw, -hh, 0.0f)
        };
                SlipShader* lineShader = SlipShaderLibrary::Get("Line");
                if (lineShader) {
                    lineShader->use();
                    lineShader->setMat4("u_Projection", projection);
                    lineShader->setMat4("u_View", view);
                    lineShader->setMat4("u_Model", m_SelectedEntity->getMatrix());
                    lineShader->setVec4("u_Color", 0.26f, 0.59f, 0.98f, 1.0f);
                    if (m_LineVAO == 0) {
                        glGenVertexArrays(1, &m_LineVAO);
                        glGenBuffers(1, &m_LineVBO);
            }
                    glBindVertexArray(m_LineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
                    glBufferData(GL_ARRAY_BUFFER, rectLines.size() * sizeof(glm::vec3), rectLines.data(), GL_DYNAMIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
                    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(rectLines.size()));
                    glBindVertexArray(0);
        }
    }
}
        else if (auto selTg = m_SelectedEntity->GetComponent<TileGridComponent>()) {
            if (selTg->IsEnabled()) {
                // Dibujar cuadrícula de guía para el TileGrid seleccionado
                std::vector<glm::vec3> gridLines;
                int span = 8;
                glm::vec2 cSize = selTg->cellSize;
                glm::vec2 off = selTg->GetAnchorOffset();
                for (int x = -span; x <= span; ++x) {
                    gridLines.push_back(glm::vec3(x * cSize.x + off.x, -span * cSize.y + off.y, 0.0f));
                    gridLines.push_back(glm::vec3(x * cSize.x + off.x,  span * cSize.y + off.y, 0.0f));
        }
                for (int y = -span; y <= span; ++y) {
                    gridLines.push_back(glm::vec3(-span * cSize.x + off.x, y * cSize.y + off.y, 0.0f));
                    gridLines.push_back(glm::vec3( span * cSize.x + off.x, y * cSize.y + off.y, 0.0f));
        }
                // Cruz de anclaje (Anchor Point)
                gridLines.push_back(glm::vec3(-0.3f, 0.0f, 0.01f));
                gridLines.push_back(glm::vec3( 0.3f, 0.0f, 0.01f));
                gridLines.push_back(glm::vec3(0.0f, -0.3f, 0.01f));
                gridLines.push_back(glm::vec3(0.0f,  0.3f, 0.01f));

                SlipShader* lineShader = SlipShaderLibrary::Get("Line");
                if (lineShader) {
                    lineShader->use();
                    lineShader->setMat4("u_Projection", projection);
                    lineShader->setMat4("u_View", view);
                    lineShader->setMat4("u_Model", m_SelectedEntity->getMatrix());
                    lineShader->setVec4("u_Color", 0.3f, 0.85f, 1.0f, 0.65f);
                    if (m_LineVAO == 0) {
                        glGenVertexArrays(1, &m_LineVAO);
                        glGenBuffers(1, &m_LineVBO);
            }
                    glBindVertexArray(m_LineVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
                    glBufferData(GL_ARRAY_BUFFER, gridLines.size() * sizeof(glm::vec3), gridLines.data(), GL_DYNAMIC_DRAW);
                    glEnableVertexAttribArray(0);
                    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
                    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(gridLines.size()));
                    glBindVertexArray(0);
        }
    }
}
    }

    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}

void ViewportPanel::RenderCanvasUI(const glm::mat4& view, const glm::mat4& projection) {
    if (!m_ContextLevel || m_QuadVAO == 0 || m_FboWidth <= 0 || m_FboHeight <= 0) return;

    std::vector<SlipEntity*> canvasEntities;
    for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
        if (!ent) continue;
        auto cv = ent->GetComponent<CanvasComponent>();
        if (cv && cv->IsEnabled()) {
            canvasEntities.push_back(ent);
}
    }
    if (canvasEntities.empty()) return;

    std::sort(canvasEntities.begin(), canvasEntities.end(), [](SlipEntity* a, SlipEntity* b) {
        auto ca = a->GetComponent<CanvasComponent>();
        auto cb = b->GetComponent<CanvasComponent>();
        return (ca ? ca->sortingOrder : 0) < (cb ? cb->sortingOrder : 0);
    });

    bool isPlayMode = (SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession());
    bool useOverlay = isPlayMode || (m_ActiveCameraType == ViewportCameraType::GameCamera);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    bool androidTouch = isPlayMode;
#else
    bool androidTouch = false;
#endif

    SlipShader* spriteShader = SlipShaderLibrary::Get("Sprite2D");
    if (!spriteShader) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);

    // Helper para dibujar un rectángulo 2D en coordenadas de Canvas (0,0 arriba-izquierda, +Y hacia abajo)
    auto drawCanvasQuad = [&](const glm::mat4& uiProj, const glm::mat4& uiView,
                              const glm::vec4& rect, float worldScale, const glm::vec3& canvasOrigin,
                              bool overlayMode, const glm::vec4& col, SlipTexture* tex) {
        spriteShader->use();
        spriteShader->setMat4("u_Projection", uiProj);
        spriteShader->setMat4("u_View", uiView);
        spriteShader->setVec4("u_Color", col.r, col.g, col.b, col.a);
        spriteShader->setVec4("u_UVRect", 0.0f, 0.0f, 1.0f, 1.0f);
        spriteShader->setBool("u_FlipX", false);
        spriteShader->setBool("u_FlipY", overlayMode);

        bool hasTex = (tex != nullptr && tex->getID() != 0);
        spriteShader->setBool("u_HasTexture", hasTex);
        if (hasTex) {
            spriteShader->setInt("u_MainTexture", 0);
            tex->draw();
}

        glm::mat4 model(1.0f);
        if (overlayMode) {
            float cx = rect.x + rect.z * 0.5f;
            float cy = rect.y + rect.w * 0.5f;
            model = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(rect.z, rect.w, 1.0f));
} else {
            // En EditorCamera: convertir rect de Canvas (0..refW, 0..refH) a mundo 3D centrado en canvasOrigin
            float cx = canvasOrigin.x + rect.x * worldScale + (rect.z * worldScale) * 0.5f;
            float cy = canvasOrigin.y - rect.y * worldScale - (rect.w * worldScale) * 0.5f;
            model = glm::translate(glm::mat4(1.0f), glm::vec3(cx, cy, canvasOrigin.z)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(rect.z * worldScale, rect.w * worldScale, 1.0f));
}

        spriteShader->setMat4("u_Model", model);
        glBindVertexArray(m_QuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    };

    for (SlipEntity* canvasEnt : canvasEntities) {
        auto canvasComp = canvasEnt->GetComponent<CanvasComponent>();
        if (!canvasComp) continue;

        float refW = (std::max)(100.0f, canvasComp->referenceResolution.x);
        float refH = (std::max)(100.0f, canvasComp->referenceResolution.y);
        glm::vec4 rootRect(0.0f, 0.0f, refW, refH);

        bool renderAsOverlay = useOverlay && (canvasComp->renderMode == CanvasRenderMode::ScreenSpaceOverlay);
        glm::mat4 uiProj = projection;
        glm::mat4 uiView = view;
        float worldScale = 0.01f; // 100 px de Canvas = 1 unidad de mundo en el Editor
        glm::vec3 canvasTopLeftWorld = canvasEnt->position + glm::vec3(-refW * worldScale * 0.5f, refH * worldScale * 0.5f, 0.0f);

        if (renderAsOverlay) {
            uiProj = glm::ortho(0.0f, refW, refH, 0.0f, -10.0f, 10.0f);
            uiView = glm::mat4(1.0f);
} else if (m_ShowGizmos && m_ActiveCameraType == ViewportCameraType::EditorCamera) {
            // Dibujar el marco guía del Canvas en la vista del Editor (estilo Unity3D)
            float cw = refW * worldScale;
            float ch = refH * worldScale;
            glm::vec3 tl = canvasTopLeftWorld;
            glm::vec3 tr = tl + glm::vec3(cw, 0.0f, 0.0f);
            glm::vec3 br = tl + glm::vec3(cw, -ch, 0.0f);
            glm::vec3 bl = tl + glm::vec3(0.0f, -ch, 0.0f);
            std::vector<glm::vec3> frameLines = { tl, tr, tr, br, br, bl, bl, tl };

            SlipShader* lineShader = SlipShaderLibrary::Get("Line");
            if (lineShader) {
                lineShader->use();
                lineShader->setMat4("u_Projection", projection);
                lineShader->setMat4("u_View", view);
                lineShader->setMat4("u_Model", glm::mat4(1.0f));
                lineShader->setVec4("u_Color", 0.78f, 0.82f, 0.90f, 0.85f);
                if (m_LineVAO == 0) {
                    glGenVertexArrays(1, &m_LineVAO);
                    glGenBuffers(1, &m_LineVBO);
        }
                glBindVertexArray(m_LineVAO);
                glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
                glBufferData(GL_ARRAY_BUFFER, frameLines.size() * sizeof(glm::vec3), frameLines.data(), GL_DYNAMIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(frameLines.size()));
                glBindVertexArray(0);
    }
}

        // Función recursiva para renderizar la jerarquía de elementos UI hijos del Canvas
        std::function<void(SlipEntity*, const glm::vec4&)> renderUIHierarchy = [&](SlipEntity* ent, const glm::vec4& parentRect) {
            if (!ent) return;
            glm::vec4 myRect = parentRect;

            if (auto rt = ent->GetComponent<RectTransformComponent>()) {
                if (rt->IsEnabled()) {
                    myRect = rt->CalculateCanvasRect(parentRect);

                    // Sincronizar posición en el mundo del Editor con el RectTransform para que el Gizmo se ubique sobre el elemento UI
                    if (!renderAsOverlay && m_ActiveGizmoAxis == -1) {
                        float cx = canvasTopLeftWorld.x + (myRect.x + myRect.z * 0.5f) * worldScale;
                        float cy = canvasTopLeftWorld.y - (myRect.y + myRect.w * 0.5f) * worldScale;
                        ent->position = glm::vec3(cx, cy, canvasEnt->position.z);
                        if (auto tc = ent->GetComponent<TransformComponent>()) {
                            tc->position = ent->position;
                }
            }

                    // 1. UIImageComponent
                    if (auto img = ent->GetComponent<UIImageComponent>()) {
                        if (img->IsEnabled()) {
                            drawCanvasQuad(uiProj, uiView, myRect, worldScale, canvasTopLeftWorld, renderAsOverlay, img->color, img->texture);
                }
            }

                    // 2. UIButtonComponent
                    if (auto btn = ent->GetComponent<UIButtonComponent>()) {
                        if (btn->IsEnabled()) {
                            glm::vec4 btnCol = btn->GetCurrentStateColor();
                            SlipTexture* btnTex = nullptr;
                            if (auto img = ent->GetComponent<UIImageComponent>()) {
                                btnTex = img->texture;
                                btnCol *= img->color;
                    }
                            drawCanvasQuad(uiProj, uiView, myRect, worldScale, canvasTopLeftWorld, renderAsOverlay, btnCol, btnTex);
                }
            }

                    // 3. UISliderComponent
                    if (auto sld = ent->GetComponent<UISliderComponent>()) {
                        if (sld->IsEnabled()) {
                            // Fondo (track)
                            float trackH = (std::max)(6.0f, myRect.w * 0.40f);
                            glm::vec4 trackRect(myRect.x, myRect.y + (myRect.w - trackH) * 0.5f, myRect.z, trackH);
                            drawCanvasQuad(uiProj, uiView, trackRect, worldScale, canvasTopLeftWorld, renderAsOverlay, sld->backgroundColor, nullptr);

                            // Relleno (fill)
                            float norm = sld->GetNormalizedValue();
                            if (norm > 0.001f) {
                                glm::vec4 fillRect(trackRect.x, trackRect.y, trackRect.z * norm, trackRect.w);
                                drawCanvasQuad(uiProj, uiView, fillRect, worldScale, canvasTopLeftWorld, renderAsOverlay, sld->fillColor, nullptr);
                    }

                            // Manija (handle)
                            float handleW = (std::max)(12.0f, myRect.w * 0.75f);
                            float handleH = myRect.w;
                            float handleX = myRect.x + norm * myRect.z - handleW * 0.5f;
                            glm::vec4 handleRect(handleX, myRect.y, handleW, handleH);
                            drawCanvasQuad(uiProj, uiView, handleRect, worldScale, canvasTopLeftWorld, renderAsOverlay, sld->handleColor, nullptr);
                }
            }

                    // 4. UIProgressBarComponent
                    if (auto pb = ent->GetComponent<UIProgressBarComponent>()) {
                        if (pb->IsEnabled()) {
                            drawCanvasQuad(uiProj, uiView, myRect, worldScale, canvasTopLeftWorld, renderAsOverlay, pb->backgroundColor, nullptr);
                            float norm = pb->GetNormalizedValue();
                            if (norm > 0.001f) {
                                glm::vec4 fillRect(myRect.x + 2.0f, myRect.y + 2.0f, (std::max)(0.0f, (myRect.z - 4.0f) * norm), (std::max)(0.0f, myRect.w - 4.0f));
                                drawCanvasQuad(uiProj, uiView, fillRect, worldScale, canvasTopLeftWorld, renderAsOverlay, pb->fillColor, nullptr);
                    }
                            if (pb->showPercentageText) {
                                int pct = static_cast<int>( std::round(norm * 100.0f) );
                                std::string pctStr = std::to_string(pct) + "%";
                                float fSize = (std::max)(12.0f, myRect.w * 0.55f);
                                glm::vec2 txtDim = SlipFontManager::Get().MeasureText(pctStr, "", fSize);
                                float tx = myRect.x + (myRect.z - txtDim.x) * 0.5f;
                                float ty = myRect.y + (myRect.w - txtDim.y) * 0.5f;
                                if (renderAsOverlay) {
                                    SlipFontManager::Get().DrawText2D(pctStr, "", fSize, glm::vec2(tx, ty), glm::vec4(1.0f), uiProj);
                        } else {
                                    glm::mat4 textScaleMat = glm::translate(glm::mat4(1.0f), canvasTopLeftWorld) *
                                                             glm::scale(glm::mat4(1.0f), glm::vec3(worldScale, -worldScale, 1.0f));
                                    SlipFontManager::Get().DrawText2D(pctStr, "", fSize, glm::vec2(tx, ty), glm::vec4(1.0f), projection * view * textScaleMat);
                        }
                    }
                }
            }

                    if (auto joy = ent->GetComponent<UIJoystickComponent>()) {
                        if (joy->IsEnabled()) {
                            bool show = !isPlayMode || androidTouch;
                            bool runtimeInvisible = isPlayMode && androidTouch && joy->mode == UIJoystickMode::Invisible;
                            if (show && !runtimeInvisible) {
                                glm::vec4 baseCol = joy->backgroundColor;
                                glm::vec4 knobCol = joy->knobColor;
                                if (!isPlayMode) {
                                    baseCol.a *= 0.45f;
                                    knobCol.a *= 0.45f;
                        }
                                glm::vec2 center(myRect.x + myRect.z * 0.5f, myRect.y + myRect.w * 0.5f);
                                if (joy->floatingActive && joy->mode != UIJoystickMode::Fixed) center = joy->floatingCenter;
                                if (!isPlayMode && joy->mode == UIJoystickMode::Invisible) {
                                    drawCanvasQuad(uiProj, uiView, myRect, worldScale, canvasTopLeftWorld, renderAsOverlay, baseCol, nullptr);
                                    float fSize = 18.0f;
                                    if (renderAsOverlay) {
                                        SlipFontManager::Get().DrawText2D("Invisible", "", fSize, glm::vec2(myRect.x + 8.0f, myRect.y + 8.0f), glm::vec4(1.0f), uiProj);
                            }
                        } else {
                                    float visual = (std::max)(32.0f, joy->travelRadius * 2.0f);
                                    glm::vec4 baseRect(center.x - visual * 0.5f, center.y - visual * 0.5f, visual, visual);
                                    drawCanvasQuad(uiProj, uiView, baseRect, worldScale, canvasTopLeftWorld, renderAsOverlay, baseCol, nullptr);
                                    float knob = (std::max)(18.0f, joy->travelRadius * 0.72f);
                                    glm::vec2 knobPos = center + joy->knobOffset;
                                    glm::vec4 knobRect(knobPos.x - knob * 0.5f, knobPos.y - knob * 0.5f, knob, knob);
                                    drawCanvasQuad(uiProj, uiView, knobRect, worldScale, canvasTopLeftWorld, renderAsOverlay, knobCol, nullptr);
                        }
                    }
                }
            }

                    if (auto actionBtn = ent->GetComponent<UIActionButtonComponent>()) {
                        if (actionBtn->IsEnabled()) {
                            bool show = !isPlayMode || androidTouch;
                            if (show) {
                                glm::vec4 col = actionBtn->isPressed ? actionBtn->pressedColor : actionBtn->normalColor;
                                if (!isPlayMode) col.a *= 0.45f;
                                drawCanvasQuad(uiProj, uiView, myRect, worldScale, canvasTopLeftWorld, renderAsOverlay, col, nullptr);
                                std::string label = actionBtn->actionName.empty() ? "Acción" : actionBtn->actionName;
                                float fSize = (std::max)(14.0f, myRect.w * 0.28f);
                                glm::vec2 txtDim = SlipFontManager::Get().MeasureText(label, "", fSize);
                                glm::vec2 txtPos(myRect.x + (myRect.z - txtDim.x) * 0.5f, myRect.y + (myRect.w - txtDim.y) * 0.5f);
                                glm::vec4 txtCol(1.0f, 1.0f, 1.0f, isPlayMode ? 1.0f : 0.7f);
                                if (renderAsOverlay) {
                                    SlipFontManager::Get().DrawText2D(label, "", fSize, txtPos, txtCol, uiProj);
                        } else {
                                    glm::mat4 textScaleMat = glm::translate(glm::mat4(1.0f), canvasTopLeftWorld) *
                                                             glm::scale(glm::mat4(1.0f), glm::vec3(worldScale, -worldScale, 1.0f));
                                    SlipFontManager::Get().DrawText2D(label, "", fSize, txtPos, txtCol, projection * view * textScaleMat);
                        }
                    }
                }
            }

                    // 5. UITextComponent
                    if (auto txt = ent->GetComponent<UITextComponent>()) {
                        if (txt->IsEnabled() && !txt->text.empty()) {
                            glm::vec2 txtDim = SlipFontManager::Get().MeasureText(txt->text, txt->fontPath, txt->fontSize);
                            float tx = myRect.x;
                            if (txt->alignH == UITextAlignH::Center) tx = myRect.x + (myRect.z - txtDim.x) * 0.5f;
                            else if (txt->alignH == UITextAlignH::Right) tx = myRect.x + myRect.z - txtDim.x;

                            float ty = myRect.y;
                            if (txt->alignV == UITextAlignV::Middle) ty = myRect.y + (myRect.w - txtDim.y) * 0.5f;
                            else if (txt->alignV == UITextAlignV::Bottom) ty = myRect.y + myRect.w - txtDim.y;

                            if (renderAsOverlay) {
                                SlipFontManager::Get().DrawText2D(txt->text, txt->fontPath, txt->fontSize, glm::vec2(tx, ty), txt->color, uiProj);
                    } else {
                                glm::mat4 textScaleMat = glm::translate(glm::mat4(1.0f), canvasTopLeftWorld) *
                                                         glm::scale(glm::mat4(1.0f), glm::vec3(worldScale, -worldScale, 1.0f));
                                SlipFontManager::Get().DrawText2D(txt->text, txt->fontPath, txt->fontSize, glm::vec2(tx, ty), txt->color, projection * view * textScaleMat);
                    }
                }
            }

                    // Contorno de selección para elemento UI seleccionado en el Editor
                    if (!renderAsOverlay && ent == m_SelectedEntity && m_ShowGizmos) {
                        glm::vec3 tl = canvasTopLeftWorld + glm::vec3(myRect.x * worldScale, -myRect.y * worldScale, 0.0f);
                        glm::vec3 tr = tl + glm::vec3(myRect.z * worldScale, 0.0f, 0.0f);
                        glm::vec3 br = tl + glm::vec3(myRect.z * worldScale, -myRect.w * worldScale, 0.0f);
                        glm::vec3 bl = tl + glm::vec3(0.0f, -myRect.w * worldScale, 0.0f);
                        std::vector<glm::vec3> selLines = { tl, tr, tr, br, br, bl, bl, tl };

                        SlipShader* lineShader = SlipShaderLibrary::Get("Line");
                        if (lineShader) {
                            lineShader->use();
                            lineShader->setMat4("u_Projection", projection);
                            lineShader->setMat4("u_View", view);
                            lineShader->setMat4("u_Model", glm::mat4(1.0f));
                            lineShader->setVec4("u_Color", 0.26f, 0.59f, 0.98f, 1.0f);
                            glBindVertexArray(m_LineVAO);
                            glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
                            glBufferData(GL_ARRAY_BUFFER, selLines.size() * sizeof(glm::vec3), selLines.data(), GL_DYNAMIC_DRAW);
                            glEnableVertexAttribArray(0);
                            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
                            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(selLines.size()));
                            glBindVertexArray(0);
                }
            }
        }
    }

            for (SlipEntity* child : ent->GetChildren()) {
                renderUIHierarchy(child, myRect);
    }
};

        renderUIHierarchy(canvasEnt, rootRect);
    }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

void ViewportPanel::ProcessUIInteraction() {
    if (!m_ContextLevel || m_ViewportSize.x <= 1.0f || m_ViewportSize.y <= 1.0f) return;

    bool isPlayMode = (SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession());
    bool isGameCam = (m_ActiveCameraType == ViewportCameraType::GameCamera);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    bool androidTouch = isPlayMode;
#else
    bool androidTouch = false;
#endif

    if (!androidTouch) {
        std::function<void(SlipEntity*)> resetTouch = [&](SlipEntity* ent) {
            if (!ent) return;
            if (auto joy = ent->GetComponent<UIJoystickComponent>()) {
                joy->value = glm::vec2(0.0f);
                joy->knobOffset = glm::vec2(0.0f);
                joy->floatingActive = false;
                joy->capturedFinger = -1;
    }
            if (auto actionBtn = ent->GetComponent<UIActionButtonComponent>()) {
                actionBtn->isPressed = false;
                actionBtn->capturedFinger = -1;
    }
            for (SlipEntity* child : ent->GetChildren()) resetTouch(child);
};
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) resetTouch(ent);
    }

    if (!isPlayMode && !isGameCam) return;

    ImVec2 mousePos = ImGui::GetMousePos();
    float localX = mousePos.x - m_ViewportBounds[0].x;
    float localY = mousePos.y - m_ViewportBounds[0].y;
    bool insideViewport = (localX >= 0.0f && localX <= m_ViewportSize.x && localY >= 0.0f && localY <= m_ViewportSize.y);

    bool mouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool mouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
    bool mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);

    for (SlipEntity* canvasEnt : m_ContextLevel->GetEntities()) {
        if (!canvasEnt) continue;
        auto canvasComp = canvasEnt->GetComponent<CanvasComponent>();
        if (!canvasComp || !canvasComp->IsEnabled()) continue;

        float refW = (std::max)(100.0f, canvasComp->referenceResolution.x);
        float refH = (std::max)(100.0f, canvasComp->referenceResolution.y);
        glm::vec4 rootRect(0.0f, 0.0f, refW, refH);

        float canvasMouseX = (localX / m_ViewportSize.x) * refW;
        float canvasMouseY = (localY / m_ViewportSize.y) * refH;

        struct TouchPoint { int64_t id; float x; float y; };
        std::vector<TouchPoint> touchPoints;
        std::vector<int64_t> claimedFingers;
        if (androidTouch && SlipEditor::GetInstance()) {
            for (const TouchFingerState& finger : SlipEditor::GetInstance()->GetTouchFingers()) {
                float lx = finger.x - m_ViewportBounds[0].x;
                float ly = finger.y - m_ViewportBounds[0].y;
                if (lx < 0.0f || ly < 0.0f || lx > m_ViewportSize.x || ly > m_ViewportSize.y) continue;
                touchPoints.push_back({ finger.id, (lx / m_ViewportSize.x) * refW, (ly / m_ViewportSize.y) * refH });
    }
            std::function<void(SlipEntity*)> collectClaims = [&](SlipEntity* ent) {
                if (!ent) return;
                if (auto joy = ent->GetComponent<UIJoystickComponent>()) {
                    if (joy->capturedFinger >= 0) claimedFingers.push_back(joy->capturedFinger);
        }
                if (auto actionBtn = ent->GetComponent<UIActionButtonComponent>()) {
                    if (actionBtn->capturedFinger >= 0) claimedFingers.push_back(actionBtn->capturedFinger);
        }
                for (SlipEntity* child : ent->GetChildren()) collectClaims(child);
    };
            collectClaims(canvasEnt);
}
        auto fingerClaimed = [&](int64_t id) {
            for (int64_t claimed : claimedFingers) if (claimed == id) return true;
            return false;
};
        auto findPoint = [&](int64_t id) -> const TouchPoint* {
            for (const TouchPoint& point : touchPoints) if (point.id == id) return &point;
            return nullptr;
};

        std::function<void(SlipEntity*, const glm::vec4&)> processNode = [&](SlipEntity* ent, const glm::vec4& parentRect) {
            if (!ent) return;
            glm::vec4 myRect = parentRect;
            if (auto rt = ent->GetComponent<RectTransformComponent>()) {
                if (rt->IsEnabled()) {
                    myRect = rt->CalculateCanvasRect(parentRect);
                    bool hovered = insideViewport &&
                                   (canvasMouseX >= myRect.x && canvasMouseX <= myRect.x + myRect.z &&
                                    canvasMouseY >= myRect.y && canvasMouseY <= myRect.y + myRect.w);

                    if (auto btn = ent->GetComponent<UIButtonComponent>()) {
                        btn->clickedThisFrame = false;
                        if (btn->IsEnabled() && btn->interactable) {
                            btn->isHovered = hovered;
                            if (hovered && mouseClicked) {
                                btn->isPressed = true;
                    }
                            if (btn->isPressed && mouseReleased) {
                                if (hovered) {
                                    btn->TriggerClick();
                        }
                                btn->isPressed = false;
                    }
                            if (!mouseDown) {
                                btn->isPressed = false;
                    }
                } else {
                            btn->isHovered = false;
                            btn->isPressed = false;
                }
            }

                    if (auto sld = ent->GetComponent<UISliderComponent>()) {
                        if (sld->IsEnabled() && sld->interactable) {
                            if (hovered && mouseClicked) {
                                sld->isDragging = true;
                    }
                            if (!mouseDown) {
                                sld->isDragging = false;
                    }
                            if (sld->isDragging && myRect.z > 1.0f) {
                                float norm = (canvasMouseX - myRect.x) / myRect.z;
                                sld->SetNormalizedValue(norm, true);
                    }
                } else {
                            sld->isDragging = false;
                }
            }

                    if (auto joy = ent->GetComponent<UIJoystickComponent>()) {
                        if (!androidTouch || !joy->IsEnabled() || !joy->interactable) {
                            joy->value = glm::vec2(0.0f);
                            joy->knobOffset = glm::vec2(0.0f);
                            joy->floatingActive = false;
                            joy->capturedFinger = -1;
                } else {
                            glm::vec2 home(myRect.x + myRect.z * 0.5f, myRect.y + myRect.w * 0.5f);
                            if (joy->capturedFinger >= 0 && !findPoint(joy->capturedFinger)) {
                                joy->capturedFinger = -1;
                                joy->floatingActive = false;
                                joy->value = glm::vec2(0.0f);
                                joy->knobOffset = glm::vec2(0.0f);
                    }
                            if (joy->capturedFinger < 0) {
                                for (const TouchPoint& point : touchPoints) {
                                    if (fingerClaimed(point.id)) continue;
                                    float dx = point.x - home.x;
                                    float dy = point.y - home.y;
                                    bool inRect = point.x >= myRect.x && point.x <= myRect.x + myRect.z &&
                                                  point.y >= myRect.y && point.y <= myRect.y + myRect.w;
                                    if (inRect || std::sqrt(dx * dx + dy * dy) <= joy->activationRadius) {
                                        joy->capturedFinger = point.id;
                                        claimedFingers.push_back(point.id);
                                        joy->floatingCenter = (joy->mode == UIJoystickMode::Fixed) ? home : glm::vec2(point.x, point.y);
                                        joy->floatingActive = joy->mode != UIJoystickMode::Fixed;
                                        break;
                            }
                        }
                    }
                            if (const TouchPoint* point = findPoint(joy->capturedFinger)) {
                                glm::vec2 base = (joy->mode == UIJoystickMode::Fixed) ? home : joy->floatingCenter;
                                glm::vec2 delta(point->x - base.x, point->y - base.y);
                                float travel = (std::max)(joy->travelRadius, 1.0f);
                                float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
                                if (len > travel && joy->mode != UIJoystickMode::Fixed) {
                                    glm::vec2 dir = delta / len;
                                    joy->floatingCenter = glm::vec2(point->x, point->y) - dir * travel;
                                    base = joy->floatingCenter;
                                    delta = glm::vec2(point->x, point->y) - base;
                                    len = travel;
                        }
                                if (len > travel && len > 0.0001f) {
                                    delta *= travel / len;
                                    len = travel;
                        }
                                joy->knobOffset = delta;
                                joy->floatingActive = joy->mode != UIJoystickMode::Fixed;
                                joy->value = glm::vec2(delta.x / travel, -delta.y / travel);
                    }
                            SlipInputMap::Get().SetTouchStick(joy->stickName, joy->value.x, joy->value.y);
                }
            }

                    if (auto actionBtn = ent->GetComponent<UIActionButtonComponent>()) {
                        if (!androidTouch || !actionBtn->IsEnabled() || !actionBtn->interactable) {
                            actionBtn->isPressed = false;
                            actionBtn->capturedFinger = -1;
                } else {
                            if (actionBtn->capturedFinger >= 0 && !findPoint(actionBtn->capturedFinger)) {
                                actionBtn->capturedFinger = -1;
                                actionBtn->isPressed = false;
                    }
                            if (actionBtn->capturedFinger < 0) {
                                for (const TouchPoint& point : touchPoints) {
                                    if (fingerClaimed(point.id)) continue;
                                    bool inRect = point.x >= myRect.x && point.x <= myRect.x + myRect.z &&
                                                  point.y >= myRect.y && point.y <= myRect.y + myRect.w;
                                    if (inRect) {
                                        actionBtn->capturedFinger = point.id;
                                        claimedFingers.push_back(point.id);
                                        actionBtn->isPressed = true;
                                        break;
                            }
                        }
                    } else {
                                actionBtn->isPressed = true;
                    }
                            if (actionBtn->isPressed) {
                                SlipInputMap::Get().SetTouchAction(actionBtn->actionName, true);
                    }
                }
            }
        }
    }
            for (SlipEntity* child : ent->GetChildren()) {
                processNode(child, myRect);
    }
};

        processNode(canvasEnt, rootRect);
    }
}

void ViewportPanel::CleanUpRenderer() {
#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (SlipRHI::GetActiveAPI() == GraphicsAPI::Vulkan) {
        if (m_VkViewportDescriptor != nullptr) {
            ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(m_VkViewportDescriptor));
            m_VkViewportDescriptor = nullptr;
        }
        m_VkViewportGen = 0;
        return;
    }
#endif
    if (m_QuadVAO != 0) {
        glDeleteVertexArrays(1, &m_QuadVAO);
        m_QuadVAO = 0;
    }
    if (m_QuadVBO != 0) {
        glDeleteBuffers(1, &m_QuadVBO);
        m_QuadVBO = 0;
    }
    if (m_ParticleVAO != 0) {
        glDeleteVertexArrays(1, &m_ParticleVAO);
        m_ParticleVAO = 0;
    }
    if (m_ParticleVBO != 0) {
        glDeleteBuffers(1, &m_ParticleVBO);
        m_ParticleVBO = 0;
    }
    if (m_LineVAO != 0) {
        glDeleteVertexArrays(1, &m_LineVAO);
        m_LineVAO = 0;
    }
    if (m_LineVBO != 0) {
        glDeleteBuffers(1, &m_LineVBO);
        m_LineVBO = 0;
    }
    if (m_GridVAO != 0) {
        glDeleteVertexArrays(1, &m_GridVAO);
        m_GridVAO = 0;
    }
    if (m_GridVBO != 0) {
        glDeleteBuffers(1, &m_GridVBO);
        m_GridVBO = 0;
    }
    if (m_ShaderProgram != 0) {
        glDeleteProgram(m_ShaderProgram);
        m_ShaderProgram = 0;
    }
    if (m_ColorTexture != 0) {
        glDeleteTextures(1, &m_ColorTexture);
        m_ColorTexture = 0;
    }
    if (m_RBO != 0) {
        glDeleteRenderbuffers(1, &m_RBO);
        m_RBO = 0;
    }
    if (m_DepthTexture != 0) {
        glDeleteTextures(1, &m_DepthTexture);
        m_DepthTexture = 0;
    }
    if (m_NormalTexture != 0) {
        glDeleteTextures(1, &m_NormalTexture);
        m_NormalTexture = 0;
    }
    if (m_ColorCopy != 0) {
        glDeleteTextures(1, &m_ColorCopy);
        m_ColorCopy = 0;
    }
    if (m_PresentTexture != 0) {
        glDeleteTextures(1, &m_PresentTexture);
        m_PresentTexture = 0;
    }
    if (m_PresentDepthTexture != 0) {
        glDeleteTextures(1, &m_PresentDepthTexture);
        m_PresentDepthTexture = 0;
    }
    if (m_PresentFBO != 0) {
        glDeleteFramebuffers(1, &m_PresentFBO);
        m_PresentFBO = 0;
    }
    if (m_OutlineTexture != 0) {
        glDeleteTextures(1, &m_OutlineTexture);
        m_OutlineTexture = 0;
    }
    if (m_OutlineFBO != 0) {
        glDeleteFramebuffers(1, &m_OutlineFBO);
        m_OutlineFBO = 0;
    }
    if (m_SkyVAO != 0) {
        glDeleteVertexArrays(1, &m_SkyVAO);
        m_SkyVAO = 0;
    }
    if (m_ReflectionCubemap != 0) {
        glDeleteTextures(1, &m_ReflectionCubemap);
        m_ReflectionCubemap = 0;
    }
    if (m_ReflectionFBO != 0) {
        glDeleteFramebuffers(1, &m_ReflectionFBO);
        m_ReflectionFBO = 0;
    }
    if (m_ReflectionRBO != 0) {
        glDeleteRenderbuffers(1, &m_ReflectionRBO);
        m_ReflectionRBO = 0;
    }
    SlipPostProcess::Get().Shutdown();
    if (m_SSRVAO != 0) {
        glDeleteVertexArrays(1, &m_SSRVAO);
        m_SSRVAO = 0;
    }
    if (m_SSRTarget != 0) {
        glDeleteTextures(1, &m_SSRTarget);
        m_SSRTarget = 0;
    }
    if (m_SSRFBO != 0) {
        glDeleteFramebuffers(1, &m_SSRFBO);
        m_SSRFBO = 0;
    }
    m_SSRWidth = 0;
    m_SSRHeight = 0;
    if (m_FBO != 0) {
        glDeleteFramebuffers(1, &m_FBO);
        m_FBO = 0;
    }
    ClearStressTestInstances();
    m_RendererInitialized = false;
}

bool ViewportPanel::ProjectToScreen(const glm::vec3& worldPos, const glm::mat4& viewProj, glm::vec2& outScreenPos) {
    glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0f);
    if (clip.w <= 0.001f) return false;

    glm::vec3 ndc = glm::vec3(clip) / clip.w;
    // No descartar por ndc.z ni por bordes de pantalla para que el arrastre del gizmo sea 100% continuo
    float x = m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * m_ViewportSize.x;
    float y = m_ViewportBounds[0].y + (1.0f - (ndc.y * 0.5f + 0.5f)) * m_ViewportSize.y;
    outScreenPos = glm::vec2(x, y);
    return true;
}

SlipEntity* ViewportPanel::CastRayFromMouse(const glm::vec2& mouseScreenPos, const glm::mat4& view, const glm::mat4& projection) {
    if (!m_ContextLevel) return nullptr;
    if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f) return nullptr;

    float localX = mouseScreenPos.x - m_ViewportBounds[0].x;
    float localY = mouseScreenPos.y - m_ViewportBounds[0].y;
    if (localX < 0.0f || localX > m_ViewportSize.x || localY < 0.0f || localY > m_ViewportSize.y) {
        return nullptr;
    }

    float ndcX = (2.0f * localX) / m_ViewportSize.x - 1.0f;
    float ndcY = 1.0f - (2.0f * localY) / m_ViewportSize.y;

    glm::mat4 invVP = glm::inverse(projection * view);
    glm::vec4 pNear = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    if (std::abs(pNear.w) < 1e-6f) return nullptr;
    pNear /= pNear.w;

    glm::vec4 pFar = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    if (std::abs(pFar.w) < 1e-6f) return nullptr;
    pFar /= pFar.w;

    glm::vec3 rayOrigin = glm::vec3(pNear);
    glm::vec3 rayDir = glm::normalize(glm::vec3(pFar - pNear));

    SlipEntity* bestEntity = nullptr;
    float bestDist = 1e9f;

    auto intersectRayAABB = [](const glm::vec3& orig, const glm::vec3& dir,
                               const glm::vec3& bmin, const glm::vec3& bmax,
                               float& outTNear) -> bool {
        float tmin = -1e30f;
        float tmax =  1e30f;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dir[a]) < 1e-7f) {
                if (orig[a] < bmin[a] || orig[a] > bmax[a]) return false;
    } else {
                float invD = 1.0f / dir[a];
                float t0 = (bmin[a] - orig[a]) * invD;
                float t1 = (bmax[a] - orig[a]) * invD;
                if (t0 > t1) std::swap(t0, t1);
                tmin = (std::max)(tmin, t0);
                tmax = (std::min)(tmax, t1);
                if (tmax < tmin) return false;
    }
}
        if (tmax < 0.0f) return false;
        outTNear = (tmin >= 0.0f) ? tmin : tmax;
        return true;
    };

    auto intersectRayTriangle = [](const glm::vec3& orig, const glm::vec3& dir,
                                   const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                                   float& outT) -> bool {
        const float EPS = 1e-7f;
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        glm::vec3 h = glm::cross(dir, edge2);
        float a = glm::dot(edge1, h);
        if (std::abs(a) < EPS) return false;
        float f = 1.0f / a;
        glm::vec3 s = orig - v0;
        float u = f * glm::dot(s, h);
        if (u < 0.0f || u > 1.0f) return false;
        glm::vec3 q = glm::cross(s, edge1);
        float v = f * glm::dot(dir, q);
        if (v < 0.0f || u + v > 1.0f) return false;
        float t = f * glm::dot(edge2, q);
        if (t > EPS) {
            outT = t;
            return true;
}
        return false;
    };

    for (SlipEntity* entity : m_ContextLevel->GetEntities()) {
        if (!entity) continue;
        glm::vec3 pos = entity->position;
        glm::vec3 scale = entity->scale;

        // Soporte preciso de picking para elementos UI (RectTransform) y Sprites 2D en el Viewport
        if (auto rt = entity->GetComponent<RectTransformComponent>()) {
            if (std::abs(rayDir.z) > 1e-5f) {
                float tPlane = (pos.z - rayOrigin.z) / rayDir.z;
                if (tPlane > 0.0f) {
                    glm::vec3 hitPt = rayOrigin + rayDir * tPlane;
                    float hw = (rt->size.x * 0.01f) * 0.5f;
                    float hh = (rt->size.y * 0.01f) * 0.5f;
                    if (std::abs(hitPt.x - pos.x) <= hw && std::abs(hitPt.y - pos.y) <= hh) {
                        if (tPlane < bestDist) {
                            bestDist = tPlane;
                            bestEntity = entity;
                }
            }
        }
    }
            continue;
} else if (auto sr = entity->GetComponent<SpriteRendererComponent>()) {
            if (std::abs(rayDir.z) > 1e-5f) {
                float tPlane = (pos.z - rayOrigin.z) / rayDir.z;
                if (tPlane > 0.0f) {
                    glm::vec3 hitPt = rayOrigin + rayDir * tPlane;
                    glm::vec2 qSize = sr->GetBaseQuadSize();
                    float hw = (qSize.x * std::abs(scale.x)) * 0.5f;
                    float hh = (qSize.y * std::abs(scale.y)) * 0.5f;
                    if (std::abs(hitPt.x - pos.x) <= hw && std::abs(hitPt.y - pos.y) <= hh) {
                        if (tPlane < bestDist) {
                            bestDist = tPlane;
                            bestEntity = entity;
                        }
                    }
                }
            }
            continue;
        } else if (auto tg = entity->GetComponent<TileGridComponent>()) {
            if (std::abs(rayDir.z) > 1e-5f) {
                float tPlane = (pos.z - rayOrigin.z) / rayDir.z;
                if (tPlane > 0.0f) {
                    glm::vec3 hitPt = rayOrigin + rayDir * tPlane;
                    auto cell = tg->WorldToCell(glm::vec2(hitPt.x, hitPt.y));
                    if (tg->HasTile(cell.first, cell.second) || tg->tiles.empty()) {
                        if (tPlane < bestDist) {
                            bestDist = tPlane;
                            bestEntity = entity;
                        }
                    }
                }
            }
            continue;
        }

        auto meshComp = entity->GetComponent<MeshComponent>();
        auto skinnedComp = entity->GetComponent<SkinnedMeshComponent>();
        SlipMesh* model = nullptr;
        if (meshComp && meshComp->IsEnabled() && meshComp->model) {
            model = meshComp->model;
        } else if (skinnedComp && skinnedComp->IsEnabled() && skinnedComp->model) {
            model = skinnedComp->model;
        }
        SlipPrimitiveEntity* prim = dynamic_cast<SlipPrimitiveEntity*>(entity);
        bool hasValidMesh = (model != nullptr);

        // Ignorar volúmenes (PostProcessVolume / DDGIVolume) sin malla para que nunca bloqueen clics en la escena
        if (!hasValidMesh && !prim) {
            if (entity->HasComponent<PostProcessVolumeComponent>() ||
                entity->HasComponent<DDGIVolumeComponent>() ||
                entity->HasComponent<ReflectionProbeComponent>()) {
                continue;
    }
}

        // Selección exacta por Ray-AABB + Ray-Triangle para Mallas 3D y Primitivos
        if (hasValidMesh) {
            if (!model->hasBounds) {
                model->computeBounds();
    }

            glm::mat4 modelMat = entity->getMatrix();
            float det = glm::determinant(modelMat);
            if (std::abs(det) < 1e-8f) continue;
            glm::mat4 invModel = glm::inverse(modelMat);

            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
            glm::vec3 localDirRaw = glm::vec3(invModel * glm::vec4(rayDir, 0.0f));
            float dirLen = glm::length(localDirRaw);
            if (dirLen < 1e-7f) continue;
            glm::vec3 localDir = localDirRaw / dirLen;

            float aabbT = 0.0f;
            if (!intersectRayAABB(localOrigin, localDir, model->aabbMin, model->aabbMax, aabbT)) {
                continue;
    }

            float closestLocalT = 1e30f;
            bool hitTri = false;
            size_t totalTrisChecked = 0;

            size_t totalModelIndices = 0;
            for (const auto& sm : model->meshes) {
                totalModelIndices += sm.indices.empty() ? sm.vertices.size() : sm.indices.size();
            }

            // En modelos masivos (> 75,000 índices / 25,000 triángulos), evitar iterar millones de triángulos en CPU por clic
            if (totalModelIndices > 75000) {
                if (model->hasClusterLOD && !model->clusters.empty()) {
                    for (const auto& cl : model->clusters) {
                        if (cl.lodLevel != 0) continue; // Probar esferas delimitadoras de las hojas base (LOD 0)
                        glm::vec3 oc = localOrigin - cl.center;
                        float b = glm::dot(oc, localDir);
                        float c = glm::dot(oc, oc) - cl.radius * cl.radius;
                        float disc = b * b - c;
                        if (disc >= 0.0f) {
                            float tSph = -b - std::sqrt(disc);
                            if (tSph < 0.0f) tSph = 0.0f;
                            if (tSph < closestLocalT) {
                                closestLocalT = tSph;
                                hitTri = true;
                            }
                        }
                    }
                }
                if (!hitTri && aabbT >= 0.0f) {
                    closestLocalT = aabbT;
                    hitTri = true;
                }
            } else {
                for (const auto& sm : model->meshes) {
                    if (sm.vertices.empty()) continue;
                    if (!sm.indices.empty()) {
                        for (size_t i = 0; i + 2 < sm.indices.size(); i += 3) {
                            uint32_t i0 = sm.indices[i + 0];
                            uint32_t i1 = sm.indices[i + 1];
                            uint32_t i2 = sm.indices[i + 2];
                            if (i0 >= sm.vertices.size() || i1 >= sm.vertices.size() || i2 >= sm.vertices.size()) continue;
                            ++totalTrisChecked;
                            float tTri = 0.0f;
                            if (intersectRayTriangle(localOrigin, localDir,
                                                     sm.vertices[i0].Position,
                                                     sm.vertices[i1].Position,
                                                     sm.vertices[i2].Position, tTri)) {
                                if (tTri < closestLocalT) {
                                    closestLocalT = tTri;
                                    hitTri = true;
                                }
                            }
                        }
                    } else {
                        for (size_t i = 0; i + 2 < sm.vertices.size(); i += 3) {
                            ++totalTrisChecked;
                            float tTri = 0.0f;
                            if (intersectRayTriangle(localOrigin, localDir,
                                                     sm.vertices[i + 0].Position,
                                                     sm.vertices[i + 1].Position,
                                                     sm.vertices[i + 2].Position, tTri)) {
                                if (tTri < closestLocalT) {
                                    closestLocalT = tTri;
                                    hitTri = true;
                                }
                            }
                        }
                    }
                }
            }

            // Si la malla no tenía triángulos en CPU (caso raro), usar el impacto contra su AABB local
            if (!hitTri && totalTrisChecked == 0 && aabbT >= 0.0f) {
                closestLocalT = aabbT;
                hitTri = true;
    }

            if (hitTri) {
                glm::vec3 localHitPt = localOrigin + localDir * closestLocalT;
                glm::vec3 worldHitPt = glm::vec3(modelMat * glm::vec4(localHitPt, 1.0f));
                float worldDist = glm::length(worldHitPt - rayOrigin);
                if (worldDist < bestDist) {
                    bestDist = worldDist;
                    bestEntity = entity;
        }
    }
            continue;
} else if (prim) {
            glm::mat4 modelMat = entity->getMatrix();
            float det = glm::determinant(modelMat);
            if (std::abs(det) < 1e-8f) continue;
            glm::mat4 invModel = glm::inverse(modelMat);

            glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(rayOrigin, 1.0f));
            glm::vec3 localDirRaw = glm::vec3(invModel * glm::vec4(rayDir, 0.0f));
            float dirLen = glm::length(localDirRaw);
            if (dirLen < 1e-7f) continue;
            glm::vec3 localDir = localDirRaw / dirLen;

            glm::vec3 bmin(-0.5f), bmax(0.5f);
            if (prim->primitiveType == PrimitiveType::Plane) {
                bmin = glm::vec3(-2.0f, -0.02f, -2.0f);
                bmax = glm::vec3( 2.0f,  0.02f,  2.0f);
    }

            float localT = 0.0f;
            if (intersectRayAABB(localOrigin, localDir, bmin, bmax, localT)) {
                glm::vec3 localHitPt = localOrigin + localDir * localT;
                glm::vec3 worldHitPt = glm::vec3(modelMat * glm::vec4(localHitPt, 1.0f));
                float worldDist = glm::length(worldHitPt - rayOrigin);
                if (worldDist < bestDist) {
                    bestDist = worldDist;
                    bestEntity = entity;
        }
    }
            continue;
}

        // Luces, Cámaras y Emisores de Partículas sin malla: seleccionables con un radio compacto solo cuando Gizmos están visibles
        if (m_ShowGizmos && (entity->HasComponent<LightComponent>() || entity->HasComponent<CameraComponent>() || entity->HasComponent<ParticleSystemComponent>())) {
            const float gizmoPickRadius = 0.35f;
            glm::vec3 toCenter = pos - rayOrigin;
            float t = glm::dot(toCenter, rayDir);
            if (t < 0.0f) continue;

            glm::vec3 closestPoint = rayOrigin + rayDir * t;
            float distToCenter = glm::length(closestPoint - pos);
            if (distToCenter <= gizmoPickRadius && t < bestDist) {
                bestDist = t;
                bestEntity = entity;
    }
}
    }

    return bestEntity;
}

void ViewportPanel::DrawColliderWireframe(const glm::mat4& view, const glm::mat4& projection) {
    if (!m_SelectedEntity) return;

    bool has3D = m_SelectedEntity->GetComponent<ColliderComponent>() != nullptr;
    bool hasBox2D = m_SelectedEntity->GetComponent<BoxCollider2DComponent>() != nullptr;
    bool hasCirc2D = m_SelectedEntity->GetComponent<CircleCollider2DComponent>() != nullptr;
    bool hasLight2D = m_SelectedEntity->GetComponent<Light2DComponent>() != nullptr;
    bool hasShadow2D = m_SelectedEntity->GetComponent<ShadowCaster2DComponent>() != nullptr;

    if (!has3D && !hasBox2D && !hasCirc2D && !hasLight2D && !hasShadow2D) return;

    std::vector<glm::vec3> lineVertices;
    const float PI = 3.14159265358979323846f;

    if (auto boxCol = m_SelectedEntity->GetComponent<BoxColliderComponent>()) {
        glm::vec3 h = boxCol->size * 0.5f;
        glm::vec3 c = boxCol->centerOffset;

        glm::vec3 p0 = c + glm::vec3(-h.x, -h.y, -h.z);
        glm::vec3 p1 = c + glm::vec3( h.x, -h.y, -h.z);
        glm::vec3 p2 = c + glm::vec3( h.x,  h.y, -h.z);
        glm::vec3 p3 = c + glm::vec3(-h.x,  h.y, -h.z);
        glm::vec3 p4 = c + glm::vec3(-h.x, -h.y,  h.z);
        glm::vec3 p5 = c + glm::vec3( h.x, -h.y,  h.z);
        glm::vec3 p6 = c + glm::vec3( h.x,  h.y,  h.z);
        glm::vec3 p7 = c + glm::vec3(-h.x,  h.y,  h.z);

        // 12 aristas del Box
        lineVertices.push_back(p0); lineVertices.push_back(p1);
        lineVertices.push_back(p1); lineVertices.push_back(p2);
        lineVertices.push_back(p2); lineVertices.push_back(p3);
        lineVertices.push_back(p3); lineVertices.push_back(p0);

        lineVertices.push_back(p4); lineVertices.push_back(p5);
        lineVertices.push_back(p5); lineVertices.push_back(p6);
        lineVertices.push_back(p6); lineVertices.push_back(p7);
        lineVertices.push_back(p7); lineVertices.push_back(p4);

        lineVertices.push_back(p0); lineVertices.push_back(p4);
        lineVertices.push_back(p1); lineVertices.push_back(p5);
        lineVertices.push_back(p2); lineVertices.push_back(p6);
        lineVertices.push_back(p3); lineVertices.push_back(p7);
    }
    else if (auto sphereCol = m_SelectedEntity->GetComponent<SphereColliderComponent>()) {
        float r = sphereCol->radius;
        glm::vec3 c = sphereCol->centerOffset;
        const int segments = 36;
        const float step = (2.0f * PI) / segments;

        for (int i = 0; i < segments; ++i) {
            float a1 = i * step;
            float a2 = (i + 1) * step;

            // Círculo XY
            lineVertices.push_back(c + glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f));
            lineVertices.push_back(c + glm::vec3(std::cos(a2) * r, std::sin(a2) * r, 0.0f));

            // Círculo XZ
            lineVertices.push_back(c + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
            lineVertices.push_back(c + glm::vec3(std::cos(a2) * r, 0.0f, std::sin(a2) * r));

            // Círculo YZ
            lineVertices.push_back(c + glm::vec3(0.0f, std::cos(a1) * r, std::sin(a1) * r));
            lineVertices.push_back(c + glm::vec3(0.0f, std::cos(a2) * r, std::sin(a2) * r));
}
    }
    else if (auto capCol = m_SelectedEntity->GetComponent<CapsuleColliderComponent>()) {
        float r = capCol->radius;
        float halfH = capCol->height * 0.5f;
        glm::vec3 c = capCol->centerOffset;
        glm::vec3 topC = c + glm::vec3(0.0f, halfH, 0.0f);
        glm::vec3 botC = c - glm::vec3(0.0f, halfH, 0.0f);
        const int segments = 36;
        const float step = (2.0f * PI) / segments;

        // Círculos horizontales
        for (int i = 0; i < segments; ++i) {
            float a1 = i * step;
            float a2 = (i + 1) * step;
            lineVertices.push_back(topC + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
            lineVertices.push_back(topC + glm::vec3(std::cos(a2) * r, 0.0f, std::sin(a2) * r));

            lineVertices.push_back(botC + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
            lineVertices.push_back(botC + glm::vec3(std::cos(a2) * r, 0.0f, std::sin(a2) * r));
}

        // 4 líneas verticales
        lineVertices.push_back(topC + glm::vec3(r, 0.0f, 0.0f));
        lineVertices.push_back(botC + glm::vec3(r, 0.0f, 0.0f));
        lineVertices.push_back(topC + glm::vec3(-r, 0.0f, 0.0f));
        lineVertices.push_back(botC + glm::vec3(-r, 0.0f, 0.0f));
        lineVertices.push_back(topC + glm::vec3(0.0f, 0.0f, r));
        lineVertices.push_back(botC + glm::vec3(0.0f, 0.0f, r));
        lineVertices.push_back(topC + glm::vec3(0.0f, 0.0f, -r));
        lineVertices.push_back(botC + glm::vec3(0.0f, 0.0f, -r));

        // Arcos semicirculares superior e inferior
        for (int i = 0; i < segments / 2; ++i) {
            float a1 = i * step;
            float a2 = (i + 1) * step;
            lineVertices.push_back(topC + glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f));
            lineVertices.push_back(topC + glm::vec3(std::cos(a2) * r, std::sin(a2) * r, 0.0f));
            lineVertices.push_back(topC + glm::vec3(0.0f, std::sin(a1) * r, std::cos(a1) * r));
            lineVertices.push_back(topC + glm::vec3(0.0f, std::sin(a2) * r, std::cos(a2) * r));

            lineVertices.push_back(botC + glm::vec3(std::cos(a1) * r, -std::sin(a1) * r, 0.0f));
            lineVertices.push_back(botC + glm::vec3(std::cos(a2) * r, -std::sin(a2) * r, 0.0f));
            lineVertices.push_back(botC + glm::vec3(0.0f, -std::sin(a1) * r, std::cos(a1) * r));
            lineVertices.push_back(botC + glm::vec3(0.0f, -std::sin(a2) * r, std::cos(a2) * r));
}
    }
    else if (auto box2d = m_SelectedEntity->GetComponent<BoxCollider2DComponent>()) {
        glm::vec2 h = box2d->size * 0.5f;
        glm::vec2 c = box2d->offset;
        glm::vec3 p0(c.x - h.x, c.y - h.y, 0.005f);
        glm::vec3 p1(c.x + h.x, c.y - h.y, 0.005f);
        glm::vec3 p2(c.x + h.x, c.y + h.y, 0.005f);
        glm::vec3 p3(c.x - h.x, c.y + h.y, 0.005f);
        lineVertices.push_back(p0); lineVertices.push_back(p1);
        lineVertices.push_back(p1); lineVertices.push_back(p2);
        lineVertices.push_back(p2); lineVertices.push_back(p3);
        lineVertices.push_back(p3); lineVertices.push_back(p0);
    }
    else if (auto circ2d = m_SelectedEntity->GetComponent<CircleCollider2DComponent>()) {
        float r = circ2d->radius;
        glm::vec2 c = circ2d->offset;
        const int segments = 32;
        const float step = (2.0f * PI) / segments;
        for (int i = 0; i < segments; ++i) {
            float a1 = i * step;
            float a2 = (i + 1) * step;
            lineVertices.push_back(glm::vec3(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r, 0.005f));
            lineVertices.push_back(glm::vec3(c.x + std::cos(a2) * r, c.y + std::sin(a2) * r, 0.005f));
}
    }
    else if (auto l2d = m_SelectedEntity->GetComponent<Light2DComponent>()) {
        float r = l2d->radius;
        const int segments = 36;
        const float step = (2.0f * PI) / segments;
        for (int i = 0; i < segments; ++i) {
            float a1 = i * step;
            float a2 = (i + 1) * step;
            lineVertices.push_back(glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.005f));
            lineVertices.push_back(glm::vec3(std::cos(a2) * r, std::sin(a2) * r, 0.005f));
}
        if (l2d->innerRadius > 0.0f) {
            float ir = l2d->innerRadius;
            for (int i = 0; i < segments; ++i) {
                float a1 = i * step;
                float a2 = (i + 1) * step;
                lineVertices.push_back(glm::vec3(std::cos(a1) * ir, std::sin(a1) * ir, 0.005f));
                lineVertices.push_back(glm::vec3(std::cos(a2) * ir, std::sin(a2) * ir, 0.005f));
    }
}
        if (l2d->lightType == Light2DType::Spot) {
            float halfAngle = glm::radians(l2d->spotAngle * 0.5f);
            lineVertices.push_back(glm::vec3(0.0f, 0.0f, 0.005f));
            lineVertices.push_back(glm::vec3(std::sin(halfAngle) * r, -std::cos(halfAngle) * r, 0.005f));
            lineVertices.push_back(glm::vec3(0.0f, 0.0f, 0.005f));
            lineVertices.push_back(glm::vec3(-std::sin(halfAngle) * r, -std::cos(halfAngle) * r, 0.005f));
}
    }
    else if (auto sc2d = m_SelectedEntity->GetComponent<ShadowCaster2DComponent>()) {
        if (sc2d->shape == ShadowCasterShape2D::Box) {
            glm::vec2 h = sc2d->size * 0.5f;
            glm::vec2 c = sc2d->offset;
            glm::vec3 p0(c.x - h.x, c.y - h.y, 0.005f);
            glm::vec3 p1(c.x + h.x, c.y - h.y, 0.005f);
            glm::vec3 p2(c.x + h.x, c.y + h.y, 0.005f);
            glm::vec3 p3(c.x - h.x, c.y + h.y, 0.005f);
            lineVertices.push_back(p0); lineVertices.push_back(p1);
            lineVertices.push_back(p1); lineVertices.push_back(p2);
            lineVertices.push_back(p2); lineVertices.push_back(p3);
            lineVertices.push_back(p3); lineVertices.push_back(p0);
} else {
            float r = sc2d->radius;
            glm::vec2 c = sc2d->offset;
            const int segments = 32;
            const float step = (2.0f * PI) / segments;
            for (int i = 0; i < segments; ++i) {
                float a1 = i * step;
                float a2 = (i + 1) * step;
                lineVertices.push_back(glm::vec3(c.x + std::cos(a1) * r, c.y + std::sin(a1) * r, 0.005f));
                lineVertices.push_back(glm::vec3(c.x + std::cos(a2) * r, c.y + std::sin(a2) * r, 0.005f));
    }
}
    }

    if (lineVertices.empty()) return;

    glm::vec4 wireColor(0.15f, 1.0f, 0.35f, 1.0f);
    if (m_SelectedEntity->GetComponent<Light2DComponent>()) wireColor = glm::vec4(1.0f, 0.9f, 0.2f, 1.0f);
    else if (m_SelectedEntity->GetComponent<ShadowCaster2DComponent>()) wireColor = glm::vec4(1.0f, 0.5f, 0.1f, 1.0f);

    SlipShader* lineShader = SlipShaderLibrary::Get("Line");
    if (!lineShader) lineShader = SlipShaderLibrary::Get("Unlit");
    if (lineShader) {
        lineShader->use();
        glm::mat4 projMat = projection;
        glm::mat4 viewMat = view;
        glm::mat4 modelMat = m_SelectedEntity->getMatrix();
        lineShader->setMat4("u_Projection", projMat);
        lineShader->setMat4("u_View", viewMat);
        lineShader->setMat4("u_Model", modelMat);
        lineShader->setVec4("u_Color", wireColor.r, wireColor.g, wireColor.b, wireColor.a);
    } else if (m_ShaderProgram != 0) {
        glUseProgram(m_ShaderProgram);
        GLint uModelLoc = glGetUniformLocation(m_ShaderProgram, "u_Model");
        GLint uViewLoc = glGetUniformLocation(m_ShaderProgram, "u_View");
        GLint uProjLoc = glGetUniformLocation(m_ShaderProgram, "u_Projection");
        GLint uColorLoc = glGetUniformLocation(m_ShaderProgram, "u_Color");
        glm::mat4 modelMat = m_SelectedEntity->getMatrix();
        if (uModelLoc >= 0) glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(modelMat));
        if (uViewLoc >= 0) glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        if (uProjLoc >= 0) glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (uColorLoc >= 0) glUniform4f(uColorLoc, wireColor.r, wireColor.g, wireColor.b, wireColor.a);
    }

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (m_LineVAO == 0) {
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);
    }

    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(glm::vec3), lineVertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

void ViewportPanel::DrawCameraFrustum(const glm::mat4& view, const glm::mat4& projection, CameraComponent* cam) {
    if (!cam || !m_SelectedEntity) return;

    float aspect = (m_ViewportSize.y > 0.0f) ? (m_ViewportSize.x / m_ViewportSize.y) : 1.777f;
    std::vector<glm::vec3> frustumVerts;

    if (cam->projectionType == CameraProjection::Orthographic) {
        float h = cam->orthoSize;
        float w = h * aspect;
        float zNear = -cam->nearClip;
        float zFar = -(std::min)(cam->farClip, 50.0f);

        glm::vec3 nTL(-w,  h, zNear), nTR( w,  h, zNear), nBR( w, -h, zNear), nBL(-w, -h, zNear);
        glm::vec3 fTL(-w,  h, zFar),  fTR( w,  h, zFar),  fBR( w, -h, zFar),  fBL(-w, -h, zFar);

        // Near quad
        frustumVerts.push_back(nTL); frustumVerts.push_back(nTR);
        frustumVerts.push_back(nTR); frustumVerts.push_back(nBR);
        frustumVerts.push_back(nBR); frustumVerts.push_back(nBL);
        frustumVerts.push_back(nBL); frustumVerts.push_back(nTL);

        // Far quad
        frustumVerts.push_back(fTL); frustumVerts.push_back(fTR);
        frustumVerts.push_back(fTR); frustumVerts.push_back(fBR);
        frustumVerts.push_back(fBR); frustumVerts.push_back(fBL);
        frustumVerts.push_back(fBL); frustumVerts.push_back(fTL);

        // Connectors
        frustumVerts.push_back(nTL); frustumVerts.push_back(fTL);
        frustumVerts.push_back(nTR); frustumVerts.push_back(fTR);
        frustumVerts.push_back(nBR); frustumVerts.push_back(fBR);
        frustumVerts.push_back(nBL); frustumVerts.push_back(fBL);
    } else {
        // Perspective
        float radFov = glm::radians(cam->fov * 0.5f);
        float tanHalfFov = tan(radFov);

        float zNear = -cam->nearClip;
        float hNear = tanHalfFov * cam->nearClip;
        float wNear = hNear * aspect;

        float farDist = (std::min)(cam->farClip, 25.0f);
        if (farDist <= cam->nearClip) farDist = cam->nearClip + 1.0f;
        float zFar = -farDist;
        float hFar = tanHalfFov * farDist;
        float wFar = hFar * aspect;

        glm::vec3 origin(0.0f);
        glm::vec3 nTL(-wNear,  hNear, zNear), nTR( wNear,  hNear, zNear), nBR( wNear, -hNear, zNear), nBL(-wNear, -hNear, zNear);
        glm::vec3 fTL(-wFar,   hFar,  zFar),  fTR( wFar,   hFar,  zFar),  fBR( wFar,  -hFar,  zFar),  fBL(-wFar,  -hFar,  zFar);

        // Near quad
        frustumVerts.push_back(nTL); frustumVerts.push_back(nTR);
        frustumVerts.push_back(nTR); frustumVerts.push_back(nBR);
        frustumVerts.push_back(nBR); frustumVerts.push_back(nBL);
        frustumVerts.push_back(nBL); frustumVerts.push_back(nTL);

        // Far quad
        frustumVerts.push_back(fTL); frustumVerts.push_back(fTR);
        frustumVerts.push_back(fTR); frustumVerts.push_back(fBR);
        frustumVerts.push_back(fBR); frustumVerts.push_back(fBL);
        frustumVerts.push_back(fBL); frustumVerts.push_back(fTL);

        // Rays from origin to near and near to far
        frustumVerts.push_back(origin); frustumVerts.push_back(nTL);
        frustumVerts.push_back(origin); frustumVerts.push_back(nTR);
        frustumVerts.push_back(origin); frustumVerts.push_back(nBR);
        frustumVerts.push_back(origin); frustumVerts.push_back(nBL);
        frustumVerts.push_back(nTL); frustumVerts.push_back(fTL);
        frustumVerts.push_back(nTR); frustumVerts.push_back(fTR);
        frustumVerts.push_back(nBR); frustumVerts.push_back(fBR);
        frustumVerts.push_back(nBL); frustumVerts.push_back(fBL);

        // Marcador superior en el Near Plane indicando la orientación Up (estilo Unity 3D)
        float upH = hNear * 0.4f;
        glm::vec3 upPeak(0.0f, hNear + upH, zNear);
        frustumVerts.push_back(glm::vec3(-wNear * 0.4f, hNear, zNear));
        frustumVerts.push_back(upPeak);
        frustumVerts.push_back(upPeak);
        frustumVerts.push_back(glm::vec3( wNear * 0.4f, hNear, zNear));
    }

    if (frustumVerts.empty()) return;

    SlipShader* lineShader = SlipShaderLibrary::Get("Line");
    if (!lineShader) lineShader = SlipShaderLibrary::Get("Unlit");
    if (lineShader) {
        lineShader->use();
        glm::mat4 projMat = projection;
        glm::mat4 viewMat = view;
        glm::mat4 modelMat = m_SelectedEntity->getMatrix();
        lineShader->setMat4("u_Projection", projMat);
        lineShader->setMat4("u_View", viewMat);
        lineShader->setMat4("u_Model", modelMat);
        lineShader->setVec4("u_Color", 0.25f, 0.85f, 1.0f, 1.0f); // Cyan brillante estilo Unity
    } else if (m_ShaderProgram != 0) {
        glUseProgram(m_ShaderProgram);
        GLint uModelLoc = glGetUniformLocation(m_ShaderProgram, "u_Model");
        GLint uViewLoc = glGetUniformLocation(m_ShaderProgram, "u_View");
        GLint uProjLoc = glGetUniformLocation(m_ShaderProgram, "u_Projection");
        GLint uColorLoc = glGetUniformLocation(m_ShaderProgram, "u_Color");
        glm::mat4 modelMat = m_SelectedEntity->getMatrix();
        if (uModelLoc >= 0) glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(modelMat));
        if (uViewLoc >= 0) glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        if (uProjLoc >= 0) glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (uColorLoc >= 0) glUniform4f(uColorLoc, 0.25f, 0.85f, 1.0f, 1.0f);
    }

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (m_LineVAO == 0) {
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);
    }

    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, frustumVerts.size() * sizeof(glm::vec3), frustumVerts.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(frustumVerts.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

void ViewportPanel::DrawProceduralSky(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const glm::vec3& mainLightDir) {
    const auto& gfxSettings = SlipGraphicsSettings::Get().GetData();
    if (!gfxSettings.skyEnabled) return;

    SlipShader* shader = SlipShaderLibrary::Get("ProceduralSky");
    if (!shader) return;

    if (m_SkyVAO == 0) {
        float skyVerts[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             3.0f, -1.0f, 2.0f, 0.0f,
            -1.0f,  3.0f, 0.0f, 2.0f
};
        GLuint vbo = 0;
        glGenVertexArrays(1, &m_SkyVAO);
        glGenBuffers(1, &vbo);
        glBindVertexArray(m_SkyVAO);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(skyVerts), skyVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }

    shader->use();
    shader->setMat4("u_InvProj", glm::inverse(projection));
    shader->setMat4("u_InvView", glm::inverse(view));

    glm::vec3 zenithColor = glm::vec3(gfxSettings.skyZenithColor.r, gfxSettings.skyZenithColor.g, gfxSettings.skyZenithColor.b);
    glm::vec3 horizonColor = glm::vec3(gfxSettings.skyHorizonColor.r, gfxSettings.skyHorizonColor.g, gfxSettings.skyHorizonColor.b);
    glm::vec3 groundColor = glm::vec3(gfxSettings.skyGroundColor.r, gfxSettings.skyGroundColor.g, gfxSettings.skyGroundColor.b);
    glm::vec3 sunColor = glm::vec3(gfxSettings.sunColor.r, gfxSettings.sunColor.g, gfxSettings.sunColor.b);
    float sunIntensity = gfxSettings.sunIntensity;
    float sunGlow = gfxSettings.sunGlow;
    float sunsetFactor = 0.0f;
    float nightFactor = 0.0f;

    if (m_ContextLevel) {
        if (SlipSkyComponent* skyComp = SlipSkyComponent::FindActive(m_ContextLevel)) {
            if (skyComp->params.followDirectionalLight && glm::length(mainLightDir) > 0.0001f) {
                skyComp->params.sunDirection = -glm::normalize(mainLightDir);
            }
            if (skyComp->params.dynamicAtmosphere) {
                skyComp->UpdateAtmosphereFromSun();
                zenithColor = skyComp->currentAtmosphere.zenithColor;
                horizonColor = skyComp->currentAtmosphere.horizonColor;
                groundColor = skyComp->currentAtmosphere.groundColor;
                sunColor = skyComp->currentAtmosphere.sunColor;
                sunIntensity = skyComp->currentAtmosphere.sunIntensity;
                sunGlow = skyComp->currentAtmosphere.sunGlow;
                sunsetFactor = skyComp->currentAtmosphere.sunsetFactor;
                nightFactor = skyComp->currentAtmosphere.nightFactor;
            }
        }
    }

    shader->setVec3("u_ZenithColor", zenithColor.r, zenithColor.g, zenithColor.b);
    shader->setVec3("u_HorizonColor", horizonColor.r, horizonColor.g, horizonColor.b);
    shader->setVec3("u_GroundColor", groundColor.r, groundColor.g, groundColor.b);
    shader->setFloat("u_HorizonSharpness", gfxSettings.skyHorizonSharpness);
    shader->setBool("u_SunEnabled", gfxSettings.sunDiscEnabled);
    shader->setVec3("u_SunDir", -mainLightDir.x, -mainLightDir.y, -mainLightDir.z);
    shader->setVec3("u_SunColor", sunColor.r, sunColor.g, sunColor.b);
    shader->setFloat("u_SunSize", gfxSettings.sunSize);
    shader->setFloat("u_SunGlow", sunGlow);
    shader->setFloat("u_SunIntensity", sunIntensity);
    shader->setFloat("u_SunsetFactor", sunsetFactor);
    shader->setFloat("u_NightFactor", nightFactor);

    glDisable(GL_CULL_FACE);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_SkyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
}

void ViewportPanel::BakeReflectionCubemap(const glm::vec3& probePos, bool includeDynamic) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    (void)probePos; (void)includeDynamic;
#else
    const auto& gfxSettings = SlipGraphicsSettings::Get().GetData();
    if (!gfxSettings.reflectionProbeEnabled) return;

    int res = gfxSettings.reflectionProbeResolution;
    if (res < 64) res = 64;
    if (res > 1024) res = 1024;

    if (m_ReflectionCubemap == 0) {
        glGenTextures(1, &m_ReflectionCubemap);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_ReflectionCubemap);
        for (unsigned int i = 0; i < 6; ++i) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
}
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    }

    if (m_ReflectionFBO == 0) {
        glGenFramebuffers(1, &m_ReflectionFBO);
        glGenRenderbuffers(1, &m_ReflectionRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, m_ReflectionFBO);
        glBindRenderbuffer(GL_RENDERBUFFER, m_ReflectionRBO);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, res, res);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_ReflectionRBO);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 300.0f);
    glm::mat4 captureViews[6] = {
        glm::lookAt(probePos, probePos + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(probePos, probePos + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Desvincular el cubemap de todas las unidades de textura antes de renderizar a él para evitar feedback loops en GPU
    for (int t = 0; t < 8; ++t) {
        glActiveTexture(GL_TEXTURE0 + t);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_ReflectionFBO);
    glViewport(0, 0, res, res);
    GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBufs);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_STENCIL_TEST);

    SlipShader* litShader = SlipShaderLibrary::Get("Lit");
    glm::vec3 bakeLightDir = glm::normalize(glm::vec3(-0.4f, -0.9f, -0.4f));
    glm::vec3 bakeLightColor = glm::vec3(1.0f, 0.98f, 0.95f);
    float bakeLightIntensity = 1.0f;

    bool hasStatic = false;
    if (m_ContextLevel) {
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            if (ent->IsStatic()) {
                hasStatic = true;
    }
            auto lc = ent->GetComponent<LightComponent>();
            if (lc && lc->IsEnabled() && lc->type == LightType::Directional) {
                glm::vec3 rot = ent->GetTransform() ? ent->GetTransform()->rotation : ent->rotation;
                glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
                bakeLightDir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));
                bakeLightColor = lc->color;
                bakeLightIntensity = lc->intensity;
    }
}
    }

    for (unsigned int i = 0; i < 6; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, m_ReflectionCubemap, 0);
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Cielo procedimental en el fondo del cubemap
        DrawProceduralSky(captureViews[i], captureProj, probePos, bakeLightDir);

        if (m_ContextLevel && litShader) {
            litShader->use();
            litShader->setMat4("u_Projection", captureProj);
            litShader->setMat4("u_View", captureViews[i]);
            litShader->setVec3("u_CameraPos", probePos.x, probePos.y, probePos.z);
            litShader->setBool("u_HasEnvCube", false);
            litShader->setBool("u_DDGIEnabled", false);
            litShader->setBool("u_ShadowsEnabled", false);
            litShader->setInt("u_NumDirLights", 1);
            litShader->setVec3("u_DirLights[0].direction", bakeLightDir.x, bakeLightDir.y, bakeLightDir.z);
            litShader->setVec3("u_DirLights[0].color", bakeLightColor.x, bakeLightColor.y, bakeLightColor.z);
            litShader->setFloat("u_DirLights[0].intensity", bakeLightIntensity);
            litShader->setInt("u_NumPointLights", 0);
            litShader->setInt("u_NumSpotLights", 0);

            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                if (!ent) continue;
                // Si la escena ya tiene entidades estáticas marcadas y no se incluye dinámicos, filtrar
                if (hasStatic && !includeDynamic && !ent->IsStatic()) continue;

                auto mc = ent->GetComponent<MeshComponent>();
                SlipPrimitiveEntity* prim = dynamic_cast<SlipPrimitiveEntity*>(ent);
                if ((!mc || !mc->IsEnabled() || !mc->model) && !prim) continue;

                glm::mat4 mMat = ent->getMatrix();
                litShader->setMat4("u_Model", mMat);

                if (mc && mc->model) {
                    if (mc->model->hasClusterLOD && mc->model->clusterVAO != 0 && !mc->model->clusters.empty() &&
                        mc->model->clusterHeader.totalIndices > 60000) {
                        if (m_StressTestEntities.size() > 50 && glm::length(glm::vec3(mMat[3]) - probePos) > 25.0f) {
                            continue;
                        }
                        litShader->setVec3("u_Albedo", 0.75f, 0.75f, 0.75f);
                        litShader->setFloat("u_Roughness", 0.55f);
                        litShader->setFloat("u_Metallic", 0.0f);
                        litShader->setFloat("u_AO", 1.0f);
                        litShader->setBool("u_HasAlbedoMap", false);
                        litShader->setBool("u_HasNormalMap", false);
                        litShader->setBool("u_HasRoughnessMap", false);
                        litShader->setBool("u_HasMetallicMap", false);
                        litShader->setBool("u_HasAOMap", false);
                        litShader->setBool("u_HasEmissiveMap", false);
                        litShader->setBool("u_HasOpacityMap", false);
                        litShader->setBool("u_PackedORM", false);
                        litShader->setBool("u_AlphaClip", false);

                        glBindVertexArray(mc->model->clusterVAO);
                        uint32_t rootCnt = (std::min)(mc->model->clusterHeader.rootClusterCount,
                                                      static_cast<uint32_t>(mc->model->clusters.size()));
                        if (rootCnt > 0) {
                            uint32_t firstIdx = mc->model->clusters[0].indexOffset;
                            uint32_t totalRootIdx = 0;
                            for (uint32_t r = 0; r < rootCnt; ++r) {
                                if (mc->model->clusters[r].indexOffset == firstIdx + totalRootIdx) {
                                    totalRootIdx += mc->model->clusters[r].indexCount;
                                }
                            }
                            if (totalRootIdx > 0) {
                                void* offset = (void*)(uintptr_t)(firstIdx * sizeof(unsigned int));
                                glDrawElements(GL_TRIANGLES, totalRootIdx, GL_UNSIGNED_INT, offset);
                            }
                        }
                        glBindVertexArray(0);
                        continue;
                    }
                    bool isUnified = mc->model->isUnifiedBuffer;
                    for (const auto& sm : mc->model->meshes) {
                        if (sm.VAO == 0) continue;
                        uint32_t count = isUnified ? sm.indexCount : static_cast<uint32_t>(sm.indices.size());
                        if (count == 0) continue;

                        size_t slot = (sm.materialIndex >= 0) ? (size_t)sm.materialIndex : 0;
                        SlipMaterial* sMat = mc->GetSubmeshMaterial(slot);
                        if (sMat) {
                            sMat->bindMaterialProperties(litShader);
                } else {
                            litShader->setVec3("u_Albedo", 0.8f, 0.8f, 0.8f);
                            litShader->setFloat("u_Roughness", 0.5f);
                            litShader->setFloat("u_Metallic", 0.0f);
                            litShader->setFloat("u_AO", 1.0f);
                            litShader->setVec3("u_EmissiveColor", 0.0f, 0.0f, 0.0f);
                            litShader->setFloat("u_EmissiveIntensity", 0.0f);
                            litShader->setBool("u_HasAlbedoMap", false);
                            litShader->setBool("u_HasNormalMap", false);
                            litShader->setBool("u_HasRoughnessMap", false);
                            litShader->setBool("u_HasMetallicMap", false);
                            litShader->setBool("u_HasAOMap", false);
                            litShader->setBool("u_HasEmissiveMap", false);
                            litShader->setBool("u_HasOpacityMap", false);
                            litShader->setBool("u_PackedORM", false);
                            litShader->setBool("u_AlphaClip", false);
                }
                        glBindVertexArray(sm.VAO);
                        void* offset = isUnified ? (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int)) : 0;
                        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                        glBindVertexArray(0);
            }
        } else if (prim && prim->GetVAO() != 0) {
                    litShader->setVec3("u_Albedo", prim->color.r, prim->color.g, prim->color.b);
                    litShader->setFloat("u_Roughness", 0.5f);
                    litShader->setFloat("u_Metallic", 0.0f);
                    litShader->setFloat("u_AO", 1.0f);
                    litShader->setBool("u_HasAlbedoMap", false);
                    litShader->setBool("u_HasNormalMap", false);
                    litShader->setBool("u_HasRoughnessMap", false);
                    litShader->setBool("u_HasMetallicMap", false);
                    litShader->setBool("u_HasAOMap", false);
                    litShader->setBool("u_HasEmissiveMap", false);
                    litShader->setBool("u_HasOpacityMap", false);
                    litShader->setBool("u_PackedORM", false);
                    litShader->setBool("u_AlphaClip", false);
                    glBindVertexArray(prim->GetVAO());
                    glDrawElements(GL_TRIANGLES, prim->GetIndexCount(), GL_UNSIGNED_INT, 0);
                    glBindVertexArray(0);
        }
    }
}
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    SlipDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, m_ReflectionCubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
#endif
}

void ViewportPanel::BakeReflectionProbe(ReflectionProbeComponent* probe, const glm::vec3& probePos) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    (void)probe; (void)probePos;
#else
    if (!probe) return;

    int res = probe->resolution;
    if (res < 64) res = 64;
    if (res > 1024) res = 1024;

    if (probe->cubemap == 0) {
        glGenTextures(1, &probe->cubemap);
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, probe->cubemap);
    for (unsigned int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA16F, res, res, 0, GL_RGBA, GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

    if (m_ReflectionFBO == 0) {
        glGenFramebuffers(1, &m_ReflectionFBO);
        glGenRenderbuffers(1, &m_ReflectionRBO);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, m_ReflectionFBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_ReflectionRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, res, res);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_ReflectionRBO);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    glm::mat4 captureProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 300.0f);
    glm::mat4 captureViews[6] = {
        glm::lookAt(probePos, probePos + glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(probePos, probePos + glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(probePos, probePos + glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f))
    };

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    for (int t = 0; t < 8; ++t) {
        glActiveTexture(GL_TEXTURE0 + t);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    }
    glActiveTexture(GL_TEXTURE0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_ReflectionFBO);
    glViewport(0, 0, res, res);
    GLenum drawBufs[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBufs);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_STENCIL_TEST);

    SlipShader* litShader = SlipShaderLibrary::Get("Lit");
    glm::vec3 bakeLightDir = glm::normalize(glm::vec3(-0.4f, -0.9f, -0.4f));
    glm::vec3 bakeLightColor = glm::vec3(1.0f, 0.98f, 0.95f);
    float bakeLightIntensity = 1.0f;

    bool hasStatic = false;
    if (m_ContextLevel) {
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            if (ent->IsStatic()) hasStatic = true;
            auto lc = ent->GetComponent<LightComponent>();
            if (lc && lc->IsEnabled() && lc->type == LightType::Directional) {
                glm::vec3 rot = ent->GetTransform() ? ent->GetTransform()->rotation : ent->rotation;
                glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
                bakeLightDir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));
                bakeLightColor = lc->color;
                bakeLightIntensity = lc->intensity;
            }
        }
    }

    for (unsigned int i = 0; i < 6; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, probe->cubemap, 0);
        glClearColor(0.12f, 0.14f, 0.18f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        DrawProceduralSky(captureViews[i], captureProj, probePos, bakeLightDir);

        if (m_ContextLevel && litShader) {
            litShader->use();
            litShader->setMat4("u_Projection", captureProj);
            litShader->setMat4("u_View", captureViews[i]);
            litShader->setVec3("u_CameraPos", probePos.x, probePos.y, probePos.z);
            litShader->setBool("u_HasEnvCube", false);
            litShader->setBool("u_DDGIEnabled", false);
            litShader->setBool("u_ShadowsEnabled", false);
            litShader->setInt("u_NumDirLights", 1);
            litShader->setVec3("u_DirLights[0].direction", bakeLightDir.x, bakeLightDir.y, bakeLightDir.z);
            litShader->setVec3("u_DirLights[0].color", bakeLightColor.x, bakeLightColor.y, bakeLightColor.z);
            litShader->setFloat("u_DirLights[0].intensity", bakeLightIntensity);
            litShader->setInt("u_NumPointLights", 0);
            litShader->setInt("u_NumSpotLights", 0);

            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                if (!ent) continue;
                if (hasStatic && !probe->includeDynamic && !ent->IsStatic()) continue;

                auto mc = ent->GetComponent<MeshComponent>();
                SlipPrimitiveEntity* prim = dynamic_cast<SlipPrimitiveEntity*>(ent);
                if ((!mc || !mc->IsEnabled() || !mc->model) && !prim) continue;

                glm::mat4 mMat = ent->getMatrix();
                litShader->setMat4("u_Model", mMat);

                if (mc && mc->model) {
                    if (mc->model->hasClusterLOD && mc->model->clusterVAO != 0 && !mc->model->clusters.empty() &&
                        mc->model->clusterHeader.totalIndices > 60000) {
                        if (m_StressTestEntities.size() > 50 && glm::length(glm::vec3(mMat[3]) - probePos) > 25.0f) {
                            continue;
                        }
                        litShader->setVec3("u_Albedo", 0.75f, 0.75f, 0.75f);
                        litShader->setFloat("u_Roughness", 0.55f);
                        litShader->setFloat("u_Metallic", 0.0f);
                        litShader->setFloat("u_AO", 1.0f);
                        litShader->setBool("u_HasAlbedoMap", false);
                        litShader->setBool("u_HasNormalMap", false);
                        litShader->setBool("u_HasRoughnessMap", false);
                        litShader->setBool("u_HasMetallicMap", false);
                        litShader->setBool("u_HasAOMap", false);
                        litShader->setBool("u_HasEmissiveMap", false);
                        litShader->setBool("u_HasOpacityMap", false);
                        litShader->setBool("u_PackedORM", false);
                        litShader->setBool("u_AlphaClip", false);

                        glBindVertexArray(mc->model->clusterVAO);
                        uint32_t rootCnt = (std::min)(mc->model->clusterHeader.rootClusterCount,
                                                      static_cast<uint32_t>(mc->model->clusters.size()));
                        if (rootCnt > 0) {
                            uint32_t firstIdx = mc->model->clusters[0].indexOffset;
                            uint32_t totalRootIdx = 0;
                            for (uint32_t r = 0; r < rootCnt; ++r) {
                                if (mc->model->clusters[r].indexOffset == firstIdx + totalRootIdx) {
                                    totalRootIdx += mc->model->clusters[r].indexCount;
                                }
                            }
                            if (totalRootIdx > 0) {
                                void* offset = (void*)(uintptr_t)(firstIdx * sizeof(unsigned int));
                                glDrawElements(GL_TRIANGLES, totalRootIdx, GL_UNSIGNED_INT, offset);
                            }
                        }
                        glBindVertexArray(0);
                        continue;
                    }
                    bool isUnified = mc->model->isUnifiedBuffer;
                    for (const auto& sm : mc->model->meshes) {
                        if (sm.VAO == 0) continue;
                        uint32_t count = isUnified ? sm.indexCount : static_cast<uint32_t>(sm.indices.size());
                        if (count == 0) continue;

                        size_t slot = (sm.materialIndex >= 0) ? (size_t)sm.materialIndex : 0;
                        SlipMaterial* sMat = mc->GetSubmeshMaterial(slot);
                        if (sMat) {
                            sMat->bindMaterialProperties(litShader);
                        } else {
                            litShader->setVec3("u_Albedo", 0.8f, 0.8f, 0.8f);
                            litShader->setFloat("u_Roughness", 0.5f);
                            litShader->setFloat("u_Metallic", 0.0f);
                            litShader->setFloat("u_AO", 1.0f);
                            litShader->setVec3("u_EmissiveColor", 0.0f, 0.0f, 0.0f);
                            litShader->setFloat("u_EmissiveIntensity", 0.0f);
                            litShader->setBool("u_HasAlbedoMap", false);
                            litShader->setBool("u_HasNormalMap", false);
                            litShader->setBool("u_HasRoughnessMap", false);
                            litShader->setBool("u_HasMetallicMap", false);
                            litShader->setBool("u_HasAOMap", false);
                            litShader->setBool("u_HasEmissiveMap", false);
                            litShader->setBool("u_HasOpacityMap", false);
                            litShader->setBool("u_PackedORM", false);
                            litShader->setBool("u_AlphaClip", false);
                        }
                        glBindVertexArray(sm.VAO);
                        void* offset = isUnified ? (void*)(uintptr_t)(sm.firstIndex * sizeof(unsigned int)) : 0;
                        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, offset);
                    }
                } else if (prim && prim->GetVAO() != 0) {
                    litShader->setVec3("u_Albedo", 0.7f, 0.7f, 0.75f);
                    litShader->setFloat("u_Roughness", 0.4f);
                    litShader->setFloat("u_Metallic", 0.0f);
                    litShader->setFloat("u_AO", 1.0f);
                    litShader->setBool("u_HasAlbedoMap", false);
                    litShader->setBool("u_HasNormalMap", false);
                    litShader->setBool("u_HasRoughnessMap", false);
                    litShader->setBool("u_HasMetallicMap", false);
                    litShader->setBool("u_HasAOMap", false);
                    litShader->setBool("u_HasEmissiveMap", false);
                    litShader->setBool("u_HasOpacityMap", false);
                    litShader->setBool("u_PackedORM", false);
                    litShader->setBool("u_AlphaClip", false);
                    glBindVertexArray(prim->GetVAO());
                    glDrawElements(GL_TRIANGLES, prim->GetIndexCount(), GL_UNSIGNED_INT, 0);
                }
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    SlipDrawBuffer(GL_COLOR_ATTACHMENT0);

    glBindTexture(GL_TEXTURE_CUBE_MAP, probe->cubemap);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

    probe->isBaked = true;
#endif
}

void ViewportPanel::BakeAllReflectionProbes() {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!m_ContextLevel) return;
    for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
        if (!ent) continue;
        if (auto probe = ent->GetComponent<ReflectionProbeComponent>()) {
            glm::vec3 entPos = ent->GetTransform() ? ent->GetTransform()->position : ent->position;
            BakeReflectionProbe(probe.get(), entPos);
        }
    }
#endif
}

void ViewportPanel::DrawSSR(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const glm::vec3& mainLightDir) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    (void)view; (void)projection; (void)eye; (void)mainLightDir;
#else
    SlipShader* shader = SlipShaderLibrary::Get("SSR");
    if (!shader || m_ColorCopy == 0 || m_DepthTexture == 0 || m_NormalTexture == 0) return;

    const auto& gfxSettings = SlipGraphicsSettings::Get().GetData();

    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glBindTexture(GL_TEXTURE_2D, m_ColorCopy);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, m_FboWidth, m_FboHeight);

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);

    if (m_SSRVAO == 0) glGenVertexArrays(1, &m_SSRVAO);
    int quality = gfxSettings.ssrQuality;
    if (quality < 0) quality = 0;
    if (quality > 2) quality = 2;
    float scale = (quality == 0) ? 0.5f : (quality == 1) ? 0.75f : 1.0f;
    int steps = (quality == 0) ? 16 : (quality == 1) ? 32 : 64;
    float maxDist = (quality == 0) ? 18.0f : (quality == 1) ? 30.0f : 40.0f;
    int traceW = (std::max)(1, static_cast<int>(m_FboWidth * scale));
    int traceH = (std::max)(1, static_cast<int>(m_FboHeight * scale));

    shader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_ColorCopy);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_DepthTexture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_NormalTexture);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_ColorCopy);

    shader->setInt("u_Color", 0);
    shader->setInt("u_Depth", 1);
    shader->setInt("u_Geometry", 2);
    shader->setInt("u_Reflection", 3);

    // Cubemap de entorno o cielo procedimental para fallback
    ReflectionProbeComponent* activeProbe = nullptr;
    if (m_ContextLevel) {
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent) continue;
            auto probe = ent->GetComponent<ReflectionProbeComponent>();
            if (probe && probe->IsEnabled() && probe->isBaked && probe->cubemap != 0) {
                glm::vec3 entPos = ent->GetTransform() ? ent->GetTransform()->position : ent->position;
                if (probe->IsInside(eye, entPos)) {
                    activeProbe = probe.get();
                    break;
                }
            }
        }
    }

    if (activeProbe && activeProbe->cubemap != 0 && gfxSettings.reflectionProbeEnabled) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP, activeProbe->cubemap);
        shader->setInt("u_EnvCube", 4);
        shader->setBool("u_HasEnvCube", true);
    } else if (m_ReflectionCubemap != 0 && gfxSettings.reflectionProbeEnabled) {
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_CUBE_MAP, m_ReflectionCubemap);
        shader->setInt("u_EnvCube", 4);
        shader->setBool("u_HasEnvCube", true);
    } else {
        shader->setBool("u_HasEnvCube", false);
    }

    shader->setInt("u_Steps", steps);
    shader->setFloat("u_MaxDist", maxDist);
    glm::mat4 viewProj = projection * view;
    shader->setMat4("u_ViewProj", viewProj);
    shader->setMat4("u_InvViewProj", glm::inverse(viewProj));
    shader->setMat4("u_View", view);
    shader->setVec3("u_CameraPos", eye.x, eye.y, eye.z);

    // Parámetros avanzados de SSR para eliminar artefactos oscuros y costuras
    shader->setFloat("u_RoughnessCutoff", gfxSettings.ssrRoughnessCutoff);
    shader->setFloat("u_Thickness", gfxSettings.ssrThickness);
    shader->setFloat("u_EdgeFade", gfxSettings.ssrEdgeFade);

    // Fallback de cielo si un rayo no impacta nada
    glm::vec3 skyZenith = glm::vec3(gfxSettings.skyZenithColor.r, gfxSettings.skyZenithColor.g, gfxSettings.skyZenithColor.b);
    glm::vec3 skyHorizon = glm::vec3(gfxSettings.skyHorizonColor.r, gfxSettings.skyHorizonColor.g, gfxSettings.skyHorizonColor.b);
    glm::vec3 skyGround = glm::vec3(gfxSettings.skyGroundColor.r, gfxSettings.skyGroundColor.g, gfxSettings.skyGroundColor.b);
    if (m_ContextLevel) {
        if (auto* skyComp = SlipSkyComponent::FindActive(m_ContextLevel)) {
            skyZenith = skyComp->currentAtmosphere.zenithColor;
            skyHorizon = skyComp->currentAtmosphere.horizonColor;
            skyGround = skyComp->currentAtmosphere.groundColor;
        }
    }
    shader->setVec3("u_SkyZenith", skyZenith.r, skyZenith.g, skyZenith.b);
    shader->setVec3("u_SkyHorizon", skyHorizon.r, skyHorizon.g, skyHorizon.b);
    shader->setVec3("u_SkyGround", skyGround.r, skyGround.g, skyGround.b);

    glBindVertexArray(m_SSRVAO);

    // Configurar FBO para trazado de reflejos desacoplado de m_FBO (mantiene intactos m_DepthTexture y m_NormalTexture)
    if (m_SSRFBO == 0) glGenFramebuffers(1, &m_SSRFBO);
    if (m_SSRTarget == 0) glGenTextures(1, &m_SSRTarget);
    if (m_SSRWidth != traceW || m_SSRHeight != traceH) {
        m_SSRWidth = traceW;
        m_SSRHeight = traceH;
        glBindTexture(GL_TEXTURE_2D, m_SSRTarget);
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, traceW, traceH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, traceW, traceH, 0, GL_RGBA, GL_FLOAT, nullptr);
#endif
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, m_SSRFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SSRTarget, 0);
    }

    // PASO 1: Raymarch en m_SSRFBO independiente
    glBindFramebuffer(GL_FRAMEBUFFER, m_SSRFBO);
    glViewport(0, 0, traceW, traceH);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    shader->setInt("u_Mode", 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    // PASO 2: Composición en m_FBO sin alterar ningún attachment de profundidad o normales
    // Desvincular texturas de profundidad, geometría y cielo antes de enlazar m_FBO para evitar feedback loops
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, m_FBO);
    glViewport(0, 0, m_FboWidth, m_FboHeight);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, m_SSRTarget);
    shader->setInt("u_Mode", 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE4); glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);

    // Restaurar estados normales de renderizado
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
#endif
}

void ViewportPanel::DrawAabbGizmo(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& center, const glm::vec3& size, const glm::vec4& color) {
    glm::vec3 h = size * 0.5f;
    glm::vec3 p[8] = {
        center + glm::vec3(-h.x, -h.y, -h.z), center + glm::vec3(h.x, -h.y, -h.z),
        center + glm::vec3(h.x, -h.y, h.z), center + glm::vec3(-h.x, -h.y, h.z),
        center + glm::vec3(-h.x, h.y, -h.z), center + glm::vec3(h.x, h.y, -h.z),
        center + glm::vec3(h.x, h.y, h.z), center + glm::vec3(-h.x, h.y, h.z)
    };
    int edges[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
    std::vector<glm::vec3> lines;
    for (auto& e : edges) {
        lines.push_back(p[e[0]]);
        lines.push_back(p[e[1]]);
    }
    SlipShader* lineShader = SlipShaderLibrary::Get("Line");
    if (!lineShader) return;
    lineShader->use();
    lineShader->setMat4("u_Projection", projection);
    lineShader->setMat4("u_View", view);
    lineShader->setMat4("u_Model", glm::mat4(1.0f));
    lineShader->setVec4("u_Color", color);
    if (m_LineVAO == 0) {
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);
    }
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, lines.size() * sizeof(glm::vec3), lines.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lines.size()));
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

void ViewportPanel::DrawVolumeGizmo(const glm::mat4& view, const glm::mat4& projection, DDGIVolumeComponent* volume) {
    if (!volume || !m_SelectedEntity) return;
    DrawAabbGizmo(view, projection, m_SelectedEntity->position, volume->size, glm::vec4(0.95f, 0.72f, 0.25f, 1.0f));
}

void ViewportPanel::DrawReflectionProbeGizmo(const glm::mat4& view, const glm::mat4& projection, ReflectionProbeComponent* probe) {
    if (!probe || !m_SelectedEntity) return;
    DrawAabbGizmo(view, projection, m_SelectedEntity->position, probe->size, glm::vec4(0.2f, 0.85f, 0.95f, 1.0f));
}

void ViewportPanel::DrawPostProcessGizmo(const glm::mat4& view, const glm::mat4& projection, PostProcessVolumeComponent* volume) {
    if (!volume || volume->isGlobal || !m_SelectedEntity) return;
    DrawAabbGizmo(view, projection, m_SelectedEntity->position, volume->size, glm::vec4(0.62f, 0.45f, 0.95f, 1.0f));
}

void ViewportPanel::DrawVClustBoundsGizmo(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& eye, const SlipFrustum& frustum) {
    if (!m_ContextLevel) return;

    SlipShader* lineShader = SlipShaderLibrary::Get("Line");
    if (!lineShader) lineShader = SlipShaderLibrary::Get("Unlit");
    if (!lineShader) return;

    std::vector<SlipEntity*> targetEntities;
    auto getEntityClusterMesh = [](SlipEntity* ent) -> SlipMesh* {
        if (!ent) return nullptr;
        if (auto mc = ent->GetComponent<MeshComponent>()) {
            if (mc->IsEnabled() && mc->model && mc->model->hasClusterLOD && !mc->model->clusters.empty()) {
                return mc->model;
            }
        }
        if (auto smc = ent->GetComponent<SkinnedMeshComponent>()) {
            if (smc->IsEnabled() && smc->model && smc->model->hasClusterLOD && !smc->model->clusters.empty() &&
                (!smc->skeleton || smc->skeleton->GetBoneCount() == 0)) {
                return smc->model;
            }
        }
        if (auto tc = ent->GetComponent<SlipTerrainComponent>()) {
            if (tc->IsEnabled() && tc->GetMesh() && tc->GetMesh()->hasClusterLOD && !tc->GetMesh()->clusters.empty()) {
                return tc->GetMesh();
            }
        }
        return nullptr;
    };

    if (m_SelectedEntity && getEntityClusterMesh(m_SelectedEntity) != nullptr) {
        targetEntities.push_back(m_SelectedEntity);
    }
    if (targetEntities.empty()) {
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (getEntityClusterMesh(ent) != nullptr) {
                targetEntities.push_back(ent);
                if (targetEntities.size() >= 8) break; // Limitar en caso de no haber selección activa
            }
        }
    }
    if (targetEntities.empty()) return;

    const float PI = 3.14159265358979323846f;
    const int ringSegments = 16;
    const float vClustFovRad = glm::radians(45.0f);
    const float vClustViewportH = static_cast<float>((std::max)(1, m_FboHeight));

    // Agrupar líneas por nivel LOD (0..4) para dibujar con colores de mapa de calor en 5 llamadas rápidas
    std::vector<glm::vec3> lodLines[5];
    size_t totalSpheresDrawn = 0;

    for (SlipEntity* ent : targetEntities) {
        SlipMesh* targetModel = getEntityClusterMesh(ent);
        if (!targetModel) continue;

        glm::mat4 worldMat = ent->getMatrix();
        std::vector<SlipVisibleCluster> visClusters;
        SlipRender::TraverseClusterHierarchy(
            targetModel->clusters,
            targetModel->clusterHeader.rootClusterCount,
            worldMat,
            frustum,
            eye,
            vClustViewportH,
            vClustFovRad,
            visClusters,
            nullptr,
            m_VClustSseThreshold,
            true
        );

        float sx = glm::length(glm::vec3(worldMat[0]));
        float sy = glm::length(glm::vec3(worldMat[1]));
        float sz = glm::length(glm::vec3(worldMat[2]));
        float maxScale = (std::max)(sx, (std::max)(sy, sz));

        for (const SlipVisibleCluster& vc : visClusters) {
            if (vc.clusterId >= targetModel->clusters.size()) continue;
            if (totalSpheresDrawn >= 256) break;
            ++totalSpheresDrawn;

            const SlipCluster& cl = targetModel->clusters[vc.clusterId];
            glm::vec3 wCenter = glm::vec3(worldMat * glm::vec4(cl.center, 1.0f));
            float wRadius = cl.radius * maxScale;
            int bucket = (std::min)(static_cast<int>(cl.lodLevel), 4);
            auto& lines = lodLines[bucket];

            for (int s = 0; s < ringSegments; ++s) {
                float a0 = (float)s / ringSegments * 2.0f * PI;
                float a1 = (float)(s + 1) / ringSegments * 2.0f * PI;
                // Anillo XY
                lines.push_back(wCenter + glm::vec3(std::cos(a0) * wRadius, std::sin(a0) * wRadius, 0.0f));
                lines.push_back(wCenter + glm::vec3(std::cos(a1) * wRadius, std::sin(a1) * wRadius, 0.0f));
                // Anillo XZ
                lines.push_back(wCenter + glm::vec3(std::cos(a0) * wRadius, 0.0f, std::sin(a0) * wRadius));
                lines.push_back(wCenter + glm::vec3(std::cos(a1) * wRadius, 0.0f, std::sin(a1) * wRadius));
            }

            // Eje del cono normal promedio (Backface Cone Axis)
            if (glm::length(cl.coneAxis) > 0.01f) {
                glm::vec3 wAxis = glm::normalize(glm::mat3(worldMat) * cl.coneAxis);
                lines.push_back(wCenter);
                lines.push_back(wCenter + wAxis * (wRadius * 0.65f));
            }
        }
    }

    if (m_LineVAO == 0) {
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);
    }

    const glm::vec4 lodColors[5] = {
        glm::vec4(0.18f, 0.92f, 0.35f, 1.0f), // LOD 0: Verde
        glm::vec4(0.20f, 0.85f, 0.95f, 1.0f), // LOD 1: Cian
        glm::vec4(0.95f, 0.85f, 0.20f, 1.0f), // LOD 2: Amarillo
        glm::vec4(0.98f, 0.50f, 0.15f, 1.0f), // LOD 3: Naranja
        glm::vec4(0.95f, 0.20f, 0.20f, 1.0f)  // LOD 4+: Rojo
    };

    lineShader->use();
    lineShader->setMat4("u_Projection", projection);
    lineShader->setMat4("u_View", view);
    lineShader->setMat4("u_Model", glm::mat4(1.0f));

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    for (int b = 0; b < 5; ++b) {
        if (lodLines[b].empty()) continue;
        lineShader->setVec4("u_Color", lodColors[b]);
        glBufferData(GL_ARRAY_BUFFER, lodLines[b].size() * sizeof(glm::vec3), lodLines[b].data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lodLines[b].size()));
    }

    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

void ViewportPanel::ClearStressTestInstances() {
    if (m_ContextLevel && !m_StressTestEntities.empty()) {
        for (SlipEntity* ent : m_StressTestEntities) {
            if (!ent) continue;
            if (m_SelectedEntity == ent) {
                m_SelectedEntity = nullptr;
                if (m_OnSelectCallback) m_OnSelectCallback(nullptr);
            }
            m_ContextLevel->RemoveEntity(ent);
            delete ent;
        }
        m_LastStaticHash = 0;
    }
    m_StressTestEntities.clear();

    if (m_StressFallbackMesh) {
        delete m_StressFallbackMesh;
        m_StressFallbackMesh = nullptr;
    }
}

void ViewportPanel::SpawnStressTestInstances(int instanceCount) {
    if (!m_ContextLevel || instanceCount <= 0) return;

    // 1. Buscar antes de limpiar si el usuario tiene seleccionado un modelo propio importado
    SlipMesh* sharedMesh = nullptr;
    std::string sharedPath = "VClust_Procedural_Sphere";
    SlipMaterial* sourceMaterial = nullptr;

    auto isStressEntity = [&](SlipEntity* e) -> bool {
        return std::find(m_StressTestEntities.begin(), m_StressTestEntities.end(), e) != m_StressTestEntities.end();
    };

    if (m_SelectedEntity && !isStressEntity(m_SelectedEntity)) {
        if (auto mc = m_SelectedEntity->GetComponent<MeshComponent>()) {
            if (mc->model && !mc->model->meshes.empty()) {
                sharedMesh = mc->model;
                sharedPath = mc->modelPath;
                if (!mc->submeshMaterials.empty()) sourceMaterial = mc->submeshMaterials[0];
            }
        } else if (auto smc = m_SelectedEntity->GetComponent<SkinnedMeshComponent>()) {
            if (smc->model && !smc->model->meshes.empty()) {
                sharedMesh = smc->model;
                sharedPath = smc->modelPath;
                if (!smc->submeshMaterials.empty()) sourceMaterial = smc->submeshMaterials[0];
            }
        }
    }

    if (!sharedMesh) {
        for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
            if (!ent || isStressEntity(ent)) continue;
            if (auto mc = ent->GetComponent<MeshComponent>()) {
                if (mc->model && !mc->model->meshes.empty()) {
                    sharedMesh = mc->model;
                    sharedPath = mc->modelPath;
                    if (!mc->submeshMaterials.empty()) sourceMaterial = mc->submeshMaterials[0];
                    break;
                }
            } else if (auto smc = ent->GetComponent<SkinnedMeshComponent>()) {
                if (smc->model && !smc->model->meshes.empty()) {
                    sharedMesh = smc->model;
                    sharedPath = smc->modelPath;
                    if (!smc->submeshMaterials.empty()) sourceMaterial = smc->submeshMaterials[0];
                    break;
                }
            }
        }
    }

    // Limpiar instancias de estrés previas
    ClearStressTestInstances();

    // 2. Si la escena no tiene ningún modelo importado, generar una esfera densa procedural (8,192 triángulos por instancia = ~8.19M triángulos en 1,000 objetos)
    if (!sharedMesh) {
        std::vector<Vertex> vertices;
        std::vector<unsigned int> indices;
        const int rings = 64;
        const int sectors = 64;
        const float radius = 1.2f;
        const float PI = 3.14159265358979323846f;

        vertices.reserve((rings + 1) * (sectors + 1));
        for (int r = 0; r <= rings; ++r) {
            float v = static_cast<float>(r) / static_cast<float>(rings);
            float phi = v * PI;
            for (int s = 0; s <= sectors; ++s) {
                float u = static_cast<float>(s) / static_cast<float>(sectors);
                float theta = u * 2.0f * PI;

                // Ligero relieve sinusoidal para apreciar mejor la simplificación volumétrica de los niveles LOD
                float wave = 1.0f + 0.08f * std::sin(6.0f * phi) * std::cos(6.0f * theta);
                glm::vec3 unitNormal(
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi),
                    std::sin(phi) * std::sin(theta)
                );

                Vertex vert{};
                vert.Position = unitNormal * (radius * wave);
                vert.Normal = glm::normalize(unitNormal);
                vert.TexCoords = glm::vec2(u, v);
                vert.Tangent = glm::normalize(glm::vec3(-std::sin(theta), 0.0f, std::cos(theta)));
                vert.Bitangent = glm::cross(vert.Normal, vert.Tangent);
                for (int b = 0; b < 4; ++b) {
                    vert.m_BoneIDs[b] = -1;
                    vert.m_Weights[b] = 0.0f;
                }
                vert.materialIndex = 0;
                vertices.push_back(vert);
            }
        }

        indices.reserve(rings * sectors * 6);
        for (int r = 0; r < rings; ++r) {
            for (int s = 0; s < sectors; ++s) {
                uint32_t i0 = r * (sectors + 1) + s;
                uint32_t i1 = i0 + 1;
                uint32_t i2 = (r + 1) * (sectors + 1) + s;
                uint32_t i3 = i2 + 1;
                indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
                indices.push_back(i1); indices.push_back(i2); indices.push_back(i3);
            }
        }

        m_StressFallbackMesh = new SlipMesh(vertices, indices);
        if (!m_StressFallbackMesh->meshes.empty()) {
            m_StressFallbackMesh->meshes[0].indexCount = static_cast<uint32_t>(indices.size());
            m_StressFallbackMesh->meshes[0].vertexCount = static_cast<uint32_t>(vertices.size());
        }
        m_StressFallbackMesh->computeBounds();
        sharedMesh = m_StressFallbackMesh;
    }

    // 3. Garantizar que el modelo compartido tenga la jerarquía V-Clust construida y subida a GPU
    if (sharedMesh && (!sharedMesh->hasClusterLOD || sharedMesh->clusters.empty())) {
        SlipClusterBuildOptions opts;
        opts.maxVerticesPerCluster = 64;
        opts.maxTrianglesPerCluster = 124;
        opts.coneWeight = 0.5f;
        opts.maxHierarchyLevels = 4;

        if (!sharedMesh->hasBounds) sharedMesh->computeBounds();
        float diag = glm::length(sharedMesh->aabbMax - sharedMesh->aabbMin);
        opts.baseVoxelSize = (std::max)(0.002f, diag * 0.015f);

        SlipClusterBuildResult buildRes;
        if (SlipClusterBuilder::BuildFromSubmeshes(sharedMesh->meshes, opts, buildRes) && !buildRes.clusters.empty()) {
            sharedMesh->clusterHeader = buildRes.header;
            sharedMesh->clusters = std::move(buildRes.clusters);
            sharedMesh->clusterVertices = std::move(buildRes.quantizedVertices);
            sharedMesh->clusterIndices = std::move(buildRes.indices);
            sharedMesh->hasClusterLOD = true;
            sharedMesh->setupClusterBuffers();
        }
    } else if (sharedMesh && sharedMesh->clusterVAO == 0 && !sharedMesh->clusterVertices.empty()) {
        sharedMesh->setupClusterBuffers();
    }

    if (!sharedMesh) return;

    // Calcular espaciado automático según el tamaño real del modelo para formar un Cubo 3D (10 x 10 x 10 = 1,000 instancias)
    if (!sharedMesh->hasBounds) sharedMesh->computeBounds();
    glm::vec3 ext = sharedMesh->aabbMax - sharedMesh->aabbMin;
    float maxDim = (std::max)({ ext.x, ext.y, ext.z });
    float spacing = (std::max)(2.5f, maxDim * 2.25f);

    int side = static_cast<int>(std::ceil(std::cbrt(static_cast<float>(instanceCount))));
    if (side < 1) side = 1;
    float halfSpan = (side - 1) * spacing * 0.5f;

    m_StressTestEntities.reserve(instanceCount);
    for (int i = 0; i < instanceCount; ++i) {
        int ix = i % side;
        int iy = (i / side) % side;
        int iz = i / (side * side);

        SlipEntity* ent = new SlipEntity("VClust_Inst_" + std::to_string(i));
        float x = (ix * spacing) - halfSpan;
        float y = (iy * spacing) + 0.5f;
        float z = -(iz * spacing) - 4.0f; // Hacia adelante (-Z) formando un cubo 3D para apreciar todos los niveles LOD

        ent->position = glm::vec3(x, y, z);
        ent->rotation = glm::vec3(0.0f);
        ent->scale = glm::vec3(1.0f);
        if (auto tc = ent->GetTransform()) {
            tc->position = ent->position;
            tc->rotation = ent->rotation;
            tc->scale = ent->scale;
        }
        ent->SetStatic(true);

        auto mc = ent->AddComponent<MeshComponent>();
        mc->SetSharedModel(sharedMesh, sharedPath);
        if (sourceMaterial) {
            if (mc->submeshMaterials.empty()) mc->submeshMaterials.resize(1, nullptr);
            mc->submeshMaterials[0] = sourceMaterial;
        }

        m_ContextLevel->AddEntity(ent);
        m_StressTestEntities.push_back(ent);
    }

    m_LastStaticHash = 0;
    m_ShowStats = true;
    m_VClustEnabled = true;
    if (m_VClustDebugMode == 0) {
        m_VClustDebugMode = 2; // Activar automáticamente el Mapa de Calor LOD para visualizar los niveles de distancia
    }

    SlipConsole::LogInfo("[V-Clust Stress Test] 1,000 instancias generadas (" +
                         std::to_string(sharedMesh->clusters.size()) + " clusters por malla en jerarquía multi-nivel).");
}

void ViewportPanel::DrawParticleGizmo(const glm::mat4& view, const glm::mat4& projection, ParticleSystemComponent* ps) {
    if (!ps || !m_SelectedEntity) return;

    std::vector<glm::vec3> lineVertices;
    const float PI = 3.14159265358979323846f;
    const int segments = 32;

    if (ps->emitterShape == ParticleEmitterShape::Point) {
        float s = 0.25f;
        lineVertices.push_back(glm::vec3(-s, 0.0f, 0.0f)); lineVertices.push_back(glm::vec3(s, 0.0f, 0.0f));
        lineVertices.push_back(glm::vec3(0.0f, -s, 0.0f)); lineVertices.push_back(glm::vec3(0.0f, s, 0.0f));
        lineVertices.push_back(glm::vec3(0.0f, 0.0f, -s)); lineVertices.push_back(glm::vec3(0.0f, 0.0f, s));
    } else if (ps->emitterShape == ParticleEmitterShape::Sphere) {
        float r = (std::max)(0.05f, ps->shapeRadius);
        for (int i = 0; i < segments; ++i) {
            float a0 = (float)i / segments * 2.0f * PI;
            float a1 = (float)(i + 1) / segments * 2.0f * PI;
            lineVertices.push_back(glm::vec3(std::cos(a0) * r, std::sin(a0) * r, 0.0f));
            lineVertices.push_back(glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f));
            lineVertices.push_back(glm::vec3(std::cos(a0) * r, 0.0f, std::sin(a0) * r));
            lineVertices.push_back(glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
            lineVertices.push_back(glm::vec3(0.0f, std::cos(a0) * r, std::sin(a0) * r));
            lineVertices.push_back(glm::vec3(0.0f, std::cos(a1) * r, std::sin(a1) * r));
}
    } else if (ps->emitterShape == ParticleEmitterShape::Cone) {
        float baseR = (std::max)(0.05f, ps->shapeRadius);
        float coneH = 1.5f;
        float topR = baseR + coneH * std::tan(glm::radians(glm::clamp(ps->coneAngle, 0.0f, 85.0f)));
        for (int i = 0; i < segments; ++i) {
            float a0 = (float)i / segments * 2.0f * PI;
            float a1 = (float)(i + 1) / segments * 2.0f * PI;
            lineVertices.push_back(glm::vec3(std::cos(a0) * baseR, 0.0f, std::sin(a0) * baseR));
            lineVertices.push_back(glm::vec3(std::cos(a1) * baseR, 0.0f, std::sin(a1) * baseR));
            lineVertices.push_back(glm::vec3(std::cos(a0) * topR, coneH, std::sin(a0) * topR));
            lineVertices.push_back(glm::vec3(std::cos(a1) * topR, coneH, std::sin(a1) * topR));
}
        lineVertices.push_back(glm::vec3(baseR, 0.0f, 0.0f)); lineVertices.push_back(glm::vec3(topR, coneH, 0.0f));
        lineVertices.push_back(glm::vec3(-baseR, 0.0f, 0.0f)); lineVertices.push_back(glm::vec3(-topR, coneH, 0.0f));
        lineVertices.push_back(glm::vec3(0.0f, 0.0f, baseR)); lineVertices.push_back(glm::vec3(0.0f, coneH, topR));
        lineVertices.push_back(glm::vec3(0.0f, 0.0f, -baseR)); lineVertices.push_back(glm::vec3(0.0f, coneH, -topR));
    } else if (ps->emitterShape == ParticleEmitterShape::Box) {
        glm::vec3 h = ps->boxSize * 0.5f;
        glm::vec3 p[8] = {
            glm::vec3(-h.x, -h.y, -h.z), glm::vec3(h.x, -h.y, -h.z),
            glm::vec3(h.x, -h.y, h.z), glm::vec3(-h.x, -h.y, h.z),
            glm::vec3(-h.x, h.y, -h.z), glm::vec3(h.x, h.y, -h.z),
            glm::vec3(h.x, h.y, h.z), glm::vec3(-h.x, h.y, h.z)
};
        int edges[12][2] = { {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7} };
        for (auto& e : edges) {
            lineVertices.push_back(p[e[0]]);
            lineVertices.push_back(p[e[1]]);
}
    } else if (ps->emitterShape == ParticleEmitterShape::Circle) {
        float r = (std::max)(0.05f, ps->shapeRadius);
        for (int i = 0; i < segments; ++i) {
            float a0 = (float)i / segments * 2.0f * PI;
            float a1 = (float)(i + 1) / segments * 2.0f * PI;
            lineVertices.push_back(glm::vec3(std::cos(a0) * r, 0.0f, std::sin(a0) * r));
            lineVertices.push_back(glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
}
    }

    // Flecha de dirección de lanzamiento + initialVelocityOffset
    glm::vec3 launchVec = glm::vec3(0.0f, 1.2f, 0.0f) + ps->initialVelocityOffset * 0.2f;
    lineVertices.push_back(glm::vec3(0.0f));
    lineVertices.push_back(launchVec);

    if (lineVertices.empty()) return;

    SlipShader* lineShader = SlipShaderLibrary::Get("Line");
    if (!lineShader) return;
    lineShader->use();
    lineShader->setMat4("u_Projection", projection);
    lineShader->setMat4("u_View", view);
    lineShader->setMat4("u_Model", m_SelectedEntity->getMatrix());
    lineShader->setVec4("u_Color", 0.20f, 0.90f, 1.0f, 1.0f);

    if (m_LineVAO == 0) {
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);
    }
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(glm::vec3), lineVertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

void ViewportPanel::DrawLightGizmo(const glm::mat4& view, const glm::mat4& projection, LightComponent* light) {
    if (!light || !m_SelectedEntity) return;

    std::vector<glm::vec3> lineVertices;
    const float PI = 3.14159265358979323846f;
    glm::vec3 pos = m_SelectedEntity->position;
    glm::vec3 rot = m_SelectedEntity->rotation;
    glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0)) *
                       glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
    glm::vec3 dir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));

    glm::vec3 up = (std::abs(dir.y) > 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
    glm::vec3 right = glm::normalize(glm::cross(dir, up));
    up = glm::normalize(glm::cross(right, dir));

    if (light->type == LightType::Directional) {
        float sunRadius = 0.8f;
        const int segments = 32;
        for (int s = 0; s < segments; ++s) {
            float a0 = (float)s / segments * 2.0f * PI;
            float a1 = (float)(s + 1) / segments * 2.0f * PI;
            glm::vec3 p0 = pos + (right * std::cos(a0) + up * std::sin(a0)) * sunRadius;
            glm::vec3 p1 = pos + (right * std::cos(a1) + up * std::sin(a1)) * sunRadius;
            lineVertices.push_back(p0); lineVertices.push_back(p1);
}

        // Flecha central
        float arrowLen = 2.5f;
        glm::vec3 arrowTip = pos + dir * arrowLen;
        lineVertices.push_back(pos); lineVertices.push_back(arrowTip);
        lineVertices.push_back(arrowTip); lineVertices.push_back(arrowTip - dir * 0.4f + right * 0.2f);
        lineVertices.push_back(arrowTip); lineVertices.push_back(arrowTip - dir * 0.4f - right * 0.2f);
        lineVertices.push_back(arrowTip); lineVertices.push_back(arrowTip - dir * 0.4f + up * 0.2f);
        lineVertices.push_back(arrowTip); lineVertices.push_back(arrowTip - dir * 0.4f - up * 0.2f);

        // 4 rayos secundarios paralelos
        glm::vec3 rayOffsets[4] = { right * sunRadius, -right * sunRadius, up * sunRadius, -up * sunRadius };
        for (int r = 0; r < 4; ++r) {
            glm::vec3 rStart = pos + rayOffsets[r];
            glm::vec3 rEnd = rStart + dir * 1.8f;
            lineVertices.push_back(rStart); lineVertices.push_back(rEnd);
            lineVertices.push_back(rEnd); lineVertices.push_back(rEnd - dir * 0.25f + right * 0.1f);
            lineVertices.push_back(rEnd); lineVertices.push_back(rEnd - dir * 0.25f - right * 0.1f);
}
    } else if (light->type == LightType::Point) {
        float r = (std::max)(light->range, 0.1f);
        const int segments = 36;
        for (int s = 0; s < segments; ++s) {
            float a0 = (float)s / segments * 2.0f * PI;
            float a1 = (float)(s + 1) / segments * 2.0f * PI;
            // Anillo XY
            lineVertices.push_back(pos + glm::vec3(std::cos(a0) * r, std::sin(a0) * r, 0.0f));
            lineVertices.push_back(pos + glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f));
            // Anillo XZ
            lineVertices.push_back(pos + glm::vec3(std::cos(a0) * r, 0.0f, std::sin(a0) * r));
            lineVertices.push_back(pos + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
            // Anillo YZ
            lineVertices.push_back(pos + glm::vec3(0.0f, std::cos(a0) * r, std::sin(a0) * r));
            lineVertices.push_back(pos + glm::vec3(0.0f, std::cos(a1) * r, std::sin(a1) * r));
}

        // Cruz central
        float cs = 0.25f;
        lineVertices.push_back(pos - glm::vec3(cs, 0, 0)); lineVertices.push_back(pos + glm::vec3(cs, 0, 0));
        lineVertices.push_back(pos - glm::vec3(0, cs, 0)); lineVertices.push_back(pos + glm::vec3(0, cs, 0));
        lineVertices.push_back(pos - glm::vec3(0, 0, cs)); lineVertices.push_back(pos + glm::vec3(0, 0, cs));
    } else if (light->type == LightType::Spot) {
        float range = (std::max)(light->range, 0.1f);
        float radHalfAngle = glm::radians(light->spotAngle * 0.5f);
        float baseRadius = range * std::tan(radHalfAngle);
        glm::vec3 baseCenter = pos + dir * range;

        const int segments = 36;
        for (int s = 0; s < segments; ++s) {
            float a0 = (float)s / segments * 2.0f * PI;
            float a1 = (float)(s + 1) / segments * 2.0f * PI;
            glm::vec3 p0 = baseCenter + (right * std::cos(a0) + up * std::sin(a0)) * baseRadius;
            glm::vec3 p1 = baseCenter + (right * std::cos(a1) + up * std::sin(a1)) * baseRadius;
            lineVertices.push_back(p0); lineVertices.push_back(p1);
}

        lineVertices.push_back(pos); lineVertices.push_back(baseCenter + right * baseRadius);
        lineVertices.push_back(pos); lineVertices.push_back(baseCenter - right * baseRadius);
        lineVertices.push_back(pos); lineVertices.push_back(baseCenter + up * baseRadius);
        lineVertices.push_back(pos); lineVertices.push_back(baseCenter - up * baseRadius);

        lineVertices.push_back(pos); lineVertices.push_back(baseCenter);
        lineVertices.push_back(baseCenter); lineVertices.push_back(baseCenter - dir * 0.3f + right * 0.15f);
        lineVertices.push_back(baseCenter); lineVertices.push_back(baseCenter - dir * 0.3f - right * 0.15f);
    }

    if (lineVertices.empty()) return;

    SlipShader* lineShader = SlipShaderLibrary::Get("Line");
    if (!lineShader) lineShader = SlipShaderLibrary::Get("Unlit");
    if (lineShader) {
        lineShader->use();
        lineShader->setMat4("u_Projection", projection);
        lineShader->setMat4("u_View", view);
        lineShader->setMat4("u_Model", glm::mat4(1.0f));
        lineShader->setVec4("u_Color", 1.0f, 0.88f, 0.25f, 1.0f);
    } else if (m_ShaderProgram != 0) {
        glUseProgram(m_ShaderProgram);
        GLint uModelLoc = glGetUniformLocation(m_ShaderProgram, "u_Model");
        GLint uViewLoc = glGetUniformLocation(m_ShaderProgram, "u_View");
        GLint uProjLoc = glGetUniformLocation(m_ShaderProgram, "u_Projection");
        GLint uColorLoc = glGetUniformLocation(m_ShaderProgram, "u_Color");
        glm::mat4 idMat(1.0f);
        if (uModelLoc >= 0) glUniformMatrix4fv(uModelLoc, 1, GL_FALSE, glm::value_ptr(idMat));
        if (uViewLoc >= 0) glUniformMatrix4fv(uViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        if (uProjLoc >= 0) glUniformMatrix4fv(uProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        if (uColorLoc >= 0) glUniform4f(uColorLoc, 1.0f, 0.88f, 0.25f, 1.0f);
    }

    glDisable(GL_STENCIL_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    if (m_LineVAO == 0) {
        glGenVertexArrays(1, &m_LineVAO);
        glGenBuffers(1, &m_LineVBO);
    }

    glBindVertexArray(m_LineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_LineVBO);
    glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(glm::vec3), lineVertices.data(), GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);

    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size()));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glDepthFunc(GL_LESS);
}

// ==========================================================================
// Captura continua del mouse para el gizmo (SDL relative mode).
// En modo relativo SDL entrega coordenadas virtuales sin límites, por lo que el
// arrastre continúa más allá de los bordes de la ventana y de la pantalla.
// ==========================================================================
void ViewportPanel::BeginGizmoMouseCapture() {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    m_GizmoMouseCaptured = true;
    SDL_SetRelativeMouseMode(SDL_TRUE);
    ImGui::GetIO().MouseDrawCursor = true;
#endif
}

void ViewportPanel::EndGizmoMouseCapture() {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    m_GizmoMouseCaptured = false;
    SDL_SetRelativeMouseMode(SDL_FALSE);
    ImGui::GetIO().MouseDrawCursor = false;
    // Devolver el cursor del sistema a la última posición virtual del arrastre
    SDL_Window* win = SlipEditor::GetInstance() ? SlipEditor::GetInstance()->GetWindow() : nullptr;
    if (win) {
        int w = 0, h = 0;
        SDL_GetWindowSize(win, &w, &h);
        float cx = ImGui::GetMousePos().x;
        float cy = ImGui::GetMousePos().y;
        if (w > 0 && h > 0) {
            cx = glm::clamp(cx, 0.0f, (float)(w - 1));
            cy = glm::clamp(cy, 0.0f, (float)(h - 1));
        }
        SDL_WarpMouseInWindow(win, (int)cx, (int)cy);
    }
#endif
}

// ==========================================================================
// Animación suave de la cámara al enfocar la selección (tecla F)
// ==========================================================================
void ViewportPanel::FocusOnSelection() {
    if (m_Selection.empty()) return;

    // Calcular centro y radio de la envolvente de la selección
    glm::vec3 minB(0.0f), maxB(0.0f);
    int count = 0;
    for (SlipEntity* ent : m_Selection) {
        if (!ent) continue;
        glm::vec3 pos = glm::vec3(ent->getMatrix()[3]);
        glm::vec3 halfExtent = glm::length(ent->scale) > 1e-4f ? ent->scale * 0.5f : glm::vec3(0.5f);
        if (count == 0) {
            minB = pos - halfExtent;
            maxB = pos + halfExtent;
        } else {
            minB = glm::min(minB, pos - halfExtent);
            maxB = glm::max(maxB, pos + halfExtent);
        }
        count++;
    }
    if (count == 0) return;

    glm::vec3 center = (minB + maxB) * 0.5f;
    float radius = glm::length(maxB - minB) * 0.5f;
    radius = (std::max)(radius, 1.0f);
    float endDist = glm::clamp(radius / std::tan(glm::radians(45.0f) * 0.5f) * 1.35f, 0.5f, 150.0f);

    m_FocusStartTarget = m_CameraTarget;
    m_FocusEndTarget = center;
    m_FocusStartDist = m_CameraDistance;
    m_FocusEndDist = endDist;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    // Fly Cam: animar posición + orientación para mirar al centro
    m_FocusStartPos = m_CameraPos;
    glm::vec3 focusDir = center - m_CameraPos;
    if (glm::length(focusDir) > 1e-4f) {
        m_FocusEndPos = center - glm::normalize(focusDir) * endDist;
        glm::vec3 dirEnd = glm::normalize(center - m_FocusEndPos);
        m_FocusEndPitch = glm::degrees(std::asin(glm::clamp(-dirEnd.y, -1.0f, 1.0f)));
        m_FocusEndYaw = glm::degrees(std::atan2(-dirEnd.x, -dirEnd.z));
        // Envolver el yaw final al camino más corto
        float yawDelta = m_FocusEndYaw - m_FocusStartYaw;
        while (yawDelta > 180.0f) yawDelta -= 360.0f;
        while (yawDelta < -180.0f) yawDelta += 360.0f;
        m_FocusEndYaw = m_FocusStartYaw + yawDelta;
    } else {
        m_FocusEndPos = m_FocusStartPos;
        m_FocusEndPitch = m_FocusStartPitch;
        m_FocusEndYaw = m_FocusStartYaw;
    }
#endif

    m_FocusActive = true;
    m_FocusProgress = 0.0f;
}

void ViewportPanel::UpdateFocusAnimation(float dt) {
    if (!m_FocusActive) return;
    m_FocusProgress = glm::min(m_FocusProgress + dt / m_FocusDuration, 1.0f);
    float t = m_FocusProgress;
    t = t * t * (3.0f - 2.0f * t); // smoothstep

    m_CameraTarget = glm::mix(m_FocusStartTarget, m_FocusEndTarget, t);
    m_CameraDistance = glm::mix(m_FocusStartDist, m_FocusEndDist, t);

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    m_CameraPos = glm::mix(m_FocusStartPos, m_FocusEndPos, t);
    m_CameraPitch = glm::mix(m_FocusStartPitch, m_FocusEndPitch, t);
    m_CameraYaw = glm::mix(m_FocusStartYaw, m_FocusEndYaw, t);
#else
    float radPitch = glm::radians(m_CameraPitch);
    float radYaw = glm::radians(m_CameraYaw);
    glm::vec3 orbitFwd(-cos(radPitch) * sin(radYaw), -sin(radPitch), -cos(radPitch) * cos(radYaw));
    m_CameraPos = m_CameraTarget - orbitFwd * m_CameraDistance;
#endif

    if (m_FocusProgress >= 1.0f) {
        m_FocusActive = false;
    }
}

// Snapshot de la multi-selección al iniciar un arrastre del gizmo
void ViewportPanel::BuildGizmoDragSnapshots() {
    m_GizmoDragSnapshots.clear();
    std::vector<SlipEntity*> list = m_Selection;
    if (list.empty() && m_SelectedEntity) list.push_back(m_SelectedEntity);
    for (SlipEntity* ent : list) {
        if (!ent) continue;
        GizmoDragSnapshot snap;
        snap.entity = ent;
        snap.initialPos = ent->position;
        snap.initialRot = ent->rotation;
        snap.initialScale = ent->scale;
        snap.initialWorldPos = glm::vec3(ent->getMatrix()[3]);
        if (auto rt = ent->GetComponent<RectTransformComponent>()) {
            snap.hasRect = true;
            snap.initialAnchoredPos = rt->anchoredPosition;
            snap.initialRectSize = rt->size;
        }
        m_GizmoDragSnapshots.push_back(snap);
    }
}

void ViewportPanel::DrawGizmo(const glm::mat4& view, const glm::mat4& projection) {
    m_GizmoHoveredAxis = -1;

    // Gestionar la captura continua del mouse mientras dura el arrastre
    if (m_ActiveGizmoAxis != -1 && !m_GizmoMouseCaptured) {
        BeginGizmoMouseCapture();
    } else if (m_ActiveGizmoAxis == -1 && m_GizmoMouseCaptured) {
        EndGizmoMouseCapture();
    }

    if (SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession()) {
        m_ActiveGizmoAxis = -1;
        return;
    }
    if (!m_SelectedEntity || m_GizmoMode == GizmoMode::None) {
        m_ActiveGizmoAxis = -1;
        return;
    }
    if (m_ViewportSize.x <= 10.0f || m_ViewportSize.y <= 10.0f) {
        m_ActiveGizmoAxis = -1;
        return;
    }

    glm::mat4 viewProj = projection * view;

    glm::vec3 eye;
    if (m_Is2DMode) {
        eye = glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 15.0f);
    } else {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
        eye = m_CameraPos;
#else
        float radPitch = glm::radians(m_CameraPitch);
        float radYaw = glm::radians(m_CameraYaw);
        eye.x = m_CameraTarget.x + m_CameraDistance * cos(radPitch) * sin(radYaw);
        eye.y = m_CameraTarget.y + m_CameraDistance * sin(radPitch);
        eye.z = m_CameraTarget.z + m_CameraDistance * cos(radPitch) * cos(radYaw);
#endif
    }

    glm::mat4 entityWorldMat = m_SelectedEntity->getMatrix();
    glm::vec3 entityPos = glm::vec3(entityWorldMat[3]);
    float camDist = m_Is2DMode ? m_CameraDistance : glm::length(eye - entityPos);

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    const float gizmoScale = 1.85f;
    const float gizmoHitDist = 32.0f;
    const float gizmoThickBase = 5.0f;
    const float gizmoThickHigh = 8.5f;
    const float gizmoHandleRadius = 14.0f;
    const float gizmoCenterRadius = 9.0f;
#else
    const float gizmoScale = 1.0f;
    const float gizmoHitDist = 12.0f;
    const float gizmoThickBase = 2.5f;
    const float gizmoThickHigh = 4.5f;
    const float gizmoHandleRadius = 6.0f;
    const float gizmoCenterRadius = 4.5f;
#endif

    float gizmoSize = (std::max)(0.45f * gizmoScale, camDist * 0.16f * gizmoScale);

    glm::vec2 origin2D;
    if (!ProjectToScreen(entityPos, viewProj, origin2D)) return;

    // Calcular ejes del Gizmo según el espacio (Global o Local)
    glm::vec3 axisX = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 axisY = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 axisZ = glm::vec3(0.0f, 0.0f, 1.0f);

    if (m_GizmoSpace == GizmoSpace::Local && !m_SelectedEntity->HasComponent<RectTransformComponent>()) {
        if (glm::length(glm::vec3(entityWorldMat[0])) > 1e-4f) axisX = glm::normalize(glm::vec3(entityWorldMat[0]));
        if (glm::length(glm::vec3(entityWorldMat[1])) > 1e-4f) axisY = glm::normalize(glm::vec3(entityWorldMat[1]));
        if (glm::length(glm::vec3(entityWorldMat[2])) > 1e-4f) axisZ = glm::normalize(glm::vec3(entityWorldMat[2]));
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 mousePos = ImGui::GetMousePos();
    bool isMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    // =========================================================================
    // MODO ROTACIÓN: GIZMO ESFERA CON 3 ANILLOS ORTOGONALES (ESTILO UNITY 3D)
    // =========================================================================
    if (m_GizmoMode == GizmoMode::Rotate) {
        const int segments = 48;
        const float step = (2.0f * 3.14159265359f) / segments;
        float R = gizmoSize * 1.1f;

        std::vector<glm::vec2> ringX(segments + 1), ringY(segments + 1), ringZ(segments + 1);
        std::vector<bool> validX(segments + 1), validY(segments + 1), validZ(segments + 1);

        for (int i = 0; i <= segments; ++i) {
            float ang = i * step;
            float c = cos(ang) * R;
            float s = sin(ang) * R;

            validX[i] = !m_Is2DMode && ProjectToScreen(entityPos + (axisY * c + axisZ * s), viewProj, ringX[i]);
            validY[i] = !m_Is2DMode && ProjectToScreen(entityPos + (axisZ * c + axisX * s), viewProj, ringY[i]);
            validZ[i] = ProjectToScreen(entityPos + (axisX * c + axisY * s), viewProj, ringZ[i]);
}

        int hoveredAxis = -1;
        float closestDist = gizmoHitDist;

        if (m_ActiveGizmoAxis == -1 && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            for (int i = 0; i < segments; ++i) {
                if (validX[i] && validX[i+1]) {
                    float d = DistancePointToSegment(mousePos, ImVec2(ringX[i].x, ringX[i].y), ImVec2(ringX[i+1].x, ringX[i+1].y));
                    if (d < closestDist) { closestDist = d; hoveredAxis = 0; }
        }
                if (validY[i] && validY[i+1]) {
                    float d = DistancePointToSegment(mousePos, ImVec2(ringY[i].x, ringY[i].y), ImVec2(ringY[i+1].x, ringY[i+1].y));
                    if (d < closestDist) { closestDist = d; hoveredAxis = 1; }
        }
                if (validZ[i] && validZ[i+1]) {
                    float d = DistancePointToSegment(mousePos, ImVec2(ringZ[i].x, ringZ[i].y), ImVec2(ringZ[i+1].x, ringZ[i+1].y));
                    if (d < closestDist) { closestDist = d; hoveredAxis = 2; }
        }
    }
            m_GizmoHoveredAxis = hoveredAxis;

            if (hoveredAxis != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                m_ActiveGizmoAxis = hoveredAxis;
                m_GizmoDragStartMouse = glm::vec2(mousePos.x, mousePos.y);
                m_GizmoInitialRot = m_SelectedEntity->rotation;
                m_GizmoDragOrigin2D = origin2D;
                m_GizmoStartAngle = atan2(mousePos.y - origin2D.y, mousePos.x - origin2D.x);
                BuildGizmoDragSnapshots();
    }
}

        if (m_ActiveGizmoAxis != -1) {
            if (isMouseDown) {
                float currentAngle = atan2(mousePos.y - m_GizmoDragOrigin2D.y, mousePos.x - m_GizmoDragOrigin2D.x);
                float angleDelta = currentAngle - m_GizmoStartAngle;
                while (angleDelta > 3.14159265f) angleDelta -= 2.0f * 3.14159265f;
                while (angleDelta < -3.14159265f) angleDelta += 2.0f * 3.14159265f;

                m_GizmoStartAngle = currentAngle;

                glm::vec3 activeAxis = (m_ActiveGizmoAxis == 0) ? axisX : ((m_ActiveGizmoAxis == 1) ? axisY : axisZ);
                float camDot = glm::dot(activeAxis, glm::normalize(eye - entityPos));
                float rotDeltaRad = (camDot >= 0.0f) ? -angleDelta : angleDelta;

                glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(m_SelectedEntity->rotation.x), glm::vec3(1, 0, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(m_SelectedEntity->rotation.y), glm::vec3(0, 1, 0)) *
                                   glm::rotate(glm::mat4(1.0f), glm::radians(m_SelectedEntity->rotation.z), glm::vec3(0, 0, 1));

                glm::mat4 deltaRot = glm::rotate(glm::mat4(1.0f), rotDeltaRad, activeAxis);
                glm::mat4 newRotMat = deltaRot * rotMat;

                if (m_SelectedEntity->GetParent()) {
                    glm::mat3 parentRot = glm::mat3(m_SelectedEntity->GetParent()->getMatrix());
                    float pLenX = glm::length(parentRot[0]);
                    float pLenY = glm::length(parentRot[1]);
                    float pLenZ = glm::length(parentRot[2]);
                    if (pLenX > 1e-4f) parentRot[0] /= pLenX;
                    if (pLenY > 1e-4f) parentRot[1] /= pLenY;
                    if (pLenZ > 1e-4f) parentRot[2] /= pLenZ;
                    glm::mat4 invParentRot = glm::mat4(glm::transpose(parentRot));
                    glm::mat4 localDeltaRot = invParentRot * deltaRot * glm::mat4(parentRot);
                    newRotMat = localDeltaRot * rotMat;
        }

                float rx = 0.0f, ry = 0.0f, rz = 0.0f;
                glm::extractEulerAngleXYZ(newRotMat, rx, ry, rz);
                m_SelectedEntity->rotation = glm::vec3(glm::degrees(rx), glm::degrees(ry), glm::degrees(rz));
                if (auto bc = m_SelectedEntity->GetComponent<BoneComponent>()) {
                    bc->orientation = glm::normalize(glm::quat_cast(newRotMat));
        }

                if (auto tc = m_SelectedEntity->GetComponent<TransformComponent>()) {
                    tc->rotation = m_SelectedEntity->rotation;
        }

                // Rotar el resto de la multi-selección alrededor del pivote (posición de la primaria)
                for (auto& snap : m_GizmoDragSnapshots) {
                    if (!snap.entity || snap.entity == m_SelectedEntity) continue;
                    SlipEntity* ent = snap.entity;

                    // Orbitar la posición mundial alrededor del pivote
                    glm::vec3 worldPos = entityPos + glm::vec3(deltaRot * glm::vec4(snap.initialWorldPos - entityPos, 1.0f));
                    if (ent->GetParent()) {
                        glm::mat4 invParent = glm::inverse(ent->GetParent()->getMatrix());
                        ent->position = glm::vec3(invParent * glm::vec4(worldPos, 1.0f));
                    } else {
                        ent->position = worldPos;
                    }

                    // Aplicar la misma rotación delta a la orientación local de la entidad
                    glm::mat4 ownRotMat = glm::rotate(glm::mat4(1.0f), glm::radians(snap.initialRot.x), glm::vec3(1, 0, 0)) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(snap.initialRot.y), glm::vec3(0, 1, 0)) *
                                          glm::rotate(glm::mat4(1.0f), glm::radians(snap.initialRot.z), glm::vec3(0, 0, 1));
                    glm::mat4 newOwnRotMat = deltaRot * ownRotMat;
                    if (ent->GetParent()) {
                        glm::mat3 parentRot = glm::mat3(ent->GetParent()->getMatrix());
                        float pLenX = glm::length(parentRot[0]);
                        float pLenY = glm::length(parentRot[1]);
                        float pLenZ = glm::length(parentRot[2]);
                        if (pLenX > 1e-4f) parentRot[0] /= pLenX;
                        if (pLenY > 1e-4f) parentRot[1] /= pLenY;
                        if (pLenZ > 1e-4f) parentRot[2] /= pLenZ;
                        glm::mat4 invParentRot = glm::mat4(glm::transpose(parentRot));
                        newOwnRotMat = invParentRot * deltaRot * glm::mat4(parentRot) * ownRotMat;
                    }
                    float orx = 0.0f, ory = 0.0f, orz = 0.0f;
                    glm::extractEulerAngleXYZ(newOwnRotMat, orx, ory, orz);
                    ent->rotation = glm::vec3(glm::degrees(orx), glm::degrees(ory), glm::degrees(orz));

                    if (auto tc = ent->GetComponent<TransformComponent>()) {
                        tc->position = ent->position;
                        tc->rotation = ent->rotation;
                        tc->scale = ent->scale;
                    }
                    if (auto col = ent->GetComponent<ColliderComponent>()) {
                        col->SyncTransformFromEntity();
                    }
                    for (SlipEntity* p = ent; p != nullptr; p = p->GetParent()) {
                        if (auto smc = p->GetComponent<SkinnedMeshComponent>()) {
                            smc->MarkBoneMatricesDirty();
                            smc->UpdateBoneMatrices();
                            break;
                        }
                    }
                }

                // Notificar ancestros SkinnedMeshComponent para reflejar la deformación inmediata
                for (SlipEntity* p = m_SelectedEntity; p != nullptr; p = p->GetParent()) {
                    if (auto smc = p->GetComponent<SkinnedMeshComponent>()) {
                        smc->MarkBoneMatricesDirty();
                        smc->UpdateBoneMatrices();
                        break;
            }
        }
    } else {
                m_ActiveGizmoAxis = -1;
    }
}

        drawList->AddCircle(ImVec2(origin2D.x, origin2D.y), (R / (std::max)(0.5f, camDist)) * (m_ViewportSize.y * 0.75f), IM_COL32(180, 180, 180, 70), 48, 1.5f);

        ImU32 colX = (m_ActiveGizmoAxis == 0 || hoveredAxis == 0) ? IM_COL32(255, 235, 60, 255) : IM_COL32(235, 65, 65, 230);
        ImU32 colY = (m_ActiveGizmoAxis == 1 || hoveredAxis == 1) ? IM_COL32(255, 235, 60, 255) : IM_COL32(65, 225, 65, 230);
        ImU32 colZ = (m_ActiveGizmoAxis == 2 || hoveredAxis == 2) ? IM_COL32(255, 235, 60, 255) : IM_COL32(65, 135, 255, 230);

        float thickX = (m_ActiveGizmoAxis == 0 || hoveredAxis == 0) ? gizmoThickHigh : gizmoThickBase;
        float thickY = (m_ActiveGizmoAxis == 1 || hoveredAxis == 1) ? gizmoThickHigh : gizmoThickBase;
        float thickZ = (m_ActiveGizmoAxis == 2 || hoveredAxis == 2) ? gizmoThickHigh : gizmoThickBase;

        for (int i = 0; i < segments; ++i) {
            if (validX[i] && validX[i+1]) drawList->AddLine(ImVec2(ringX[i].x, ringX[i].y), ImVec2(ringX[i+1].x, ringX[i+1].y), colX, thickX);
            if (validY[i] && validY[i+1]) drawList->AddLine(ImVec2(ringY[i].x, ringY[i].y), ImVec2(ringY[i+1].x, ringY[i+1].y), colY, thickY);
            if (validZ[i] && validZ[i+1]) drawList->AddLine(ImVec2(ringZ[i].x, ringZ[i].y), ImVec2(ringZ[i+1].x, ringZ[i+1].y), colZ, thickZ);
}

        drawList->AddCircleFilled(ImVec2(origin2D.x, origin2D.y), gizmoCenterRadius, IM_COL32(245, 245, 245, 255));
        return;
    }

    // =========================================================================
    // MODO TRASLACIÓN Y ESCALA: EJES AXIALES + GIZMOS PLANARES DE 2 EJES (XY, XZ, YZ)
    // =========================================================================
    glm::vec3 xEnd3D = entityPos + axisX * gizmoSize;
    glm::vec3 yEnd3D = entityPos + axisY * gizmoSize;
    glm::vec3 zEnd3D = entityPos + axisZ * gizmoSize;

    glm::vec2 xScreen(0.0f), yScreen(0.0f), zScreen(0.0f);
    bool hasX = ProjectToScreen(xEnd3D, viewProj, xScreen);
    bool hasY = ProjectToScreen(yEnd3D, viewProj, yScreen);
    bool hasZ = !m_Is2DMode && ProjectToScreen(zEnd3D, viewProj, zScreen);

    // Cuadrados de movimiento y escala planar en 2 ejes (estilo Unity3D)
    // 3: Plano XY, 4: Plano XZ, 5: Plano YZ
    float pOff = gizmoSize * 0.10f;
    float pExt = gizmoSize * 0.38f;

    glm::vec2 xyQ[4], xzQ[4], yzQ[4];
    bool hasXY = ProjectToScreen(entityPos + axisX * pOff + axisY * pOff, viewProj, xyQ[0]) &&
                 ProjectToScreen(entityPos + axisX * pExt + axisY * pOff, viewProj, xyQ[1]) &&
                 ProjectToScreen(entityPos + axisX * pExt + axisY * pExt, viewProj, xyQ[2]) &&
                 ProjectToScreen(entityPos + axisX * pOff + axisY * pExt, viewProj, xyQ[3]);

    bool hasXZ = !m_Is2DMode &&
                 ProjectToScreen(entityPos + axisX * pOff + axisZ * pOff, viewProj, xzQ[0]) &&
                 ProjectToScreen(entityPos + axisX * pExt + axisZ * pOff, viewProj, xzQ[1]) &&
                 ProjectToScreen(entityPos + axisX * pExt + axisZ * pExt, viewProj, xzQ[2]) &&
                 ProjectToScreen(entityPos + axisX * pOff + axisZ * pExt, viewProj, xzQ[3]);

    bool hasYZ = !m_Is2DMode &&
                 ProjectToScreen(entityPos + axisY * pOff + axisZ * pOff, viewProj, yzQ[0]) &&
                 ProjectToScreen(entityPos + axisY * pExt + axisZ * pOff, viewProj, yzQ[1]) &&
                 ProjectToScreen(entityPos + axisY * pExt + axisZ * pExt, viewProj, yzQ[2]) &&
                 ProjectToScreen(entityPos + axisY * pOff + axisZ * pExt, viewProj, yzQ[3]);

    // Detección de Hover (prioridad a los cuadrados de 2 ejes y luego a los ejes individuales)
    int hoveredAxis = -1;
    float closestDist = gizmoHitDist;

    if (m_ActiveGizmoAxis == -1 && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (hasXY && PointInQuad2D(mousePos, ImVec2(xyQ[0].x, xyQ[0].y), ImVec2(xyQ[1].x, xyQ[1].y), ImVec2(xyQ[2].x, xyQ[2].y), ImVec2(xyQ[3].x, xyQ[3].y))) {
            hoveredAxis = 3; // XY
} else if (hasXZ && PointInQuad2D(mousePos, ImVec2(xzQ[0].x, xzQ[0].y), ImVec2(xzQ[1].x, xzQ[1].y), ImVec2(xzQ[2].x, xzQ[2].y), ImVec2(xzQ[3].x, xzQ[3].y))) {
            hoveredAxis = 4; // XZ
} else if (hasYZ && PointInQuad2D(mousePos, ImVec2(yzQ[0].x, yzQ[0].y), ImVec2(yzQ[1].x, yzQ[1].y), ImVec2(yzQ[2].x, yzQ[2].y), ImVec2(yzQ[3].x, yzQ[3].y))) {
            hoveredAxis = 5; // YZ
} else {
            if (hasX) {
                float d = DistancePointToSegment(mousePos, ImVec2(origin2D.x, origin2D.y), ImVec2(xScreen.x, xScreen.y));
                if (d < closestDist) { closestDist = d; hoveredAxis = 0; }
    }
            if (hasY) {
                float d = DistancePointToSegment(mousePos, ImVec2(origin2D.x, origin2D.y), ImVec2(yScreen.x, yScreen.y));
                if (d < closestDist) { closestDist = d; hoveredAxis = 1; }
    }
            if (hasZ) {
                float d = DistancePointToSegment(mousePos, ImVec2(origin2D.x, origin2D.y), ImVec2(zScreen.x, zScreen.y));
                if (d < closestDist) { closestDist = d; hoveredAxis = 2; }
    }
}
        m_GizmoHoveredAxis = hoveredAxis;

        if (hoveredAxis != -1 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_ActiveGizmoAxis = hoveredAxis;
            m_GizmoDragStartMouse = glm::vec2(mousePos.x, mousePos.y);
            m_GizmoInitialPos = m_SelectedEntity->position;
            m_GizmoInitialRot = m_SelectedEntity->rotation;
            m_GizmoInitialScale = m_SelectedEntity->scale;
            m_GizmoDragOrigin2D = origin2D;
            BuildGizmoDragSnapshots();

            if (auto rt = m_SelectedEntity->GetComponent<RectTransformComponent>()) {
                m_GizmoInitialAnchoredPos = rt->anchoredPosition;
                m_GizmoInitialRectSize = rt->size;
    }

            // Bloquear vectores de dirección y sensibilidad mundo/píxel al inicio del arrastre
            // Esto garantiza sensibilidad 1:1 constante hasta los bordes del viewport sin saltos ni zonas muertas
            glm::vec2 vecX = xScreen - origin2D;
            float lenX = glm::length(vecX);
            m_GizmoDragAxisDirX = (lenX > 1.0f) ? (vecX / lenX) : glm::vec2(1.0f, 0.0f);
            m_GizmoDragWorldPerPixelX = gizmoSize / (std::max)(lenX, 16.0f);

            glm::vec2 vecY = yScreen - origin2D;
            float lenY = glm::length(vecY);
            m_GizmoDragAxisDirY = (lenY > 1.0f) ? (vecY / lenY) : glm::vec2(0.0f, -1.0f);
            m_GizmoDragWorldPerPixelY = gizmoSize / (std::max)(lenY, 16.0f);

            glm::vec2 vecZ = zScreen - origin2D;
            float lenZ = glm::length(vecZ);
            m_GizmoDragAxisDirZ = (lenZ > 1.0f) ? (vecZ / lenZ) : glm::vec2(0.0f, 1.0f);
            m_GizmoDragWorldPerPixelZ = gizmoSize / (std::max)(lenZ, 16.0f);
}
    }

    // Proceso de arrastre con sensibilidad lineal constante
    if (m_ActiveGizmoAxis != -1) {
        if (isMouseDown) {
            glm::vec2 mouseDelta = glm::vec2(mousePos.x - m_GizmoDragStartMouse.x, mousePos.y - m_GizmoDragStartMouse.y);

            auto solvePlanarMove = [&](const glm::vec2& dirA, float wppA, const glm::vec2& dirB, float wppB) -> glm::vec2 {
                // vA y vB son los píxeles de pantalla por cada 1 unidad de mundo en los ejes A y B
                glm::vec2 vA = dirA / (std::max)(wppA, 1e-5f);
                glm::vec2 vB = dirB / (std::max)(wppB, 1e-5f);
                float det = vA.x * vB.y - vA.y * vB.x;
                if (std::abs(det) > 25.0f) {
                    float dA = (mouseDelta.x * vB.y - mouseDelta.y * vB.x) / det;
                    float dB = (vA.x * mouseDelta.y - vA.y * mouseDelta.x) / det;
                    return glm::vec2(dA, dB);
        }
                return glm::vec2(glm::dot(mouseDelta, dirA) * wppA, glm::dot(mouseDelta, dirB) * wppB);
    };

            glm::vec3 gizmoWorldOffset(0.0f);
            glm::vec3 gizmoScaleFactor(1.0f);

            if (m_GizmoMode == GizmoMode::Translate) {
                glm::vec3 worldOffset(0.0f);
                if (m_ActiveGizmoAxis == 0) {
                    worldOffset = axisX * (glm::dot(mouseDelta, m_GizmoDragAxisDirX) * m_GizmoDragWorldPerPixelX);
        } else if (m_ActiveGizmoAxis == 1) {
                    worldOffset = axisY * (glm::dot(mouseDelta, m_GizmoDragAxisDirY) * m_GizmoDragWorldPerPixelY);
        } else if (m_ActiveGizmoAxis == 2) {
                    worldOffset = axisZ * (glm::dot(mouseDelta, m_GizmoDragAxisDirZ) * m_GizmoDragWorldPerPixelZ);
        } else if (m_ActiveGizmoAxis == 3) { // Plano XY
                    glm::vec2 dXY = solvePlanarMove(m_GizmoDragAxisDirX, m_GizmoDragWorldPerPixelX, m_GizmoDragAxisDirY, m_GizmoDragWorldPerPixelY);
                    worldOffset = axisX * dXY.x + axisY * dXY.y;
        } else if (m_ActiveGizmoAxis == 4) { // Plano XZ
                    glm::vec2 dXZ = solvePlanarMove(m_GizmoDragAxisDirX, m_GizmoDragWorldPerPixelX, m_GizmoDragAxisDirZ, m_GizmoDragWorldPerPixelZ);
                    worldOffset = axisX * dXZ.x + axisZ * dXZ.y;
        } else if (m_ActiveGizmoAxis == 5) { // Plano YZ
                    glm::vec2 dYZ = solvePlanarMove(m_GizmoDragAxisDirY, m_GizmoDragWorldPerPixelY, m_GizmoDragAxisDirZ, m_GizmoDragWorldPerPixelZ);
                    worldOffset = axisY * dYZ.x + axisZ * dYZ.y;
        }
                gizmoWorldOffset = worldOffset;

                if (m_SelectedEntity->GetParent()) {
                    glm::mat4 invParent = glm::inverse(m_SelectedEntity->GetParent()->getMatrix());
                    glm::vec3 localOffset = glm::vec3(invParent * glm::vec4(worldOffset, 0.0f));
                    m_SelectedEntity->position = m_GizmoInitialPos + localOffset;
                } else {
                    m_SelectedEntity->position = m_GizmoInitialPos + worldOffset;
                }

                if (m_SnapToGrid2D && m_GridSnapSize > 0.001f) {
                    m_SelectedEntity->position.x = std::round(m_SelectedEntity->position.x / m_GridSnapSize) * m_GridSnapSize;
                    m_SelectedEntity->position.y = std::round(m_SelectedEntity->position.y / m_GridSnapSize) * m_GridSnapSize;
                }

                if (auto rt = m_SelectedEntity->GetComponent<RectTransformComponent>()) {
                    rt->anchoredPosition = m_GizmoInitialAnchoredPos + glm::vec2(worldOffset.x * 100.0f, worldOffset.y * 100.0f);
        }
    } else if (m_GizmoMode == GizmoMode::Scale) {
                float dx = glm::dot(mouseDelta, m_GizmoDragAxisDirX) * 0.015f;
                float dy = glm::dot(mouseDelta, m_GizmoDragAxisDirY) * 0.015f;
                float dz = glm::dot(mouseDelta, m_GizmoDragAxisDirZ) * 0.015f;

                if (auto rt = m_SelectedEntity->GetComponent<RectTransformComponent>()) {
                    glm::vec2 newSize = m_GizmoInitialRectSize;
                    if (m_ActiveGizmoAxis == 0 || m_ActiveGizmoAxis == 3 || m_ActiveGizmoAxis == 4) {
                        newSize.x = (std::max)(4.0f, m_GizmoInitialRectSize.x * (std::max)(0.05f, 1.0f + dx));
            }
                    if (m_ActiveGizmoAxis == 1 || m_ActiveGizmoAxis == 3 || m_ActiveGizmoAxis == 5) {
                        newSize.y = (std::max)(4.0f, m_GizmoInitialRectSize.y * (std::max)(0.05f, 1.0f + dy));
            }
                    rt->size = newSize;
        } else {
                    glm::vec3 newScale = m_GizmoInitialScale;
                    if (m_ActiveGizmoAxis == 0) newScale.x = (std::max)(0.01f, m_GizmoInitialScale.x + dx);
                    else if (m_ActiveGizmoAxis == 1) newScale.y = (std::max)(0.01f, m_GizmoInitialScale.y + dy);
                    else if (m_ActiveGizmoAxis == 2) newScale.z = (std::max)(0.01f, m_GizmoInitialScale.z + dz);
                    else if (m_ActiveGizmoAxis == 3) { // Escalar en X e Y simultáneamente
                        newScale.x = (std::max)(0.01f, m_GizmoInitialScale.x + dx);
                        newScale.y = (std::max)(0.01f, m_GizmoInitialScale.y + dy);
            } else if (m_ActiveGizmoAxis == 4) { // Escalar en X y Z simultáneamente
                        newScale.x = (std::max)(0.01f, m_GizmoInitialScale.x + dx);
                        newScale.z = (std::max)(0.01f, m_GizmoInitialScale.z + dz);
            } else if (m_ActiveGizmoAxis == 5) { // Escalar en Y y Z simultáneamente
                        newScale.y = (std::max)(0.01f, m_GizmoInitialScale.y + dy);
                        newScale.z = (std::max)(0.01f, m_GizmoInitialScale.z + dz);
            }
                    m_SelectedEntity->scale = newScale;
                    glm::vec3 safeInit = glm::max(glm::abs(m_GizmoInitialScale), glm::vec3(1e-4f));
                    gizmoScaleFactor = glm::max(newScale / safeInit, glm::vec3(0.0f));
        }
    }

            // Aplicar el mismo delta a la multi-selección (mover/escalar todos juntos)
            for (auto& snap : m_GizmoDragSnapshots) {
                if (!snap.entity || snap.entity == m_SelectedEntity) continue;
                SlipEntity* ent = snap.entity;

                if (m_GizmoMode == GizmoMode::Translate) {
                    if (ent->GetParent()) {
                        glm::mat4 invParentMulti = glm::inverse(ent->GetParent()->getMatrix());
                        ent->position = snap.initialPos + glm::vec3(invParentMulti * glm::vec4(gizmoWorldOffset, 0.0f));
                    } else {
                        ent->position = snap.initialPos + gizmoWorldOffset;
                    }
                    if (m_SnapToGrid2D && m_GridSnapSize > 0.001f) {
                        ent->position.x = std::round(ent->position.x / m_GridSnapSize) * m_GridSnapSize;
                        ent->position.y = std::round(ent->position.y / m_GridSnapSize) * m_GridSnapSize;
                    }
                } else if (m_GizmoMode == GizmoMode::Scale) {
                    ent->scale = glm::max(snap.initialScale * gizmoScaleFactor, glm::vec3(0.01f));
                }

                if (auto tc = ent->GetComponent<TransformComponent>()) {
                    tc->position = ent->position;
                    tc->rotation = ent->rotation;
                    tc->scale = ent->scale;
                }
                if (auto col = ent->GetComponent<ColliderComponent>()) {
                    col->SyncTransformFromEntity();
                }
                for (SlipEntity* p = ent; p != nullptr; p = p->GetParent()) {
                    if (auto smc = p->GetComponent<SkinnedMeshComponent>()) {
                        smc->MarkBoneMatricesDirty();
                        smc->UpdateBoneMatrices();
                        break;
                    }
                }
            }

            // Sincronizar con TransformComponent y Collider
            if (auto tc = m_SelectedEntity->GetComponent<TransformComponent>()) {
                tc->position = m_SelectedEntity->position;
                tc->rotation = m_SelectedEntity->rotation;
                tc->scale = m_SelectedEntity->scale;
    }
            if (auto col = m_SelectedEntity->GetComponent<ColliderComponent>()) {
                col->SyncTransformFromEntity();
    }

            // Notificar ancestros SkinnedMeshComponent para reflejar la deformación inmediata
            for (SlipEntity* p = m_SelectedEntity; p != nullptr; p = p->GetParent()) {
                if (auto smc = p->GetComponent<SkinnedMeshComponent>()) {
                    smc->MarkBoneMatricesDirty();
                    smc->UpdateBoneMatrices();
                    break;
        }
    }
} else {
            m_ActiveGizmoAxis = -1;
}
    }

    // Dibujo de los cuadrados de 2 ejes (XY, XZ, YZ) estilo Unity3D
    auto drawPlanarQuad = [&](const glm::vec2 q[4], bool activeOrHovered, ImU32 baseFill, ImU32 baseBorder) {
        ImU32 fillCol = activeOrHovered ? IM_COL32(255, 235, 60, 130) : baseFill;
        ImU32 borderCol = activeOrHovered ? IM_COL32(255, 235, 60, 255) : baseBorder;
        ImVec2 pts[4] = {
            ImVec2(q[0].x, q[0].y),
            ImVec2(q[1].x, q[1].y),
            ImVec2(q[2].x, q[2].y),
            ImVec2(q[3].x, q[3].y)
};
        drawList->AddConvexPolyFilled(pts, 4, fillCol);
        drawList->AddPolyline(pts, 4, borderCol, ImDrawFlags_Closed, activeOrHovered ? 2.5f : 1.5f);
    };

    if (hasYZ) {
        drawPlanarQuad(yzQ, (m_ActiveGizmoAxis == 5 || hoveredAxis == 5), IM_COL32(235, 65, 65, 85), IM_COL32(235, 65, 65, 225));
    }
    if (hasXZ) {
        drawPlanarQuad(xzQ, (m_ActiveGizmoAxis == 4 || hoveredAxis == 4), IM_COL32(65, 225, 65, 85), IM_COL32(65, 225, 65, 225));
    }
    if (hasXY) {
        drawPlanarQuad(xyQ, (m_ActiveGizmoAxis == 3 || hoveredAxis == 3), IM_COL32(65, 135, 255, 85), IM_COL32(65, 135, 255, 225));
    }

    // Colores y grosores de los ejes principales (se iluminan también cuando se activa un plano que contiene ese eje)
    bool highlightX = (m_ActiveGizmoAxis == 0 || hoveredAxis == 0 || m_ActiveGizmoAxis == 3 || hoveredAxis == 3 || m_ActiveGizmoAxis == 4 || hoveredAxis == 4);
    bool highlightY = (m_ActiveGizmoAxis == 1 || hoveredAxis == 1 || m_ActiveGizmoAxis == 3 || hoveredAxis == 3 || m_ActiveGizmoAxis == 5 || hoveredAxis == 5);
    bool highlightZ = (m_ActiveGizmoAxis == 2 || hoveredAxis == 2 || m_ActiveGizmoAxis == 4 || hoveredAxis == 4 || m_ActiveGizmoAxis == 5 || hoveredAxis == 5);

    ImU32 colX = highlightX ? IM_COL32(255, 235, 60, 255) : IM_COL32(235, 65, 65, 240);
    ImU32 colY = highlightY ? IM_COL32(255, 235, 60, 255) : IM_COL32(65, 225, 65, 240);
    ImU32 colZ = highlightZ ? IM_COL32(255, 235, 60, 255) : IM_COL32(65, 135, 255, 240);

    float thickX = highlightX ? gizmoThickHigh : gizmoThickBase;
    float thickY = highlightY ? gizmoThickHigh : gizmoThickBase;
    float thickZ = highlightZ ? gizmoThickHigh : gizmoThickBase;

    // Eje X
    if (hasX) {
        drawList->AddLine(ImVec2(origin2D.x, origin2D.y), ImVec2(xScreen.x, xScreen.y), colX, thickX);
        if (m_GizmoMode == GizmoMode::Translate) {
            drawList->AddCircleFilled(ImVec2(xScreen.x, xScreen.y), gizmoHandleRadius, colX);
} else if (m_GizmoMode == GizmoMode::Scale) {
            float hr = gizmoHandleRadius * 0.75f;
            drawList->AddRectFilled(ImVec2(xScreen.x - hr, xScreen.y - hr), ImVec2(xScreen.x + hr, xScreen.y + hr), colX);
}
        drawList->AddText(ImVec2(xScreen.x + gizmoHandleRadius + 2.0f, xScreen.y - 8), colX, "X");
    }

    // Eje Y
    if (hasY) {
        drawList->AddLine(ImVec2(origin2D.x, origin2D.y), ImVec2(yScreen.x, yScreen.y), colY, thickY);
        if (m_GizmoMode == GizmoMode::Translate) {
            drawList->AddCircleFilled(ImVec2(yScreen.x, yScreen.y), gizmoHandleRadius, colY);
} else if (m_GizmoMode == GizmoMode::Scale) {
            float hr = gizmoHandleRadius * 0.75f;
            drawList->AddRectFilled(ImVec2(yScreen.x - hr, yScreen.y - hr), ImVec2(yScreen.x + hr, yScreen.y + hr), colY);
}
        drawList->AddText(ImVec2(yScreen.x + gizmoHandleRadius + 2.0f, yScreen.y - 8), colY, "Y");
    }

    // Eje Z
    if (hasZ) {
        drawList->AddLine(ImVec2(origin2D.x, origin2D.y), ImVec2(zScreen.x, zScreen.y), colZ, thickZ);
        if (m_GizmoMode == GizmoMode::Translate) {
            drawList->AddCircleFilled(ImVec2(zScreen.x, zScreen.y), gizmoHandleRadius, colZ);
} else if (m_GizmoMode == GizmoMode::Scale) {
            float hr = gizmoHandleRadius * 0.75f;
            drawList->AddRectFilled(ImVec2(zScreen.x - hr, zScreen.y - hr), ImVec2(zScreen.x + hr, zScreen.y + hr), colZ);
}
        drawList->AddText(ImVec2(zScreen.x + gizmoHandleRadius + 2.0f, zScreen.y - 8), colZ, "Z");
    }

    // Centro del Gizmo
    drawList->AddCircleFilled(ImVec2(origin2D.x, origin2D.y), gizmoCenterRadius, IM_COL32(245, 245, 245, 255));
}

void ViewportPanel::OnImGuiRender() {
    if (!m_IsOpen) return;

    bool androidGameView = SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsAndroidGameView();

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    if (androidGameView && mainViewport) {
        ImGui::SetNextWindowPos(mainViewport->WorkPos);
        ImGui::SetNextWindowSize(mainViewport->WorkSize);
        ImGui::SetNextWindowViewport(mainViewport->ID);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (androidGameView) {
        windowFlags |= ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                       ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;
    }

    if (ImGui::Begin("Viewport", &m_IsOpen, windowFlags)) {
        ImGuiID dockId = ImGui::GetWindowDockID();
        if (dockId == 0 && !androidGameView) {
            static int s_UndockedFrameCount = 0;
            if (++s_UndockedFrameCount > 1) {
                s_UndockedFrameCount = 0;
                if (SlipEditor::GetInstance()) SlipEditor::GetInstance()->ResetDefaultLayout();
    }
}
        m_IsFocused = ImGui::IsWindowFocused();
        m_IsHovered = ImGui::IsWindowHovered();

        // 1. Barra de Herramientas superior del Viewport (estilo Unity/Unreal: siempre visible en edición y oculta en modo de juego)
        if (!androidGameView) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
        float topSpacing = 12.0f;
        float btnHeight = 32.0f;
        bool isCompact = true;
#else
        float topSpacing = 6.0f;
        float btnHeight = ImGui::GetFrameHeight() + 2.0f;
        bool isCompact = (ImGui::GetWindowWidth() < 750.0f);
#endif
        ImGui::Dummy(ImVec2(0.0f, topSpacing));

        float barHeight = btnHeight + 14.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.11f, 0.15f, 0.98f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
        if (ImGui::BeginChild("##ViewportTopBar", ImVec2(0.0f, barHeight), false,
            ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(isCompact ? 6.0f : 8.0f, 4.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(isCompact ? 4.0f : 6.0f, 4.0f));

            auto drawGizmoBtn = [&](const char* label, bool active) -> bool {
                ImGui::SetNextItemAllowOverlap();
                if (active) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.48f, 0.92f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.56f, 1.0f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
        }
                bool clicked = ImGui::Button(label, ImVec2(0.0f, btnHeight));
                ImGui::PopStyleColor(3);
                return clicked;
    };

            const char* moveLabel = isCompact ? " Mover " : " [W] Mover ";
            const char* rotateLabel = isCompact ? " Rotar " : " [E] Rotar ";
            const char* scaleLabel = isCompact ? " Escalar " : " [R] Escalar ";
            const char* freeLabel = isCompact ? " Libre " : " [Q] Libre ";

            if (drawGizmoBtn(moveLabel, m_GizmoMode == GizmoMode::Translate)) m_GizmoMode = GizmoMode::Translate;
            ImGui::SameLine();
            if (drawGizmoBtn(rotateLabel, m_GizmoMode == GizmoMode::Rotate)) m_GizmoMode = GizmoMode::Rotate;
            ImGui::SameLine();
            if (drawGizmoBtn(scaleLabel, m_GizmoMode == GizmoMode::Scale)) m_GizmoMode = GizmoMode::Scale;
            ImGui::SameLine();
            if (drawGizmoBtn(freeLabel, m_GizmoMode == GizmoMode::None)) m_GizmoMode = GizmoMode::None;
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            if (m_GizmoSpace == GizmoSpace::Local) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.55f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.16f, 0.65f, 0.42f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                const char* localLabel = isCompact ? " Local " : " [X] Local ";
                if (ImGui::Button(localLabel, ImVec2(0.0f, btnHeight))) m_GizmoSpace = GizmoSpace::World;
                ImGui::PopStyleColor(3);
    } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
                const char* globalLabel = isCompact ? " Global " : " [X] Global ";
                if (ImGui::Button(globalLabel, ImVec2(0.0f, btnHeight))) m_GizmoSpace = GizmoSpace::Local;
                ImGui::PopStyleColor(3);
    }

            // Alternador de Cámara (Cámara de Editor vs Cámara del Juego estilo Unity)
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            bool isGameCam = (m_ActiveCameraType == ViewportCameraType::GameCamera);
            const char* camLabel = isGameCam ? (isCompact ? " Cam: Juego " : " [C] Cam: Juego ")
                                             : (isCompact ? " Cam: Editor " : " [C] Cam: Editor ");
            if (isGameCam) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.42f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.50f, 0.18f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
    }
            if (ImGui::Button(camLabel, ImVec2(0.0f, btnHeight))) {
                m_ActiveCameraType = isGameCam ? ViewportCameraType::EditorCamera : ViewportCameraType::GameCamera;
    }
            ImGui::PopStyleColor(3);

            // Alternador de visualización de Gizmos
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            const char* gizmoLabel = m_ShowGizmos ? (isCompact ? " Gizmos: ON " : " [G] Gizmos: ON ")
                                                  : (isCompact ? " Gizmos: OFF " : " [G] Gizmos: OFF ");
            if (m_ShowGizmos) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.58f, 0.38f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
    }
            if (ImGui::Button(gizmoLabel, ImVec2(0.0f, btnHeight))) {
                m_ShowGizmos = !m_ShowGizmos;
    }
            ImGui::PopStyleColor(3);

            // Alternador de visualización de la Grilla 3D/2D (Grid ON/OFF)
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            const char* gridLabel = m_ShowGrid ? " Grid: ON " : " Grid: OFF ";
            if (m_ShowGrid) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.72f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.54f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
    }
            if (ImGui::Button(gridLabel, ImVec2(0.0f, btnHeight))) {
                m_ShowGrid = !m_ShowGrid;
    }
            ImGui::PopStyleColor(3);

            // Alternador de modo de vista 2D / 3D (estilo Unity3D)
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            const char* mode2DLabel = m_Is2DMode ? (isCompact ? " 2D " : " [2D] Vista: 2D ")
                                                 : (isCompact ? " 3D " : " [3D] Vista: 3D ");
            if (m_Is2DMode) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.28f, 0.82f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.36f, 0.92f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
    }
            if (ImGui::Button(mode2DLabel, ImVec2(0.0f, btnHeight))) {
                m_Is2DMode = !m_Is2DMode;
                if (m_Is2DMode) {
                    m_CameraPitch = 0.0f;
                    m_CameraYaw = 0.0f;
                }
            }
            ImGui::PopStyleColor(3);

            // Magnetismo por cuadrícula 2D (Grid Snap)
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            const char* snapLabel = m_SnapToGrid2D ? (isCompact ? " Snap " : " [#] Snap: ON ")
                                                   : (isCompact ? " Snap " : " [#] Snap: OFF ");
            if (m_SnapToGrid2D) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.65f, 0.40f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f, 0.75f, 0.48f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
            }
            if (ImGui::Button(snapLabel, ImVec2(0.0f, btnHeight))) {
                m_SnapToGrid2D = !m_SnapToGrid2D;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Magnetismo por rejilla (Grid Snapping) para alinear sprites y objetos 2D");
            }
            ImGui::PopStyleColor(3);

            if (m_SnapToGrid2D) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(isCompact ? 45.0f : 55.0f);
                ImGui::DragFloat("##SnapSize", &m_GridSnapSize, 0.1f, 0.1f, 32.0f, "%.1f");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tamano de celda para Snap");
            }

            // Alternador de Estadísticas (Stats ON/OFF)
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            const char* statsLabel = m_ShowStats ? (isCompact ? " Stats " : " Stats: ON ")
                                                 : (isCompact ? " Stats " : " Stats: OFF ");
            if (m_ShowStats) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.58f, 0.45f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.55f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
    }
            if (ImGui::Button(statsLabel, ImVec2(0.0f, btnHeight))) {
                m_ShowStats = !m_ShowStats;
    }
            ImGui::PopStyleColor(3);

            // Menú interactivo de Preview V-Clust (Cluster LOD) y Prueba de Estrés
            ImGui::SameLine();
            ImGui::SetNextItemAllowOverlap();
            const char* vClustBtnLabel = (m_VClustDebugMode == 1) ? " V-Clust: Clusters " :
                                         (m_VClustDebugMode == 2) ? " V-Clust: Heatmap " :
                                         (m_VClustEnabled ? " V-Clust: ON " : " V-Clust: OFF ");
            if (m_VClustDebugMode != 0) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.25f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.72f, 0.35f, 0.95f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else if (m_VClustEnabled) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.52f, 0.68f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.62f, 0.78f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.18f, 0.22f, 0.90f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.28f, 0.36f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.88f, 0.92f, 1.0f));
            }
            if (ImGui::Button(vClustBtnLabel, ImVec2(0.0f, btnHeight))) {
                ImGui::OpenPopup("##VClustPopup");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Configurar V-Clust (Voxel-Anchored Cluster LOD), Preview de Clusters y Prueba de Estres de 1,000 objetos");
            }
            ImGui::PopStyleColor(3);

            if (ImGui::BeginPopup("##VClustPopup")) {
                ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "V-Clust (Voxel-Anchored Cluster LOD)");
                ImGui::Separator();
                ImGui::Checkbox("Activar V-Clust LOD en Runtime", &m_VClustEnabled);
                ImGui::Spacing();

                ImGui::Text("Modo de Preview Visual:");
                ImGui::RadioButton("Material Real (Normal)", &m_VClustDebugMode, 0);
                ImGui::RadioButton("Vista de Clusters (Colores Estilo Nanite)", &m_VClustDebugMode, 1);
                ImGui::RadioButton("Mapa de Calor por Nivel LOD (0..N)", &m_VClustDebugMode, 2);
                ImGui::Checkbox("Mostrar Esferas y Conos Normales (Gizmo)", &m_VClustShowBounds);

                ImGui::Spacing();
                ImGui::SetNextItemWidth(160.0f);
                ImGui::SliderFloat("Umbral SSE (px)", &m_VClustSseThreshold, 0.25f, 8.0f, "%.2f px");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Error proyectado maximo en pixeles antes de subdividir hacia un LOD mas detallado");
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.95f, 0.80f, 0.25f, 1.0f), "Prueba de Estres Masiva (1,000 Objetos):");
                ImGui::TextWrapped("Clona tu modelo 3D seleccionado (o genera una esfera de alta densidad de 8,192 tris) 1,000 veces compartiendo buffers V-Clust.");
                if (ImGui::Button("Generar 1,000 Objetos (Stress Test)", ImVec2(260.0f, 28.0f))) {
                    SpawnStressTestInstances(1000);
                    ImGui::CloseCurrentPopup();
                }
                if (!m_StressTestEntities.empty()) {
                    if (ImGui::Button("Limpiar Instancias de Estres", ImVec2(260.0f, 26.0f))) {
                        ClearStressTestInstances();
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::EndPopup();
            }

            ImGui::PopStyleVar(3);
}
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
}

        // 2. Área restante disponible para el Viewport 3D
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        if (availSize.x > 0.0f && availSize.y > 0.0f) {
            if (m_ViewportSize.x != availSize.x || m_ViewportSize.y != availSize.y) {
                m_ViewportSize = { availSize.x, availSize.y };
    }
}

        ImVec2 minBound = ImGui::GetWindowContentRegionMin();
        ImVec2 maxBound = ImGui::GetWindowContentRegionMax();
        ImVec2 windowPos = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { minBound.x + windowPos.x, minBound.y + windowPos.y };
        m_ViewportBounds[1] = { maxBound.x + windowPos.x, maxBound.y + windowPos.y };

#if defined(SLIP_ENABLE_VULKAN) && !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
        if (SlipRHI::GetActiveAPI() == GraphicsAPI::Vulkan) {
            VulkanRHI* vkRhi = dynamic_cast<VulkanRHI*>(SlipRHI::Get());
            bool isCanvasHovered = false;
            bool isCanvasActive = false;

            CameraComponent* activeGameCam = nullptr;
            if (m_ActiveCameraType == ViewportCameraType::GameCamera && m_ContextLevel) {
                activeGameCam = m_ContextLevel->GetMainCamera();
            }

            float aspect = (m_ViewportSize.y > 0.0f) ? (m_ViewportSize.x / m_ViewportSize.y) : 1.0f;
            glm::mat4 projection(1.0f);
            glm::mat4 view(1.0f);
            glm::vec3 eye(0.0f);
            glm::vec3 forward(0.0f, 0.0f, -1.0f), right(1.0f, 0.0f, 0.0f), up(0.0f, 1.0f, 0.0f);

            if (activeGameCam) {
                if (SlipEditor::GetInstance() && !SlipEditor::GetInstance()->IsPlaying()) {
                    activeGameCam->OnUpdate(ImGui::GetIO().DeltaTime);
                }
                view = activeGameCam->GetViewMatrix();
                projection = activeGameCam->GetProjectionMatrix(aspect);
                eye = activeGameCam->GetPosition();
                forward = activeGameCam->GetForward();
                up = activeGameCam->GetUp();
                right = activeGameCam->GetRight();
            } else if (m_Is2DMode) {
                float orthoHalfH = (std::max)(0.5f, m_CameraDistance * 0.65f);
                float orthoHalfW = orthoHalfH * aspect;
                eye = glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 15.0f);
                view = glm::lookAt(eye, glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 0.0f), up);
                projection = glm::ortho(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, -100.0f, 100.0f);
            } else {
                float radPitch = glm::radians(m_CameraPitch);
                float radYaw = glm::radians(m_CameraYaw);
                forward = glm::normalize(glm::vec3(
                    -cos(radPitch) * sin(radYaw),
                    -sin(radPitch),
                    -cos(radPitch) * cos(radYaw)
                ));
                right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
                up = glm::normalize(glm::cross(right, forward));

                if (!m_CameraPosInitialized) {
                    m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                    m_CameraPosInitialized = true;
                }
                eye = m_CameraPos;
                m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
                view = glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
                projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 1000.0f);
            }

            if (vkRhi && m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f) {
                vkRhi->EnsureViewportSize(static_cast<int>(m_ViewportSize.x), static_cast<int>(m_ViewportSize.y));
                vkRhi->SetSelectedEntity((m_ShowGizmos && m_ActiveCameraType == ViewportCameraType::EditorCamera && !androidGameView) ? m_SelectedEntity : nullptr);
                vkRhi->RenderSceneToViewport(m_ContextLevel, view, projection, eye);
                m_StatDrawCalls = static_cast<int>(vkRhi->GetLastDrawCallCount());

                if (m_VkViewportGen != vkRhi->GetViewportGeneration() && vkRhi->GetViewportImageView() != VK_NULL_HANDLE) {
                    if (m_VkViewportDescriptor != nullptr) {
                        ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(m_VkViewportDescriptor));
                        m_VkViewportDescriptor = nullptr;
                    }
                    m_VkViewportDescriptor = ImGui_ImplVulkan_AddTexture(
                        vkRhi->GetViewportSampler(),
                        vkRhi->GetViewportImageView(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    );
                    m_VkViewportGen = vkRhi->GetViewportGeneration();
                }

                if (m_VkViewportDescriptor != nullptr) {
                    ImVec2 cursorPos = ImGui::GetCursorScreenPos();
                    m_ViewportBounds[0] = { cursorPos.x, cursorPos.y };
                    m_ViewportBounds[1] = { cursorPos.x + m_ViewportSize.x, cursorPos.y + m_ViewportSize.y };
                    ImGui::Image(
                        reinterpret_cast<ImTextureID>(m_VkViewportDescriptor),
                        ImVec2(m_ViewportSize.x, m_ViewportSize.y),
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f)
                    );

                    ImGui::SetCursorScreenPos(cursorPos);
                    ImGui::SetNextItemAllowOverlap();
                    ImGui::InvisibleButton("##ViewportCanvasArea", ImVec2(m_ViewportSize.x, m_ViewportSize.y),
                        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
                    isCanvasHovered = ImGui::IsItemHovered();
                    isCanvasActive = ImGui::IsItemActive();

                    const glm::mat4 vkViewProj = projection * view;
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    if (m_ShowGizmos) {
                        auto drawLine3D = [&](const glm::vec3& a, const glm::vec3& b, ImU32 col, float thickness = 1.5f) {
                            glm::vec2 sp0, sp1;
                            if (ProjectToScreen(a, vkViewProj, sp0) && ProjectToScreen(b, vkViewProj, sp1)) {
                                dl->AddLine(ImVec2(sp0.x, sp0.y), ImVec2(sp1.x, sp1.y), col, thickness);
                            }
                        };
                        auto drawBox3D = [&](const glm::mat4& xform, const glm::vec3& bMin, const glm::vec3& bMax, ImU32 col, float thickness = 1.5f) {
                            glm::vec3 corners[8] = {
                                glm::vec3(xform * glm::vec4(bMin.x, bMin.y, bMin.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMax.x, bMin.y, bMin.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMax.x, bMax.y, bMin.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMin.x, bMax.y, bMin.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMin.x, bMin.y, bMax.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMax.x, bMin.y, bMax.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMax.x, bMax.y, bMax.z, 1.0f)),
                                glm::vec3(xform * glm::vec4(bMin.x, bMax.y, bMax.z, 1.0f))
                            };
                            static const int kBoxEdges[12][2] = {
                                {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
                            };
                            for (const auto& e : kBoxEdges) {
                                drawLine3D(corners[e[0]], corners[e[1]], col, thickness);
                            }
                        };

                        std::vector<glm::vec3> debugLineData;
                        SlipDebug::Get().ConsumePendingLines(debugLineData);
                        for (size_t i = 0; i + 3 < debugLineData.size(); i += 4) {
                            const glm::vec3& c = debugLineData[i + 1];
                            ImU32 col32 = IM_COL32(
                                static_cast<int>(glm::clamp(c.r, 0.0f, 1.0f) * 255.0f),
                                static_cast<int>(glm::clamp(c.g, 0.0f, 1.0f) * 255.0f),
                                static_cast<int>(glm::clamp(c.b, 0.0f, 1.0f) * 255.0f),
                                230
                            );
                            drawLine3D(debugLineData[i], debugLineData[i + 2], col32, 1.5f);
                        }

                        if (m_ContextLevel && m_ActiveCameraType == ViewportCameraType::EditorCamera && !androidGameView) {
                            for (SlipEntity* ent : m_ContextLevel->GetEntities()) {
                                if (!ent) continue;
                                glm::vec3 entPos = glm::vec3(ent->getMatrix()[3]);
                                glm::vec2 sPos;
                                if (auto lc = ent->GetComponent<LightComponent>()) {
                                    if (ProjectToScreen(entPos, vkViewProj, sPos)) {
                                        dl->AddCircleFilled(ImVec2(sPos.x, sPos.y), 6.0f, IM_COL32(255, 220, 90, 220));
                                        dl->AddCircle(ImVec2(sPos.x, sPos.y), 9.0f, IM_COL32(255, 195, 45, 180), 12, 1.5f);
                                    }
                                    if (lc->type == LightType::Directional && ent != m_SelectedEntity) {
                                        glm::vec3 rot = ent->rotation;
                                        glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0)) *
                                                           glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0)) *
                                                           glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
                                        glm::vec3 dir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));
                                        glm::vec3 upAxis = (std::abs(dir.y) > 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                                        glm::vec3 rightAxis = glm::normalize(glm::cross(dir, upAxis));
                                        upAxis = glm::normalize(glm::cross(rightAxis, dir));
                                        glm::vec3 tip = entPos + dir * 2.2f;
                                        const ImU32 dirCol = IM_COL32(255, 215, 70, 210);
                                        drawLine3D(entPos, tip, dirCol, 1.8f);
                                        drawLine3D(tip, tip - dir * 0.35f + rightAxis * 0.18f, dirCol, 1.6f);
                                        drawLine3D(tip, tip - dir * 0.35f - rightAxis * 0.18f, dirCol, 1.6f);
                                        drawLine3D(tip, tip - dir * 0.35f + upAxis * 0.18f, dirCol, 1.6f);
                                        drawLine3D(tip, tip - dir * 0.35f - upAxis * 0.18f, dirCol, 1.6f);
                                    }
                                }
                                if (ent->HasComponent<CameraComponent>() && ProjectToScreen(entPos, vkViewProj, sPos)) {
                                    dl->AddRect(ImVec2(sPos.x - 6.0f, sPos.y - 4.0f), ImVec2(sPos.x + 6.0f, sPos.y + 4.0f), IM_COL32(130, 210, 255, 220), 2.0f, 0, 1.5f);
                                }
                            }
                        }

                        if (m_SelectedEntity) {
                            if (auto selLight = m_SelectedEntity->GetComponent<LightComponent>()) {
                                constexpr float kPi = 3.14159265358979323846f;
                                const ImU32 lightCol = IM_COL32(255, 225, 64, 240);
                                glm::vec3 pos = m_SelectedEntity->position;
                                glm::vec3 rot = m_SelectedEntity->rotation;
                                glm::mat4 rotMat = glm::rotate(glm::mat4(1.0f), glm::radians(rot.x), glm::vec3(1, 0, 0)) *
                                                   glm::rotate(glm::mat4(1.0f), glm::radians(rot.y), glm::vec3(0, 1, 0)) *
                                                   glm::rotate(glm::mat4(1.0f), glm::radians(rot.z), glm::vec3(0, 0, 1));
                                glm::vec3 dir = glm::normalize(glm::vec3(rotMat * glm::vec4(0.0f, -1.0f, -0.5f, 0.0f)));
                                glm::vec3 upAxis = (std::abs(dir.y) > 0.9f) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
                                glm::vec3 rightAxis = glm::normalize(glm::cross(dir, upAxis));
                                upAxis = glm::normalize(glm::cross(rightAxis, dir));

                                if (selLight->type == LightType::Directional) {
                                    float sunRadius = 0.8f;
                                    const int segments = 32;
                                    for (int s = 0; s < segments; ++s) {
                                        float a0 = static_cast<float>(s) / segments * 2.0f * kPi;
                                        float a1 = static_cast<float>(s + 1) / segments * 2.0f * kPi;
                                        glm::vec3 p0 = pos + (rightAxis * std::cos(a0) + upAxis * std::sin(a0)) * sunRadius;
                                        glm::vec3 p1 = pos + (rightAxis * std::cos(a1) + upAxis * std::sin(a1)) * sunRadius;
                                        drawLine3D(p0, p1, lightCol, 1.6f);
                                    }
                                    glm::vec3 arrowTip = pos + dir * 2.5f;
                                    drawLine3D(pos, arrowTip, lightCol, 2.2f);
                                    drawLine3D(arrowTip, arrowTip - dir * 0.4f + rightAxis * 0.2f, lightCol, 2.0f);
                                    drawLine3D(arrowTip, arrowTip - dir * 0.4f - rightAxis * 0.2f, lightCol, 2.0f);
                                    drawLine3D(arrowTip, arrowTip - dir * 0.4f + upAxis * 0.2f, lightCol, 2.0f);
                                    drawLine3D(arrowTip, arrowTip - dir * 0.4f - upAxis * 0.2f, lightCol, 2.0f);
                                    glm::vec3 rayOffsets[4] = { rightAxis * sunRadius, -rightAxis * sunRadius, upAxis * sunRadius, -upAxis * sunRadius };
                                    for (int r = 0; r < 4; ++r) {
                                        glm::vec3 rStart = pos + rayOffsets[r];
                                        glm::vec3 rEnd = rStart + dir * 1.8f;
                                        drawLine3D(rStart, rEnd, lightCol, 1.4f);
                                        drawLine3D(rEnd, rEnd - dir * 0.25f + rightAxis * 0.1f, lightCol, 1.4f);
                                        drawLine3D(rEnd, rEnd - dir * 0.25f - rightAxis * 0.1f, lightCol, 1.4f);
                                    }
                                } else if (selLight->type == LightType::Point) {
                                    float r = (std::max)(selLight->range, 0.1f);
                                    const int segments = 36;
                                    for (int s = 0; s < segments; ++s) {
                                        float a0 = static_cast<float>(s) / segments * 2.0f * kPi;
                                        float a1 = static_cast<float>(s + 1) / segments * 2.0f * kPi;
                                        drawLine3D(pos + glm::vec3(std::cos(a0) * r, std::sin(a0) * r, 0.0f), pos + glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f), lightCol, 1.4f);
                                        drawLine3D(pos + glm::vec3(std::cos(a0) * r, 0.0f, std::sin(a0) * r), pos + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r), lightCol, 1.4f);
                                        drawLine3D(pos + glm::vec3(0.0f, std::cos(a0) * r, std::sin(a0) * r), pos + glm::vec3(0.0f, std::cos(a1) * r, std::sin(a1) * r), lightCol, 1.4f);
                                    }
                                    float cs = 0.25f;
                                    drawLine3D(pos - glm::vec3(cs, 0, 0), pos + glm::vec3(cs, 0, 0), lightCol, 1.5f);
                                    drawLine3D(pos - glm::vec3(0, cs, 0), pos + glm::vec3(0, cs, 0), lightCol, 1.5f);
                                    drawLine3D(pos - glm::vec3(0, 0, cs), pos + glm::vec3(0, 0, cs), lightCol, 1.5f);
                                } else if (selLight->type == LightType::Spot) {
                                    float range = (std::max)(selLight->range, 0.1f);
                                    float baseRadius = range * std::tan(glm::radians(selLight->spotAngle * 0.5f));
                                    glm::vec3 baseCenter = pos + dir * range;
                                    const int segments = 36;
                                    for (int s = 0; s < segments; ++s) {
                                        float a0 = static_cast<float>(s) / segments * 2.0f * kPi;
                                        float a1 = static_cast<float>(s + 1) / segments * 2.0f * kPi;
                                        drawLine3D(baseCenter + (rightAxis * std::cos(a0) + upAxis * std::sin(a0)) * baseRadius,
                                                   baseCenter + (rightAxis * std::cos(a1) + upAxis * std::sin(a1)) * baseRadius, lightCol, 1.5f);
                                    }
                                    drawLine3D(pos, baseCenter + rightAxis * baseRadius, lightCol, 1.4f);
                                    drawLine3D(pos, baseCenter - rightAxis * baseRadius, lightCol, 1.4f);
                                    drawLine3D(pos, baseCenter + upAxis * baseRadius, lightCol, 1.4f);
                                    drawLine3D(pos, baseCenter - upAxis * baseRadius, lightCol, 1.4f);
                                    drawLine3D(pos, baseCenter, lightCol, 1.8f);
                                    drawLine3D(baseCenter, baseCenter - dir * 0.3f + rightAxis * 0.15f, lightCol, 1.6f);
                                    drawLine3D(baseCenter, baseCenter - dir * 0.3f - rightAxis * 0.15f, lightCol, 1.6f);
                                }
                            }

                            if (auto selPPV = m_SelectedEntity->GetComponent<PostProcessVolumeComponent>()) {
                                if (!selPPV->isGlobal) {
                                    glm::vec3 h = selPPV->size * 0.5f;
                                    drawBox3D(glm::translate(glm::mat4(1.0f), m_SelectedEntity->position), -h, h, IM_COL32(158, 115, 242, 225), 1.6f);
                                }
                            }

                            if (auto selProbe = m_SelectedEntity->GetComponent<ReflectionProbeComponent>()) {
                                glm::vec3 h = selProbe->size * 0.5f;
                                drawBox3D(glm::translate(glm::mat4(1.0f), m_SelectedEntity->position), -h, h, IM_COL32(51, 217, 242, 225), 1.6f);
                            }

                            if (auto boxCol = m_SelectedEntity->GetComponent<BoxColliderComponent>()) {
                                glm::vec3 h = boxCol->size * 0.5f;
                                drawBox3D(m_SelectedEntity->getMatrix(), boxCol->centerOffset - h, boxCol->centerOffset + h, IM_COL32(80, 240, 120, 210), 1.5f);
                            }

                            if (SlipGraphicsSettings::Get().GetData().giMode == GlobalIlluminationMode::PathTracing) {
                                if (auto mc = m_SelectedEntity->GetComponent<MeshComponent>()) {
                                    if (mc->model) {
                                        if (mc->model->aabbMin == glm::vec3(-0.5f) && mc->model->aabbMax == glm::vec3(0.5f)) {
                                            mc->model->computeBounds();
                                        }
                                        drawBox3D(m_SelectedEntity->getMatrix(), mc->model->aabbMin, mc->model->aabbMax, IM_COL32(66, 150, 250, 235), 2.0f);
                                    }
                                } else if (dynamic_cast<SlipPrimitiveEntity*>(m_SelectedEntity)) {
                                    drawBox3D(m_SelectedEntity->getMatrix(), glm::vec3(-0.5f), glm::vec3(0.5f), IM_COL32(66, 150, 250, 235), 2.0f);
                                }
                            }
                        }
                    } else {
                        SlipDebug::Get().cleanLines();
                    }

                    if (m_ActiveCameraType == ViewportCameraType::GameCamera && !activeGameCam) {
                        const char* noCamMsg = "Sin CameraComponent en la escena (Mostrando vista de editor)";
                        ImVec2 txtSz = ImGui::CalcTextSize(noCamMsg);
                        ImVec2 center(m_ViewportBounds[0].x + (m_ViewportSize.x - txtSz.x) * 0.5f, m_ViewportBounds[0].y + 16.0f);
                        dl->AddRectFilled(ImVec2(center.x - 10.0f, center.y - 4.0f),
                                          ImVec2(center.x + txtSz.x + 10.0f, center.y + txtSz.y + 4.0f),
                                          IM_COL32(20, 24, 30, 210), 4.0f);
                        dl->AddText(center, IM_COL32(255, 185, 40, 255), noCamMsg);
                    }

                    if (m_ActiveCameraType == ViewportCameraType::EditorCamera && !androidGameView) {
                        DrawGizmo(view, projection);
                    }
                }
            }

            // Focus animation & camera navigation in Vulkan Viewport
            if (m_FocusActive) {
                bool userNavigating = ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                                      ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                                      (ImGui::IsKeyDown(ImGuiKey_LeftAlt) && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
                                      ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_A) ||
                                      ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_D);
                if (userNavigating) {
                    m_FocusActive = false;
                } else {
                    UpdateFocusAnimation(ImGui::GetIO().DeltaTime);
                }
            }

            if ((isCanvasActive || isCanvasHovered) &&
                m_ActiveGizmoAxis == -1 &&
                m_GizmoHoveredAxis == -1 &&
                m_ActiveCameraType == ViewportCameraType::EditorCamera)
            {
                if (m_Is2DMode && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
                    ImGuiMouseButton panBtn = ImGui::IsMouseDragging(ImGuiMouseButton_Right) ? ImGuiMouseButton_Right : ImGuiMouseButton_Middle;
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta(panBtn);
                    float panSpeed = (m_CameraDistance * 1.3f) / (std::max)(100.0f, m_ViewportSize.y);
                    m_CameraTarget += right * (-dragDelta.x * panSpeed) + up * (dragDelta.y * panSpeed);
                    m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                    ImGui::ResetMouseDragDelta(panBtn);
                } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                    m_CameraYaw -= dragDelta.x * 0.35f;
                    m_CameraPitch += dragDelta.y * 0.35f;
                    m_CameraPitch = glm::clamp(m_CameraPitch, -89.0f, 89.0f);
                    m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
                } else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt) && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                    m_CameraYaw -= dragDelta.x * 0.35f;
                    m_CameraPitch += dragDelta.y * 0.35f;
                    m_CameraPitch = glm::clamp(m_CameraPitch, -89.0f, 89.0f);
                    m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
                } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                    ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
                    float panSpeed = 0.008f * m_CameraDistance;
                    glm::vec3 panDelta = right * (-dragDelta.x * panSpeed) + up * (dragDelta.y * panSpeed);
                    m_CameraPos += panDelta;
                    m_CameraTarget += panDelta;
                    ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
                }

                if (ImGui::GetIO().MouseWheel != 0.0f) {
                    float wheelDelta = ImGui::GetIO().MouseWheel;
                    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                        m_CameraPos += forward * (wheelDelta * 2.5f);
                        m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
                    } else {
                        m_CameraPos += forward * (wheelDelta * (std::max)(1.0f, m_CameraDistance * 0.25f));
                        m_CameraDistance -= wheelDelta * (m_CameraDistance * 0.15f);
                        m_CameraDistance = glm::clamp(m_CameraDistance, 0.5f, 150.0f);
                        m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
                    }
                }
            }

            if ((m_IsHovered || m_IsFocused) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && m_ActiveGizmoAxis == -1) {
                if (ImGui::IsKeyPressed(ImGuiKey_Q)) m_GizmoMode = GizmoMode::None;
                if (ImGui::IsKeyPressed(ImGuiKey_W)) m_GizmoMode = GizmoMode::Translate;
                if (ImGui::IsKeyPressed(ImGuiKey_E)) m_GizmoMode = GizmoMode::Rotate;
                if (ImGui::IsKeyPressed(ImGuiKey_R)) m_GizmoMode = GizmoMode::Scale;
                if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                    m_GizmoSpace = (m_GizmoSpace == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_C)) {
                    m_ActiveCameraType = (m_ActiveCameraType == ViewportCameraType::GameCamera) ? ViewportCameraType::EditorCamera : ViewportCameraType::GameCamera;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                    FocusOnSelection();
                }
            }

            if ((m_IsHovered || m_IsFocused) && ImGui::IsMouseDown(ImGuiMouseButton_Right) && m_ActiveCameraType == ViewportCameraType::EditorCamera) {
                float dt = ImGui::GetIO().DeltaTime;
                if (dt > 0.1f) dt = 0.1f;
                float speed = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 18.0f : 7.0f;
                glm::vec3 moveDir(0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_W)) moveDir += forward;
                if (ImGui::IsKeyDown(ImGuiKey_S)) moveDir -= forward;
                if (ImGui::IsKeyDown(ImGuiKey_D)) moveDir += right;
                if (ImGui::IsKeyDown(ImGuiKey_A)) moveDir -= right;
                if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
                if (ImGui::IsKeyDown(ImGuiKey_Q)) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);
                if (glm::length(moveDir) > 0.001f) {
                    m_CameraPos += glm::normalize(moveDir) * speed * dt;
                    m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
                }
            }

            if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_ActiveGizmoAxis == -1 && m_GizmoHoveredAxis == -1 && !ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
                ImVec2 mousePos = ImGui::GetMousePos();
                SlipEntity* hit = CastRayFromMouse(glm::vec2(mousePos.x, mousePos.y), view, projection);
                if (ImGui::GetIO().KeyCtrl && hit) {
                    bool wasSelected = IsEntitySelected(hit);
                    std::vector<SlipEntity*> sel;
                    if (wasSelected) {
                        for (SlipEntity* e : m_Selection) {
                            if (e != hit) sel.push_back(e);
                        }
                    } else {
                        sel = m_Selection;
                        sel.push_back(hit);
                    }
                    ApplySelection(sel, hit);
                    if (m_OnMultiSelectCallback) m_OnMultiSelectCallback(sel, hit);
                } else {
                    m_SelectedEntity = hit;
                    if (m_OnSelectCallback) {
                        m_OnSelectCallback(hit);
                    }
                }
            }

            if (vkRhi && m_ShowStats && m_ViewportSize.x > 200.0f && m_ViewportSize.y > 150.0f) {
                const auto& caps = vkRhi->GetCapabilities();
                const auto& gfxCfg = SlipGraphicsSettings::Get().GetData();
                ImVec2 statsPos(m_ViewportBounds[0].x + 12.0f, m_ViewportBounds[1].y - 160.0f);
                ImGui::SetNextWindowPos(statsPos, ImGuiCond_Always);
                ImGui::SetNextWindowBgAlpha(0.78f);
                ImGuiWindowFlags statsFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                              ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                              ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
                if (ImGui::Begin("##ViewportStatsHUD", nullptr, statsFlags)) {
                    float fps = ImGui::GetIO().Framerate;
                    float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
                    ImVec4 fpsColor = (fps >= 55.0f) ? ImVec4(0.35f, 1.0f, 0.45f, 1.0f) :
                                      ((fps >= 28.0f) ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));

                    ImGui::TextColored(fpsColor, "FPS: %.1f  (%.2f ms)", fps, ms);
                    ImGui::Separator();
                    ImGui::Text("API: Vulkan %u.%u (%s)", caps.apiVersionMajor, caps.apiVersionMinor, caps.deviceName.c_str());
                    ImGui::Text("Viewport: %dx%d | Draw Calls: %d", vkRhi->GetViewportWidth(), vkRhi->GetViewportHeight(), m_StatDrawCalls);
                    ImGui::Text("Instancias RT (TLAS): %u", vkRhi->GetLastRtInstanceCount());
                    if (gfxCfg.giMode == GlobalIlluminationMode::PathTracing && caps.supportsRayQuery) {
                        if (gfxCfg.ptMode == PathTracingMode::RealtimeSVGF) {
                            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                                               "GI: Path Tracing Tiempo Real (SVGF | %d spp/f)",
                                               (std::min)(gfxCfg.giRaysPerPixel, 2));
                        } else {
                            ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f),
                                               "GI: Path Tracing Progresivo (%u / %d spp | %d spp/f)",
                                               vkRhi->GetPathTracingSampleCount(), gfxCfg.ptMaxSamples, gfxCfg.giRaysPerPixel);
                        }
                    } else if (gfxCfg.giMode == GlobalIlluminationMode::RTGI && caps.supportsRayQuery) {
                        ImGui::TextColored(ImVec4(0.45f, 0.95f, 0.65f, 1.0f),
                                           "GI: RTGI + Denoiser (%s)", gfxCfg.giDenoiserEnabled ? "ON" : "OFF");
                    } else {
                        ImGui::TextDisabled("GI: Off (PBR Directo)");
                    }
                    ImGui::End();
                }
            }

            ImGui::End();
            ImGui::PopStyleVar();
            return;
        }
#endif

        if (!m_RendererInitialized) {
            InitRenderer();
}

        // Renderizado al Framebuffer antes de mostrar la imagen
        if (m_RendererInitialized && m_ViewportSize.x > 1.0f && m_ViewportSize.y > 1.0f) {
            ResizeFBO((int)m_ViewportSize.x, (int)m_ViewportSize.y);
            RenderScene();
}

        // Mostrar el Framebuffer limpio en el Viewport
        uint32_t textureID = m_TextureID;
        if (textureID == 0) textureID = (m_PresentTexture != 0) ? m_PresentTexture : m_ColorTexture;
        if (textureID == 0 && SlipFrameBuffer::GetInstance() != nullptr) {
            textureID = SlipFrameBuffer::GetInstance()->getTexture();
}

        bool isCanvasHovered = false;
        bool isCanvasActive = false;

        if (textureID != 0 && m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f) {
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            m_ViewportBounds[0] = { cursorPos.x, cursorPos.y };
            m_ViewportBounds[1] = { cursorPos.x + m_ViewportSize.x, cursorPos.y + m_ViewportSize.y };
            ImGui::Image(
                (ImTextureID)(intptr_t)textureID,
                ImVec2(m_ViewportSize.x, m_ViewportSize.y),
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f)
            );

            if (androidGameView && SlipEditor::GetInstance()) {
                SlipEditor* editor = SlipEditor::GetInstance();
                float btnHeight = 36.0f;
                float padding = 16.0f;
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 6.0f));
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.24f, 0.32f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));

                float totalW = 100.0f + 95.0f + 85.0f + 16.0f;
                ImGui::SetCursorScreenPos(ImVec2(m_ViewportBounds[1].x - totalW - padding, m_ViewportBounds[0].y + padding));

                if (ImGui::Button("[X] Detener", ImVec2(100.0f, btnHeight))) {
                    editor->StopPlayMode();
        }
                ImGui::SameLine();
                if (editor->IsPaused()) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.30f, 0.90f));
                    if (ImGui::Button("[>] Play", ImVec2(95.0f, btnHeight))) editor->ResumePlayMode();
                    ImGui::PopStyleColor();
        } else {
                    if (ImGui::Button("[||] Pausa", ImVec2(95.0f, btnHeight))) editor->PausePlayMode();
        }
                ImGui::SameLine();
                if (ImGui::Button("Editor", ImVec2(85.0f, btnHeight))) {
                    editor->SetAndroidGameView(false);
        }

                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(2);
    }

            // Drag & Drop: Soltar Prefab o Modelo 3D directamente en el Viewport 3D
            if (!androidGameView && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM")) {
                    std::string droppedPath = (const char*)payload->Data;
                    if (m_ContextLevel) {
                        auto hasExt = [&](const char* ext) {
                            size_t elen = std::strlen(ext);
                            if (droppedPath.size() < elen) return false;
                            std::string tail = droppedPath.substr(droppedPath.size() - elen);
                            for (char& c : tail) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            return tail == ext;
                };
                        if (hasExt(".prefab")) {
                            glm::vec3 spawnPos = m_CameraTarget;
                            SlipEntity* inst = SlipPrefab::Instantiate(droppedPath, m_ContextLevel, spawnPos);
                            if (inst) {
                                m_SelectedEntity = inst;
                                if (m_OnSelectCallback) m_OnSelectCallback(inst);
                    }
                } else if (hasExt(".fbx") || hasExt(".obj") || hasExt(".dae") || hasExt(".gltf") || hasExt(".glb")) {
                            size_t slashPos = droppedPath.find_last_of("/\\");
                            std::string baseName = (slashPos != std::string::npos) ? droppedPath.substr(slashPos + 1) : droppedPath;
                            size_t dotPos = baseName.find_last_of('.');
                            if (dotPos != std::string::npos) baseName = baseName.substr(0, dotPos);
                            if (baseName.empty()) baseName = "Modelo3D";

                            SlipEntity* ent = new SlipEntity(SlipPrefab::MakeUniqueName(baseName, m_ContextLevel));
                            ent->position = glm::vec3(m_CameraTarget.x, 0.0f, m_CameraTarget.z);
                            if (auto tc = ent->GetComponent<TransformComponent>()) {
                                tc->position = ent->position;
                    }
                            AssetMetadata meta;
                            SlipModelExtract::EnsureMetaForFile(droppedPath, &meta);
                            ModelInspectionInfo info;
                            SlipModelExtract::InspectModelDetailed(droppedPath, info);
                            bool isSkinned = (info.boneCount > 0 || !info.animations.empty());

                            // No volver a aplicar meta.importScale ni meta.importRotation en el Transform de la entidad
                            // cuando el modelo es estático y bakeTransforms está activo, porque BakeModelToCache ya los horneó en los vértices.
                            if (isSkinned || !meta.bakeTransforms) {
                                if (meta.importScale > 0.0f && std::abs(meta.importScale - 1.0f) > 1e-4f) {
                                    ent->scale = glm::vec3(meta.importScale);
                                    if (auto tc = ent->GetComponent<TransformComponent>()) {
                                        tc->scale = ent->scale;
                                    }
                                }
                                if (glm::length(meta.importRotation) > 0.01f) {
                                    ent->rotation = meta.importRotation;
                                    if (auto tc = ent->GetComponent<TransformComponent>()) {
                                        tc->rotation = ent->rotation;
                                    }
                                }
                            }

                            if (isSkinned) {
                                auto smc = ent->AddComponent<SkinnedMeshComponent>();
                                m_ContextLevel->AddEntity(ent);
                                smc->SetModel(droppedPath);
                                SlipConsole::LogSuccess("Modelo con huesos instanciado con SkinnedMeshComponent: " + baseName);
                    } else {
                                auto mc = ent->AddComponent<MeshComponent>(droppedPath);
                                ent->SetStatic(true);
                                m_ContextLevel->AddEntity(ent);
                                m_LastStaticHash = 0;
                                SlipConsole::LogSuccess("Modelo estatico V-Clust instanciado con MeshComponent: " + baseName);
                    }
                            m_SelectedEntity = ent;
                            if (m_OnSelectCallback) m_OnSelectCallback(ent);
                }
            }
        }
                ImGui::EndDragDropTarget();
    }
            ImGui::SetCursorScreenPos(cursorPos);
            ImGui::SetNextItemAllowOverlap();
            // Botón invisible que cubre el Viewport completo para capturar toques táctiles y clics del ratón
            ImGui::InvisibleButton("##ViewportCanvasArea", ImVec2(m_ViewportSize.x, m_ViewportSize.y),
                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
            isCanvasHovered = ImGui::IsItemHovered();
            isCanvasActive = ImGui::IsItemActive();
} else {
            ImVec2 windowSize = ImGui::GetWindowSize();
            const char* msg = "Viewport - Inicializando Framebuffer...";
            ImVec2 textSize = ImGui::CalcTextSize(msg);
            ImGui::SetCursorPos(ImVec2((windowSize.x - textSize.x) * 0.5f, (windowSize.y - textSize.y) * 0.5f));
            ImGui::TextDisabled("%s", msg);
}

        // 1. Matrices de Cámara y Parámetros
        CameraComponent* activeGameCam = nullptr;
        if (m_ActiveCameraType == ViewportCameraType::GameCamera && m_ContextLevel) {
            activeGameCam = m_ContextLevel->GetMainCamera();
}

        float aspect = (m_FboHeight > 0) ? ((float)m_FboWidth / (float)m_FboHeight) : 1.0f;
        glm::mat4 projection;
        glm::mat4 view;
        glm::vec3 eye;
        glm::vec3 forward, right, up;

        if (activeGameCam) {
            view = activeGameCam->GetViewMatrix();
            projection = activeGameCam->GetProjectionMatrix(aspect);
            eye = activeGameCam->GetPosition();
            forward = activeGameCam->GetForward();
            up = activeGameCam->GetUp();
            right = activeGameCam->GetRight();
} else if (m_Is2DMode) {
            float orthoHalfH = (std::max)(0.5f, m_CameraDistance * 0.65f);
            float orthoHalfW = orthoHalfH * aspect;
            eye = glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 15.0f);
            forward = glm::vec3(0.0f, 0.0f, -1.0f);
            right = glm::vec3(1.0f, 0.0f, 0.0f);
            up = glm::vec3(0.0f, 1.0f, 0.0f);
            view = glm::lookAt(eye, glm::vec3(m_CameraTarget.x, m_CameraTarget.y, 0.0f), up);
            projection = glm::ortho(-orthoHalfW, orthoHalfW, -orthoHalfH, orthoHalfH, -100.0f, 100.0f);
} else {
            float radPitch = glm::radians(m_CameraPitch);
            float radYaw = glm::radians(m_CameraYaw);
            forward = glm::normalize(glm::vec3(
                -cos(radPitch) * sin(radYaw),
                -sin(radPitch),
                -cos(radPitch) * cos(radYaw)
            ));
            right = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
            up = glm::normalize(glm::cross(right, forward));

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
            if (!m_CameraPosInitialized) {
                m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                m_CameraPosInitialized = true;
    }
            eye = m_CameraPos;
            m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
            view = glm::lookAt(eye, eye + forward, glm::vec3(0.0f, 1.0f, 0.0f));
#else
            eye.x = m_CameraTarget.x + m_CameraDistance * cos(radPitch) * sin(radYaw);
            eye.y = m_CameraTarget.y + m_CameraDistance * sin(radPitch);
            eye.z = m_CameraTarget.z + m_CameraDistance * cos(radPitch) * cos(radYaw);
            m_CameraPos = eye;
            view = glm::lookAt(eye, m_CameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
#endif

            projection = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
}

        // Procesar interacción con botones y sliders del sistema UI en Play Mode o Cámara de Juego
        ProcessUIInteraction();

        if (SlipEditor::GetInstance() && SlipEditor::GetInstance()->IsInPlaySession()
            && SlipInput::HasInstance() && SlipInput::Get().WantsCursorCapture()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const char* hint = SlipInput::Get().IsCursorReleasedByUser()
                ? "Alt izquierdo: volver a capturar el cursor"
                : "Alt izquierdo: liberar el cursor";
            ImVec2 txtSz = ImGui::CalcTextSize(hint);
            ImVec2 pos(m_ViewportBounds[0].x + (m_ViewportSize.x - txtSz.x) * 0.5f,
                       m_ViewportBounds[0].y + m_ViewportSize.y - txtSz.y - 18.0f);
            drawList->AddRectFilled(ImVec2(pos.x - 10.0f, pos.y - 4.0f),
                                    ImVec2(pos.x + txtSz.x + 10.0f, pos.y + txtSz.y + 4.0f),
                                    IM_COL32(20, 24, 30, 210), 4.0f);
            drawList->AddText(pos, IM_COL32(235, 235, 235, 255), hint);
}

        // Aviso en pantalla si se seleccionó la vista de Juego pero no hay cámara en la escena
        if (m_ActiveCameraType == ViewportCameraType::GameCamera && !activeGameCam) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const char* noCamMsg = "Sin CameraComponent en la escena (Mostrando vista de editor)";
            ImVec2 txtSz = ImGui::CalcTextSize(noCamMsg);
            ImVec2 center = ImVec2(m_ViewportBounds[0].x + (m_ViewportSize.x - txtSz.x) * 0.5f,
                                   m_ViewportBounds[0].y + 16.0f);
            drawList->AddRectFilled(ImVec2(center.x - 10.0f, center.y - 4.0f),
                                    ImVec2(center.x + txtSz.x + 10.0f, center.y + txtSz.y + 4.0f),
                                    IM_COL32(20, 24, 30, 210), 4.0f);
            drawList->AddText(center, IM_COL32(255, 185, 40, 255), noCamMsg);
}

        // 2. Dibujar Gizmo 3D Interactivo (únicamente en vista de Editor)
        if (m_ActiveCameraType == ViewportCameraType::EditorCamera && !androidGameView) {
            DrawGizmo(view, projection);
}

        // 3. Control de Navegación de Cámara (Solo en vista de EditorCamera; la cámara del juego se mueve por script o transform)
        // Animación de focus (tecla F): se cancela si el usuario navega manualmente
        if (m_FocusActive) {
            bool userNavigating = ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
                                  ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
                                  (ImGui::IsKeyDown(ImGuiKey_LeftAlt) && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
                                  ImGui::IsKeyDown(ImGuiKey_W) || ImGui::IsKeyDown(ImGuiKey_A) ||
                                  ImGui::IsKeyDown(ImGuiKey_S) || ImGui::IsKeyDown(ImGuiKey_D);
            if (userNavigating) {
                m_FocusActive = false;
            } else {
                UpdateFocusAnimation(ImGui::GetIO().DeltaTime);
            }
        }
        ImVec2 mousePos = ImGui::GetMousePos();
        bool canNavigate = (isCanvasActive || isCanvasHovered) &&
                           (m_ActiveGizmoAxis == -1) &&
                           (m_GizmoHoveredAxis == -1) &&
                           (m_ActiveCameraType == ViewportCameraType::EditorCamera);

        if (canNavigate) {
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
            auto editor = SlipEditor::GetInstance();
            if (editor) {
                const auto& gesture = editor->GetTouchGesture();
                // Gesto de 2 dedos en Android: Desplazamiento (Pan) arriba/abajo e izquierda/derecha + Zoom (Pinch)
                if (gesture.fingerCount >= 2) {
                    float panSpeed = 0.0075f * m_CameraDistance;
                    m_CameraTarget += right * (-gesture.panDeltaX * panSpeed) + up * (gesture.panDeltaY * panSpeed);

                    // Zona muerta de 5px y sensibilidad reducida para que el gesto de Pan no active zoom brusco por error
                    if (std::abs(gesture.pinchDelta) > 5.0f) {
                        float effectivePinch = (gesture.pinchDelta > 0.0f) ? (gesture.pinchDelta - 5.0f) : (gesture.pinchDelta + 5.0f);
                        m_CameraDistance -= effectivePinch * (m_CameraDistance * 0.0025f);
                        m_CameraDistance = glm::clamp(m_CameraDistance, 0.5f, 80.0f);
            }
        }
                // Gesto de 1 dedo: Rotación orbital natural de la cámara
                else if (gesture.fingerCount == 1 || ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                        ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                        m_CameraYaw -= dragDelta.x * 0.45f;
                        m_CameraPitch += dragDelta.y * 0.45f;
                        m_CameraPitch = glm::clamp(m_CameraPitch, -89.0f, 89.0f);
                        m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
        }
    }
#else
            // En PC: Fly Cam estilo Unreal / Unity (primera persona / libre)
            // En modo 2D: Clic derecho o central hacen Pan 2D
            if (m_Is2DMode && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
                ImGuiMouseButton panBtn = ImGui::IsMouseDragging(ImGuiMouseButton_Right) ? ImGuiMouseButton_Right : ImGuiMouseButton_Middle;
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(panBtn);
                float panSpeed = (m_CameraDistance * 1.3f) / (std::max)(100.0f, m_ViewportSize.y);
                m_CameraTarget += right * (-dragDelta.x * panSpeed) + up * (dragDelta.y * panSpeed);
                m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                ImGui::ResetMouseDragDelta(panBtn);
    } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                // Fly Cam: Rotar sobre la posición de la cámara (pitch & yaw en primera persona sobre el eje propio, sin orbitar a distancia)
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                m_CameraYaw -= dragDelta.x * 0.35f;
                m_CameraPitch += dragDelta.y * 0.35f;
                m_CameraPitch = glm::clamp(m_CameraPitch, -89.0f, 89.0f);
                m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Right);
    } else if (ImGui::IsKeyDown(ImGuiKey_LeftAlt) && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                // Alt + Clic Izquierdo: Órbita clásica alrededor del objeto / punto objetivo (estilo Maya/Unity)
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                m_CameraYaw -= dragDelta.x * 0.35f;
                m_CameraPitch += dragDelta.y * 0.35f;
                m_CameraPitch = glm::clamp(m_CameraPitch, -89.0f, 89.0f);
                m_CameraPos = m_CameraTarget - forward * m_CameraDistance;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }
            // Desplazamiento / Panorámica: Clic central arrastrado
            else if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
                ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Middle);
                float panSpeed = 0.008f * m_CameraDistance;
                glm::vec3 panDelta = right * (-dragDelta.x * panSpeed) + up * (dragDelta.y * panSpeed);
                m_CameraPos += panDelta;
                m_CameraTarget += panDelta;
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Middle);
    }

            // Zoom / Desplazamiento con rueda del ratón
            if (ImGui::GetIO().MouseWheel != 0.0f) {
                float wheelDelta = ImGui::GetIO().MouseWheel;
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    // Si se mantiene clic derecho, rueda mueve la cámara adelante / atrás
                    m_CameraPos += forward * (wheelDelta * 2.5f);
                    m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
        } else {
                    m_CameraPos += forward * (wheelDelta * (std::max)(1.0f, m_CameraDistance * 0.25f));
                    m_CameraDistance -= wheelDelta * (m_CameraDistance * 0.15f);
                    m_CameraDistance = glm::clamp(m_CameraDistance, 0.5f, 150.0f);
                    m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
        }
    }
#endif
}

        // 4. Atajos de teclado para Modos de Gizmo (Q, W, E, R, X) y Cámara (C)
        if ((m_IsHovered || m_IsFocused) && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && m_ActiveGizmoAxis == -1) {
            if (ImGui::IsKeyPressed(ImGuiKey_Q)) m_GizmoMode = GizmoMode::None;
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_GizmoMode = GizmoMode::Translate;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_GizmoMode = GizmoMode::Rotate;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_GizmoMode = GizmoMode::Scale;
            if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                m_GizmoSpace = (m_GizmoSpace == GizmoSpace::Local) ? GizmoSpace::World : GizmoSpace::Local;
    }
            if (ImGui::IsKeyPressed(ImGuiKey_C)) {
                m_ActiveCameraType = (m_ActiveCameraType == ViewportCameraType::GameCamera) ? ViewportCameraType::EditorCamera : ViewportCameraType::GameCamera;
    }
            // Tecla F: enfocar la cámara en la selección actual
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                FocusOnSelection();
            }
}

        // 5. Movimiento de cámara libre con teclado estilo Unity/UE (WASD + Q/E + Shift) mientras se mantiene Clic Derecho (solo en EditorCamera)
        if ((m_IsHovered || m_IsFocused) && ImGui::IsMouseDown(ImGuiMouseButton_Right) && (m_ActiveCameraType == ViewportCameraType::EditorCamera)) {
            float dt = ImGui::GetIO().DeltaTime;
            if (dt > 0.1f) dt = 0.1f;
            float speed = ImGui::IsKeyDown(ImGuiKey_LeftShift) ? 18.0f : 7.0f;

            glm::vec3 moveDir(0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_W)) moveDir += forward;
            if (ImGui::IsKeyDown(ImGuiKey_S)) moveDir -= forward;
            if (ImGui::IsKeyDown(ImGuiKey_D)) moveDir += right;
            if (ImGui::IsKeyDown(ImGuiKey_A)) moveDir -= right;
            if (ImGui::IsKeyDown(ImGuiKey_E) || ImGui::IsKeyDown(ImGuiKey_Space)) moveDir += glm::vec3(0.0f, 1.0f, 0.0f);
            if (ImGui::IsKeyDown(ImGuiKey_Q)) moveDir -= glm::vec3(0.0f, 1.0f, 0.0f);

            if (glm::length(moveDir) > 0.001f) {
                moveDir = glm::normalize(moveDir);
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
                m_CameraPos += moveDir * speed * dt;
                m_CameraTarget = m_CameraPos + forward * m_CameraDistance;
#else
                m_CameraTarget += moveDir * speed * dt;
#endif
    }
}

        // 6. Selección de entidades mediante clic en el Viewport 3D
        if (isCanvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_ActiveGizmoAxis == -1 && m_GizmoHoveredAxis == -1 && !ImGui::IsKeyDown(ImGuiKey_LeftAlt)) {
            SlipEntity* hit = CastRayFromMouse(glm::vec2(mousePos.x, mousePos.y), view, projection);
            if (ImGui::GetIO().KeyCtrl && hit) {
                // Ctrl+Clic: alternar la entidad en la multi-selección
                bool wasSelected = IsEntitySelected(hit);
                std::vector<SlipEntity*> sel;
                if (wasSelected) {
                    for (SlipEntity* e : m_Selection) {
                        if (e != hit) sel.push_back(e);
                    }
                } else {
                    sel = m_Selection;
                    sel.push_back(hit);
                }
                ApplySelection(sel, hit);
                if (m_OnMultiSelectCallback) m_OnMultiSelectCallback(sel, hit);
            } else {
                m_SelectedEntity = hit;
                if (m_OnSelectCallback) {
                    m_OnSelectCallback(hit);
                }
            }
        }

        // 7. Overlay flotante con estadísticas de rendimiento en tiempo real (FPS, Draw Calls, Culling, V-Clust)
        if (m_ShowStats && m_ViewportSize.x > 200.0f && m_ViewportSize.y > 150.0f) {
            float hudOffsetY = (m_VClustEnabled && m_VClustFrameStats.totalClustersTraversed > 0) ? 235.0f : 128.0f;
            ImVec2 statsPos = ImVec2(m_ViewportBounds[0].x + 12.0f, m_ViewportBounds[1].y - hudOffsetY);
            ImGui::SetNextWindowPos(statsPos, ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.76f);
            ImGuiWindowFlags statsFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                          ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                          ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
            if (ImGui::Begin("##ViewportStatsHUD", nullptr, statsFlags)) {
                float fps = ImGui::GetIO().Framerate;
                float ms = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
                ImVec4 fpsColor = (fps >= 55.0f) ? ImVec4(0.35f, 1.0f, 0.45f, 1.0f) :
                                  ((fps >= 28.0f) ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f) : ImVec4(1.0f, 0.35f, 0.35f, 1.0f));

                ImGui::TextColored(fpsColor, "FPS: %.1f  (%.2f ms)", fps, ms);
                ImGui::Separator();
                ImGui::Text("Draw Calls: %d", m_StatDrawCalls);
                ImGui::Text("Submallas Visibles: %d / %d", m_StatVisibleSubmeshes, m_StatTotalSubmeshes);
                ImGui::Text("Frustum Culled: %d", m_StatFrustumCulled);
                if (m_Visible2DSpritesCount > 0 || m_FrustumCulled2DCount > 0) {
                    ImGui::Text("Sprites 2D: %d visibles (%d culled)", m_Visible2DSpritesCount, m_FrustumCulled2DCount);
                }
                auto& gfxSettings = SlipGraphicsSettings::Get().GetData();
                if (gfxSettings.occlusionCullingEnabled) {
                    ImGui::Text("Occlusion Culled: %d", m_StatOcclusionCulled);
        } else {
                    ImGui::TextDisabled("Occlusion: OFF");
        }

                if (m_VClustEnabled && m_VClustFrameStats.totalClustersTraversed > 0) {
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(0.35f, 0.88f, 1.0f, 1.0f), "V-Clust LOD Activo (SSE <= %.2f px)", m_VClustSseThreshold);
                    ImGui::Text("Clusters Visibles: %u / %u", m_VClustFrameStats.visibleClusters, m_VClustFrameStats.totalClustersTraversed);
                    ImGui::Text("Culled (Frustum: %u | Backface Cone: %u)", m_VClustFrameStats.frustumCulledClusters, m_VClustFrameStats.backfaceCulledClusters);
                    ImGui::Text("Triangulos V-Clust: %u", m_VClustFrameStats.trianglesSubmitted);
                    if (m_VClustDebugMode == 2) {
                        ImGui::TextColored(ImVec4(0.18f, 0.92f, 0.35f, 1.0f), "LOD0"); ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.20f, 0.85f, 0.95f, 1.0f), "LOD1"); ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.95f, 0.85f, 0.20f, 1.0f), "LOD2"); ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.98f, 0.50f, 0.15f, 1.0f), "LOD3"); ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.95f, 0.20f, 0.20f, 1.0f), "LOD4+");
                    }
                }
                ImGui::End();
    }
}
    }
    ImGui::End();

    ImGui::PopStyleVar();
}
