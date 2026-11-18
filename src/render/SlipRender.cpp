#include "SlipEngine/render/SlipRender.h"
#include "SlipEngine/render/SlipMaterial.h"
#include "SlipEngine/render/SlipShaderLibrary.h"
#include "SlipEngine/scene/SlipSkyComponent.h"

#include <algorithm>
#include <cmath>

// ============================================================================
// Compute Shader GLSL 450 para GPU Culling y generación de Indirect Commands en PC
// ============================================================================
static const char* kVClustGPUCullingComputeGLSL = R"(#version 450 core
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

struct SlipClusterGPU {
    uint  vertexOffset;
    uint  vertexCount;
    uint  indexOffset;
    uint  indexCount;
    vec3  center;
    float radius;
    vec3  coneAxis;
    float coneCutoff;
    float lodError;
    float parentError;
    int   parentId;
    int   firstChildId;
    uint  childCount;
    uint  lodLevel;
    uint  materialIndex;
    uint  submeshIndex;
    uint  flags;
};

struct DrawElementsIndirectCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

layout(std430, binding = 0) readonly buffer ClusterBuffer {
    SlipClusterGPU clusters[];
};

layout(std430, binding = 1) writeonly buffer IndirectCommandBuffer {
    DrawElementsIndirectCommand commands[];
};

layout(binding = 2, offset = 0) uniform atomic_uint visibleClusterCount;

uniform mat4  u_WorldTransform;
uniform vec4  u_FrustumPlanes[6];
uniform vec3  u_CameraWorldPos;
uniform float u_ViewportHeight;
uniform float u_TanHalfFov;
uniform float u_SseThreshold;
uniform uint  u_TotalClusters;

float ComputeSSE(float err, float dist) {
    float d = max(dist, 0.0001);
    return (err * u_ViewportHeight) / (2.0 * d * max(u_TanHalfFov, 0.0001));
}

bool IsSphereInFrustum(vec3 center, float radius) {
    for (int i = 0; i < 6; ++i) {
        if (dot(u_FrustumPlanes[i].xyz, center) + u_FrustumPlanes[i].w < -radius) {
            return false;
        }
    }
    return true;
}

bool IsBackfaceConeCulled(vec3 worldCenter, float worldRadius, vec3 worldConeAxis, float coneCutoff) {
    if (coneCutoff <= 0.0 || coneCutoff >= 1.0) return false;
    vec3 toCluster = worldCenter - u_CameraWorldPos;
    float dist = length(toCluster);
    if (dist <= worldRadius + 0.0001) return false;
    vec3 viewDir = toCluster / dist;
    float d = dot(viewDir, worldConeAxis);
    if (d <= 0.0) return false;
    // Evaluamos el cono normal promedio frente al vector de visión considerando el radio esférico
    float sinCone = sqrt(max(0.0, 1.0 - coneCutoff * coneCutoff));
    return d >= (coneCutoff + (worldRadius * sinCone) / dist);
}

void main() {
    uint idx = gl_GlobalInvocationID.x;
    if (idx >= u_TotalClusters) return;

    SlipClusterGPU c = clusters[idx];
    if (c.indexCount == 0u) return;

    vec3 worldCenter = (u_WorldTransform * vec4(c.center, 1.0)).xyz;
    float scaleX = length(u_WorldTransform[0].xyz);
    float scaleY = length(u_WorldTransform[1].xyz);
    float scaleZ = length(u_WorldTransform[2].xyz);
    float maxScale = max(scaleX, max(scaleY, scaleZ));
    float worldRadius = c.radius * maxScale;

    float dist = max(length(worldCenter - u_CameraWorldPos) - worldRadius, 0.001);
    float selfSSE   = ComputeSSE(c.lodError * maxScale, dist);
    float parentSSE = ComputeSSE(c.parentError * maxScale, dist);

    // Condición de corte continuo del árbol de clusters:
    // El clúster es elegido si su propio error <= umbral (o es hoja LOD 0)
    // Y su padre tiene un error > umbral (por lo que el padre tuvo que subdividirse)
    bool isLeaf = (c.childCount == 0u);
    bool selfAcceptable = isLeaf || (selfSSE <= u_SseThreshold);
    bool parentTooCoarse = (c.parentId < 0) || (parentSSE > u_SseThreshold);

    if (!selfAcceptable || !parentTooCoarse) return;

    if (!IsSphereInFrustum(worldCenter, worldRadius)) return;

    vec3 worldAxis = normalize(mat3(u_WorldTransform) * c.coneAxis);
    if (IsBackfaceConeCulled(worldCenter, worldRadius, worldAxis, c.coneCutoff)) return;

    uint outIdx = atomicCounterIncrement(visibleClusterCount);
    commands[outIdx].count         = c.indexCount;
    commands[outIdx].instanceCount = 1u;
    commands[outIdx].firstIndex    = c.indexOffset;
    commands[outIdx].baseVertex    = int(c.vertexOffset);
    commands[outIdx].baseInstance  = 0u;
}
)";

const char* SlipRender::GetPCClusterCullingComputeShaderSource()
{
    return kVClustGPUCullingComputeGLSL;
}

SlipRender::~SlipRender()
{
    Shutdown();
}

