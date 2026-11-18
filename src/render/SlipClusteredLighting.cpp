#include "SlipEngine/render/SlipClusteredLighting.h"
#include "SlipEngine/render/SlipShader.h"
#include "SlipEngine/core/SlipConsole.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
#include <SDL2/SDL.h>
#endif

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>

static const char* s_DefaultBuildGridComp = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 9, local_size_z = 1) in;

struct ClusterAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

layout(std430, binding = 3) buffer ClusterAABBs {
    ClusterAABB clusters[];
};

uniform mat4 u_InverseProjection;
uniform vec2 u_ScreenDimensions;
uniform uvec3 u_GridDim;
uniform float u_ZNear;
uniform float u_ZFar;

vec4 ScreenToView(vec4 screenCoord) {
    vec4 viewCoord = u_InverseProjection * screenCoord;
    return viewCoord / viewCoord.w;
}

void main() {
    uvec3 tileID = gl_GlobalInvocationID.xyz;
    if (tileID.x >= u_GridDim.x || tileID.y >= u_GridDim.y || tileID.z >= u_GridDim.z) return;

    uint clusterIndex = tileID.x + tileID.y * u_GridDim.x + tileID.z * u_GridDim.x * u_GridDim.y;

    vec2 tileSizeNDC = 2.0 / vec2(u_GridDim.xy);
    vec2 minNDC_XY = -1.0 + vec2(tileID.xy) * tileSizeNDC;
    vec2 maxNDC_XY = minNDC_XY + tileSizeNDC;

    float zSliceNear = -u_ZNear * pow(u_ZFar / u_ZNear, float(tileID.z) / float(u_GridDim.z));
    float zSliceFar  = -u_ZNear * pow(u_ZFar / u_ZNear, float(tileID.z + 1u) / float(u_GridDim.z));

    vec4 p0 = ScreenToView(vec4(minNDC_XY.x, minNDC_XY.y, -1.0, 1.0));
    vec4 p1 = ScreenToView(vec4(maxNDC_XY.x, minNDC_XY.y, -1.0, 1.0));
    vec4 p2 = ScreenToView(vec4(maxNDC_XY.x, maxNDC_XY.y, -1.0, 1.0));
    vec4 p3 = ScreenToView(vec4(minNDC_XY.x, maxNDC_XY.y, -1.0, 1.0));

    vec3 near0 = p0.xyz * (zSliceNear / p0.z);
    vec3 near1 = p1.xyz * (zSliceNear / p1.z);
    vec3 near2 = p2.xyz * (zSliceNear / p2.z);
    vec3 near3 = p3.xyz * (zSliceNear / p3.z);

    vec3 far0 = p0.xyz * (zSliceFar / p0.z);
    vec3 far1 = p1.xyz * (zSliceFar / p1.z);
    vec3 far2 = p2.xyz * (zSliceFar / p2.z);
    vec3 far3 = p3.xyz * (zSliceFar / p3.z);

    vec3 minPoint = min(min(min(near0, near1), min(near2, near3)), min(min(far0, far1), min(far2, far3)));
    vec3 maxPoint = max(max(max(near0, near1), max(near2, near3)), max(max(far0, far1), max(far2, far3)));

    clusters[clusterIndex].minPoint = vec4(minPoint, 1.0);
    clusters[clusterIndex].maxPoint = vec4(maxPoint, 1.0);
}
)GLSL";

static const char* s_DefaultCullLightsComp = R"GLSL(
#version 430 core
layout(local_size_x = 16, local_size_y = 9, local_size_z = 1) in;

struct GpuLight {
    vec4 position_range;
    vec4 color_intensity;
    vec4 direction_spotAngle;
    uvec4 params;
};

struct ClusterAABB {
    vec4 minPoint;
    vec4 maxPoint;
};

layout(std430, binding = 2) readonly buffer LightsBuffer {
    GpuLight u_Lights[];
};

layout(std430, binding = 3) readonly buffer ClusterAABBs {
    ClusterAABB u_ClusterAABBs[];
};

layout(std430, binding = 4) writeonly buffer ClusterGridBuffer {
    uvec2 u_ClusterGrid[];
};

layout(std430, binding = 5) writeonly buffer LightIndexBuffer {
    uint u_GlobalLightIndexList[];
};

