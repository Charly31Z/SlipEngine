#include "SlipEngine/scene/SlipTerrainComponent.h"
#include "SlipEngine/scene/SlipEntity.h"
#include "SlipEngine/physics/SlipPhysics.h"
#include "SlipEngine/editor/SlipClusterBuilder.h"
#include "SlipEngine/tools/SlipFileSystem.h"
#include "SlipEngine/core/SlipConsole.h"
#include "stb_image.h"
#include <algorithm>
#include <cmath>

SlipTerrainComponent::SlipTerrainComponent()
{
    m_Name = "TerrainComponent";
}

SlipTerrainComponent::~SlipTerrainComponent()
{
    DestroyPhysics();

    if (m_Mesh) {
        delete m_Mesh;
        m_Mesh = nullptr;
    }
    if (m_Material) {
        delete m_Material;
        m_Material = nullptr;
    }
}

void SlipTerrainComponent::OnInit()
{
    if (!m_Mesh) {
        RebuildTerrain();
    }
}

void SlipTerrainComponent::OnStart()
{
    SyncTransformToPhysics();
}

void SlipTerrainComponent::OnUpdate(float deltaTime)
{
    (void)deltaTime;
    SyncTransformToPhysics();
}

void SlipTerrainComponent::OnDrawInspectorUI()
{
    // Manejado reactivamente en InspectorPanel
}

uint32_t SlipTerrainComponent::GetClusterCount() const
{
    return m_Mesh ? static_cast<uint32_t>(m_Mesh->clusters.size()) : 0;
}

void SlipTerrainComponent::SetMaterial(const std::string& path)
{
    materialPath = path;
    if (m_Material) {
        delete m_Material;
        m_Material = nullptr;
    }
    if (!path.empty()) {
        m_Material = new SlipMaterial(path);
        m_Material->init();
    }
}

void SlipTerrainComponent::SetHeightmap(const std::string& path)
{
    heightmapPath = path;
    RebuildTerrain();
}

void SlipTerrainComponent::DestroyPhysics()
{
    if (m_RigidBody) {
        SlipPhysics::Get().getWorld().removeRigidBody(m_RigidBody);
        delete m_RigidBody;
        m_RigidBody = nullptr;
    }
    if (m_MotionState) {
        delete m_MotionState;
        m_MotionState = nullptr;
    }
    if (m_CollisionShape) {
        delete m_CollisionShape;
        m_CollisionShape = nullptr;
    }
    if (m_TriangleMesh) {
        delete m_TriangleMesh;
        m_TriangleMesh = nullptr;
    }
}

void SlipTerrainComponent::SyncTransformToPhysics()
{
    if (!m_RigidBody || !m_CollisionShape || !m_Owner) return;

    glm::mat4 curMat = m_Owner->getMatrix();
    if (curMat == m_LastTransform) return;
    m_LastTransform = curMat;

    glm::vec3 worldScale(
        glm::length(glm::vec3(curMat[0])),
        glm::length(glm::vec3(curMat[1])),
        glm::length(glm::vec3(curMat[2]))
    );
    if (worldScale.x < 0.0001f) worldScale.x = 1.0f;
    if (worldScale.y < 0.0001f) worldScale.y = 1.0f;
    if (worldScale.z < 0.0001f) worldScale.z = 1.0f;

    glm::mat3 rotM(
        glm::vec3(curMat[0]) / worldScale.x,
        glm::vec3(curMat[1]) / worldScale.y,
        glm::vec3(curMat[2]) / worldScale.z
    );

    glm::vec3 worldPos = glm::vec3(curMat[3]);
    glm::quat q = glm::quat_cast(rotM);
    btQuaternion btQ(q.x, q.y, q.z, q.w);

    btTransform trans(btQ, btVector3(worldPos.x, worldPos.y, worldPos.z));

    m_RigidBody->setWorldTransform(trans);
    if (m_MotionState) {
        m_MotionState->setWorldTransform(trans);
    }
    m_CollisionShape->setLocalScaling(btVector3(worldScale.x, worldScale.y, worldScale.z));
    SlipPhysics::Get().getWorld().updateSingleAabb(m_RigidBody);
}