void SlipRender::Initialize()
{
    if (m_Initialized) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (glGenBuffers != nullptr) {
        glGenBuffers(1, &m_IndirectBuffer);
    }
#endif
    m_Initialized = true;
}

void SlipRender::Shutdown()
{
    if (!m_Initialized) return;

#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (m_IndirectBuffer != 0 && glDeleteBuffers != nullptr) {
        glDeleteBuffers(1, &m_IndirectBuffer);
        m_IndirectBuffer = 0;
    }
    if (m_ClusterSSBO != 0 && glDeleteBuffers != nullptr) {
        glDeleteBuffers(1, &m_ClusterSSBO);
        m_ClusterSSBO = 0;
    }
    if (m_AtomicCounterBuf != 0 && glDeleteBuffers != nullptr) {
        glDeleteBuffers(1, &m_AtomicCounterBuf);
        m_AtomicCounterBuf = 0;
    }
    if (m_ComputeProgram != 0 && glDeleteProgram != nullptr) {
        glDeleteProgram(m_ComputeProgram);
        m_ComputeProgram = 0;
    }
#endif
    m_IndirectCapacity = 0;
    ShutdownCloudSystem();
    m_Initialized = false;
}

// ============================================================================
// FASE 3.1: Proyección del Screen-Space Error (SSE)
// Fórmula exacta requerida:
//   ErrorPixeles = (lodError * ViewportHeight) / (2.0f * Distancia * tan(FOV * 0.5f))
// ============================================================================
float SlipRender::ComputeScreenSpaceError(
    float lodError,
    float distance,
    float viewportHeight,
    float fovRadians)
{
    if (lodError <= 0.0f) {
        return 0.0f;
    }
    if (lodError >= 1e20f) {
        return 1e30f;
    }

    const float safeDistance = std::max(distance, 1e-4f);
    const float safeHalfTan  = std::max(std::tan(fovRadians * 0.5f), 1e-4f);
    const float safeHeight   = std::max(viewportHeight, 1.0f);

    return (lodError * safeHeight) / (2.0f * safeDistance * safeHalfTan);
}

// ============================================================================
// FASE 3.2: Frustum Culling Esférico contra los 6 planos de SlipCamera/SlipFrustum
// ============================================================================
bool SlipRender::IsClusterFrustumVisible(
    const SlipCluster& cluster,
    const glm::mat4& worldTransform,
    const SlipFrustum& frustum,
    glm::vec3* outWorldCenter,
    float* outWorldRadius)
{
    glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4(cluster.center, 1.0f));

    // Escala máxima uniforme de las columnas de la matriz de transformación
    float sx = glm::length(glm::vec3(worldTransform[0]));
    float sy = glm::length(glm::vec3(worldTransform[1]));
    float sz = glm::length(glm::vec3(worldTransform[2]));
    float maxScale = std::max({ sx, sy, sz, 1e-5f });
    float worldRadius = cluster.radius * maxScale;

    if (outWorldCenter) *outWorldCenter = worldCenter;
    if (outWorldRadius) *outWorldRadius = worldRadius;

    for (int i = 0; i < SlipFrustum::COUNT; ++i) {
        float signedDist = frustum.planes[i].GetSignedDistanceToPoint(worldCenter);
        if (signedDist < -worldRadius) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// FASE 3.2: Backface Cluster Culling evaluando el cono de normales
// Un clúster completo mira de espaldas a la cámara si:
//   dot(normalize(worldCenter - cameraPos), worldConeAxis) >= coneCutoff
// (considerando además el ángulo subtendido por el radio de la esfera delimitadora)
// ============================================================================
bool SlipRender::IsClusterBackfaceCulled(
    const SlipCluster& cluster,
    const glm::mat4& worldTransform,
    const glm::vec3& cameraWorldPos)
{
    // Si coneCutoff <= 0.0f o >= 1.0f, las normales del clúster cubren más de un hemisferio y no puede descartarse
    if (cluster.coneCutoff <= 0.0f || cluster.coneCutoff >= 0.999f) {
        return false;
    }

    float axisLenSq = glm::dot(cluster.coneAxis, cluster.coneAxis);
    if (axisLenSq < 1e-6f) {
        return false;
    }

    glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4(cluster.center, 1.0f));
    float sx = glm::length(glm::vec3(worldTransform[0]));
    float sy = glm::length(glm::vec3(worldTransform[1]));
    float sz = glm::length(glm::vec3(worldTransform[2]));
    float maxScale = std::max({ sx, sy, sz, 1e-5f });
    float worldRadius = cluster.radius * maxScale;

    glm::vec3 toCluster = worldCenter - cameraWorldPos;
    float dist = glm::length(toCluster);
    if (dist <= worldRadius + 1e-4f) {
        // La cámara está dentro o tocando la esfera delimitadora del clúster
        return false;
    }

    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(worldTransform)));
    glm::vec3 worldConeAxis = normalMat * cluster.coneAxis;
    float worldAxisLen = glm::length(worldConeAxis);
    if (worldAxisLen < 1e-6f) {
        return false;
    }
    worldConeAxis /= worldAxisLen;

    glm::vec3 viewDir = toCluster / dist;
    float d = glm::dot(viewDir, worldConeAxis);
    if (d <= 0.0f) {
        // El cono normal apunta en dirección a la cámara (frontal), no puede cullearse
        return false;
    }

    // Prueba conservadora de cono con radio esférico (meshoptimizer formula):
    float sinCone = std::sqrt(std::max(0.0f, 1.0f - cluster.coneCutoff * cluster.coneCutoff));
    return d >= (cluster.coneCutoff + (worldRadius * sinCone) / dist);
}