layout(std430, binding = 6) buffer AtomicCounterBuffer {
    uint u_GlobalLightIndexCounter;
};

uniform mat4 u_ViewMatrix;
uniform uint u_NumLights;
uniform uvec3 u_GridDim;
uniform uint u_MaxIndicesTotal;

bool TestSphereAABB(vec3 center, float radius, vec3 aabbMin, vec3 aabbMax) {
    vec3 closest = clamp(center, aabbMin, aabbMax);
    vec3 delta = closest - center;
    return dot(delta, delta) <= (radius * radius);
}

#define MAX_LIGHTS_PER_CLUSTER_LOCAL 128
uint localVisibleLights[MAX_LIGHTS_PER_CLUSTER_LOCAL];

void main() {
    uvec3 tileID = gl_GlobalInvocationID.xyz;
    if (tileID.x >= u_GridDim.x || tileID.y >= u_GridDim.y || tileID.z >= u_GridDim.z) return;

    uint clusterIndex = tileID.x + tileID.y * u_GridDim.x + tileID.z * u_GridDim.x * u_GridDim.y;

    ClusterAABB aabb = u_ClusterAABBs[clusterIndex];
    vec3 aabbMin = aabb.minPoint.xyz;
    vec3 aabbMax = aabb.maxPoint.xyz;

    uint visibleCount = 0u;

    for (uint i = 0u; i < u_NumLights && visibleCount < MAX_LIGHTS_PER_CLUSTER_LOCAL; ++i) {
        GpuLight light = u_Lights[i];
        uint type = light.params.x;

        if (type >= 2u) {
            continue;
        }

        vec3 lightPosView = (u_ViewMatrix * vec4(light.position_range.xyz, 1.0)).xyz;
        float lightRange  = light.position_range.w;

        if (TestSphereAABB(lightPosView, lightRange, aabbMin, aabbMax)) {
            localVisibleLights[visibleCount++] = i;
        }
    }

    uint offset = 0u;
    if (visibleCount > 0u) {
        offset = atomicAdd(u_GlobalLightIndexCounter, visibleCount);
        for (uint j = 0u; j < visibleCount; ++j) {
            uint writeIdx = offset + j;
            if (writeIdx < u_MaxIndicesTotal) {
                u_GlobalLightIndexList[writeIdx] = localVisibleLights[j];
            }
        }
    }

    u_ClusterGrid[clusterIndex] = uvec2(offset, visibleCount);
}
)GLSL";

static GLuint CompileComputeProgram(const char* src, const std::string& shaderName) {
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!glCreateShader || !glCreateProgram) return 0;
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        GLchar infoLog[1024];
        glGetShaderInfoLog(shader, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[ClusteredLighting] Error compilando Compute Shader (" << shaderName << "): " << infoLog << std::endl;
        SlipConsole::LogError("[ClusteredLighting] Error en Compute Shader: " + shaderName);
        glDeleteShader(shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);

    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        GLchar infoLog[1024];
        glGetProgramInfoLog(program, sizeof(infoLog), nullptr, infoLog);
        std::cerr << "[ClusteredLighting] Error linkeando Program (" << shaderName << "): " << infoLog << std::endl;
        glDeleteProgram(program);
        glDeleteShader(shader);
        return 0;
    }

    glDeleteShader(shader);
    return program;
#else
    return 0;
#endif
}

SlipClusteredLighting& SlipClusteredLighting::Get() {
    static SlipClusteredLighting s_Instance;
    return s_Instance;
}

SlipClusteredLighting::~SlipClusteredLighting() {
    Shutdown();
}