void SlipTerrainComponent::BuildGeometry(std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices)
{
    outVertices.clear();
    outIndices.clear();

    uint32_t resX = (std::max)(2u, resolutionX);
    uint32_t resZ = (std::max)(2u, resolutionZ);
    float safeWidth = (std::max)(1.0f, width);
    float safeLength = (std::max)(1.0f, length);

    // 1. Cargar imagen de heightmap en CPU si existe
    int hmWidth = 0, hmHeight = 0, hmChannels = 0;
    unsigned char* hmData = nullptr;

    if (!heightmapPath.empty()) {
        std::string resolved = heightmapPath;
        if (!SlipFileSystem::Exists(resolved)) {
            resolved = SlipFileSystem::CombinePath(SlipFileSystem::GetProjectAssetsPath(), heightmapPath);
        }
        if (SlipFileSystem::Exists(resolved)) {
            hmData = stbi_load(resolved.c_str(), &hmWidth, &hmHeight, &hmChannels, 1);
        }
    }

    auto sampleHeight = [&](float u, float v) -> float {
        if (!hmData || hmWidth <= 0 || hmHeight <= 0) return 0.0f;

        float cu = glm::clamp(u, 0.0f, 1.0f);
        float cv = glm::clamp(v, 0.0f, 1.0f);

        float fx = cu * (hmWidth - 1);
        float fz = cv * (hmHeight - 1);

        int x0 = static_cast<int>(std::floor(fx));
        int z0 = static_cast<int>(std::floor(fz));
        int x1 = (std::min)(x0 + 1, hmWidth - 1);
        int z1 = (std::min)(z0 + 1, hmHeight - 1);

        float tx = fx - x0;
        float tz = fz - z0;

        float h00 = static_cast<float>(hmData[z0 * hmWidth + x0]) / 255.0f;
        float h10 = static_cast<float>(hmData[z0 * hmWidth + x1]) / 255.0f;
        float h01 = static_cast<float>(hmData[z1 * hmWidth + x0]) / 255.0f;
        float h11 = static_cast<float>(hmData[z1 * hmWidth + x1]) / 255.0f;

        float hBottom = h00 * (1.0f - tx) + h10 * tx;
        float hTop    = h01 * (1.0f - tx) + h11 * tx;
        float hFinal  = hBottom * (1.0f - tz) + hTop * tz;

        return hFinal * heightScale;
    };

    outVertices.reserve((resX + 1) * (resZ + 1));
    float du = 1.0f / static_cast<float>(resX);
    float dv = 1.0f / static_cast<float>(resZ);
    float dx = safeWidth * du;
    float dz = safeLength * dv;

    for (uint32_t iz = 0; iz <= resZ; ++iz) {
        float v = static_cast<float>(iz) / static_cast<float>(resZ);
        float z = (v - 0.5f) * safeLength;

        for (uint32_t ix = 0; ix <= resX; ++ix) {
            float u = static_cast<float>(ix) / static_cast<float>(resX);
            float x = (u - 0.5f) * safeWidth;
            float y = sampleHeight(u, v);

            // Normales analíticas usando diferencias centrales
            float hL = sampleHeight(u - du, v);
            float hR = sampleHeight(u + du, v);
            float hD = sampleHeight(u, v - dv);
            float hU = sampleHeight(u, v + dv);

            glm::vec3 normal = glm::normalize(glm::vec3(hL - hR, 2.0f * dx, hD - hU));
            glm::vec3 tangent = glm::normalize(glm::vec3(2.0f * dx, hR - hL, 0.0f));

            Vertex vert;
            vert.Position = glm::vec3(x, y, z);
            vert.Normal = normal;
            vert.Tangent = tangent;
            vert.TexCoords = glm::vec2(u * uvScale.x, v * uvScale.y);

            outVertices.push_back(vert);
        }
    }

    if (hmData) {
        stbi_image_free(hmData);
        hmData = nullptr;
    }

    // Generar índices
    outIndices.reserve(resX * resZ * 6);
    for (uint32_t iz = 0; iz < resZ; ++iz) {
        uint32_t row1 = iz * (resX + 1);
        uint32_t row2 = (iz + 1) * (resX + 1);

        for (uint32_t ix = 0; ix < resX; ++ix) {
            uint32_t i0 = row1 + ix;
            uint32_t i1 = row1 + ix + 1;
            uint32_t i2 = row2 + ix;
            uint32_t i3 = row2 + ix + 1;

            outIndices.push_back(i0);
            outIndices.push_back(i2);
            outIndices.push_back(i1);

            outIndices.push_back(i1);
            outIndices.push_back(i2);
            outIndices.push_back(i3);
        }
    }
}