// ============================================================================
// FASE 3.3: Traversal Jerárquico y Selección de LOD en Espacio de Pantalla
// ============================================================================
void SlipRender::TraverseClusterHierarchy(
    const std::vector<SlipCluster>& clusters,
    uint32_t rootClusterCount,
    const glm::mat4& worldTransform,
    const SlipFrustum& frustum,
    const glm::vec3& cameraWorldPos,
    float viewportHeight,
    float fovRadians,
    std::vector<SlipVisibleCluster>& outVisibleClusters,
    SlipVClustStats* inoutStats,
    float ssePixelThreshold,
    bool enableBackfaceConeCulling)
{
    outVisibleClusters.clear();
    if (clusters.empty()) return;

    float sx = glm::length(glm::vec3(worldTransform[0]));
    float sy = glm::length(glm::vec3(worldTransform[1]));
    float sz = glm::length(glm::vec3(worldTransform[2]));
    float maxScale = std::max({ sx, sy, sz, 1e-5f });
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(worldTransform)));

    // Si rootClusterCount es 0, considerar como raíces todos los clusters con parentId == -1
    std::vector<uint32_t> stack;
    stack.reserve(64);

    if (rootClusterCount > 0 && rootClusterCount <= clusters.size()) {
        for (uint32_t i = 0; i < rootClusterCount; ++i) {
            stack.push_back(rootClusterCount - 1 - i);
        }
    } else {
        for (size_t i = clusters.size(); i > 0; --i) {
            if (clusters[i - 1].parentId < 0) {
                stack.push_back(static_cast<uint32_t>(i - 1));
            }
        }
    }

    while (!stack.empty()) {
        uint32_t clusterIdx = stack.back();
        stack.pop_back();

        if (clusterIdx >= clusters.size()) continue;
        const SlipCluster& c = clusters[clusterIdx];

        if (inoutStats) {
            inoutStats->totalClustersTraversed++;
        }

        // 1. Frustum Culling Esférico optimizado contra los 6 planos de la cámara
        glm::vec3 worldCenter = glm::vec3(worldTransform * glm::vec4(c.center, 1.0f));
        float worldRadius = c.radius * maxScale;
        bool inFrustum = true;
        for (int p = 0; p < SlipFrustum::COUNT; ++p) {
            if (frustum.planes[p].GetSignedDistanceToPoint(worldCenter) < -worldRadius) {
                inFrustum = false;
                break;
            }
        }
        if (!inFrustum) {
            if (inoutStats) {
                inoutStats->frustumCulledClusters++;
            }
            continue;
        }

        // 2. Calcular distancia efectiva desde la cámara al clúster de forma continua y estable
        // Evitar que clusters grandes (worldRadius grande) resten su radio completo produciendo
        // distancias de 0.001m que disparan el error en píxeles a cientos de miles y provocan
        // alternancia violenta de LODs (popping/shimmering) al mover mínimamente la cámara
        float centerDist = glm::length(worldCenter - cameraWorldPos);
        float effectiveDist = std::max(centerDist - worldRadius * 0.25f, 0.5f);

        // 3. Evaluar el Screen-Space Error (SSE) proyectado en píxeles
        float scaledLodError = c.lodError * maxScale;
        float errorPixeles = ComputeScreenSpaceError(scaledLodError, effectiveDist, viewportHeight, fovRadians);

        // Regla de decisión V-Clust:
        // - Si ErrorPixeles <= ssePixelThreshold (o no tiene hijos): procesar este clúster padre.
        // - Si ErrorPixeles > ssePixelThreshold (y tiene hijos): descender a los clusters hijos.
        bool hasValidChildren = (c.childCount > 0 &&
                                 c.firstChildId >= 0 &&
                                 static_cast<size_t>(c.firstChildId + c.childCount) <= clusters.size());

        if (errorPixeles > ssePixelThreshold && hasValidChildren) {
            // Descender a los clusters hijos (insertar en orden inverso para visitar en orden natural)
            for (uint32_t k = c.childCount; k > 0; --k) {
                stack.push_back(static_cast<uint32_t>(c.firstChildId) + (k - 1));
            }
            continue;
        }

        // 4. Backface Cluster Culling optimizado evaluando el cono normal pretransformado
        if (enableBackfaceConeCulling && c.coneCutoff > 0.0f && c.coneCutoff < 0.999f) {
            glm::vec3 toCluster = worldCenter - cameraWorldPos;
            float dist = glm::length(toCluster);
            if (dist > worldRadius + 1e-4f) {
                glm::vec3 worldConeAxis = normalMat * c.coneAxis;
                float worldAxisLen = glm::length(worldConeAxis);
                if (worldAxisLen > 1e-6f) {
                    worldConeAxis /= worldAxisLen;
                    glm::vec3 viewDir = toCluster / dist;
                    float d = glm::dot(viewDir, worldConeAxis);
                    if (d > 0.0f) {
                        float sinCone = std::sqrt(std::max(0.0f, 1.0f - c.coneCutoff * c.coneCutoff));
                        if (d >= (c.coneCutoff + (worldRadius * sinCone) / dist)) {
                            if (inoutStats) {
                                inoutStats->backfaceCulledClusters++;
                            }
                            continue;
                        }
                    }
                }
            }
        }

        if (c.indexCount == 0) continue;

        // 5. Clúster aprobado para renderizado
        SlipVisibleCluster vis{};
        vis.clusterId     = clusterIdx;
        vis.materialIndex = c.materialIndex;
        vis.submeshIndex  = c.submeshIndex;
        vis.lodLevel      = c.lodLevel;
        vis.indexOffset   = c.indexOffset;
        vis.indexCount    = c.indexCount;
        vis.vertexOffset  = static_cast<int32_t>(c.vertexOffset);
        vis.distanceToCam = centerDist;
        vis.projectedSSE  = errorPixeles;

        outVisibleClusters.push_back(vis);

        if (inoutStats) {
            inoutStats->visibleClusters++;
            inoutStats->trianglesSubmitted += (c.indexCount / 3);
        }
    }
}