void SlipClusteredLighting::Initialize() {
    if (m_Initialized) return;

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    m_IsSupported = false;
    m_Initialized = true;
    return;
#else
    if (!glad_glGetIntegerv) {
        gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress);
    }

    int major = 0, minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    if (major < 4 || (major == 4 && minor < 3) || !GLAD_GL_VERSION_4_3) {
        SlipConsole::LogWarning("SlipClusteredLighting: OpenGL 4.3+ no disponible. Clustered Shading desactivado.");
        m_IsSupported = false;
        m_Initialized = true;
        return;
    }

    // 1. Cargar y compilar shaders de cómputo
    std::string buildGridSrc = s_DefaultBuildGridComp;
    std::string buildGridPath = "assets/shaders/clustered_grid_build.comp";
    if (SlipFileSystem::Exists(buildGridPath)) {
        std::string loaded;
        if (SlipFileSystem::ReadAllText(buildGridPath, loaded) && !loaded.empty()) {
            buildGridSrc = loaded;
        }
    }

    std::string cullLightsSrc = s_DefaultCullLightsComp;
    std::string cullLightsPath = "assets/shaders/clustered_cull_lights.comp";
    if (SlipFileSystem::Exists(cullLightsPath)) {
        std::string loaded;
        if (SlipFileSystem::ReadAllText(cullLightsPath, loaded) && !loaded.empty()) {
            cullLightsSrc = loaded;
        }
    }

    GLuint progGrid = CompileComputeProgram(buildGridSrc.c_str(), "clustered_grid_build");
    GLuint progCull = CompileComputeProgram(cullLightsSrc.c_str(), "clustered_cull_lights");

    if (!progGrid || !progCull) {
        SlipConsole::LogError("SlipClusteredLighting: Error al inicializar compute shaders.");
        m_IsSupported = false;
        m_Initialized = true;
        return;
    }

    m_BuildGridShader = new SlipShader();
    m_BuildGridShader->ID = progGrid;

    m_CullLightsShader = new SlipShader();
    m_CullLightsShader->ID = progCull;

    // 2. Crear SSBOs
    glGenBuffers(1, &m_LightsSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_LightsSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_MaxLights * sizeof(GpuLight), nullptr, GL_DYNAMIC_DRAW);

    m_TotalClusters = m_GridDim.x * m_GridDim.y * m_GridDim.z; // 16 * 9 * 24 = 3456

    glGenBuffers(1, &m_ClusterAABBSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ClusterAABBSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_TotalClusters * sizeof(GpuClusterAABB), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_ClusterGridSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_ClusterGridSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_TotalClusters * sizeof(glm::uvec2), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_LightIndexSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_LightIndexSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_MaxIndices * sizeof(uint32_t), nullptr, GL_DYNAMIC_DRAW);

    glGenBuffers(1, &m_AtomicCounterSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_AtomicCounterSSBO);
    uint32_t zero = 0;
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(uint32_t), &zero, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    m_IsSupported = true;
    m_Initialized = true;
    SlipConsole::LogSuccess("SlipClusteredLighting inicializado con éxito (Capacidad: " + std::to_string(m_MaxLights) + " luces, " + std::to_string(m_TotalClusters) + " clusters).");
#endif
}

void SlipClusteredLighting::Shutdown() {
    if (!m_Initialized) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (m_LightsSSBO) { glDeleteBuffers(1, &m_LightsSSBO); m_LightsSSBO = 0; }
    if (m_ClusterAABBSSBO) { glDeleteBuffers(1, &m_ClusterAABBSSBO); m_ClusterAABBSSBO = 0; }
    if (m_ClusterGridSSBO) { glDeleteBuffers(1, &m_ClusterGridSSBO); m_ClusterGridSSBO = 0; }
    if (m_LightIndexSSBO) { glDeleteBuffers(1, &m_LightIndexSSBO); m_LightIndexSSBO = 0; }
    if (m_AtomicCounterSSBO) { glDeleteBuffers(1, &m_AtomicCounterSSBO); m_AtomicCounterSSBO = 0; }

    if (m_BuildGridShader) { m_BuildGridShader->destroy(); delete m_BuildGridShader; m_BuildGridShader = nullptr; }
    if (m_CullLightsShader) { m_CullLightsShader->destroy(); delete m_CullLightsShader; m_CullLightsShader = nullptr; }
#endif

    m_Initialized = false;
    m_IsSupported = false;
}