void SlipTerrainComponent::BuildCollision(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
{
    DestroyPhysics();

    if (!enableCollision || vertices.empty() || indices.empty()) return;

    m_TriangleMesh = new btTriangleMesh();
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];
        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const auto& p0 = vertices[i0].Position;
        const auto& p1 = vertices[i1].Position;
        const auto& p2 = vertices[i2].Position;

        m_TriangleMesh->addTriangle(
            btVector3(p0.x, p0.y, p0.z),
            btVector3(p1.x, p1.y, p1.z),
            btVector3(p2.x, p2.y, p2.z)
        );
    }

    m_CollisionShape = new btBvhTriangleMeshShape(m_TriangleMesh, true, true);

    glm::mat4 curMat = m_Owner ? m_Owner->getMatrix() : glm::mat4(1.0f);
    glm::vec3 worldScale(
        glm::length(glm::vec3(curMat[0])),
        glm::length(glm::vec3(curMat[1])),
        glm::length(glm::vec3(curMat[2]))
    );
    if (worldScale.x < 0.0001f) worldScale.x = 1.0f;
    if (worldScale.y < 0.0001f) worldScale.y = 1.0f;
    if (worldScale.z < 0.0001f) worldScale.z = 1.0f;

    glm::mat3 rotM(
        glm::vec3(curMat[0]) / worldScale.x,
        glm::vec3(curMat[1]) / worldScale.y,
        glm::vec3(curMat[2]) / worldScale.z
    );

    glm::vec3 worldPos = glm::vec3(curMat[3]);
    glm::quat q = glm::quat_cast(rotM);
    btQuaternion btQ(q.x, q.y, q.z, q.w);

    m_CollisionShape->setLocalScaling(btVector3(worldScale.x, worldScale.y, worldScale.z));
    m_CollisionShape->setMargin(0.02f);

    btTransform startTransform(btQ, btVector3(worldPos.x, worldPos.y, worldPos.z));

    m_MotionState = new btDefaultMotionState(startTransform);
    btRigidBody::btRigidBodyConstructionInfo rbInfo(0.0f, m_MotionState, m_CollisionShape, btVector3(0, 0, 0));
    m_RigidBody = new btRigidBody(rbInfo);
    m_RigidBody->setCollisionFlags(m_RigidBody->getCollisionFlags() | btCollisionObject::CF_STATIC_OBJECT);
    m_RigidBody->setUserPointer(m_Owner);

    SlipPhysics::Get().getWorld().addRigidBody(m_RigidBody);
    m_LastTransform = curMat;
}

void SlipTerrainComponent::RebuildTerrain()
{
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    BuildGeometry(vertices, indices);

    m_VertexCount = static_cast<uint32_t>(vertices.size());
    m_TriangleCount = static_cast<uint32_t>(indices.size() / 3);

    if (m_Mesh) {
        delete m_Mesh;
        m_Mesh = nullptr;
    }
    m_Mesh = new SlipMesh(vertices, indices);

    // Construir jerarquía de clusters V-Clust
    SlipClusterBuildOptions opts;
    opts.maxVerticesPerCluster = 64;
    opts.maxTrianglesPerCluster = 124;
    opts.coneWeight = 0.5f;
    opts.maxHierarchyLevels = 4;

    SlipClusterBuildResult buildRes;
    if (SlipClusterBuilder::BuildFromSubmeshes(m_Mesh->meshes, opts, buildRes) && !buildRes.clusters.empty()) {
        m_Mesh->clusterHeader = buildRes.header;
        m_Mesh->clusters = std::move(buildRes.clusters);
        m_Mesh->clusterVertices = std::move(buildRes.quantizedVertices);
        m_Mesh->clusterIndices = std::move(buildRes.indices);
        m_Mesh->hasClusterLOD = true;
        m_Mesh->setupClusterBuffers();
    }

    // Generar colisiones en Bullet Physics
    BuildCollision(vertices, indices);

    // Cargar material si está asignado
    if (!materialPath.empty() && !m_Material) {
        SetMaterial(materialPath);
    }

    m_Revision++;
}