void SlipRender::SelectVisibleClusters(
    const SlipMesh& mesh,
    const glm::mat4& worldTransform,
    SlipCamera& camera,
    std::vector<SlipVisibleCluster>& outVisibleClusters,
    SlipVClustStats* inoutStats,
    float ssePixelThreshold,
    bool enableBackfaceConeCulling)
{
    if (!mesh.hasClusterLOD || mesh.clusters.empty()) {
        outVisibleClusters.clear();
        return;
    }

    glm::mat4 view = camera.GetViewMatrix();
    glm::mat4 proj = camera.GetProjectionMatrix();
    SlipFrustum frustum(proj * view);

    float viewportHeight = (camera.height > 1.0f) ? camera.height : 1080.0f;
    float fovRadians     = glm::radians(glm::clamp(camera.Zoom, 10.0f, 170.0f));

    TraverseClusterHierarchy(
        mesh.clusters,
        mesh.clusterHeader.rootClusterCount,
        worldTransform,
        frustum,
        camera.position,
        viewportHeight,
        fovRadians,
        outVisibleClusters,
        inoutStats,
        ssePixelThreshold,
        enableBackfaceConeCulling
    );
}

void SlipRender::BuildIndirectCommands(
    const std::vector<SlipVisibleCluster>& visibleClusters,
    std::vector<DrawElementsIndirectCommand>& outCommands)
{
    outCommands.clear();
    outCommands.reserve(visibleClusters.size());

    for (const auto& vc : visibleClusters) {
        DrawElementsIndirectCommand cmd{};
        cmd.count         = vc.indexCount;
        cmd.instanceCount = 1;
        cmd.firstIndex    = vc.indexOffset;
        cmd.baseVertex    = vc.vertexOffset;
        cmd.baseInstance  = 0;
        outCommands.push_back(cmd);
    }
}

// ============================================================================
// FASE 4: Despacho Multiplataforma (PC OpenGL 4.5+ vs Android OpenGL ES 3.0 TBDR)
// ============================================================================
void SlipRender::RenderClusteredMesh(
    SlipMesh& mesh,
    const glm::mat4& worldTransform,
    SlipCamera& camera,
    SlipShader* activeShader,
    float ssePixelThreshold)
{
    if (!mesh.hasClusterLOD || mesh.clusters.empty()) {
        return;
    }

    if (!m_Initialized) {
        Initialize();
    }

    std::vector<SlipVisibleCluster> visibleClusters;
    SelectVisibleClusters(mesh, worldTransform, camera, visibleClusters, &m_Stats, ssePixelThreshold, true);

    if (visibleClusters.empty()) {
        return;
    }

    if (activeShader) {
        activeShader->use();
        activeShader->setMat4("model", worldTransform);
    }

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    // Ruta Android (OpenGL ES 3.0 / 3.2 TBDR):
    // Prohibido usar rasterizadores por software o compute shaders pesados en memoria global.
    // Despacho CPU-Driven por lotes ordenado por material y profundidad.
    RenderClustersAndroid_TBDR(mesh, visibleClusters, activeShader);
#else
    // Ruta PC (OpenGL 4.5+):
    // Llenado de búfer indirecto y envío en bloque con glMultiDrawElementsIndirect.
    RenderClustersPC_Indirect(mesh, visibleClusters, activeShader);
#endif
}

