#pragma once

#ifndef SLIP_TERRAIN_COMPONENT_H
#define SLIP_TERRAIN_COMPONENT_H

#include "SlipEngine.h"
#include "SlipEngine/scene/SlipComponent.h"
#include "SlipEngine/render/SlipMesh.h"
#include "SlipEngine/render/SlipMaterial.h"
#include <btBulletDynamicsCommon.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

class SLIPENGINE_API SlipTerrainComponent : public SlipComponent
{
public:
    // Dimensiones en unidades de mundo
    float width = 64.0f;
    float length = 64.0f;

    // Resolución de subdivisión de la cuadrícula
    uint32_t resolutionX = 64;
    uint32_t resolutionZ = 64;

    // Escala de desplazamiento de altura
    float heightScale = 15.0f;

    // Factor de repetición de coordenadas UV
    glm::vec2 uvScale = glm::vec2(8.0f, 8.0f);

    // Rutas de archivos
    std::string heightmapPath = "";
    std::string materialPath = "";

    // Configuración física y visual
    bool enableCollision = true;
    bool wireframe = false;
    bool castShadows = false;

    SlipTerrainComponent();
    ~SlipTerrainComponent() override;

    void OnInit() override;
    void OnStart() override;
    void OnUpdate(float deltaTime) override;
    void OnDrawInspectorUI() override;

    // Reconstruye la geometría, jerarquía V-Clust y colisión Bullet
    void RebuildTerrain();

    // Obtención de malla y material
    SlipMesh* GetMesh() const { return m_Mesh; }
    SlipMaterial* GetMaterial() const { return m_Material; }

    void SetMaterial(const std::string& path);
    void SetHeightmap(const std::string& path);

    // Estado físico
    btRigidBody* GetRigidBody() const { return m_RigidBody; }
    btCollisionShape* GetCollisionShape() const { return m_CollisionShape; }
    void SyncTransformToPhysics();

    // Estadísticas y control de versión
    uint32_t GetVertexCount() const { return m_VertexCount; }
    uint32_t GetTriangleCount() const { return m_TriangleCount; }
    uint32_t GetClusterCount() const;
    uint32_t GetRevision() const { return m_Revision; }

private:
    SlipMesh* m_Mesh = nullptr;
    SlipMaterial* m_Material = nullptr;

    btTriangleMesh* m_TriangleMesh = nullptr;
    btCollisionShape* m_CollisionShape = nullptr;
    btRigidBody* m_RigidBody = nullptr;
    btDefaultMotionState* m_MotionState = nullptr;

    glm::mat4 m_LastTransform = glm::mat4(0.0f);
    uint32_t m_VertexCount = 0;
    uint32_t m_TriangleCount = 0;
    uint32_t m_Revision = 0;

    void BuildGeometry(std::vector<Vertex>& outVertices, std::vector<unsigned int>& outIndices);
    void BuildVClusters();
    void BuildCollision(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices);
    void DestroyPhysics();
};

#endif // !SLIP_TERRAIN_COMPONENT_H