void SlipClusteredLighting::UpdateGrid(
    const glm::mat4& projection,
    const glm::mat4& invProjection,
    float zNear,
    float zFar,
    uint32_t screenWidth,
    uint32_t screenHeight
) {
    if (!IsEnabled()) return;

    // Solo recomputar AABBs si cambiaron los parámetros de proyección o pantalla
    bool needsUpdate = (projection != m_CachedProj ||
                        screenWidth != m_CachedWidth ||
                        screenHeight != m_CachedHeight ||
                        std::abs(zNear - m_CachedNear) > 1e-4f ||
                        std::abs(zFar - m_CachedFar) > 1e-4f);

    if (!needsUpdate) return;

    m_CachedProj = projection;
    m_CachedWidth = screenWidth;
    m_CachedHeight = screenHeight;
    m_CachedNear = zNear;
    m_CachedFar = zFar;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!m_BuildGridShader || m_BuildGridShader->ID == 0) return;

    m_BuildGridShader->use();
    m_BuildGridShader->setMat4("u_InverseProjection", invProjection);
    m_BuildGridShader->setVec2("u_ScreenDimensions", static_cast<float>(screenWidth), static_cast<float>(screenHeight));
    glUniform3ui(glGetUniformLocation(m_BuildGridShader->ID, "u_GridDim"), m_GridDim.x, m_GridDim.y, m_GridDim.z);
    m_BuildGridShader->setFloat("u_ZNear", zNear);
    m_BuildGridShader->setFloat("u_ZFar", zFar);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ClusterAABBSSBO);

    // Despacho: (1, 1, 24) grupos de trabajo con local_size (16, 9, 1) = 16x9x24 celdas
    glDispatchCompute(1, 1, m_GridDim.z);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#endif
}

void SlipClusteredLighting::UploadLights(const std::vector<GpuLight>& lights) {
    if (!IsEnabled()) return;

    m_UploadedLightCount = static_cast<uint32_t>((std::min)(lights.size(), static_cast<size_t>(m_MaxLights)));
    if (m_UploadedLightCount == 0) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_LightsSSBO);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, m_UploadedLightCount * sizeof(GpuLight), lights.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
#endif
}

void SlipClusteredLighting::CullLights(const glm::mat4& viewMatrix) {
    if (!IsEnabled()) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (!m_CullLightsShader || m_CullLightsShader->ID == 0) return;

    // Resetear contador atómico a 0
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_AtomicCounterSSBO);
    uint32_t zero = 0;
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t), &zero);

    m_CullLightsShader->use();
    m_CullLightsShader->setMat4("u_ViewMatrix", viewMatrix);
    glUniform1ui(glGetUniformLocation(m_CullLightsShader->ID, "u_NumLights"), m_UploadedLightCount);
    glUniform3ui(glGetUniformLocation(m_CullLightsShader->ID, "u_GridDim"), m_GridDim.x, m_GridDim.y, m_GridDim.z);
    glUniform1ui(glGetUniformLocation(m_CullLightsShader->ID, "u_MaxIndicesTotal"), m_MaxIndices);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_LightsSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_ClusterAABBSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_ClusterGridSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_LightIndexSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, m_AtomicCounterSSBO);

    glDispatchCompute(1, 1, m_GridDim.z);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
#endif
}

void SlipClusteredLighting::BindClusterData(SlipShader* shader, float zNear, float zFar, uint32_t screenW, uint32_t screenH) {
    if (!shader) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (IsEnabled() && m_IsSupported) {
        shader->setBool("u_ClusteredLightingEnabled", true);

        // Enlazar SSBOs necesarios para el fragment shader
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_LightsSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_ClusterGridSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_LightIndexSSBO);

        // Uniformes de direccionamiento de cluster en el fragment shader
        glUniform3ui(glGetUniformLocation(shader->ID, "u_ClusterGridDim"), m_GridDim.x, m_GridDim.y, m_GridDim.z);
        float tileW = (screenW > 0) ? (static_cast<float>(screenW) / static_cast<float>(m_GridDim.x)) : 1.0f;
        float tileH = (screenH > 0) ? (static_cast<float>(screenH) / static_cast<float>(m_GridDim.y)) : 1.0f;
        shader->setVec2("u_ClusterTileSize", tileW, tileH);
        shader->setFloat("u_ClusterZNear", zNear);
        shader->setFloat("u_ClusterZFar", zFar);

        float logZFactor = (std::log(zFar / zNear)) / static_cast<float>(m_GridDim.z);
        shader->setFloat("u_ClusterLogZFactor", (logZFactor > 1e-6f) ? logZFactor : 1.0f);
    } else {
        shader->setBool("u_ClusteredLightingEnabled", false);
    }
#else
    shader->setBool("u_ClusteredLightingEnabled", false);
#endif
}