void SlipRender::RenderClustersPC_Indirect(
    SlipMesh& mesh,
    const std::vector<SlipVisibleCluster>& visibleClusters,
    SlipShader* activeShader)
{
#if !defined(SLIP_PLATFORM_ANDROID) && !defined(__ANDROID__)
    if (visibleClusters.empty()) return;

    unsigned int targetVAO = (mesh.clusterVAO != 0) ? mesh.clusterVAO : mesh.masterVAO;
    if (targetVAO == 0) return;

    // Agrupar los clusters visibles por material para minimizar cambios de estado de textura/uniforms
    std::vector<SlipVisibleCluster> sortedClusters = visibleClusters;
    std::sort(sortedClusters.begin(), sortedClusters.end(),
        [](const SlipVisibleCluster& a, const SlipVisibleCluster& b) {
            if (a.materialIndex != b.materialIndex) return a.materialIndex < b.materialIndex;
            return a.distanceToCam < b.distanceToCam;
        });

    std::vector<DrawElementsIndirectCommand> indirectCommands;
    BuildIndirectCommands(sortedClusters, indirectCommands);

    if (m_IndirectBuffer == 0 && glGenBuffers != nullptr) {
        glGenBuffers(1, &m_IndirectBuffer);
    }

    const size_t requiredBytes = indirectCommands.size() * sizeof(DrawElementsIndirectCommand);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, m_IndirectBuffer);
    if (requiredBytes > m_IndirectCapacity) {
        m_IndirectCapacity = requiredBytes * 2;
        glBufferData(GL_DRAW_INDIRECT_BUFFER, m_IndirectCapacity, nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_DRAW_INDIRECT_BUFFER, 0, requiredBytes, indirectCommands.data());

    glBindVertexArray(targetVAO);

    // Emitir un único glMultiDrawElementsIndirect por lote de material
    size_t batchStart = 0;
    while (batchStart < sortedClusters.size()) {
        uint32_t matIdx = sortedClusters[batchStart].materialIndex;
        size_t batchEnd = batchStart + 1;
        while (batchEnd < sortedClusters.size() && sortedClusters[batchEnd].materialIndex == matIdx) {
            ++batchEnd;
        }

        if (activeShader && matIdx < mesh.materials.size() && mesh.materials[matIdx].mat != nullptr) {
            mesh.materials[matIdx].mat->bindMaterialProperties(activeShader);
        }

        const GLsizei drawCount = static_cast<GLsizei>(batchEnd - batchStart);
        const void* indirectOffset = reinterpret_cast<const void*>(batchStart * sizeof(DrawElementsIndirectCommand));

        if (glMultiDrawElementsIndirect != nullptr) {
            glMultiDrawElementsIndirect(
                GL_TRIANGLES,
                GL_UNSIGNED_INT,
                indirectOffset,
                drawCount,
                sizeof(DrawElementsIndirectCommand)
            );
            m_Stats.drawCallsIssued++;
        } else {
            // Fallback seguro en caso de contexto OpenGL < 4.3
            for (size_t i = batchStart; i < batchEnd; ++i) {
                const auto& cmd = indirectCommands[i];
                glDrawElements(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(cmd.count),
                    GL_UNSIGNED_INT,
                    reinterpret_cast<const void*>(static_cast<uintptr_t>(cmd.firstIndex) * sizeof(uint32_t))
                );
                m_Stats.drawCallsIssued++;
            }
        }

        batchStart = batchEnd;
    }

    glBindVertexArray(0);
    glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
#else
    (void)mesh;
    (void)visibleClusters;
    (void)activeShader;
#endif
}

void SlipRender::RenderClustersAndroid_TBDR(
    SlipMesh& mesh,
    std::vector<SlipVisibleCluster>& visibleClusters,
    SlipShader* activeShader)
{
    if (visibleClusters.empty()) return;

    unsigned int targetVAO = (mesh.clusterVAO != 0) ? mesh.clusterVAO : mesh.masterVAO;
    if (targetVAO == 0) return;

    // 1. Ordenamiento amigable con arquitecturas TBDR móviles (Adreno / Mali / PowerVR):
    //    - Primero por materialIndex (evita cambios de descriptor/textura y flushes de tilers).
    //    - Segundo por distancia front-to-back cuantizada en bandas para maximizar Hidden Surface Removal
    //      / Early-Z rechazando fragmentos antes del sombreado sin romper la contigüidad del EBO.
    //    - Tercero por indexOffset ascendente para fusionar (coalesce) clusters adyacentes en el EBO
    //      en una sola llamada glDrawElementsInstanced / glDrawElements.
    std::sort(visibleClusters.begin(), visibleClusters.end(),
        [](const SlipVisibleCluster& a, const SlipVisibleCluster& b) {
            if (a.materialIndex != b.materialIndex) {
                return a.materialIndex < b.materialIndex;
            }
            // Banda de profundidad de 4 metros para equilibrar Early-Z y fusión de rangos EBO contiguos
            int depthBucketA = static_cast<int>(a.distanceToCam * 0.25f);
            int depthBucketB = static_cast<int>(b.distanceToCam * 0.25f);
            if (depthBucketA != depthBucketB) {
                return depthBucketA < depthBucketB;
            }
            return a.indexOffset < b.indexOffset;
        });

    glBindVertexArray(targetVAO);

    uint32_t currentMaterial = UINT32_MAX;
    size_t i = 0;
    const size_t count = visibleClusters.size();

    while (i < count) {
        const SlipVisibleCluster& first = visibleClusters[i];

        // Cambiar estado de material únicamente cuando varía el materialIndex del lote
        if (first.materialIndex != currentMaterial) {
            currentMaterial = first.materialIndex;
            if (activeShader && currentMaterial < mesh.materials.size() && mesh.materials[currentMaterial].mat != nullptr) {
                mesh.materials[currentMaterial].mat->bindMaterialProperties(activeShader);
            }
        }

        // Fusionar (coalesce) clusters cuyos rangos de índices en el EBO sean contiguos
        uint32_t mergedFirstIndex = first.indexOffset;
        uint32_t mergedIndexCount = first.indexCount;

        size_t next = i + 1;
        while (next < count &&
               visibleClusters[next].materialIndex == currentMaterial &&
               visibleClusters[next].indexOffset == (mergedFirstIndex + mergedIndexCount)) {
            mergedIndexCount += visibleClusters[next].indexCount;
            ++next;
        }

        const void* eboByteOffset = reinterpret_cast<const void*>(static_cast<uintptr_t>(mergedFirstIndex) * sizeof(uint32_t));

        // Ejecución mediante glDrawElementsInstanced (1 instancia por lote fusionado) compatible con OpenGL ES 3.0 puro
        glDrawElementsInstanced(
            GL_TRIANGLES,
            static_cast<GLsizei>(mergedIndexCount),
            GL_UNSIGNED_INT,
            eboByteOffset,
            1
        );

        m_Stats.drawCallsIssued++;
        i = next;
    }

    glBindVertexArray(0);
}

// ============================================================================
// FASE 5: Sistema Hbrido de Nubes Volumtricas (PC + Android)
// ============================================================================
SlipRender& SlipRender::GetInstance()
{
    static SlipRender s_Instance;
    return s_Instance;
}

void SlipRender::InitCloudSystem()
{
#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    InitSliceMesh();
#else
    InitFullscreenQuad();
#endif
}

void SlipRender::ShutdownCloudSystem()
{
    if (m_SliceVAO != 0) {
        glDeleteVertexArrays(1, &m_SliceVAO);
        glDeleteBuffers(1, &m_SliceVBO);
        glDeleteBuffers(1, &m_SliceEBO);
        m_SliceVAO = m_SliceVBO = m_SliceEBO = 0;
        m_SliceIndexCount = 0;
    }
    if (m_FullscreenQuadVAO != 0) {
        glDeleteVertexArrays(1, &m_FullscreenQuadVAO);
        glDeleteBuffers(1, &m_FullscreenQuadVBO);
        m_FullscreenQuadVAO = m_FullscreenQuadVBO = 0;
    }
    if (m_CloudFBO != 0) {
        glDeleteFramebuffers(1, &m_CloudFBO);
        m_CloudFBO = 0;
    }
    if (m_CloudColorTex != 0) {
        glDeleteTextures(1, &m_CloudColorTex);
        m_CloudColorTex = 0;
    }
    m_CloudFboWidth = m_CloudFboHeight = 0;

    if (m_CloudUpscaleFBO != 0) {
        glDeleteFramebuffers(1, &m_CloudUpscaleFBO);
        m_CloudUpscaleFBO = 0;
    }
    if (m_CloudUpscaleTex != 0) {
        glDeleteTextures(1, &m_CloudUpscaleTex);
        m_CloudUpscaleTex = 0;
    }
    m_CloudUpscaleWidth = m_CloudUpscaleHeight = 0;
    if (m_CloudBlitProgram != 0) {
        glDeleteProgram(m_CloudBlitProgram);
        m_CloudBlitProgram = 0;
    }
}

void SlipRender::InitSliceMesh()
{
    if (m_SliceVAO != 0) return;

    const int GRID_RES = 16;
    std::vector<float> vertices;
    vertices.reserve((GRID_RES + 1) * (GRID_RES + 1) * 5);

    for (int z = 0; z <= GRID_RES; ++z) {
        float fz = static_cast<float>(z) / static_cast<float>(GRID_RES);
        float pz = fz * 2.0f - 1.0f;
        for (int x = 0; x <= GRID_RES; ++x) {
            float fx = static_cast<float>(x) / static_cast<float>(GRID_RES);
            float px = fx * 2.0f - 1.0f;

            // X, Y (0), Z
            vertices.push_back(px);
            vertices.push_back(0.0f);
            vertices.push_back(pz);

            // UVs
            vertices.push_back(fx);
            vertices.push_back(fz);
        }
    }

    std::vector<unsigned int> indices;
    indices.reserve(GRID_RES * GRID_RES * 6);
    for (int z = 0; z < GRID_RES; ++z) {
        for (int x = 0; x < GRID_RES; ++x) {
            unsigned int row0 = z * (GRID_RES + 1);
            unsigned int row1 = (z + 1) * (GRID_RES + 1);

            indices.push_back(row0 + x);
            indices.push_back(row1 + x);
            indices.push_back(row0 + x + 1);

            indices.push_back(row0 + x + 1);
            indices.push_back(row1 + x);
            indices.push_back(row1 + x + 1);
        }
    }

    m_SliceIndexCount = static_cast<uint32_t>(indices.size());

    glGenVertexArrays(1, &m_SliceVAO);
    glGenBuffers(1, &m_SliceVBO);
    glGenBuffers(1, &m_SliceEBO);

    glBindVertexArray(m_SliceVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_SliceVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_SliceEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    // Atributo 0: Posicin
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

    // Atributo 1: Coordenadas UV
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
}

void SlipRender::InitFullscreenQuad()
{
    if (m_FullscreenQuadVAO != 0) return;

    float quadVerts[] = {
        // PosX, PosY,   U,    V
        -1.0f,  1.0f,  0.0f, 1.0f,
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,

        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
         1.0f,  1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &m_FullscreenQuadVAO);
    glGenBuffers(1, &m_FullscreenQuadVBO);

    glBindVertexArray(m_FullscreenQuadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_FullscreenQuadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

    glBindVertexArray(0);
}

void SlipRender::EnsureCloudFBO(int width, int height)
{
    if (m_CloudFBO != 0 && m_CloudFboWidth == width && m_CloudFboHeight == height) {
        return;
    }

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    if (m_CloudFBO == 0) {
        glGenFramebuffers(1, &m_CloudFBO);
    }
    if (m_CloudColorTex == 0) {
        glGenTextures(1, &m_CloudColorTex);
    }

    m_CloudFboWidth = width;
    m_CloudFboHeight = height;

    glBindFramebuffer(GL_FRAMEBUFFER, m_CloudFBO);

    glBindTexture(GL_TEXTURE_2D, m_CloudColorTex);
#if defined(GL_RGBA16F)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_CloudColorTex, 0);
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
}

void SlipRender::EnsureCloudUpscaleFBO(int width, int height)
{
    if (m_CloudUpscaleFBO != 0 && m_CloudUpscaleWidth == width && m_CloudUpscaleHeight == height) {
        return;
    }

    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    if (m_CloudUpscaleFBO == 0) {
        glGenFramebuffers(1, &m_CloudUpscaleFBO);
    }
    if (m_CloudUpscaleTex == 0) {
        glGenTextures(1, &m_CloudUpscaleTex);
    }

    m_CloudUpscaleWidth = width;
    m_CloudUpscaleHeight = height;

    glBindFramebuffer(GL_FRAMEBUFFER, m_CloudUpscaleFBO);

    glBindTexture(GL_TEXTURE_2D, m_CloudUpscaleTex);
#if defined(GL_RGBA16F)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#endif
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_CloudUpscaleTex, 0);
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFBO));
}

static GLuint CompileCloudBlitProgram() {
    const char* vSrc =
        "#version 330 core\n"
        "layout(location = 0) in vec2 a_Pos;\n"
        "layout(location = 1) in vec2 a_UV;\n"
        "out vec2 v_UV;\n"
        "void main() {\n"
        "    v_UV = a_UV;\n"
        "    gl_Position = vec4(a_Pos, 0.0, 1.0);\n"
        "}\n";
    const char* fSrc =
        "#version 330 core\n"
        "in vec2 v_UV;\n"
        "uniform sampler2D u_CloudTex;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = texture(u_CloudTex, v_UV);\n"
        "}\n";

    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vSrc, nullptr);
    glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fSrc, nullptr);
    glCompileShader(fs);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

void SlipRender::RenderClouds(
    SlipSkyComponent* skyComp,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPos,
    unsigned int sceneDepthTexture,
    int viewportWidth,
    int viewportHeight,
    float nearPlane,
    float farPlane,
    float currentTime
)
{
    if (!skyComp || !skyComp->IsEnabled()) return;

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
    (void)sceneDepthTexture; (void)viewportWidth; (void)viewportHeight; (void)nearPlane; (void)farPlane;
    RenderCloudsAndroid_Slices(skyComp, view, projection, cameraPos, currentTime);
#else
    RenderCloudsPC_Raymarch(skyComp, view, projection, cameraPos, sceneDepthTexture, viewportWidth, viewportHeight, nearPlane, farPlane, currentTime);
#endif
}

#if defined(SLIP_PLATFORM_ANDROID) || defined(__ANDROID__)
void SlipRender::RenderCloudsAndroid_Slices(
    SlipSkyComponent* skyComp,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPos,
    float currentTime
)
{
    if (!skyComp || !skyComp->IsEnabled()) return;

    InitSliceMesh();
    if (m_SliceVAO == 0 || m_SliceIndexCount == 0) return;

    SlipShader* shader = SlipShaderLibrary::LoadOrGet("CloudSlices");
    if (!shader) return;

    shader->use();
    shader->setMat4("u_View", view);
    shader->setMat4("u_Projection", projection);
    shader->setVec3("u_CameraPos", cameraPos);
    shader->setFloat("u_DomeRadius", 5000.0f);

    skyComp->BindParameters(shader, currentTime);

    // Configuracin ptima de estados OpenGL ES 3.0 para mitigar fill-rate TBDR
    glDepthMask(GL_FALSE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    glBindVertexArray(m_SliceVAO);

    int totalSlices = (std::max)(1, skyComp->params.totalSlices);
    glDrawElementsInstanced(
        GL_TRIANGLES,
        static_cast<GLsizei>(m_SliceIndexCount),
        GL_UNSIGNED_INT,
        nullptr,
        totalSlices
    );

    glBindVertexArray(0);

    // Restaurar estados
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}
#else
void SlipRender::RenderCloudsPC_Raymarch(
    SlipSkyComponent* skyComp,
    const glm::mat4& view,
    const glm::mat4& projection,
    const glm::vec3& cameraPos,
    unsigned int sceneDepthTexture,
    int viewportWidth,
    int viewportHeight,
    float nearPlane,
    float farPlane,
    float currentTime
)
{
    if (!skyComp || !skyComp->IsEnabled()) return;

    InitFullscreenQuad();
    if (m_FullscreenQuadVAO == 0) return;

    // 1. Instanciación y control de Framebuffer a resolución escalada (1 = Nativa, 2 = 1/2, 4 = 1/4)
    int factor = (std::clamp)(skyComp->params.downscaleFactor, 1, 4);
    int lowW = (std::max)(1, viewportWidth / factor);
    int lowH = (std::max)(1, viewportHeight / factor);

    GLint originalFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &originalFBO);

    EnsureCloudFBO(lowW, lowH);

    // =========================================================================
    // FASE 3.2: Pase de Raymarching Volumtrico a 1/4 Resolucin
    // =========================================================================
    glBindFramebuffer(GL_FRAMEBUFFER, m_CloudFBO);
    glViewport(0, 0, lowW, lowH);
    GLenum drawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &drawBuf);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    SlipShader* raymarchShader = SlipShaderLibrary::LoadOrGet("CloudRaymarch");
    if (raymarchShader) {
        raymarchShader->use();
        raymarchShader->setMat4("u_InvProjection", glm::inverse(projection));
        raymarchShader->setMat4("u_InvView", glm::inverse(view));
        raymarchShader->setVec3("u_CameraPos", cameraPos);
        raymarchShader->setFloat("u_NearPlane", nearPlane);
        raymarchShader->setFloat("u_FarPlane", farPlane);
        raymarchShader->setVec2("u_ScreenResolution", static_cast<float>(lowW), static_cast<float>(lowH));

        // Buffer de profundidad de escena en unidad de textura 2
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
        raymarchShader->setInt("u_SceneDepth", 2);

        // Inyeccin de parmetros climticos, solar y texturas de ruido (0 y 1)
        skyComp->BindParameters(raymarchShader, currentTime);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(m_FullscreenQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // Desvincular texturas para evitar feedback loops en FBOs posteriores
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    }

    // =========================================================================
    // FASE 3.3: Pase de Bilateral Upscale a resolución nativa en FBO dedicado
    // (Resuelve la oclusión por profundidad y elimina feedback loops con originalFBO)
    // =========================================================================
    EnsureCloudUpscaleFBO(viewportWidth, viewportHeight);
    glBindFramebuffer(GL_FRAMEBUFFER, m_CloudUpscaleFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);
    GLenum upDrawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &upDrawBuf);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    SlipShader* compositeShader = SlipShaderLibrary::LoadOrGet("CloudBilateralUpscale");
    if (compositeShader) {
        compositeShader->use();
        compositeShader->setMat4("u_InvProjection", glm::inverse(projection));
        compositeShader->setMat4("u_InvView", glm::inverse(view));
        compositeShader->setVec3("u_CameraPos", cameraPos);
        compositeShader->setFloat("u_CloudBaseHeight", skyComp->params.cloudBaseHeight);
        compositeShader->setVec2("u_LowResTexelSize", 1.0f / static_cast<float>(lowW), 1.0f / static_cast<float>(lowH));
        compositeShader->setFloat("u_DepthSensitivity", skyComp->params.depthSensitivity);
        compositeShader->setFloat("u_NearPlane", nearPlane);
        compositeShader->setFloat("u_FarPlane", farPlane);

        // Nubes a 1/4 res (Unidad 0)
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_CloudColorTex);
        compositeShader->setInt("u_LowResClouds", 0);

        // Buffer de profundidad full-res (Unidad 1)
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glBindTexture(GL_TEXTURE_2D, sceneDepthTexture);
        compositeShader->setInt("u_SceneDepth", 1);

        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(m_FullscreenQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
    }

    // =========================================================================
    // FASE 3.4: Composición final sobre originalFBO (sin ningún attachment en conflicto)
    // =========================================================================
    glBindFramebuffer(GL_FRAMEBUFFER, originalFBO);
    glViewport(0, 0, viewportWidth, viewportHeight);
    GLenum origDrawBuf = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &origDrawBuf);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

    GLint prevDepthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
    GLboolean depthTestWasEnabled = glIsEnabled(GL_DEPTH_TEST);

    if (m_CloudBlitProgram == 0) {
        m_CloudBlitProgram = CompileCloudBlitProgram();
    }
    if (m_CloudBlitProgram != 0) {
        glUseProgram(m_CloudBlitProgram);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_CloudUpscaleTex);
        glUniform1i(glGetUniformLocation(m_CloudBlitProgram, "u_CloudTex"), 0);

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        glBindVertexArray(m_FullscreenQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
    }

    // Restaurar estados
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (depthTestWasEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthFunc(static_cast<GLenum>(prevDepthFunc));
    glEnable(GL_CULL_FACE);
}
#endif
